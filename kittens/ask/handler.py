#!/usr/bin/env python
# License: GPL v3 Copyright: 2018, Kovid Goyal <kovid at kovidgoyal.net>


from typing import Any, Callable, Optional, Sequence

from alatty.typing import BossType


class HandleResult:

    type_of_input: Optional[str] = None
    no_ui: bool = False

    def __init__(self, impl: Callable[..., Any], type_of_input: Optional[str], no_ui: bool, has_ready_notification: bool):
        self.impl = impl
        self.no_ui = no_ui
        self.type_of_input = type_of_input
        self.has_ready_notification = has_ready_notification

    def __call__(self, args: Sequence[str], data: Any, target_window_id: int, boss: BossType) -> Any:
        return self.impl(args, data, target_window_id, boss)


def result_handler(
    type_of_input: Optional[str] = None, no_ui: bool = False, has_ready_notification: bool = False
) -> Callable[[Callable[..., Any]], HandleResult]:

    def wrapper(impl: Callable[..., Any]) -> HandleResult:
        return HandleResult(impl, type_of_input, no_ui, has_ready_notification)

    return wrapper
