#!/usr/bin/env python
# License: GPLv3 Copyright: 2019, Kovid Goyal <kovid at kovidgoyal.net>


from typing import Any, Dict, FrozenSet, Iterable, Iterator, List, NamedTuple, Optional, Sequence, Tuple

from .boss import Boss
from .child import Child
from .cli import parse_args
from .cli_stub import LaunchCLIOptions
from .clipboard import set_clipboard_string, set_primary_selection
from .options.utils import env as parse_env
from .tabs import Tab, TabManager
from .types import OverlayType, run_once
from .utils import log_error, resolve_custom_file, which
from .window import CwdRequest, CwdRequestType, Watchers, Window

try:
    from typing import TypedDict
except ImportError:
    TypedDict = dict


class LaunchSpec(NamedTuple):
    opts: LaunchCLIOptions
    args: List[str]


@run_once
def options_spec() -> str:
    return '''
--window-title --title
The title to set for the new window. By default, title is controlled by the
child process. The special value :code:`current` will copy the title from the
currently active window.


--tab-title
The title for the new tab if launching in a new tab. By default, the title
of the active window in the tab is used as the tab title. The special value
:code:`current` will copy the title from the title of the currently active tab.


--type
type=choices
default=window
choices=window,tab,os-window,overlay,overlay-main,background,clipboard,primary
Where to launch the child process:

:code:`window`
    A new :term:`alatty window <window>` in the current tab

:code:`tab`
    A new :term:`tab` in the current OS window

:code:`os-window`
    A new :term:`operating system window <os_window>`

:code:`overlay`
    An :term:`overlay window <overlay>` covering the current active alatty window

:code:`overlay-main`
    An :term:`overlay window <overlay>` covering the current active alatty window.
    Unlike a plain overlay window, this window is considered as a :italic:`main`
    window which means it is used as the active window for getting the current working
    directory, the input text for kittens, launch commands, etc. Useful if this overlay is
    intended to run for a long time as a primary window.

:code:`background`
    The process will be run in the :italic:`background`, without a alatty
    window.

:code:`clipboard`, :code:`primary`
    These two are meant to work with :option:`--stdin-source <launch --stdin-source>` to copy
    data to the :italic:`system clipboard` or :italic:`primary selection`.

#placeholder_for_formatting#


--keep-focus --dont-take-focus
type=bool-set
Keep the focus on the currently active window instead of switching to the newly
opened window.


--cwd
completion=type:directory kwds:current,oldest,last_reported,root
The working directory for the newly launched child. Use the special value
:code:`current` to use the working directory of the currently active window.
The special value :code:`last_reported` uses the last working directory reported
by the shell (needs :ref:`shell_integration` to work). The special value
:code:`oldest` works like :code:`current` but uses the working directory of the
oldest foreground process associated with the currently active window rather
than the newest foreground process. Finally, the special value :code:`root`
refers to the process that was originally started when the window was created.


--env
type=list
Environment variables to set in the child process. Can be specified multiple
times to set different environment variables. Syntax: :code:`name=value`. Using
:code:`name=` will set to empty string and just :code:`name` will remove the
environment variable.


--var
type=list
User variables to set in the created window. Can be specified multiple
times to set different user variables. Syntax: :code:`name=value`. Using
:code:`name=` will set to empty string.


--hold
type=bool-set
Keep the window open even after the command being executed exits, at a shell prompt.


--copy-colors
type=bool-set
Set the colors of the newly created window to be the same as the colors in the
currently active window.


--copy-cmdline
type=bool-set
Ignore any specified command line and instead use the command line from the
currently active window.


--copy-env
type=bool-set
Copy the environment variables from the currently active window into the newly
launched child process. Note that this only copies the environment when the
window was first created, as it is not possible to get updated environment variables
from arbitrary processes. To copy that environment, use either the :ref:`clone-in-alatty
<clone_shell>` feature or the alatty remote control feature with :option:`kitten @ launch --copy-env`.


--location
type=choices
default=default
choices=first,after,before,neighbor,last,vsplit,hsplit,split,default
Where to place the newly created window when it is added to a tab which already
has existing windows in it. :code:`after` and :code:`before` place the new
window before or after the active window. :code:`neighbor` is a synonym for
:code:`after`. Also applies to creating a new tab, where the value of
:code:`after` will cause the new tab to be placed next to the current tab
instead of at the end. The values of :code:`vsplit`, :code:`hsplit` and
:code:`split` are only used by the :code:`splits` layout and control if the new
window is placed in a vertical, horizontal or automatic split with the currently
active window. The default is to place the window in a layout dependent manner,
typically, after the currently active window.


--stdin-source
type=choices
default=none
choices=none,@selection,@screen,@screen_scrollback,@alternate,@alternate_scrollback,@first_cmd_output_on_screen,@last_cmd_output,@last_visited_cmd_output
Pass the screen contents as :file:`STDIN` to the child process.

:code:`@selection`
    is the currently selected text.

:code:`@screen`
    is the contents of the currently active window.

:code:`@screen_scrollback`
    is the same as :code:`@screen`, but includes the scrollback buffer as well.

:code:`@alternate`
    is the secondary screen of the current active window. For example if you run
    a full screen terminal application, the secondary screen will
    be the screen you return to when quitting the application.

:code:`@first_cmd_output_on_screen`
    is the output from the first command run in the shell on screen.

:code:`@last_cmd_output`
    is the output from the last command run in the shell.

#placeholder_for_formatting#


--stdin-add-formatting
type=bool-set
When using :option:`--stdin-source <launch --stdin-source>` add formatting
escape codes, without this only plain text will be sent.


--os-window-class
Set the :italic:`WM_CLASS` property on X11 and the application id property on
Wayland for the newly created OS window when using :option:`--type=os-window
<launch --type>`. Defaults to whatever is used by the parent alatty process,
which in turn defaults to :code:`alatty`.


--os-window-name
Set the :italic:`WM_NAME` property on X11 for the newly created OS Window when
using :option:`--type=os-window <launch --type>`. Defaults to
:option:`--os-window-class <launch --os-window-class>`.


--os-window-title
Set the title for the newly created OS window. This title will override any
titles set by programs running in alatty. The special value :code:`current` will
use the title of the current OS window, if any.


--os-window-state
type=choices
default=normal
choices=normal,fullscreen,maximized,minimized
The initial state for the newly created OS Window.


--logo
completion=type:file ext:png group:"PNG images" relative:conf
Path to a PNG image to use as the logo for the newly created window. See
:opt:`window_logo_path`. Relative paths are resolved from the alatty configuration directory.


--logo-position
The position for the window logo. Only takes effect if :option:`--logo` is
specified. See :opt:`window_logo_position`.


--logo-alpha
type=float
default=-1
The amount the window logo should be faded into the background. Only takes
effect if :option:`--logo` is specified. See :opt:`window_logo_alpha`.


--color
type=list
Change colors in the newly launched window. You can either specify a path to a
:file:`.conf` file with the same syntax as :file:`alatty.conf` to read the colors
from, or specify them individually, for example::

    --color background=white --color foreground=red


--spacing
type=list
Set the margin and padding for the newly created window.
For example: :code:`margin=20` or :code:`padding-left=10` or :code:`margin-h=30`. The shorthand form sets
all values, the :code:`*-h` and :code:`*-v` variants set horizontal and vertical values.
Can be specified multiple times. Note that this is ignored for overlay windows as these use the settings
from the base window.


--watcher -w
type=list
completion=type:file ext:py relative:conf group:"Python scripts"
Path to a Python file. Appropriately named functions in this file will be called
for various events, such as when the window is resized, focused or closed. See
the section on watchers in the launch command documentation: :ref:`watchers`.
Relative paths are resolved relative to the :ref:`alatty config directory
<confloc>`. Global watchers for all windows can be specified with
:opt:`watcher` in :file:`alatty.conf`.
'''


def parse_launch_args(args: Optional[Sequence[str]] = None) -> LaunchSpec:
    args = list(args or ())
    try:
        opts, args = parse_args(result_class=LaunchCLIOptions, args=args, ospec=options_spec)
    except SystemExit as e:
        raise ValueError(str(e)) from e
    return LaunchSpec(opts, args)


def get_env(opts: LaunchCLIOptions, active_child: Optional[Child] = None, base_env: Optional[Dict[str,str]] = None) -> Dict[str, str]:
    env: Dict[str, str] = {}
    if opts.copy_env and active_child:
        env.update(active_child.foreground_environ)
    if base_env is not None:
        env.update(base_env)
    for x in opts.env:
        for k, v in parse_env(x, env):
            env[k] = v
    return env


def tab_for_window(boss: Boss, opts: LaunchCLIOptions, target_tab: Optional[Tab] = None) -> Optional[Tab]:

    def create_tab(tm: Optional[TabManager] = None) -> Tab:
        if tm is None:
            oswid = boss.add_os_window(
                wclass=opts.os_window_class,
                wname=opts.os_window_name,
                window_state=opts.os_window_state,
                override_title=opts.os_window_title or None)
            tm = boss.os_window_map[oswid]
        tab = tm.new_tab(empty_tab=True, location=opts.location)
        if opts.tab_title:
            tab.set_title(opts.tab_title)
        return tab

    if opts.type == 'tab':
        if target_tab is not None:
            tm = target_tab.tab_manager_ref() or boss.active_tab_manager
        else:
            tm = boss.active_tab_manager
        tab = create_tab(tm)
    elif opts.type == 'os-window':
        tab = create_tab()
    else:
        tab = target_tab or boss.active_tab or create_tab()

    return tab


watcher_modules: Dict[str, Any] = {}


def load_watch_modules(watchers: Iterable[str]) -> Optional[Watchers]:
    if not watchers:
        return None
    import runpy
    ans = Watchers()
    for path in watchers:
        path = resolve_custom_file(path)
        m = watcher_modules.get(path, None)
        if m is None:
            try:
                m = runpy.run_path(path, run_name='__alatty_watcher__')
            except Exception as err:
                import traceback
                log_error(traceback.format_exc())
                log_error(f'Failed to load watcher from {path} with error: {err}')
                watcher_modules[path] = False
                continue
            watcher_modules[path] = m
        if m is False:
            continue
        w = m.get('on_close')
        if callable(w):
            ans.on_close.append(w)
        w = m.get('on_resize')
        if callable(w):
            ans.on_resize.append(w)
        w = m.get('on_focus_change')
        if callable(w):
            ans.on_focus_change.append(w)
        w = m.get('on_set_user_var')
        if callable(w):
            ans.on_set_user_var.append(w)
        w = m.get('on_title_change')
        if callable(w):
            ans.on_title_change.append(w)
        w = m.get('on_cmd_startstop')
        if callable(w):
            ans.on_cmd_startstop.append(w)
    return ans


class LaunchKwds(TypedDict):

    cwd_from: Optional[CwdRequest]
    cwd: Optional[str]
    location: Optional[str]
    override_title: Optional[str]
    copy_colors_from: Optional[Window]
    cmd: Optional[List[str]]
    overlay_for: Optional[int]
    stdin: Optional[bytes]
    hold: bool


def parse_var(defn: Iterable[str]) -> Iterator[Tuple[str, str]]:
    for item in defn:
        a, sep, b = item.partition('=')
        yield a, b


class ForceWindowLaunch:

    def __init__(self) -> None:
        self.force = False

    def __bool__(self) -> bool:
        return self.force

    def __call__(self, force: bool) -> 'ForceWindowLaunch':
        self.force = force
        return self

    def __enter__(self) -> None:
        pass

    def __exit__(self, *a: object) -> None:
        self.force = False


force_window_launch = ForceWindowLaunch()
non_window_launch_types = 'background', 'clipboard', 'primary'


def _launch(
    boss: Boss,
    opts: LaunchCLIOptions,
    args: List[str],
    target_tab: Optional[Tab] = None,
    force_target_tab: bool = False,
    active: Optional[Window] = None,
    is_clone_launch: str = '',
    rc_from_window: Optional[Window] = None,
    base_env: Optional[Dict[str, str]] = None,
) -> Optional[Window]:
    active = active or boss.active_window_for_cwd
    if active:
        active_child = active.child
    else:
        active_child = None
    if opts.window_title == 'current':
        opts.window_title = active.title if active else None
    if opts.tab_title == 'current':
        atab = boss.active_tab
        opts.tab_title = atab.effective_title if atab else None
    if opts.os_window_title == 'current':
        opts.os_window_title = None
    env = get_env(opts, active_child, base_env)
    kw: LaunchKwds = {
        'cwd_from': None,
        'cwd': None,
        'location': None,
        'override_title': opts.window_title or None,
        'copy_colors_from': None,
        'cmd': None,
        'overlay_for': None,
        'stdin': None,
        'hold': False,
    }
    if opts.cwd:
        if opts.cwd == 'current':
            if active:
                kw['cwd_from'] = CwdRequest(active)
        elif opts.cwd == 'last_reported':
            if active:
                kw['cwd_from'] = CwdRequest(active, CwdRequestType.last_reported)
        elif opts.cwd == 'oldest':
            if active:
                kw['cwd_from'] = CwdRequest(active, CwdRequestType.oldest)
        elif opts.cwd == 'root':
            if active:
                kw['cwd_from'] = CwdRequest(active, CwdRequestType.root)
        else:
            kw['cwd'] = opts.cwd
        if kw['cwd_from'] is not None and rc_from_window is not None:
            kw['cwd_from'].rc_from_window_id = rc_from_window.id
    if opts.location != 'default':
        kw['location'] = opts.location
    if opts.copy_colors and active:
        kw['copy_colors_from'] = active
    pipe_data: Dict[str, Any] = {}
    if opts.stdin_source != 'none':
        q = str(opts.stdin_source)
        if opts.stdin_add_formatting:
            if q in ('@screen', '@screen_scrollback', '@alternate', '@alternate_scrollback',
                     '@first_cmd_output_on_screen', '@last_cmd_output', '@last_visited_cmd_output'):
                q = f'@ansi_{q[1:]}'
        penv, stdin = boss.process_stdin_source(window=active, stdin=q, copy_pipe_data=pipe_data)
        if stdin:
            kw['stdin'] = stdin
            if penv:
                env.update(penv)

    cmd = args or None
    if opts.copy_cmdline and active_child:
        cmd = active_child.foreground_cmdline
    if cmd:
        final_cmd: List[str] = []
        for x in cmd:
            if active and not opts.copy_cmdline:
                if x == '@selection':
                    s = boss.data_for_at(which=x, window=active)
                    if s:
                        x = s
                elif x == '@active-alatty-window-id':
                    x = str(active.id)
                elif x == '@input-line-number':
                    if 'input_line_number' in pipe_data:
                        x = str(pipe_data['input_line_number'])
                elif x == '@line-count':
                    if 'lines' in pipe_data:
                        x = str(pipe_data['lines'])
                elif x in ('@cursor-x', '@cursor-y', '@scrolled-by', '@first-line-on-screen', '@last-line-on-screen'):
                    if active is not None:
                        screen = active.screen
                        if x == '@scrolled-by':
                            x = str(screen.scrolled_by)
                        elif x == '@cursor-x':
                            x = str(screen.cursor.x + 1)
                        elif x == '@cursor-y':
                            x = str(screen.cursor.y + 1)
                        elif x == '@first-line-on-screen':
                            x = str(screen.visual_line(0) or '')
                        elif x == '@last-line-on-screen':
                            x = str(screen.visual_line(screen.lines - 1) or '')
            final_cmd.append(x)
        if rc_from_window is None and final_cmd:
            exe = which(final_cmd[0])
            if exe:
                final_cmd[0] = exe
        kw['cmd'] = final_cmd
    if force_window_launch and opts.type not in non_window_launch_types:
        opts.type = 'window'
    base_for_overlay = active
    if target_tab:
        base_for_overlay = target_tab.active_window
    if opts.type in ('overlay', 'overlay-main') and base_for_overlay:
        kw['overlay_for'] = base_for_overlay.id
    if opts.type == 'background':
        cmd = kw['cmd']
        if not cmd:
            raise ValueError('The cmd to run must be specified when running a background process')
        boss.run_background_process(
            cmd, cwd=kw['cwd'], cwd_from=kw['cwd_from'], env=env or None, stdin=kw['stdin']
        )
    elif opts.type in ('clipboard', 'primary'):
        stdin = kw.get('stdin')
        if stdin is not None:
            if opts.type == 'clipboard':
                set_clipboard_string(stdin)
            else:
                set_primary_selection(stdin)
    else:
        kw['hold'] = opts.hold
        if force_target_tab:
            tab = target_tab
        else:
            tab = tab_for_window(boss, opts, target_tab)
        if tab is not None:
            watchers = load_watch_modules(opts.watcher)
            with Window.set_ignore_focus_changes_for_new_windows(opts.keep_focus):
                new_window: Window = tab.new_window(
                    env=env or None, watchers=watchers or None, is_clone_launch=is_clone_launch, **kw)
            if opts.keep_focus:
                if active:
                    boss.set_active_window(active, switch_os_window_if_needed=True, for_keep_focus=True)
                if not Window.initial_ignore_focus_changes_context_manager_in_operation:
                    new_window.ignore_focus_changes = False
            if opts.type == 'overlay-main':
                new_window.overlay_type = OverlayType.main
            if opts.var:
                for key, val in parse_var(opts.var):
                    new_window.set_user_var(key, val)
            return new_window
    return None


def launch(
    boss: Boss,
    opts: LaunchCLIOptions,
    args: List[str],
    target_tab: Optional[Tab] = None,
    force_target_tab: bool = False,
    active: Optional[Window] = None,
    is_clone_launch: str = '',
    rc_from_window: Optional[Window] = None,
    base_env: Optional[Dict[str, str]] = None,
) -> Optional[Window]:
    active = active or boss.active_window_for_cwd
    if opts.keep_focus and active:
        orig, active.ignore_focus_changes = active.ignore_focus_changes, True
    try:
        return _launch(boss, opts, args, target_tab, force_target_tab, active, is_clone_launch, rc_from_window, base_env)
    finally:
        if opts.keep_focus and active:
            active.ignore_focus_changes = orig

@run_once
def clone_safe_opts() -> FrozenSet[str]:
    return frozenset((
        'window_title', 'tab_title', 'type', 'keep_focus', 'cwd', 'env', 'var', 'hold',
        'location', 'os_window_class', 'os_window_name', 'os_window_title', 'os_window_state',
        'logo', 'logo_position', 'logo_alpha', 'color', 'spacing',
    ))
