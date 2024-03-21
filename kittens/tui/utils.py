#!/usr/bin/env python
# License: GPLv3 Copyright: 2020, Kovid Goyal <kovid at kovidgoyal.net>

import sys
from contextlib import suppress
from typing import TYPE_CHECKING, Optional, cast

if TYPE_CHECKING:
    from alatty.options.types import Options


def alatty_opts() -> 'Options':
    from alatty.fast_data_types import get_options, set_options
    try:
        ans = cast(Optional['Options'], get_options())
    except RuntimeError:
        ans = None
    if ans is None:
        from alatty.cli import create_default_opts
        from alatty.utils import suppress_error_logging
        with suppress_error_logging():
            ans = create_default_opts()
            set_options(ans)
    return ans


def report_error(msg: str = '', return_code: int = 1, print_exc: bool = False) -> None:
    ' Report an error also sending the overlay ready message to ensure kitten is visible '
    if msg:
        print(msg, file=sys.stderr)
    if print_exc:
        _, e, _ = sys.exc_info()
        if e and not isinstance(e, (SystemExit, KeyboardInterrupt)):
            import traceback
            traceback.print_exc()
    with suppress(KeyboardInterrupt, EOFError):
        input('Press Enter to quit')
    raise SystemExit(return_code)
