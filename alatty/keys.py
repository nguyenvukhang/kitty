#!/usr/bin/env python
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

from gettext import gettext as _
from typing import TYPE_CHECKING, Any, Dict, Iterable, List, Optional

from .constants import is_macos
from .fast_data_types import (
    GLFW_MOD_ALT,
    GLFW_MOD_CONTROL,
    GLFW_MOD_HYPER,
    GLFW_MOD_META,
    GLFW_MOD_SHIFT,
    GLFW_MOD_SUPER,
    KeyEvent,
    SingleKey,
    get_boss,
    get_options,
    is_modifier_key,
    set_ignore_os_keyboard_processing,
)
from .options.types import Options
from .options.utils import KeyboardMode, KeyDefinition, KeyMap
from .typing import ScreenType

if TYPE_CHECKING:
    from .window import Window

mod_mask = GLFW_MOD_ALT | GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_SUPER | GLFW_MOD_META | GLFW_MOD_HYPER


def keyboard_mode_name(screen: ScreenType) -> str:
    flags = screen.current_key_encoding_flags()
    if flags:
        return 'alatty'
    return 'application' if screen.cursor_key_mode else 'normal'


def get_shortcut(keymap: KeyMap, ev: KeyEvent) -> Optional[List[KeyDefinition]]:
    mods = ev.mods & mod_mask
    ans = keymap.get(SingleKey(mods, False, ev.key))
    if ans is None and ev.shifted_key and mods & GLFW_MOD_SHIFT:
        ans = keymap.get(SingleKey(mods & (~GLFW_MOD_SHIFT), False, ev.shifted_key))
    if ans is None:
        ans = keymap.get(SingleKey(mods, True, ev.native_key))
    return ans


class Mappings:

    ' Manage all keyboard mappings '

    def __init__(self, global_shortcuts:Optional[Dict[str, SingleKey]] = None) -> None:
        self.keyboard_mode_stack: List[KeyboardMode] = []
        self.update_keymap(global_shortcuts)

    def update_keymap(self, global_shortcuts:Optional[Dict[str, SingleKey]] = None) -> None:
        if global_shortcuts is None:
            global_shortcuts = self.set_cocoa_global_shortcuts(self.get_options()) if is_macos else {}
        self.global_shortcuts_map: KeyMap = {v: [KeyDefinition(definition=k)] for k, v in global_shortcuts.items()}
        self.global_shortcuts = global_shortcuts
        self.keyboard_modes = self.get_options().keyboard_modes.copy()
        km = self.keyboard_modes[''].keymap
        self.keyboard_modes[''].keymap = km = km.copy()
        for sc in self.global_shortcuts.values():
            km.pop(sc, None)

    def clear_keyboard_modes(self) -> None:
        self.keyboard_mode_stack = []
        self.set_ignore_os_keyboard_processing(False)

    def pop_keyboard_mode(self) -> bool:
        passthrough = True
        if self.keyboard_mode_stack:
            self.keyboard_mode_stack.pop()
            if not self.keyboard_mode_stack:
                self.set_ignore_os_keyboard_processing(False)
            passthrough = False
        return passthrough

    def _push_keyboard_mode(self, mode: KeyboardMode) -> None:
        self.keyboard_mode_stack.append(mode)
        self.set_ignore_os_keyboard_processing(True)

    def push_keyboard_mode(self, new_mode: str) -> None:
        mode = self.keyboard_modes[new_mode]
        self._push_keyboard_mode(mode)

    def matching_key_actions(self, candidates: Iterable[KeyDefinition]) -> List[KeyDefinition]:
        matches = []
        has_sequence_match = False
        for x in candidates:
            matches.append(x)
            if x.is_sequence:
                has_sequence_match = True
        if has_sequence_match:
            last_terminal_idx = -1
            for i, x in enumerate(matches):
                if not x.rest:
                    last_terminal_idx = i
            if last_terminal_idx > -1:
                if last_terminal_idx == len(matches) -1:
                    matches = matches[last_terminal_idx:]
                else:
                    matches = matches[last_terminal_idx+1:]
            q = matches[-1].options.when_focus_on
            matches = [x for x in matches if x.options.when_focus_on == q]
        else:
            matches = [matches[-1]]
        return matches

    def dispatch_possible_special_key(self, ev: KeyEvent) -> bool:
        # Handles shortcuts, return True if the key was consumed
        is_root_mode = not self.keyboard_mode_stack
        mode = self.keyboard_modes[''] if is_root_mode else self.keyboard_mode_stack[-1]
        key_action = get_shortcut(mode.keymap, ev)
        if key_action is None:
            if is_modifier_key(ev.key):
                return False
            if self.global_shortcuts_map and get_shortcut(self.global_shortcuts_map, ev):
                return True
            if not is_root_mode:
                if mode.sequence_keys is not None:
                    self.pop_keyboard_mode()
                    w = self.get_active_window()
                    if w is not None:
                        w.send_key_sequence(*mode.sequence_keys)
                    return False
                if mode.on_unknown in ('beep', 'ignore'):
                    return True
                if mode.on_unknown == 'passthrough':
                    return False
            if not self.pop_keyboard_mode():
                return True
        else:
            final_actions = self.matching_key_actions(key_action)
            if final_actions:
                mode_pos = len(self.keyboard_mode_stack) - 1
                if final_actions[0].is_sequence:
                    if mode.sequence_keys is None:
                        sm = KeyboardMode('__sequence__')
                        sm.on_action = 'end'
                        sm.sequence_keys = [ev]
                        for fa in final_actions:
                            sm.keymap[fa.rest[0]].append(fa.shift_sequence_and_copy())
                        self._push_keyboard_mode(sm)
                        self.debug_print('\n\x1b[35mKeyPress\x1b[m matched sequence prefix, ', end='')
                    else:
                        if len(final_actions) == 1 and not final_actions[0].rest:
                            self.pop_keyboard_mode()
                            consumed = self.combine(final_actions[0].definition)
                            if not consumed:
                                w = self.get_active_window()
                                if w is not None:
                                    w.send_key_sequence(*mode.sequence_keys)
                            return consumed
                        mode.sequence_keys.append(ev)
                        self.debug_print('\n\x1b[35mKeyPress\x1b[m matched sequence prefix, ', end='')
                        mode.keymap.clear()
                        for fa in final_actions:
                            mode.keymap[fa.rest[0]].append(fa.shift_sequence_and_copy())
                    return True
                final_action = final_actions[0]
                consumed = self.combine(final_action.definition)
                if consumed and not is_root_mode and mode.on_action == 'end':
                    if mode_pos < len(self.keyboard_mode_stack) and self.keyboard_mode_stack[mode_pos] is mode:
                        del self.keyboard_mode_stack[mode_pos]
                        if not self.keyboard_mode_stack:
                            self.set_ignore_os_keyboard_processing(False)
                return consumed
        return False

    # System integration {{{
    def get_active_window(self) -> Optional['Window']:
        return get_boss().active_window

    def show_error(self, title: str, msg: str) -> None:
        return get_boss().show_error(title, msg)

    def combine(self, action_definition: str) -> bool:
        return get_boss().combine(action_definition)

    def set_ignore_os_keyboard_processing(self, on: bool) -> None:
        set_ignore_os_keyboard_processing(on)

    def get_options(self) -> Options:
        return get_options()

    def debug_print(self, *args: Any, end: str = '\n') -> None:
        pass

    def set_cocoa_global_shortcuts(self, opts: Options) -> Dict[str, SingleKey]:
        from .main import set_cocoa_global_shortcuts
        return set_cocoa_global_shortcuts(opts)
    # }}}
