#!/usr/bin/env python
# License: GPLv3 Copyright: 2021, Kovid Goyal <kovid at kovidgoyal.net>

import builtins
import typing
from importlib import import_module
from typing import Any, Callable, Dict, Iterable, Iterator, List, Optional, Set, Tuple, Union, cast

import alatty.conf.utils as generic_parsers

if typing.TYPE_CHECKING:
    Only = typing.Literal['macos', 'linux', '']
else:
    Only = str


class Unset:
    def __bool__(self) -> bool:
        return False


unset = Unset()
ParserFuncType = Callable[[str], Any]


class CoalescedIteratorData:

    option_groups: Dict[int, List['Option']] = {}
    action_groups: Dict[str, List['Mapping']] = {}
    coalesced: Set[int] = set()
    initialized: bool = False
    alatty_mod: str = 'alatty_mod'

    def initialize(self, root: 'Group') -> None:
        if self.initialized:
            return
        self.root = root
        option_groups = self.option_groups = {}
        current_group: List[Option] = []
        action_groups: Dict[str, List[Mapping]] = {}
        self.action_groups = action_groups
        coalesced = self.coalesced = set()
        self.alatty_mod = 'alatty_mod'
        for item in root.iter_all_non_groups():
            if isinstance(item, Option):
                if item.name == 'alatty_mod':
                    self.alatty_mod = item.defval_as_string
                if current_group:
                    if item.needs_coalescing:
                        current_group.append(item)
                        coalesced.add(id(item))
                        continue
                    option_groups[id(current_group[0])] = current_group[1:]
                    current_group = [item]
                else:
                    current_group.append(item)
            elif isinstance(item, Mapping):
                if item.name in action_groups:
                    coalesced.add(id(item))
                    action_groups[item.name].append(item)
                else:
                    action_groups[item.name] = []
        if current_group:
            option_groups[id(current_group[0])] = current_group[1:]

    def option_group_for_option(self, opt: 'Option') -> List['Option']:
        return self.option_groups.get(id(opt), [])

    def action_group_for_action(self, ac: 'Mapping') -> List['Mapping']:
        return self.action_groups.get(ac.name, [])


class Option:

    def __init__(
        self, name: str, defval: str, macos_default: Union[Unset, str], parser_func: ParserFuncType,
        long_text: str, documented: bool, group: 'Group', choices: Tuple[str, ...], ctype: str
    ):
        self.name = name
        self.ctype = ctype
        self.defval_as_string = defval
        self.macos_defval = macos_default
        self.long_text = long_text
        self.documented = documented
        self.group = group
        self.parser_func = parser_func
        self.choices = choices

    @property
    def needs_coalescing(self) -> bool:
        return self.documented and not self.long_text

    @property
    def is_color_table_color(self) -> bool:
        return self.name.startswith('color') and self.name[5:].isdigit()

class MultiVal:

    def __init__(self, val_as_str: str, add_to_default: bool, documented: bool, only: Only) -> None:
        self.defval_as_str = val_as_str
        self.documented = documented
        self.only = only
        self.add_to_default = add_to_default


class MultiOption:

    def __init__(self, name: str, parser_func: ParserFuncType, long_text: str, group: 'Group', ctype: str):
        self.name = name
        self.ctype = ctype
        self.parser_func = parser_func
        self.long_text = long_text
        self.group = group
        self.items: List[MultiVal] = []

    def add_value(self, val_as_str: str, add_to_default: bool, documented: bool, only: Only) -> None:
        self.items.append(MultiVal(val_as_str, add_to_default, documented, only))

    def __iter__(self) -> Iterator[MultiVal]:
        yield from self.items

class Mapping:
    add_to_default: bool
    short_text: str
    long_text: str
    documented: bool
    setting_name: str
    name: str
    only: Only

    @property
    def parseable_text(self) -> str:
        return ''

    @property
    def key_text(self) -> str:
        return ''

class ShortcutMapping(Mapping):
    setting_name: str = 'map'

    def __init__(
        self, name: str, key: str, action_def: str, short_text: str, long_text: str, add_to_default: bool, documented: bool, group: 'Group', only: Only
    ):
        self.name = name
        self.only = only
        self.key = key
        self.action_def = action_def
        self.short_text = short_text
        self.long_text = long_text
        self.documented = documented
        self.add_to_default = add_to_default
        self.group = group

    @property
    def parseable_text(self) -> str:
        return f'{self.key} {self.action_def}'

    @property
    def key_text(self) -> str:
        return self.key


class MouseMapping(Mapping):
    setting_name: str = 'mouse_map'

    def __init__(
        self, name: str, button: str, event: str, modes: str, action_def: str,
        short_text: str, long_text: str, add_to_default: bool, documented: bool, group: 'Group', only: Only
    ):
        self.name = name
        self.only = only
        self.button = button
        self.event = event
        self.modes = modes
        self.action_def = action_def
        self.short_text = short_text
        self.long_text = long_text
        self.documented = documented
        self.add_to_default = add_to_default
        self.group = group

    @property
    def parseable_text(self) -> str:
        return f'{self.button} {self.event} {self.modes} {self.action_def}'

    @property
    def key_text(self) -> str:
        return self.button


NonGroups = Union[Option, MultiOption, ShortcutMapping, MouseMapping]
GroupItem = Union[NonGroups, 'Group']


class Group:

    def __init__(self, name: str, title: str, coalesced_iterator_data: CoalescedIteratorData, start_text: str = '', parent: Optional['Group'] = None):
        self.name = name
        self.coalesced_iterator_data = coalesced_iterator_data
        self.title = title
        self.start_text = start_text
        self.end_text = ''
        self.items: List[GroupItem] = []
        self.parent = parent

    def append(self, item: GroupItem) -> None:
        self.items.append(item)

    def __iter__(self) -> Iterator[GroupItem]:
        return iter(self.items)

    def __len__(self) -> int:
        return len(self.items)

    def iter_all(self) -> Iterator[GroupItem]:
        for x in self:
            yield x
            if isinstance(x, Group):
                yield from x.iter_all()

    def iter_all_non_groups(self) -> Iterator[NonGroups]:
        for x in self:
            if isinstance(x, Group):
                yield from x.iter_all_non_groups()
            else:
                yield x

def resolve_import(name: str, module: Any = None) -> ParserFuncType:
    ans = None
    if name.count('.') > 1:
        m = import_module(name.rpartition('.')[0])
        ans = getattr(m, name.rpartition('.')[2])
    else:
        ans = getattr(builtins, name, None)
        if not callable(ans):
            ans = getattr(generic_parsers, name, None)
            if not callable(ans):
                ans = getattr(module, name)
    if not callable(ans):
        raise TypeError(f'{name} is not a function')
    return cast(ParserFuncType, ans)


class Action:

    def __init__(self, name: str, option_type: str, fields: Dict[str, str], imports: Iterable[str]):
        self.name = name
        self._parser_func = option_type
        self.fields = fields
        self.imports = frozenset(imports)

    def resolve_imports(self, module: Any) -> 'Action':
        self.parser_func = resolve_import(self._parser_func, module)
        return self
