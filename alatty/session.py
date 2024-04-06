#!/usr/bin/env python
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

from typing import TYPE_CHECKING, Iterator, List, Optional, Tuple, Union

from .cli_stub import CLIOptions
from .options.types import Options
from .os_window_size import WindowSize, WindowSizeData, WindowSizes
from .typing import SpecialWindowInstance
from .utils import resolved_shell

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

    def add_special_window(self, sw: 'SpecialWindowInstance') -> None:
        self.tabs[-1].windows.append(WindowSpec(sw))

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
