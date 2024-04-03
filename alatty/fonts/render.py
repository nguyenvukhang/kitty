#!/usr/bin/env python
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

import ctypes
import sys
from functools import partial
from math import ceil, cos, floor, pi
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional, Tuple, Union, cast

from alatty.constants import is_macos
from alatty.fast_data_types import (
    NUM_UNDERLINE_STYLES,
    get_options,
    set_font_data,
)
from alatty.fonts.box_drawing import BufType, distribute_dots, render_box_char, render_missing_glyph
from alatty.options.types import Options, defaults
from alatty.typing import CoreTextFont, FontConfigPattern
from alatty.utils import log_error

if is_macos:
    from .core_text import font_for_family as font_for_family_macos
    from .core_text import get_font_files as get_font_files_coretext
else:
    from .fontconfig import font_for_family as font_for_family_fontconfig
    from .fontconfig import get_font_files as get_font_files_fontconfig

FontObject = Union[CoreTextFont, FontConfigPattern]
current_faces: List[Tuple[FontObject, bool, bool]] = []


def get_font_files(opts: Options) -> Dict[str, Any]:
    if is_macos:
        return get_font_files_coretext(opts)
    return get_font_files_fontconfig(opts)


def font_for_family(family: str) -> FontObject:
    if is_macos:
        return font_for_family_macos(family)
    return font_for_family_fontconfig(family)


def descriptor_for_idx(idx: int) -> Tuple[FontObject, bool, bool]:
    return current_faces[idx]


def set_font_family(opts: Optional[Options] = None, override_font_size: Optional[float] = None) -> None:
    global current_faces
    opts = opts or defaults
    sz = override_font_size or opts.font_size
    font_map = get_font_files(opts)
    current_faces = [(font_map['medium'], False, False)]
    before = len(current_faces)
    num_symbol_fonts = len(current_faces) - before
    set_font_data(render_box_drawing, prerender_function, descriptor_for_idx, num_symbol_fonts, sz)


if TYPE_CHECKING:
    CBufType = ctypes.Array[ctypes.c_ubyte]
else:
    CBufType = None
UnderlineCallback = Callable[[CBufType, int, int, int, int], None]


def add_line(buf: CBufType, cell_width: int, position: int, thickness: int, cell_height: int) -> None:
    y = position - thickness // 2
    while thickness > 0 and -1 < y < cell_height:
        thickness -= 1
        ctypes.memset(ctypes.addressof(buf) + (cell_width * y), 255, cell_width)
        y += 1


def add_dline(buf: CBufType, cell_width: int, position: int, thickness: int, cell_height: int) -> None:
    a = min(position - thickness, cell_height - 1)
    b = min(position, cell_height - 1)
    top, bottom = min(a, b), max(a, b)
    deficit = 2 - (bottom - top)
    if deficit > 0:
        if bottom + deficit < cell_height:
            bottom += deficit
        elif bottom < cell_height - 1:
            bottom += 1
            if deficit > 1:
                top -= deficit - 1
        else:
            top -= deficit
    top = max(0, min(top, cell_height - 1))
    bottom = max(0, min(bottom, cell_height - 1))
    for y in {top, bottom}:
        ctypes.memset(ctypes.addressof(buf) + (cell_width * y), 255, cell_width)


def add_curl(buf: CBufType, cell_width: int, position: int, thickness: int, cell_height: int) -> None:
    max_x, max_y = cell_width - 1, cell_height - 1
    opts = get_options()
    xfactor = (4.0 if 'dense' in opts.undercurl_style else 2.0) * pi / max_x

    max_height = cell_height - (position - thickness // 2)  # descender from the font
    half_height = max(1, max_height // 4)
    thickness = max(1, thickness) - (1 if thickness < 3 else 2)

    def add_intensity(x: int, y: int, val: int) -> None:
        y += position
        y = min(y, max_y)
        idx = cell_width * y + x
        buf[idx] = min(255, buf[idx] + val)

    # Ensure curve doesn't exceed cell boundary at the bottom
    position += half_height * 2
    if position + half_height > max_y:
        position = max_y - half_height

    # Use the Wu antialias algorithm to draw the curve
    # cosine waves always have slope <= 1 so are never steep
    for x in range(cell_width):
        y = half_height * cos(x * xfactor)
        y1, y2 = floor(y - thickness), ceil(y)
        i1 = int(255 * abs(y - floor(y)))
        add_intensity(x, y1, 255 - i1)  # upper bound
        add_intensity(x, y2, i1)  # lower bound
        # fill between upper and lower bound
        for t in range(1, thickness + 1):
            add_intensity(x, y1 + t, 255)


def add_dots(buf: CBufType, cell_width: int, position: int, thickness: int, cell_height: int) -> None:
    spacing, size = distribute_dots(cell_width, cell_width // (2 * thickness))

    y = 1 + position - thickness // 2
    for i in range(y, min(y + thickness, cell_height)):
        for j, s in enumerate(spacing):
            buf[cell_width * i + j * size + s : cell_width * i + (j + 1) * size + s] = [255] * size


def add_dashes(buf: CBufType, cell_width: int, position: int, thickness: int, cell_height: int) -> None:
    halfspace_width = cell_width // 4
    y = 1 + position - thickness // 2
    for i in range(y, min(y + thickness, cell_height)):
        buf[cell_width * i : cell_width * i + (cell_width - 3 * halfspace_width)] = [255] * (cell_width - 3 * halfspace_width)
        buf[cell_width * i + 3 * halfspace_width : cell_width * (i + 1)] = [255] * (cell_width - 3 * halfspace_width)


def render_special(
    underline: int = 0,
    strikethrough: bool = False,
    missing: bool = False,
    cell_width: int = 0,
    cell_height: int = 0,
    baseline: int = 0,
    underline_position: int = 0,
    underline_thickness: int = 0,
    strikethrough_position: int = 0,
    strikethrough_thickness: int = 0,
    dpi_x: float = 96.0,
    dpi_y: float = 96.0,
) -> CBufType:
    underline_position = min(underline_position, cell_height - sum(divmod(underline_thickness, 2)))
    CharTexture = ctypes.c_ubyte * (cell_width * cell_height)

    if missing:
        buf = bytearray(cell_width * cell_height)
        render_missing_glyph(buf, cell_width, cell_height)
        return CharTexture.from_buffer(buf)

    ans = CharTexture()

    def dl(f: UnderlineCallback, *a: Any) -> None:
        try:
            f(ans, cell_width, *a)
        except Exception as e:
            log_error(f'Failed to render {f.__name__} at cell_width={cell_width} and cell_height={cell_height} with error: {e}')

    if underline:
        t = underline_thickness
        if underline > 1:
            t = max(1, min(cell_height - underline_position - 1, t))
        dl([add_line, add_line, add_dline, add_curl, add_dots, add_dashes][underline], underline_position, t, cell_height)
    if strikethrough:
        dl(add_line, strikethrough_position, strikethrough_thickness, cell_height)

    return ans


def render_cursor(
    which: int, cursor_beam_thickness: float, cursor_underline_thickness: float, cell_width: int = 0, cell_height: int = 0, dpi_x: float = 0, dpi_y: float = 0
) -> CBufType:
    CharTexture = ctypes.c_ubyte * (cell_width * cell_height)
    ans = CharTexture()

    def vert(edge: str, width_pt: float = 1) -> None:
        width = max(1, min(int(round(width_pt * dpi_x / 72.0)), cell_width))
        left = 0 if edge == 'left' else max(0, cell_width - width)
        for y in range(cell_height):
            offset = y * cell_width + left
            for x in range(offset, offset + width):
                ans[x] = 255

    def horz(edge: str, height_pt: float = 1) -> None:
        height = max(1, min(int(round(height_pt * dpi_y / 72.0)), cell_height))
        top = 0 if edge == 'top' else max(0, cell_height - height)
        for y in range(top, top + height):
            offset = y * cell_width
            for x in range(cell_width):
                ans[offset + x] = 255

    if which == 1:  # beam
        vert('left', cursor_beam_thickness)
    elif which == 2:  # underline
        horz('bottom', cursor_underline_thickness)
    elif which == 3:  # hollow
        vert('left')
        vert('right')
        horz('top')
        horz('bottom')
    return ans


def prerender_function(
    cell_width: int,
    cell_height: int,
    baseline: int,
    underline_position: int,
    underline_thickness: int,
    strikethrough_position: int,
    strikethrough_thickness: int,
    cursor_beam_thickness: float,
    cursor_underline_thickness: float,
    dpi_x: float,
    dpi_y: float,
) -> Tuple[Tuple[int, ...], Tuple[CBufType, ...]]:
    # Pre-render the special underline, strikethrough and missing and cursor cells
    f = partial(
        render_special,
        cell_width=cell_width,
        cell_height=cell_height,
        baseline=baseline,
        underline_position=underline_position,
        underline_thickness=underline_thickness,
        strikethrough_position=strikethrough_position,
        strikethrough_thickness=strikethrough_thickness,
        dpi_x=dpi_x,
        dpi_y=dpi_y,
    )
    c = partial(
        render_cursor,
        cursor_beam_thickness=cursor_beam_thickness,
        cursor_underline_thickness=cursor_underline_thickness,
        cell_width=cell_width,
        cell_height=cell_height,
        dpi_x=dpi_x,
        dpi_y=dpi_y,
    )
    # If you change the mapping of these cells you will need to change
    # NUM_UNDERLINE_STYLES and BEAM_IDX in shader.c and STRIKE_SPRITE_INDEX in
    # window.py and MISSING_GLYPH in font.c
    cells = list(map(f, range(1, NUM_UNDERLINE_STYLES + 1)))  # underline sprites
    cells.append(f(0, strikethrough=True))  # strikethrough sprite
    cells.append(f(missing=True))  # missing glyph
    cells.extend((c(1), c(2), c(3)))  # cursor glyphs
    tcells = tuple(cells)
    return tuple(map(ctypes.addressof, tcells)), tcells


def render_box_drawing(codepoint: int, cell_width: int, cell_height: int, dpi: float) -> Tuple[int, CBufType]:
    CharTexture = ctypes.c_ubyte * (cell_width * cell_height)
    buf = CharTexture()
    render_box_char(chr(codepoint), cast(BufType, buf), cell_width, cell_height, dpi)
    return ctypes.addressof(buf), buf
