#!/usr/bin/env python
# License: GPL v3 Copyright: 2018, Kovid Goyal <kovid at kovidgoyal.net>

import os
import re
from functools import lru_cache, wraps
from string import Formatter as StringFormatter
from typing import (
    Any,
    Callable,
    Dict,
    List,
    NamedTuple,
    Optional,
    Sequence,
    Tuple,
)

from .borders import Border, BorderColor
from .config import build_ansi_color_table
from .constants import config_dir
from .fast_data_types import (
    DECAWM,
    Color,
    Region,
    Screen,
    cell_size_for_window,
    get_boss,
    get_options,
    pt_to_px,
    set_tab_bar_render_data,
    update_tab_bar_edge_colors,
    viewport_for_window,
)
from .rgb import color_as_sgr, color_from_int, to_color
from .types import WindowGeometry, run_once
from .typing import EdgeLiteral
from .utils import color_as_int, log_error, sgr_sanitizer_pat


class TabBarData(NamedTuple):
    title: str
    is_active: bool
    needs_attention: bool
    tab_id: int
    num_windows: int
    num_window_groups: int
    layout_name: str
    has_activity_since_last_focus: bool
    active_fg: Optional[int]
    active_bg: Optional[int]
    inactive_fg: Optional[int]
    inactive_bg: Optional[int]


class DrawData(NamedTuple):
    leading_spaces: int
    sep: str
    trailing_spaces: int
    alpha: Sequence[float]
    active_fg: Color
    active_bg: Color
    inactive_fg: Color
    inactive_bg: Color
    default_bg: Color
    title_template: str
    active_title_template: Optional[str]
    tab_activity_symbol: str
    tab_bar_edge: EdgeLiteral
    max_tab_title_length: int

    def tab_fg(self, tab: TabBarData) -> int:
        if tab.is_active:
            if tab.active_fg is not None:
                return tab.active_fg
            return int(self.active_fg)
        if tab.inactive_fg is not None:
            return tab.inactive_fg
        return int(self.inactive_fg)

    def tab_bg(self, tab: TabBarData) -> int:
        if tab.is_active:
            if tab.active_bg is not None:
                return tab.active_bg
            return int(self.active_bg)
        if tab.inactive_bg is not None:
            return tab.inactive_bg
        return int(self.inactive_bg)


def as_rgb(x: int) -> int:
    return (x << 8) | 2


@lru_cache()
def report_template_failure(template: str, e: str) -> None:
    log_error(f'Invalid tab title template: "{template}" with error: {e}')


@lru_cache()
def compile_template(template: str) -> Any:
    try:
        return compile('f"""' + template + '"""', '<template>', 'eval')
    except Exception as e:
        report_template_failure(template, str(e))


class ColorFormatter:

    draw_data: DrawData
    tab_data: TabBarData

    def __init__(self, which: str):
        self.which = which

    def __getattr__(self, name: str) -> str:
        q = name
        if q == 'default':
            ans = '9'
        elif q == 'tab':
            col = color_from_int((self.draw_data.tab_bg if self.which == '4' else self.draw_data.tab_fg)(self.tab_data))
            ans = f'8{color_as_sgr(col)}'
        else:
            if name.startswith('_'):
                q = f'#{name[1:]}'
            c = to_color(q)
            if c is None:
                raise AttributeError(f'{name} is not a valid color')
            ans = f'8{color_as_sgr(c)}'
        return f'\x1b[{self.which}{ans}m'


class Formatter:
    reset = '\x1b[0m'
    fg = ColorFormatter('3')
    bg = ColorFormatter('4')


@run_once
def super_sub_maps() -> Tuple[Dict[int, int], Dict[int, int]]:
    import string
    sup_table = str.maketrans(
        string.ascii_lowercase + string.ascii_uppercase + string.digits + '+-=()',
        'ᵃᵇᶜᵈᵉᶠᵍʰⁱʲᵏˡᵐⁿᵒᵖqʳˢᵗᵘᵛʷˣʸᶻ' 'ᴬᴮᶜᴰᴱᶠᴳᴴᴵᴶᴷᴸᴹᴺᴼᴾQᴿˢᵀᵁⱽᵂˣʸᶻ' '⁰¹²³⁴⁵⁶⁷⁸⁹' '⁺⁻⁼⁽⁾')
    sub_table = str.maketrans(
        string.ascii_lowercase + string.ascii_uppercase + string.digits + '+-=()',
        'ₐbcdₑfgₕᵢⱼₖₗₘₙₒₚqᵣₛₜᵤᵥwₓyz' 'ₐbcdₑfgₕᵢⱼₖₗₘₙₒₚqᵣₛₜᵤᵥwₓyz' '₀₁₂₃₄₅₆₇₈₉' '₊₋₌₍₎')
    return sup_table, sub_table


class SupSub:

    def __init__(self, data: Dict[str, Any], is_subscript: bool = False):
        self.__data = data
        self.__is_subscript = is_subscript

    def __getattr__(self, name: str) -> str:
        name = str(self.__data.get(name, name))
        table = super_sub_maps()[int(self.__is_subscript)]
        return name.translate(table)


class ExtraData:
    prev_tab: Optional[TabBarData] = None
    next_tab: Optional[TabBarData] = None
    # true if the draw_tab function is called just for layout. In such cases,
    # if drawing is expensive the draw_tab function should avoid drawing and
    # just move the cursor to its final position, as if drawing was performed.
    for_layout: bool = False


def draw_attributed_string(title: str, screen: Screen) -> None:
    if '\x1b' in title:
        for x in sgr_sanitizer_pat(for_splitting=True).split(title):
            if x.startswith('\x1b') and x.endswith('m'):
                screen.apply_sgr(x[2:-1])
            else:
                screen.draw(x)
    else:
        screen.draw(title)


@lru_cache(maxsize=16)
def template_has_field(template: str, field: str) -> bool:
    q = StringFormatter()
    for (literal_text, field_name, format_spec, conversion) in q.parse(template):
        if field_name and field in field_name.split():
            return True
    return False


class TabAccessor:

    def __init__(self, tab_id: int):
        self.tab_id = tab_id

    @property
    def active_wd(self) -> str:
        tab = get_boss().tab_for_id(self.tab_id)
        return (tab.get_cwd_of_active_window() if tab else '') or ''

    @property
    def active_oldest_wd(self) -> str:
        tab = get_boss().tab_for_id(self.tab_id)
        return (tab.get_cwd_of_active_window(oldest=True) if tab else '') or ''

    @property
    def active_exe(self) -> str:
        tab = get_boss().tab_for_id(self.tab_id)
        return os.path.basename((tab.get_exe_of_active_window() if tab else '') or '')

    @property
    def active_oldest_exe(self) -> str:
        tab = get_boss().tab_for_id(self.tab_id)
        return os.path.basename((tab.get_exe_of_active_window(oldest=True) if tab else '') or '')


safe_builtins = {
    'max': max, 'min': min, 'str': str, 'repr': repr, 'abs': abs, 'len': len, 'chr': chr, 'ord': ord, 're': re,
}


def draw_title(draw_data: DrawData, screen: Screen, tab: TabBarData, index: int, max_title_length: int = 0) -> None:
    ta = TabAccessor(tab.tab_id)
    data = {
        'index': index,
        'layout_name': tab.layout_name,
        'num_windows': tab.num_windows,
        'num_window_groups': tab.num_window_groups,
        'title': tab.title,
        'tab': ta,
    }
    if draw_data.max_tab_title_length > 0:
        max_title_length = min(max_title_length, draw_data.max_tab_title_length)
    ColorFormatter.draw_data = draw_data
    ColorFormatter.tab_data = tab
    eval_locals = {
        'index': index,
        'layout_name': tab.layout_name,
        'num_windows': tab.num_windows,
        'num_window_groups': tab.num_window_groups,
        'title': tab.title,
        'tab': ta,
        'fmt': Formatter,
        'sup': SupSub(data),
        'sub': SupSub(data, True),
        'activity_symbol': draw_data.tab_activity_symbol if tab.has_activity_since_last_focus else '',
        'max_title_length': max_title_length,
    }
    template = draw_data.title_template
    if tab.is_active and draw_data.active_title_template is not None:
        template = draw_data.active_title_template
    prefix = ''
    if eval_locals['activity_symbol'] and not template_has_field(template, 'activity_symbol'):
        prefix += '{activity_symbol}'
    if prefix:
        template = '{fmt.fg.red}' + prefix + '{fmt.fg.tab}' + template
    try:
        title = eval(compile_template(template), {'__builtins__': safe_builtins}, eval_locals)
    except Exception as e:
        report_template_failure(template, str(e))
        title = tab.title
    before_draw = screen.cursor.x
    draw_attributed_string(title, screen)
    if draw_data.max_tab_title_length > 0:
        x_limit = before_draw + draw_data.max_tab_title_length
        if screen.cursor.x > x_limit:
            screen.cursor.x = x_limit - 1
            screen.draw('…')


DrawTabFunc = Callable[[DrawData, Screen, TabBarData, int, int, int, bool, ExtraData], int]

def draw_tab_with_separator(
    draw_data: DrawData, screen: Screen, tab: TabBarData,
    before: int, max_tab_length: int, index: int, is_last: bool,
    extra_data: ExtraData
) -> int:
    if draw_data.leading_spaces:
        screen.draw(' ' * draw_data.leading_spaces)
    draw_title(draw_data, screen, tab, index, max_tab_length)
    trailing_spaces = min(max_tab_length - 1, draw_data.trailing_spaces)
    max_tab_length -= trailing_spaces
    extra = screen.cursor.x - before - max_tab_length
    if extra > 0:
        screen.cursor.x -= extra + 1
        screen.draw('…')
    if trailing_spaces:
        screen.draw(' ' * trailing_spaces)
    end = screen.cursor.x
    screen.cursor.fg = 0
    if not is_last:
        screen.cursor.bg = as_rgb(color_as_int(draw_data.inactive_bg))
        screen.draw(draw_data.sep)
    screen.cursor.bg = 0
    return end


@run_once
def load_custom_draw_tab() -> DrawTabFunc:
    import runpy, traceback
    try:
        m = runpy.run_path(os.path.join(config_dir, 'tab_bar.py'))
        func: DrawTabFunc = m['draw_tab']
    except Exception as e:
        traceback.print_exc()
        log_error(f'Failed to load custom draw_tab function with error: {e}')
        return draw_tab_with_separator

    @wraps(func)
    def draw_tab(
        draw_data: DrawData, screen: Screen, tab: TabBarData,
        before: int, max_tab_length: int, index: int, is_last: bool,
        extra_data: ExtraData
    ) -> int:
        try:
            return func(draw_data, screen, tab, before, max_tab_length, index, is_last, extra_data)
        except Exception as e:
            log_error(f'Custom draw tab function failed with error: {e}')
            return draw_tab_with_separator(draw_data, screen, tab, before, max_tab_length, index, is_last, extra_data)

    return draw_tab


class TabBar:

    def __init__(self, os_window_id: int):
        self.os_window_id = os_window_id
        self.num_tabs = 1
        self.data_buffer_size = 0
        self.blank_rects: Tuple[Border, ...] = ()
        self.cell_ranges: List[Tuple[int, int]] = []
        self.laid_out_once = False
        self.apply_options()

    def apply_options(self) -> None:
        opts = get_options()
        self.dirty = True
        self.margin_width = pt_to_px(opts.tab_bar_margin_width, self.os_window_id)
        self.cell_width, cell_height = cell_size_for_window(self.os_window_id)
        if not hasattr(self, 'screen'):
            self.screen = s = Screen(None, 1, 10, 0, self.cell_width, cell_height)
        else:
            s = self.screen
        s.color_profile.update_ansi_color_table(build_ansi_color_table(opts))
        s.color_profile.set_configured_colors(
            color_as_int(opts.inactive_tab_foreground),
            color_as_int(opts.tab_bar_background or opts.background)
        )
        sep = opts.tab_separator
        self.trailing_spaces = self.leading_spaces = 0
        while sep and sep[0] == ' ':
            sep = sep[1:]
            self.trailing_spaces += 1
        while sep and sep[-1] == ' ':
            self.leading_spaces += 1
            sep = sep[:-1]
        self.sep = sep

        self.active_bg = as_rgb(color_as_int(opts.active_tab_background))
        self.active_fg = as_rgb(color_as_int(opts.active_tab_foreground))
        self.draw_data = DrawData(
            self.leading_spaces, self.sep, self.trailing_spaces,
            opts.tab_fade, opts.active_tab_foreground, opts.active_tab_background,
            opts.inactive_tab_foreground, opts.inactive_tab_background,
            opts.tab_bar_background or opts.background, opts.tab_title_template,
            opts.active_tab_title_template,
            opts.tab_activity_symbol,
            'top' if opts.tab_bar_edge == 1 else 'bottom',
            opts.tab_title_max_length,
        )
        ts = opts.tab_bar_style
        if ts == 'custom':
            self.draw_func = load_custom_draw_tab()
        else:
            self.draw_func: DrawTabFunc = draw_tab_with_separator
        self.align = lambda: None

    @property
    def current_colors(self) -> Dict[str, Color]:
        return {
            'active_tab_foreground': self.draw_data.active_fg,
            'inactive_tab_foreground': self.draw_data.inactive_fg,
            'active_tab_background': self.draw_data.active_bg,
            'inactive_tab_background': self.draw_data.inactive_bg,
            'tab_bar_background': self.draw_data.default_bg,
        }

    def update_blank_rects(self, central: Region, tab_bar: Region, vw: int, vh: int) -> None:
        opts = get_options()
        blank_rects: List[Border] = []
        bg = BorderColor.tab_bar_margin_color if opts.tab_bar_margin_color is not None else BorderColor.default_bg
        if opts.tab_bar_margin_height:
            if opts.tab_bar_edge == 3:  # bottom
                if opts.tab_bar_margin_height.outer:
                    blank_rects.append(Border(0, tab_bar.bottom + 1, vw, vh, bg))
                if opts.tab_bar_margin_height.inner:
                    blank_rects.append(Border(0, central.bottom + 1, vw, vh, bg))
            else:  # top
                if opts.tab_bar_margin_height.outer:
                    blank_rects.append(Border(0, 0, vw, tab_bar.top, bg))
                if opts.tab_bar_margin_height.inner:
                    blank_rects.append(Border(0, tab_bar.bottom + 1, vw, central.top, bg))
        g = self.window_geometry
        left_bg = right_bg = bg
        if opts.tab_bar_margin_color is None:
            left_bg = BorderColor.tab_bar_left_edge_color
            right_bg = BorderColor.tab_bar_right_edge_color
        if g.left > 0:
            blank_rects.append(Border(0, g.top, g.left, g.bottom + 1, left_bg))
        if g.right - 1 < vw:
            blank_rects.append(Border(g.right - 1, g.top, vw, g.bottom + 1, right_bg))
        self.blank_rects = tuple(blank_rects)

    def layout(self) -> None:
        central, tab_bar, vw, vh, cell_width, cell_height = viewport_for_window(self.os_window_id)
        if tab_bar.width < 2:
            return
        self.cell_width = cell_width
        s = self.screen
        viewport_width = max(4 * cell_width, tab_bar.width - 2 * self.margin_width)
        ncells = viewport_width // cell_width
        s.resize(1, ncells)
        s.reset_mode(DECAWM)
        self.laid_out_once = True
        margin = (viewport_width - ncells * cell_width) // 2 + self.margin_width
        self.window_geometry = g = WindowGeometry(
            margin, tab_bar.top, viewport_width - margin, tab_bar.bottom, s.columns, s.lines)
        self.update_blank_rects(central, tab_bar, vw, vh)
        set_tab_bar_render_data(self.os_window_id, self.screen, *g[:4])

    def update(self, data: Sequence[TabBarData]) -> None:
        if not self.laid_out_once:
            return
        s = self.screen
        last_tab = data[-1] if data else None
        ed = ExtraData()

        def draw_tab(i: int, tab: TabBarData, cell_ranges: List[Tuple[int, int]], max_tab_length: int) -> None:
            ed.prev_tab = data[i - 1] if i > 0 else None
            ed.next_tab = data[i + 1] if i + 1 < len(data) else None
            s.cursor.bg = as_rgb(self.draw_data.tab_bg(t))
            s.cursor.fg = as_rgb(self.draw_data.tab_fg(t))
            before = s.cursor.x
            end = self.draw_func(self.draw_data, s, t, before, max_tab_length, i + 1, t is last_tab, ed)
            s.cursor.bg = s.cursor.fg = 0
            cell_ranges.append((before, end))
            if not ed.for_layout and t is not last_tab and s.cursor.x > s.columns - max_tab_lengths[i+1]:
                # Stop if there is no space for next tab
                s.cursor.x = s.columns - 2
                s.cursor.bg = as_rgb(color_as_int(self.draw_data.default_bg))
                s.cursor.fg = as_rgb(0xff0000)
                s.draw(' …')
                raise StopIteration()

        unconstrained_tab_length = max(1, s.columns - 2)
        ideal_tab_lengths = [i for i in range(len(data))]
        default_max_tab_length = max(1, (s.columns // max(1, len(data))) - 1)
        max_tab_lengths = [default_max_tab_length for _ in range(len(data))]
        active_idx = 0
        extra = 0
        ed.for_layout = True
        for i, t in enumerate(data):
            s.cursor.x = 0
            draw_tab(i, t, [], unconstrained_tab_length)
            ideal_tab_lengths[i] = tl = max(1, s.cursor.x)
            if t.is_active:
                active_idx = i
            if tl < default_max_tab_length:
                max_tab_lengths[i] = tl
                extra += default_max_tab_length - tl
        if extra > 0:
            if ideal_tab_lengths[active_idx] > max_tab_lengths[active_idx]:
                d = min(extra, ideal_tab_lengths[active_idx] - max_tab_lengths[active_idx])
                max_tab_lengths[active_idx] += d
                extra -= d
            if extra > 0:
                over_achievers = tuple(i for i in range(len(data)) if ideal_tab_lengths[i] > max_tab_lengths[i])
                if over_achievers:
                    amt_per_over_achiever = extra // len(over_achievers)
                    if amt_per_over_achiever > 0:
                        for i in over_achievers:
                            max_tab_lengths[i] += amt_per_over_achiever

        s.cursor.x = 0
        s.erase_in_line(2, False)
        cr: List[Tuple[int, int]] = []
        ed.for_layout = False
        for i, t in enumerate(data):
            try:
                draw_tab(i, t, cr, max_tab_lengths[i])
            except StopIteration:
                break
        self.cell_ranges = cr
        s.erase_in_line(0, False)  # Ensure no long titles bleed after the last tab
        self.align()
        update_tab_bar_edge_colors(self.os_window_id)

    def align_with_factor(self, factor: int = 1) -> None:
        if not self.cell_ranges:
            return
        end = self.cell_ranges[-1][1]
        if end < self.screen.columns - 1:
            shift = (self.screen.columns - end) // factor
            self.screen.cursor.x = 0
            self.screen.insert_characters(shift)
            self.cell_ranges = [(s + shift, e + shift) for (s, e) in self.cell_ranges]

    def destroy(self) -> None:
        self.screen.reset_callbacks()
        del self.screen

    def tab_at(self, x: int) -> Optional[int]:
        if self.laid_out_once:
            x = (x - self.window_geometry.left) // self.cell_width
            for i, (a, b) in enumerate(self.cell_ranges):
                if a <= x <= b:
                    return i
        return None
