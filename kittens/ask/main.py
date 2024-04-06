#!/usr/bin/env python
# License: GPL v3 Copyright: 2018, Kovid Goyal <kovid at kovidgoyal.net>

import sys
from typing import (
    List,
    Optional,
)

from alatty.typing import BossType, TypedDict

from .handler import result_handler


def option_text() -> str:
    return '''\
--type -t
choices=line,yesno,choices,password
default=line
Type of input. Defaults to asking for a line of text.


--message -m
The message to display to the user. If not specified a default
message is shown.


--name -n
The name for this question. Used to store history of previous answers which can
be used for completions and via the browse history readline bindings.


--choice -c
type=list
dest=choices
A choice for the choices type.


--default -d
A default choice or text. If unspecified, it is :code:`y` for the type
:code:`yesno`, the first choice for :code:`choices` and empty for others types.
The default choice is selected when the user presses the :kbd:`Enter` key.


--prompt -p
default="> "
The prompt to use when inputting a line of text or a password.
'''


class Response(TypedDict):
    items: List[str]
    response: Optional[str]

def main(args: List[str]) -> Response:
    raise SystemExit('This must be run as kitten ask')


@result_handler()
def handle_result(args: List[str], data: Response, target_window_id: int, boss: BossType) -> None:
    if data['response'] is not None:
        func, *args = data['items']
        getattr(boss, func)(data['response'], *args)


if __name__ == '__main__':
    main(sys.argv)
elif __name__ == '__doc__':
    cd = sys.cli_docs  # type: ignore
    cd['usage'] = ''
    cd['options'] = option_text
    cd['help_text'] = 'Ask the user for input'
    cd['short_desc'] = 'Ask the user for input'
