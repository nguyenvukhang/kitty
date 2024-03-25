#!/usr/bin/env python
# License: GPL v3 Copyright: 2018, Kovid Goyal <kovid at kovidgoyal.net>

from contextlib import contextmanager
from enum import Enum, auto
from typing import Callable, Generator, TypeVar, Dict, Any

F = TypeVar('F')
all_cmds: Dict[str, Callable[..., Any]] = {}


class Mode(Enum):
    LNM = 20, ''
    IRM = 4, ''
    DECKM = 1, '?'
    DECSCNM = 5, '?'
    DECOM = 6, '?'
    DECAWM = 7, '?'
    DECARM = 8, '?'
    DECTCEM = 25, '?'
    MOUSE_BUTTON_TRACKING = 1000, '?'
    MOUSE_MOTION_TRACKING = 1002, '?'
    MOUSE_MOVE_TRACKING = 1003, '?'
    FOCUS_TRACKING = 1004, '?'
    MOUSE_UTF8_MODE = 1005, '?'
    MOUSE_SGR_MODE = 1006, '?'
    MOUSE_URXVT_MODE = 1015, '?'
    MOUSE_SGR_PIXEL_MODE = 1016, '?'
    ALTERNATE_SCREEN = 1049, '?'
    BRACKETED_PASTE = 2004, '?'
    PENDING_UPDATE = 2026, '?'
    HANDLE_TERMIOS_SIGNALS = 19997, '?'


def cmd(f: F) -> F:
    all_cmds[f.__name__] = f  # type: ignore
    return f


@cmd
def set_mode(which: Mode) -> str:
    num, private = which.value
    return f'\033[{private}{num}h'


@cmd
def reset_mode(which: Mode) -> str:
    num, private = which.value
    return f'\033[{private}{num}l'


class MouseTracking(Enum):
    none = auto()
    buttons_only = auto()
    buttons_and_drag = auto()
    full = auto()


@contextmanager
def pending_update(write: Callable[[str], None]) -> Generator[None, None, None]:
    write(set_mode(Mode.PENDING_UPDATE))
    try:
        yield
    finally:
        write(reset_mode(Mode.PENDING_UPDATE))
