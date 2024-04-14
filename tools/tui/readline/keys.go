// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package readline

import (
	"errors"
	"fmt"
	"strconv"

	"alatty/tools/tui/loop"
	"alatty/tools/tui/shortcuts"
)

var _ = fmt.Print

type ShortcutMap = shortcuts.ShortcutMap[Action]

type KeyboardState struct {
	active_shortcut_maps     []*ShortcutMap
	current_pending_keys     []string
	current_numeric_argument string
}

var _default_shortcuts *ShortcutMap

func default_shortcuts() *ShortcutMap {
	if _default_shortcuts == nil {
		sm := shortcuts.New[Action]()
		sm.AddOrPanic(ActionBackspace, "backspace")
		sm.AddOrPanic(ActionBackspace, "ctrl+h")
		sm.AddOrPanic(ActionDelete, "delete")

		sm.AddOrPanic(ActionCursorLeft, "left")
		sm.AddOrPanic(ActionCursorLeft, "ctrl+b")
		sm.AddOrPanic(ActionCursorRight, "right")
		sm.AddOrPanic(ActionCursorRight, "ctrl+f")

		sm.AddOrPanic(ActionClearScreen, "ctrl+l")
		sm.AddOrPanic(ActionAbortCurrentLine, "ctrl+c")
		sm.AddOrPanic(ActionAbortCurrentLine, "ctrl+g")

		sm.AddOrPanic(ActionEndInput, "ctrl+d")
		sm.AddOrPanic(ActionAcceptInput, "enter")

		sm.AddOrPanic(ActionKillToEndOfLine, "ctrl+k")
		sm.AddOrPanic(ActionKillToStartOfLine, "ctrl+x")
		sm.AddOrPanic(ActionKillToStartOfLine, "ctrl+u")
		sm.AddOrPanic(ActionKillNextWord, "alt+d")
		sm.AddOrPanic(ActionKillPreviousWord, "alt+backspace")
		sm.AddOrPanic(ActionKillPreviousSpaceDelimitedWord, "ctrl+w")
		sm.AddOrPanic(ActionYank, "ctrl+y")
		sm.AddOrPanic(ActionPopYank, "alt+y")

		_default_shortcuts = sm
	}
	return _default_shortcuts
}

var _history_search_shortcuts *shortcuts.ShortcutMap[Action]

func history_search_shortcuts() *shortcuts.ShortcutMap[Action] {
	if _history_search_shortcuts == nil {
		sm := shortcuts.New[Action]()
		sm.AddOrPanic(ActionBackspace, "backspace")
		sm.AddOrPanic(ActionBackspace, "ctrl+h")

		_history_search_shortcuts = sm
	}
	return _history_search_shortcuts
}

var ErrCouldNotPerformAction = errors.New("Could not perform the specified action")
var ErrAcceptInput = errors.New("Accept input")

func (self *Readline) push_keyboard_map(m *ShortcutMap) {
	maps := self.keyboard_state.active_shortcut_maps
	self.keyboard_state = KeyboardState{}
	if maps == nil {
		maps = make([]*ShortcutMap, 0, 2)
	}
	self.keyboard_state.active_shortcut_maps = append(maps, m)
}

func (self *Readline) pop_keyboard_map() {
	maps := self.keyboard_state.active_shortcut_maps
	self.keyboard_state = KeyboardState{}
	if len(maps) > 0 {
		maps = maps[:len(maps)-1]
		self.keyboard_state.active_shortcut_maps = maps
	}
}

func (self *Readline) dispatch_key_action(ac Action) error {
	self.keyboard_state.current_pending_keys = nil
	cna := self.keyboard_state.current_numeric_argument
	self.keyboard_state.current_numeric_argument = ""
	if cna == "" {
		cna = "1"
	}
	repeat_count, err := strconv.Atoi(cna)
	if err != nil || repeat_count <= 0 {
		repeat_count = 1
	}
	return self.perform_action(ac, uint(repeat_count))
}

func (self *Readline) handle_key_event(event *loop.KeyEvent) error {
	if event.Text != "" {
		return nil
	}
	sm := default_shortcuts()
	if len(self.keyboard_state.active_shortcut_maps) > 0 {
		sm = self.keyboard_state.active_shortcut_maps[len(self.keyboard_state.active_shortcut_maps)-1]
	}
	ac, pending := sm.ResolveKeyEvent(event, self.keyboard_state.current_pending_keys...)
	if pending != "" {
		event.Handled = true
		if self.keyboard_state.current_pending_keys == nil {
			self.keyboard_state.current_pending_keys = []string{}
		}
		self.keyboard_state.current_pending_keys = append(self.keyboard_state.current_pending_keys, pending)
	} else {
		self.keyboard_state.current_pending_keys = nil
		if ac != ActionNil {
			event.Handled = true
			return self.dispatch_key_action(ac)
		}
	}
	return nil
}
