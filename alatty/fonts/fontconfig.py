#!/usr/bin/env python
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

import re
from functools import lru_cache
from typing import Dict, Generator, List, Optional, Tuple, cast

from alatty.fast_data_types import (
    FC_DUAL,
    FC_MONO,
    FC_WIDTH_NORMAL,
    fc_list,
)
from alatty.fast_data_types import fc_match as fc_match_impl
from alatty.options.types import Options
from alatty.typing import FontConfigPattern

from . import ListedFont

attr_map = {(False, False): 'font_family'}


FontMap = Dict[str, Dict[str, List[FontConfigPattern]]]


def create_font_map(all_fonts: Tuple[FontConfigPattern, ...]) -> FontMap:
    ans: FontMap = {'family_map': {}, 'ps_map': {}, 'full_map': {}}
    for x in all_fonts:
        if not x.get('path'):
            continue
        f = (x.get('family') or '').lower()
        full = (x.get('full_name') or '').lower()
        ps = (x.get('postscript_name') or '').lower()
        ans['family_map'].setdefault(f, []).append(x)
        ans['ps_map'].setdefault(ps, []).append(x)
        ans['full_map'].setdefault(full, []).append(x)
    return ans


@lru_cache()
def all_fonts_map(monospaced: bool = True) -> FontMap:
    if monospaced:
        ans = fc_list(FC_DUAL) + fc_list(FC_MONO)
    else:
        # allow non-monospaced and bitmapped fonts as these are used for
        # symbol_map
        ans = fc_list(-1, True)
    return create_font_map(ans)


def list_fonts() -> Generator[ListedFont, None, None]:
    for fd in fc_list():
        f = fd.get('family')
        if f and isinstance(f, str):
            fn_ = fd.get('full_name')
            if fn_:
                fn = str(fn_)
            else:
                fn = f'{f} {fd.get("style", "")}'.strip()
            is_mono = fd.get('spacing') in ('MONO', 'DUAL')
            yield {'family': f, 'full_name': fn, 'postscript_name': str(fd.get('postscript_name', '')), 'is_monospace': is_mono}


def family_name_to_key(family: str) -> str:
    return re.sub(r'\s+', ' ', family.lower())


@lru_cache()
def fc_match(family: str, spacing: int = FC_MONO) -> FontConfigPattern:
    return fc_match_impl(family, spacing)


def find_best_match(family: str, monospaced: bool = True) -> FontConfigPattern:
    q = family_name_to_key(family)
    font_map = all_fonts_map(monospaced)

    def score(candidate: FontConfigPattern) -> Tuple[int, int, int]:
        monospace_match = 0 if candidate.get('spacing') == 'MONO' else 1
        width_score = abs(candidate.get('width', FC_WIDTH_NORMAL) - FC_WIDTH_NORMAL)

        return monospace_match, width_score

    # First look for an exact match
    for selector in ('ps_map', 'full_map', 'family_map'):
        candidates = font_map[selector].get(q)
        if not candidates:
            continue
        if len(candidates) == 1 and candidates[0].get('family') == candidates[0].get('full_name'):
            # IBM Plex Mono does this, where the full name of the regular font
            # face is the same as its family name
            continue
        candidates.sort(key=score)
        return candidates[0]

    # Use fc-match to see if we can find a monospaced font that matches family
    # When aliases are defined, spacing can cause the incorrect font to be
    # returned, so check with and without spacing and use the one that matches.
    mono_possibility = fc_match(family, FC_MONO)
    dual_possibility = fc_match(family, FC_DUAL)
    any_possibility = fc_match(family, 0)
    tries = (dual_possibility, mono_possibility) if any_possibility == dual_possibility else (mono_possibility, dual_possibility)
    for possibility in tries:
        for key, map_key in (('postscript_name', 'ps_map'), ('full_name', 'full_map'), ('family', 'family_map')):
            val: Optional[str] = cast(Optional[str], possibility.get(key))
            if val:
                candidates = font_map[map_key].get(family_name_to_key(val))
                if candidates:
                    if len(candidates) == 1:
                        # happens if the family name is an alias, so we search with
                        # the actual family name to see if we can find all the
                        # fonts in the family.
                        family_name_candidates = font_map['family_map'].get(family_name_to_key(candidates[0]['family']))
                        if family_name_candidates and len(family_name_candidates) > 1:
                            candidates = family_name_candidates
                    return sorted(candidates, key=score)[0]

    # Use fc-match with a generic family
    family = 'monospace' if monospaced else 'sans-serif'
    return fc_match(family)


def get_font_files(opts: Options) -> Dict[str, FontConfigPattern]:
    return {'medium': find_best_match(getattr(opts, 'font_family'))}


def font_for_family(family: str) -> FontConfigPattern:
    return find_best_match(family, monospaced=False)
