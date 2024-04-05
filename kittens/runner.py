#!/usr/bin/env python
# License: GPL v3 Copyright: 2018, Kovid Goyal <kovid at kovidgoyal.net>


import importlib
import os
import sys
from contextlib import contextmanager
from functools import partial
from typing import Any, Dict, FrozenSet, Generator, List

from alatty.constants import list_alatty_resources
from alatty.types import run_once
from alatty.utils import resolve_abs_or_config_path

aliases = {'url_hints': 'hints'}

def resolved_kitten(k: str) -> str:
    ans = aliases.get(k, k)
    head, tail = os.path.split(ans)
    tail = tail.replace('-', '_')
    return os.path.join(head, tail)


def path_to_custom_kitten(config_dir: str, kitten: str) -> str:
    path = resolve_abs_or_config_path(kitten, conf_dir=config_dir)
    return os.path.abspath(path)


@contextmanager
def preserve_sys_path() -> Generator[None, None, None]:
    orig = sys.path[:]
    try:
        yield
    finally:
        if sys.path != orig:
            del sys.path[:]
            sys.path.extend(orig)


def create_kitten_handler(kitten: str, orig_args: List[str]) -> Any:
    kitten = resolved_kitten(kitten)
    m = importlib.import_module(f'kittens.{kitten}.main')
    m = {'start': getattr(m, 'main'), 'end': getattr(m, 'handle_result', lambda *a, **k: None)}
    ans = partial(m['end'], [kitten] + orig_args)
    setattr(ans, 'type_of_input', getattr(m['end'], 'type_of_input', None))
    setattr(ans, 'no_ui', getattr(m['end'], 'no_ui', False))
    setattr(ans, 'has_ready_notification', getattr(m['end'], 'has_ready_notification', False))
    return ans


def launch(args: List[str]) -> None:
    config_dir, kitten = args[:2]
    kitten = resolved_kitten(kitten)
    del args[:2]
    args = [kitten] + args
    os.environ['ALATTY_CONFIG_DIRECTORY'] = config_dir
    sys.stdin = sys.__stdin__
    sys.stderr.flush()
    sys.stdout.flush()


def run_kitten(kitten: str, run_name: str = '__main__') -> None:
    import runpy
    runpy.run_module(f'kittens.{resolved_kitten(kitten)}.main', run_name=run_name)

@run_once
def all_kitten_names() -> FrozenSet[str]:
    ans = []
    for name in list_alatty_resources('kittens'):
        if '__' not in name and '.' not in name and name != 'tui':
            ans.append(name)
    return frozenset(ans)


def get_kitten_cli_docs(kitten: str) -> Any:
    setattr(sys, 'cli_docs', {})
    run_kitten(kitten, run_name='__doc__')
    ans = getattr(sys, 'cli_docs')
    delattr(sys, 'cli_docs')
    if 'help_text' in ans and 'usage' in ans and 'options' in ans:
        return ans


def main() -> None:
    try:
        args = sys.argv[1:]
        launch(args)
    except Exception:
        print('Unhandled exception running kitten:')
        import traceback
        traceback.print_exc()
        input('Press Enter to quit')
