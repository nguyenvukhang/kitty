#!/usr/bin/env python
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

import os
import shlex
import sys
from contextlib import suppress
from functools import partial
from typing import TYPE_CHECKING, Callable, Generator, Iterator, List, Mapping, Optional, Tuple, Union

from .cli_stub import CLIOptions
from .layout.interface import all_layouts
from .options.types import Options
from .options.utils import resize_window, to_layout_names, window_size
from .os_window_size import WindowSize, WindowSizeData, WindowSizes
from .typing import SpecialWindowInstance
from .utils import expandvars, log_error, resolve_custom_file, resolved_shell, shlex_split

if TYPE_CHECKING:
    from .launch import LaunchSpec
    from .window import CwdRequest


def get_os_window_sizing_data(opts: Options, session: Optional['Session'] = None) -> WindowSizeData:
    if session is None or session.os_window_size is None:
        sizes = WindowSizes(WindowSize(*opts.initial_window_width), WindowSize(*opts.initial_window_height))
    else:
        sizes = session.os_window_size
    return WindowSizeData(
        sizes, opts.remember_window_size, opts.single_window_margin_width, opts.window_margin_width,
        opts.single_window_padding_width, opts.window_padding_width)


ResizeSpec = Tuple[str, int]


class WindowSpec:

    def __init__(self, launch_spec: Union['LaunchSpec', 'SpecialWindowInstance']):
        self.launch_spec = launch_spec
        self.resize_spec: Optional[ResizeSpec] = None


class Tab:

    def __init__(self, opts: Options, name: str):
        self.windows: List[WindowSpec] = []
        self.pending_resize_spec: Optional[ResizeSpec] = None
        self.name = name.strip()
        self.active_window_idx = 0
        self.enabled_layouts = opts.enabled_layouts
        self.layout = (self.enabled_layouts or ['tall'])[0]
        self.cwd: Optional[str] = None
        self.next_title: Optional[str] = None


class Session:

    def __init__(self, default_title: Optional[str] = None):
        self.tabs: List[Tab] = []
        self.active_tab_idx = 0
        self.default_title = default_title
        self.os_window_size: Optional[WindowSizes] = None
        self.os_window_class: Optional[str] = None
        self.os_window_state: Optional[str] = None
        self.focus_os_window: bool = False

    def add_tab(self, opts: Options, name: str = '') -> None:
        if self.tabs and not self.tabs[-1].windows:
            del self.tabs[-1]
        self.tabs.append(Tab(opts, name))

    def set_next_title(self, title: str) -> None:
        self.tabs[-1].next_title = title.strip()

    def set_layout(self, val: str) -> None:
        if val.partition(':')[0] not in all_layouts:
            raise ValueError(f'{val} is not a valid layout')
        self.tabs[-1].layout = val

    def add_window(self, cmd: Union[None, str, List[str]], expand: Callable[[str], str] = lambda x: x) -> None:
        from .launch import parse_launch_args
        needs_expandvars = False
        if isinstance(cmd, str):
            needs_expandvars = True
            cmd = list(shlex_split(cmd))
        spec = parse_launch_args(cmd)
        if needs_expandvars:
            assert isinstance(cmd, list)
            limit = len(cmd)
            if len(spec.args):
                with suppress(ValueError):
                    limit = cmd.index(spec.args[0])
            cmd = [(expand(x) if i < limit else x) for i, x in enumerate(cmd)]
            spec = parse_launch_args(cmd)

        t = self.tabs[-1]
        if t.next_title and not spec.opts.window_title:
            spec.opts.window_title = t.next_title
        spec.opts.cwd = spec.opts.cwd or t.cwd
        t.windows.append(WindowSpec(spec))
        t.next_title = None
        if t.pending_resize_spec is not None:
            t.windows[-1].resize_spec = t.pending_resize_spec
            t.pending_resize_spec = None

    def resize_window(self, args: List[str]) -> None:
        s = resize_window('resize_window', shlex.join(args))[1]
        spec: ResizeSpec = s[0], s[1]
        t = self.tabs[-1]
        if t.windows:
            t.windows[-1].resize_spec = spec
        else:
            t.pending_resize_spec = spec

    def add_special_window(self, sw: 'SpecialWindowInstance') -> None:
        self.tabs[-1].windows.append(WindowSpec(sw))

    def focus(self) -> None:
        self.active_tab_idx = max(0, len(self.tabs) - 1)
        self.tabs[-1].active_window_idx = max(0, len(self.tabs[-1].windows) - 1)

    def set_enabled_layouts(self, raw: str) -> None:
        self.tabs[-1].enabled_layouts = to_layout_names(raw)
        if self.tabs[-1].layout not in self.tabs[-1].enabled_layouts:
            self.tabs[-1].layout = self.tabs[-1].enabled_layouts[0]

    def set_cwd(self, val: str) -> None:
        self.tabs[-1].cwd = val


def create_sessions(
    opts: Options,
    args: Optional[CLIOptions] = None,
    special_window: Optional['SpecialWindowInstance'] = None,
    cwd_from: Optional['CwdRequest'] = None,
) -> Iterator[Session]:
    ans = Session()
    current_layout = opts.enabled_layouts[0] if opts.enabled_layouts else 'tall'
    ans.add_tab(opts)
    ans.tabs[-1].layout = current_layout
    if special_window is None:
        cmd = args.args if args and args.args else resolved_shell(opts)
        from alatty.tabs import SpecialWindow
        special_window = SpecialWindow(cmd, cwd_from=cwd_from, hold=bool(args and args.hold))
    ans.add_special_window(special_window)
    yield ans
