#!/usr/bin/env python
# License: GPL v3 Copyright: 2017, Kovid Goyal <kovid at kovidgoyal.net>

import re
from contextlib import suppress
from typing import Optional

from .fast_data_types import Color


def parse_single_color(c: str) -> int:
    if len(c) == 1:
        c += c
    return int(c[:2], 16)


def parse_sharp(spec: str) -> Optional[Color]:
    if len(spec) in (3, 6, 9, 12):
        part_len = len(spec) // 3
        colors = re.findall(fr'[a-fA-F0-9]{{{part_len}}}', spec)
        return Color(*map(parse_single_color, colors))
    return None


def parse_rgb(spec: str) -> Optional[Color]:
    colors = spec.split('/')
    if len(colors) == 3:
        return Color(*map(parse_single_color, colors))
    return None


def color_from_int(x: int) -> Color:
    return Color((x >> 16) & 255, (x >> 8) & 255, x & 255)


def color_as_int(x: Color) -> int:
    return int(x)


def color_as_sharp(x: Color) -> str:
    return x.as_sharp


def color_as_sgr(x: Color) -> str:
    return x.as_sgr


def to_color(raw: str, validate: bool = False) -> Optional[Color]:
    val: Optional[Color] = None
    with suppress(Exception):
        if raw.startswith('#'):
            val = parse_sharp(raw[1:])
        elif raw.startswith('rgb:'):
            val = parse_rgb(raw[4:])
    if val is None and validate:
        raise ValueError(f'Invalid color name: {raw}')
    return val


if __name__ == '__main__':
    # Read RGB color table from specified rgb.txt file
    import pprint
    import sys
    data = {}
    with open(sys.argv[-1]) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('!'):
                continue
            parts = line.split()
            r, g, b = map(int, parts[:3])
            name = ' '.join(parts[3:]).lower()
            data[name] = data[name.replace(' ', '')] = r, g, b
    formatted_data = pprint.pformat(data).replace('{', '{\n ').replace('(', 'Color(')
    with open(__file__, 'r+') as src:
        raw = src.read()
        raw = re.sub(
            r'^# BEGIN_DATA_SECTION {{{$.*^# END_DATA_SECTION }}}',
            '# BEGIN_DATA_SECTION {{{\ncolor_names = %s\n# END_DATA_SECTION }}}' % formatted_data,
            raw, flags=re.DOTALL | re.MULTILINE
        )
        src.seek(0), src.truncate(), src.write(raw)
