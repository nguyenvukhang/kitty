#!/usr/bin/env python
# License: GPL v3 Copyright: 2017, Kovid Goyal <kovid at kovidgoyal.net>

import re
from typing import Dict, Generator, Iterable, List, Optional, Tuple

from alatty.fast_data_types import coretext_all_fonts
from alatty.fonts import FontFeature
from alatty.options.types import Options
from alatty.typing import CoreTextFont
from alatty.utils import log_error

from . import ListedFont

attr_map = {(False, False): 'font_family'}


FontMap = Dict[str, Dict[str, List[CoreTextFont]]]


def create_font_map(all_fonts: Iterable[CoreTextFont]) -> FontMap:
    ans: FontMap = {'family_map': {}, 'ps_map': {}, 'full_map': {}}
    for x in all_fonts:
        f = (x['family'] or '').lower()
        s = (x['style'] or '').lower()
        ps = (x['postscript_name'] or '').lower()
        ans['family_map'].setdefault(f, []).append(x)
        ans['ps_map'].setdefault(ps, []).append(x)
        ans['full_map'].setdefault(f'{f} {s}', []).append(x)
    return ans


def all_fonts_map() -> FontMap:
    ans: Optional[FontMap] = getattr(all_fonts_map, 'ans', None)
    if ans is None:
        ans = create_font_map(coretext_all_fonts())
        setattr(all_fonts_map, 'ans', ans)
    return ans


def list_fonts() -> Generator[ListedFont, None, None]:
    for fd in coretext_all_fonts():
        f = fd['family']
        if f:
            fn = f'{f} {fd.get("style", "")}'.strip()
            is_mono = bool(fd['monospace'])
            yield {'family': f, 'full_name': fn, 'postscript_name': fd['postscript_name'] or '', 'is_monospace': is_mono}


def find_best_match(family: str, ignore_face: Optional[CoreTextFont] = None) -> CoreTextFont:
    q = re.sub(r'\s+', ' ', family.lower())
    font_map = all_fonts_map()

    def score(candidate: CoreTextFont) -> Tuple[int, int, int, float]:
        style_match = 0
        monospace_match = 1 if candidate['monospace'] else 0
        is_regular_width = not candidate['expanded'] and not candidate['condensed']
        # prefer semi-bold to bold to heavy, less bold means less chance of
        # overflow
        weight_distance_from_medium = abs(candidate['weight'])
        return style_match, monospace_match, 1 if is_regular_width else 0, 1 - weight_distance_from_medium

    # First look for an exact match
    for selector in ('ps_map', 'full_map'):
        candidates = font_map[selector].get(q)
        if candidates:
            possible = sorted(candidates, key=score)[-1]
            if possible != ignore_face:
                return possible

    # Let CoreText choose the font if the family exists, otherwise
    # fallback to Menlo
    if q not in font_map['family_map']:
        log_error(f'The font {family} was not found, falling back to Menlo')
        q = 'menlo'
    candidates = font_map['family_map'][q]
    return sorted(candidates, key=score)[-1]


def resolve_family(f: str) -> str:
    if f.lower() == 'monospace':
        f = 'Menlo'
    return f


def get_font_files(opts: Options) -> Dict[str, CoreTextFont]:
    ans: Dict[str, CoreTextFont] = {}
    face = find_best_match(getattr(opts, 'font_family'))
    ans['medium'] = face
    setattr(get_font_files, 'medium_family', face['family'])
    return ans


def font_for_family(family: str) -> CoreTextFont:
    return find_best_match(resolve_family(family))
