//========================================================================
// GLFW 3.4 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2002-2006 Marcus Geelnard
// Copyright (c) 2006-2019 Camilla Löwy <elmindreda@glfw.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
// Please use C89 style variable declarations in this file because VS 2010
//========================================================================

#include "internal.h"
#include "../alatty/monotonic.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Internal key state used for sticky keys
#define _GLFW_STICK 3

//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

static void
set_key_action(_GLFWwindow *window, const GLFWkeyevent *ev, int action, int idx) {
    const unsigned sz = arraysz(window->activated_keys);
    if (idx < 0) {
        for (unsigned i = 0; i < sz; i++) {
            if (window->activated_keys[i].native_key_id == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            idx = sz - 1;
            memmove(window->activated_keys, window->activated_keys + 1, sizeof(window->activated_keys[0]) * (sz - 1));
            window->activated_keys[sz - 1].native_key_id = 0;
        }
    }
    if (action == GLFW_RELEASE) {
        memset(window->activated_keys + idx, 0, sizeof(window->activated_keys[0]));
        if (idx < (int)sz - 1) {
            memmove(window->activated_keys + idx, window->activated_keys + idx + 1, sizeof(window->activated_keys[0]) * (sz - 1 - idx));
            memset(window->activated_keys + sz - 1, 0, sizeof(window->activated_keys[0]));
        }
    } else {
        window->activated_keys[idx] = *ev;
        window->activated_keys[idx].text = NULL;
    }
}

// Notifies shared code of a physical key event
//
void _glfwInputKeyboard(_GLFWwindow* window, GLFWkeyevent* ev)
{
    if (ev->native_key_id > 0)
    {
        bool repeated = false;
        int idx = -1;
        int current_action = GLFW_RELEASE;
        const unsigned sz = arraysz(window->activated_keys);
        for (unsigned i = 0; i < sz; i++) {
            if (window->activated_keys[i].native_key_id == ev->native_key_id) {
                idx = i;
                current_action = window->activated_keys[i].action;
                break;
            }
        }

        if (ev->action == GLFW_RELEASE) {
            if (current_action == GLFW_RELEASE) return;
            if (idx > -1) {
                const GLFWkeyevent *press_event = window->activated_keys + idx;
                if (press_event->action == GLFW_PRESS || press_event->action == GLFW_REPEAT) {
                    // Compose sequences under X11 give a different key value for press and release events
                    // but we want the same key value so override it.
                    ev->native_key = press_event->native_key;
                    ev->key = press_event->key;
                    ev->shifted_key = press_event->shifted_key;
                    ev->alternate_key = press_event->alternate_key;
                }
            }
        }

        if (ev->action == GLFW_PRESS && current_action == GLFW_PRESS)
            repeated = true;

        set_key_action(window, ev, (ev->action == GLFW_RELEASE && window->stickyKeys) ? _GLFW_STICK : ev->action, idx);

        if (repeated)
            ev->action = GLFW_REPEAT;
    }


    // FIXME: will need to update ev->virtual_mods here too?
    if (window->callbacks.keyboard) {
        if (!window->lockKeyMods) ev->mods &= ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);
        window->callbacks.keyboard((GLFWwindow*) window, ev);
    }
}

// Notifies shared code of a scroll event
//
void _glfwInputScroll(_GLFWwindow* window, double xoffset, double yoffset, int flags, int mods)
{
    if (window->callbacks.scroll)
        window->callbacks.scroll((GLFWwindow*) window, xoffset, yoffset, flags, mods);
}

// Notifies shared code of a mouse button click event
//
void _glfwInputMouseClick(_GLFWwindow* window, int button, int action, int mods)
{
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST)
        return;

    if (!window->lockKeyMods)
        mods &= ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);

    if (action == GLFW_RELEASE && window->stickyMouseButtons)
        window->mouseButtons[button] = _GLFW_STICK;
    else
        window->mouseButtons[button] = (char) action;

    if (window->callbacks.mouseButton)
        window->callbacks.mouseButton((GLFWwindow*) window, button, action, mods);
}

// Notifies shared code of a cursor motion event
// The position is specified in content area relative screen coordinates
//
void _glfwInputCursorPos(_GLFWwindow* window, double xpos, double ypos)
{
    if (window->virtualCursorPosX == xpos && window->virtualCursorPosY == ypos)
        return;

    window->virtualCursorPosX = xpos;
    window->virtualCursorPosY = ypos;

    if (window->callbacks.cursorPos)
        window->callbacks.cursorPos((GLFWwindow*) window, xpos, ypos);
}

// Notifies shared code of a cursor enter/leave event
//
void _glfwInputCursorEnter(_GLFWwindow* window, bool entered)
{
    if (window->callbacks.cursorEnter)
        window->callbacks.cursorEnter((GLFWwindow*) window, entered);
}

// Notifies shared code of files or directories dropped on a window
//
int _glfwInputDrop(_GLFWwindow* window, const char *mime, const char *text, size_t sz)
{
    if (window->callbacks.drop)
        return window->callbacks.drop((GLFWwindow*) window, mime, text, sz);
    return 0;
}

//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

unsigned int
encode_utf8(uint32_t ch, char* dest) {
    if (ch < 0x80) {
        dest[0] = (char)ch;
        return 1;
    }
    if (ch < 0x800) {
        dest[0] = (ch>>6) | 0xC0;
        dest[1] = (ch & 0x3F) | 0x80;
        return 2;
    }
    if (ch < 0x10000) {
        dest[0] = (ch>>12) | 0xE0;
        dest[1] = ((ch>>6) & 0x3F) | 0x80;
        dest[2] = (ch & 0x3F) | 0x80;
        return 3;
    }
    if (ch < 0x110000) {
        dest[0] = (ch>>18) | 0xF0;
        dest[1] = ((ch>>12) & 0x3F) | 0x80;
        dest[2] = ((ch>>6) & 0x3F) | 0x80;
        dest[3] = (ch & 0x3F) | 0x80;
        return 4;
    }
    return 0;
}

const char*
_glfwGetKeyName(int key)
{
    switch (key)
    {
        /* start functional key names (auto generated by gen-key-constants.py do not edit) */
    case GLFW_FKEY_ESCAPE: return "ESCAPE";
    case GLFW_FKEY_ENTER: return "ENTER";
    case GLFW_FKEY_TAB: return "TAB";
    case GLFW_FKEY_BACKSPACE: return "BACKSPACE";
    case GLFW_FKEY_INSERT: return "INSERT";
    case GLFW_FKEY_DELETE: return "DELETE";
    case GLFW_FKEY_LEFT: return "LEFT";
    case GLFW_FKEY_RIGHT: return "RIGHT";
    case GLFW_FKEY_UP: return "UP";
    case GLFW_FKEY_DOWN: return "DOWN";
    case GLFW_FKEY_PAGE_UP: return "PAGE_UP";
    case GLFW_FKEY_PAGE_DOWN: return "PAGE_DOWN";
    case GLFW_FKEY_HOME: return "HOME";
    case GLFW_FKEY_END: return "END";
    case GLFW_FKEY_CAPS_LOCK: return "CAPS_LOCK";
    case GLFW_FKEY_SCROLL_LOCK: return "SCROLL_LOCK";
    case GLFW_FKEY_NUM_LOCK: return "NUM_LOCK";
    case GLFW_FKEY_PRINT_SCREEN: return "PRINT_SCREEN";
    case GLFW_FKEY_PAUSE: return "PAUSE";
    case GLFW_FKEY_MENU: return "MENU";
    case GLFW_FKEY_F1: return "F1";
    case GLFW_FKEY_F2: return "F2";
    case GLFW_FKEY_F3: return "F3";
    case GLFW_FKEY_F4: return "F4";
    case GLFW_FKEY_F5: return "F5";
    case GLFW_FKEY_F6: return "F6";
    case GLFW_FKEY_F7: return "F7";
    case GLFW_FKEY_F8: return "F8";
    case GLFW_FKEY_F9: return "F9";
    case GLFW_FKEY_F10: return "F10";
    case GLFW_FKEY_F11: return "F11";
    case GLFW_FKEY_F12: return "F12";
    case GLFW_FKEY_F13: return "F13";
    case GLFW_FKEY_F14: return "F14";
    case GLFW_FKEY_F15: return "F15";
    case GLFW_FKEY_F16: return "F16";
    case GLFW_FKEY_F17: return "F17";
    case GLFW_FKEY_F18: return "F18";
    case GLFW_FKEY_F19: return "F19";
    case GLFW_FKEY_F20: return "F20";
    case GLFW_FKEY_F21: return "F21";
    case GLFW_FKEY_F22: return "F22";
    case GLFW_FKEY_F23: return "F23";
    case GLFW_FKEY_F24: return "F24";
    case GLFW_FKEY_F25: return "F25";
    case GLFW_FKEY_F26: return "F26";
    case GLFW_FKEY_F27: return "F27";
    case GLFW_FKEY_F28: return "F28";
    case GLFW_FKEY_F29: return "F29";
    case GLFW_FKEY_F30: return "F30";
    case GLFW_FKEY_F31: return "F31";
    case GLFW_FKEY_F32: return "F32";
    case GLFW_FKEY_F33: return "F33";
    case GLFW_FKEY_F34: return "F34";
    case GLFW_FKEY_F35: return "F35";
    case GLFW_FKEY_KP_0: return "KP_0";
    case GLFW_FKEY_KP_1: return "KP_1";
    case GLFW_FKEY_KP_2: return "KP_2";
    case GLFW_FKEY_KP_3: return "KP_3";
    case GLFW_FKEY_KP_4: return "KP_4";
    case GLFW_FKEY_KP_5: return "KP_5";
    case GLFW_FKEY_KP_6: return "KP_6";
    case GLFW_FKEY_KP_7: return "KP_7";
    case GLFW_FKEY_KP_8: return "KP_8";
    case GLFW_FKEY_KP_9: return "KP_9";
    case GLFW_FKEY_KP_DECIMAL: return "KP_DECIMAL";
    case GLFW_FKEY_KP_DIVIDE: return "KP_DIVIDE";
    case GLFW_FKEY_KP_MULTIPLY: return "KP_MULTIPLY";
    case GLFW_FKEY_KP_SUBTRACT: return "KP_SUBTRACT";
    case GLFW_FKEY_KP_ADD: return "KP_ADD";
    case GLFW_FKEY_KP_ENTER: return "KP_ENTER";
    case GLFW_FKEY_KP_EQUAL: return "KP_EQUAL";
    case GLFW_FKEY_KP_SEPARATOR: return "KP_SEPARATOR";
    case GLFW_FKEY_KP_LEFT: return "KP_LEFT";
    case GLFW_FKEY_KP_RIGHT: return "KP_RIGHT";
    case GLFW_FKEY_KP_UP: return "KP_UP";
    case GLFW_FKEY_KP_DOWN: return "KP_DOWN";
    case GLFW_FKEY_KP_PAGE_UP: return "KP_PAGE_UP";
    case GLFW_FKEY_KP_PAGE_DOWN: return "KP_PAGE_DOWN";
    case GLFW_FKEY_KP_HOME: return "KP_HOME";
    case GLFW_FKEY_KP_END: return "KP_END";
    case GLFW_FKEY_KP_INSERT: return "KP_INSERT";
    case GLFW_FKEY_KP_DELETE: return "KP_DELETE";
    case GLFW_FKEY_KP_BEGIN: return "KP_BEGIN";
    case GLFW_FKEY_MEDIA_PLAY: return "MEDIA_PLAY";
    case GLFW_FKEY_MEDIA_PAUSE: return "MEDIA_PAUSE";
    case GLFW_FKEY_MEDIA_PLAY_PAUSE: return "MEDIA_PLAY_PAUSE";
    case GLFW_FKEY_MEDIA_REVERSE: return "MEDIA_REVERSE";
    case GLFW_FKEY_MEDIA_STOP: return "MEDIA_STOP";
    case GLFW_FKEY_MEDIA_FAST_FORWARD: return "MEDIA_FAST_FORWARD";
    case GLFW_FKEY_MEDIA_REWIND: return "MEDIA_REWIND";
    case GLFW_FKEY_MEDIA_TRACK_NEXT: return "MEDIA_TRACK_NEXT";
    case GLFW_FKEY_MEDIA_TRACK_PREVIOUS: return "MEDIA_TRACK_PREVIOUS";
    case GLFW_FKEY_MEDIA_RECORD: return "MEDIA_RECORD";
    case GLFW_FKEY_LOWER_VOLUME: return "LOWER_VOLUME";
    case GLFW_FKEY_RAISE_VOLUME: return "RAISE_VOLUME";
    case GLFW_FKEY_MUTE_VOLUME: return "MUTE_VOLUME";
    case GLFW_FKEY_LEFT_SHIFT: return "LEFT_SHIFT";
    case GLFW_FKEY_LEFT_CONTROL: return "LEFT_CONTROL";
    case GLFW_FKEY_LEFT_ALT: return "LEFT_ALT";
    case GLFW_FKEY_LEFT_SUPER: return "LEFT_SUPER";
    case GLFW_FKEY_LEFT_HYPER: return "LEFT_HYPER";
    case GLFW_FKEY_LEFT_META: return "LEFT_META";
    case GLFW_FKEY_RIGHT_SHIFT: return "RIGHT_SHIFT";
    case GLFW_FKEY_RIGHT_CONTROL: return "RIGHT_CONTROL";
    case GLFW_FKEY_RIGHT_ALT: return "RIGHT_ALT";
    case GLFW_FKEY_RIGHT_SUPER: return "RIGHT_SUPER";
    case GLFW_FKEY_RIGHT_HYPER: return "RIGHT_HYPER";
    case GLFW_FKEY_RIGHT_META: return "RIGHT_META";
    case GLFW_FKEY_ISO_LEVEL3_SHIFT: return "ISO_LEVEL3_SHIFT";
    case GLFW_FKEY_ISO_LEVEL5_SHIFT: return "ISO_LEVEL5_SHIFT";
/* end functional key names */
        case 0:                          return "UNKNOWN";
    }
    static char buf[16];
    encode_utf8(key, buf);
    return buf;
}

// Center the cursor in the content area of the specified window
//
void _glfwCenterCursorInContentArea(_GLFWwindow* window)
{
    int width, height;

    _glfwPlatformGetWindowSize(window, &width, &height);
    _glfwPlatformSetCursorPos(window, width / 2.0, height / 2.0);
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI bool glfwGetIgnoreOSKeyboardProcessing(void) {
    return _glfw.ignoreOSKeyboardProcessing;
}

GLFWAPI void glfwSetIgnoreOSKeyboardProcessing(bool enabled) {
    _glfw.ignoreOSKeyboardProcessing = enabled;
}

GLFWAPI int glfwGetInputMode(GLFWwindow* handle, int mode)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    switch (mode)
    {
        case GLFW_CURSOR:
            return window->cursorMode;
        case GLFW_STICKY_KEYS:
            return window->stickyKeys;
        case GLFW_STICKY_MOUSE_BUTTONS:
            return window->stickyMouseButtons;
        case GLFW_LOCK_KEY_MODS:
            return window->lockKeyMods;
        case GLFW_RAW_MOUSE_MOTION:
            return window->rawMouseMotion;
    }

    _glfwInputError(GLFW_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
    return 0;
}

GLFWAPI void glfwSetInputMode(GLFWwindow* handle, int mode, int value)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT();

    if (mode == GLFW_CURSOR)
    {
        if (value != GLFW_CURSOR_NORMAL &&
            value != GLFW_CURSOR_HIDDEN &&
            value != GLFW_CURSOR_DISABLED)
        {
            _glfwInputError(GLFW_INVALID_ENUM,
                            "Invalid cursor mode 0x%08X",
                            value);
            return;
        }

        if (window->cursorMode == value)
            return;

        window->cursorMode = value;

        _glfwPlatformGetCursorPos(window,
                                  &window->virtualCursorPosX,
                                  &window->virtualCursorPosY);
        _glfwPlatformSetCursorMode(window, value);
    }
    else if (mode == GLFW_STICKY_KEYS)
    {
        value = value ? true : false;
        if (window->stickyKeys == value)
            return;

        if (!value)
        {
            // Release all sticky keys
            for (unsigned i = arraysz(window->activated_keys) - 1;  i-- > 0;)
            {
                if (window->activated_keys[i].action == _GLFW_STICK) {
                    if (i < arraysz(window->activated_keys) - 1) {
                        memmove(window->activated_keys + i, window->activated_keys + i + 1, sizeof(window->activated_keys[0]) * (arraysz(window->activated_keys) - 1 - i));
                    }
                    memset(window->activated_keys + arraysz(window->activated_keys) - 1, 0, sizeof(window->activated_keys[0]));
                }
            }
        }

        window->stickyKeys = value;
    }
    else if (mode == GLFW_STICKY_MOUSE_BUTTONS)
    {
        value = value ? true : false;
        if (window->stickyMouseButtons == value)
            return;

        if (!value)
        {
            int i;

            // Release all sticky mouse buttons
            for (i = 0;  i <= GLFW_MOUSE_BUTTON_LAST;  i++)
            {
                if (window->mouseButtons[i] == _GLFW_STICK)
                    window->mouseButtons[i] = GLFW_RELEASE;
            }
        }

        window->stickyMouseButtons = value;
    }
    else if (mode == GLFW_LOCK_KEY_MODS)
    {
        window->lockKeyMods = value ? true : false;
    }
    else if (mode == GLFW_RAW_MOUSE_MOTION)
    {
        if (!_glfwPlatformRawMouseMotionSupported())
        {
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "Raw mouse motion is not supported on this system");
            return;
        }

        value = value ? true : false;
        if (window->rawMouseMotion == value)
            return;

        window->rawMouseMotion = value;
        _glfwPlatformSetRawMouseMotion(window, value);
    }
    else
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
}

GLFWAPI int glfwRawMouseMotionSupported(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(false);
    return _glfwPlatformRawMouseMotionSupported();
}

GLFWAPI const char* glfwGetKeyName(uint32_t key, int native_key)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    if (key) return _glfwGetKeyName(key);

    native_key = _glfwPlatformGetNativeKeyForKey(key);
    return _glfwPlatformGetNativeKeyName(native_key);
}

GLFWAPI int glfwGetNativeKeyForKey(uint32_t key)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(-1);

    return _glfwPlatformGetNativeKeyForKey(key);
}

GLFWAPI GLFWKeyAction glfwGetKey(GLFWwindow* handle, uint32_t key)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_RELEASE);
    if (!key) return GLFW_RELEASE;

    int current_action = GLFW_RELEASE;
    const unsigned sz = arraysz(window->activated_keys);
    int idx = -1;
    for (unsigned i = 0; i < sz; i++) {
        if (window->activated_keys[i].key == key) {
            idx = i;
            current_action = window->activated_keys[i].action;
            break;
        }
    }


    if (current_action == _GLFW_STICK)
    {
        // Sticky mode: release key now
        GLFWkeyevent ev = {0};
        set_key_action(window, &ev, GLFW_RELEASE, idx);
        current_action = GLFW_PRESS;
    }

    return current_action;
}

GLFWAPI int glfwGetMouseButton(GLFWwindow* handle, int button)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_RELEASE);

    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid mouse button %i", button);
        return GLFW_RELEASE;
    }

    if (window->mouseButtons[button] == _GLFW_STICK)
    {
        // Sticky mode: release mouse button now
        window->mouseButtons[button] = GLFW_RELEASE;
        return GLFW_PRESS;
    }

    return (int) window->mouseButtons[button];
}

GLFWAPI void glfwGetCursorPos(GLFWwindow* handle, double* xpos, double* ypos)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;

    _GLFW_REQUIRE_INIT();

    if (window->cursorMode == GLFW_CURSOR_DISABLED)
    {
        if (xpos)
            *xpos = window->virtualCursorPosX;
        if (ypos)
            *ypos = window->virtualCursorPosY;
    }
    else
        _glfwPlatformGetCursorPos(window, xpos, ypos);
}

GLFWAPI void glfwSetCursorPos(GLFWwindow* handle, double xpos, double ypos)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT();

    if (xpos != xpos || xpos < -DBL_MAX || xpos > DBL_MAX ||
        ypos != ypos || ypos < -DBL_MAX || ypos > DBL_MAX)
    {
        _glfwInputError(GLFW_INVALID_VALUE,
                        "Invalid cursor position %f %f",
                        xpos, ypos);
        return;
    }

    if (!_glfwPlatformWindowFocused(window))
        return;

    if (window->cursorMode == GLFW_CURSOR_DISABLED)
    {
        // Only update the accumulated position if the cursor is disabled
        window->virtualCursorPosX = xpos;
        window->virtualCursorPosY = ypos;
    }
    else
    {
        // Update system cursor position
        _glfwPlatformSetCursorPos(window, xpos, ypos);
    }
}

GLFWAPI GLFWcursor* glfwCreateCursor(const GLFWimage* image, int xhot, int yhot, int count)
{
    _GLFWcursor* cursor;

    assert(image != NULL);
    assert(count > 0);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    cursor = calloc(1, sizeof(_GLFWcursor));
    cursor->next = _glfw.cursorListHead;
    _glfw.cursorListHead = cursor;

    if (!_glfwPlatformCreateCursor(cursor, image, xhot, yhot, count))
    {
        glfwDestroyCursor((GLFWcursor*) cursor);
        return NULL;
    }

    return (GLFWcursor*) cursor;
}

GLFWAPI GLFWcursor* glfwCreateStandardCursor(GLFWCursorShape shape)
{
    _GLFWcursor* cursor;

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (shape >= GLFW_INVALID_CURSOR)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid standard cursor: %d", shape);
        return NULL;
    }

    cursor = calloc(1, sizeof(_GLFWcursor));
    cursor->next = _glfw.cursorListHead;
    _glfw.cursorListHead = cursor;

    if (!_glfwPlatformCreateStandardCursor(cursor, shape))
    {
        glfwDestroyCursor((GLFWcursor*) cursor);
        return NULL;
    }

    return (GLFWcursor*) cursor;
}

GLFWAPI void glfwDestroyCursor(GLFWcursor* handle)
{
    _GLFWcursor* cursor = (_GLFWcursor*) handle;

    _GLFW_REQUIRE_INIT();

    if (cursor == NULL)
        return;

    // Make sure the cursor is not being used by any window
    {
        _GLFWwindow* window;

        for (window = _glfw.windowListHead;  window;  window = window->next)
        {
            if (window->cursor == cursor)
                glfwSetCursor((GLFWwindow*) window, NULL);
        }
    }

    _glfwPlatformDestroyCursor(cursor);

    // Unlink cursor from global linked list
    {
        _GLFWcursor** prev = &_glfw.cursorListHead;

        while (*prev != cursor)
            prev = &((*prev)->next);

        *prev = cursor->next;
    }

    free(cursor);
}

GLFWAPI void glfwSetCursor(GLFWwindow* windowHandle, GLFWcursor* cursorHandle)
{
    _GLFWwindow* window = (_GLFWwindow*) windowHandle;
    _GLFWcursor* cursor = (_GLFWcursor*) cursorHandle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT();

    window->cursor = cursor;

    _glfwPlatformSetCursor(window, cursor);
}

GLFWAPI GLFWkeyboardfun glfwSetKeyboardCallback(GLFWwindow* handle, GLFWkeyboardfun cbfun)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    _GLFW_SWAP_POINTERS(window->callbacks.keyboard, cbfun);
    return cbfun;
}

GLFWAPI void glfwUpdateIMEState(GLFWwindow* handle, const GLFWIMEUpdateEvent *ev) {
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT();
#if defined(_GLFW_X11) || defined(_GLFW_WAYLAND) || defined(_GLFW_COCOA)
    _glfwPlatformUpdateIMEState(window, ev);
#else
    (void)window; (void)which; (void)a; (void)b; (void)c; (void)d;
#endif
}

GLFWAPI GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* handle,
                                                      GLFWmousebuttonfun cbfun)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    _GLFW_SWAP_POINTERS(window->callbacks.mouseButton, cbfun);
    return cbfun;
}

GLFWAPI GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* handle,
                                                  GLFWcursorposfun cbfun)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    _GLFW_SWAP_POINTERS(window->callbacks.cursorPos, cbfun);
    return cbfun;
}

GLFWAPI GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow* handle,
                                                      GLFWcursorenterfun cbfun)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    _GLFW_SWAP_POINTERS(window->callbacks.cursorEnter, cbfun);
    return cbfun;
}

GLFWAPI GLFWscrollfun glfwSetScrollCallback(GLFWwindow* handle,
                                            GLFWscrollfun cbfun)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    _GLFW_SWAP_POINTERS(window->callbacks.scroll, cbfun);
    return cbfun;
}

GLFWAPI GLFWdropfun glfwSetDropCallback(GLFWwindow* handle, GLFWdropfun cbfun)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    _GLFW_SWAP_POINTERS(window->callbacks.drop, cbfun);
    return cbfun;
}

void _glfw_free_clipboard_data(_GLFWClipboardData *cd) {
    if (cd->mime_types) {
        for (size_t i = 0; i < cd->num_mime_types; i++) free((void*)cd->mime_types[i]);
        free((void*)cd->mime_types);
    }
    memset(cd, 0, sizeof(cd[0]));
}

GLFWAPI void glfwGetClipboard(GLFWClipboardType clipboard_type, const char* mime_type, GLFWclipboardwritedatafun write_data, void *object) {
    _GLFW_REQUIRE_INIT();
    _glfwPlatformGetClipboard(clipboard_type, mime_type, write_data, object);
}

GLFWAPI void glfwSetClipboardDataTypes(GLFWClipboardType clipboard_type, const char* const *mime_types, size_t num_mime_types, GLFWclipboarditerfun get_data) {
    assert(mime_types != NULL);
    assert(get_data != NULL);
    _GLFW_REQUIRE_INIT();
    _GLFWClipboardData *cd = NULL;
    switch(clipboard_type) {
        case GLFW_CLIPBOARD: cd = &_glfw.clipboard; break;
        case GLFW_PRIMARY_SELECTION: cd = &_glfw.primary; break;
    }
    _glfw_free_clipboard_data(cd);
    cd->get_data = get_data;
    cd->mime_types = calloc(num_mime_types, sizeof(char*));
    cd->num_mime_types = 0;
    cd->ctype = clipboard_type;
    for (size_t i = 0; i < num_mime_types; i++) {
        if (mime_types[i]) {
            cd->mime_types[cd->num_mime_types++] = _glfw_strdup(mime_types[i]);
        }
    }
    _glfwPlatformSetClipboard(clipboard_type);
}

GLFWAPI monotonic_t glfwGetTime(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);
    return monotonic();
}
