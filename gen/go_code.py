#!./alatty/launcher/alatty +launch
# License: GPLv3 Copyright: 2022, Kovid Goyal <kovid at kovidgoyal.net>

import argparse
import io
import json
import os
import re
import subprocess
import sys
from contextlib import contextmanager, suppress
from functools import lru_cache
from itertools import chain
from typing import Any, Dict, Iterator, List, Sequence, Union

import alatty.constants as kc
from alatty.cli import (
    GoOption,
    go_options_for_seq,
    parse_option_spec,
    serialize_as_go_string,
)
from alatty.key_encoding import config_mod_map
from alatty.key_names import character_key_name_aliases, functional_key_name_aliases
from alatty.options.types import Options

if __name__ == '__main__' and not __package__:
    import __main__

    __main__.__package__ = 'gen'
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


changed: List[str] = []


def newer(dest: str, *sources: str) -> bool:
    try:
        dtime = os.path.getmtime(dest)
    except OSError:
        return True
    for s in chain(sources, (__file__,)):
        with suppress(FileNotFoundError):
            if os.path.getmtime(s) >= dtime:
                return True
    return False


def serialize_go_dict(x: Union[Dict[str, int], Dict[int, str], Dict[int, int], Dict[str, str]]) -> str:
    ans = []

    def s(x: Union[int, str]) -> str:
        if isinstance(x, int):
            return str(x)
        return f'"{serialize_as_go_string(x)}"'

    for k, v in x.items():
        ans.append(f'{s(k)}: {s(v)}')
    return '{' + ', '.join(ans) + '}'


@lru_cache(maxsize=1)
def enum_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser()
    p.add_argument('--from-string-func-name')
    return p


# Completions {{{


@lru_cache
def kitten_cli_docs(kitten: str) -> Any:
    from kittens.runner import get_kitten_cli_docs

    return get_kitten_cli_docs(kitten)


@lru_cache
def go_options_for_kitten(kitten: str) -> Sequence[GoOption]:
    kcd = kitten_cli_docs(kitten)
    if kcd:
        ospec = kcd['options']
        return go_options_for_seq(parse_option_spec(ospec())[0])
    return ()


# }}}


# rc command wrappers {{{
json_field_types: Dict[str, str] = {
    'bool': 'bool',
    'str': 'escaped_string',
    'list.str': '[]escaped_string',
    'dict.str': 'map[escaped_string]escaped_string',
    'float': 'float64',
    'int': 'int',
    'scroll_amount': 'any',
    'spacing': 'any',
    'colors': 'any',
}


def go_field_type(json_field_type: str) -> str:
    json_field_type = json_field_type.partition('=')[0]
    q = json_field_types.get(json_field_type)
    if q:
        return q
    if json_field_type.startswith('choices.'):
        return 'string'
    if '.' in json_field_type:
        p, r = json_field_type.split('.', 1)
        p = {'list': '[]', 'dict': 'map[string]'}[p]
        return p + go_field_type(r)
    raise TypeError(f'Unknown JSON field type: {json_field_type}')


class JSONField:

    def __init__(self, line: str) -> None:
        field_def = line.split(':', 1)[0]
        self.required = False
        self.field, self.field_type = field_def.split('/', 1)
        self.field_type, self.special_parser = self.field_type.partition('=')[::2]
        if self.field.endswith('+'):
            self.required = True
            self.field = self.field[:-1]
        self.struct_field_name = self.field[0].upper() + self.field[1:]

    def go_declaration(self) -> str:
        return self.struct_field_name + ' ' + go_field_type(self.field_type) + f'`json:"{self.field},omitempty"`'


# kittens {{{


def kitten_clis() -> None:
    for kitten in ('ask',):
        with replace_if_needed(f'kittens/{kitten}/cli_generated.go'):
            od = []
            kcd = kitten_cli_docs(kitten)
            has_underscore = '_' in kitten
            print(f'package {kitten}')
            print('import "alatty/tools/cli"')
            print('func create_cmd(root *cli.Command, run_func func(*cli.Command, *Options, []string)(int, error)) {')
            print('ans := root.AddSubCommand(&cli.Command{')
            print(f'Name: "{kitten}",')
            if kcd:
                print(f'ShortDescription: "{serialize_as_go_string(kcd["short_desc"])}",')
                if kcd['usage']:
                    print(f'Usage: "[options] {serialize_as_go_string(kcd["usage"])}",')
                print(f'HelpText: "{serialize_as_go_string(kcd["help_text"])}",')
            print('Run: func(cmd *cli.Command, args []string) (int, error) {')
            print('opts := Options{}')
            print('err := cmd.GetOptionValues(&opts)')
            print('if err != nil { return 1, err }')
            print('return run_func(cmd, &opts, args)},')
            if has_underscore:
                print('Hidden: true,')
            print('})')
            for opt in go_options_for_kitten(kitten):
                print(opt.as_option('ans'))
                od.append(opt.struct_declaration())
            if not kcd:
                print('specialize_command(ans)')
            if has_underscore:
                print("clone := root.AddClone(ans.Group, ans)")
                print('clone.Hidden = false')
                print(f'clone.Name = "{serialize_as_go_string(kitten.replace("_", "-"))}"')
            print('}')
            print('type Options struct {')
            print('\n'.join(od))
            print('}')


# }}}


# Constants {{{


def generate_constants() -> str:
    with open('alatty/data-types.h') as dt:
        m = re.search(r'^#define IMAGE_PLACEHOLDER_CHAR (\S+)', dt.read(), flags=re.M)
        assert m is not None
        placeholder_char = int(m.group(1), 16)
    dp = ", ".join(map(lambda x: f'"{serialize_as_go_string(x)}"', kc.default_pager_for_help))
    url_prefixes = ','.join(f'"{x}"' for x in Options.url_prefixes)
    option_names = '``'
    return f'''\
package alatty

type VersionType struct {{
    Major, Minor, Patch int
}}
const VersionString string = "{kc.str_version}"
const ImagePlaceholderChar rune = {placeholder_char}
const RC_ENCRYPTION_PROTOCOL_VERSION string = "{kc.RC_ENCRYPTION_PROTOCOL_VERSION}"
var VCSRevision string = ""
var IsFrozenBuild string = ""
var IsStandaloneBuild string = ""
const HandleTermiosSignals = 19997
const DefaultTermName = `{Options.term}`
var Version VersionType = VersionType{{Major: {kc.version.major}, Minor: {kc.version.minor}, Patch: {kc.version.patch},}}
var DefaultPager []string = []string{{ {dp} }}
var FunctionalKeyNameAliases = map[string]string{serialize_go_dict(functional_key_name_aliases)}
var CharacterKeyNameAliases = map[string]string{serialize_go_dict(character_key_name_aliases)}
var ConfigModMap = map[string]uint16{serialize_go_dict(config_mod_map)}
var AlattyConfigDefaults = struct {{
Term, Select_by_word_characters, Url_excluded_characters, Shell string
Wheel_scroll_multiplier int
Url_prefixes []string
}}{{
Term: "{Options.term}", Url_prefixes: []string{{ {url_prefixes} }},
Select_by_word_characters: `{Options.select_by_word_characters}`, Wheel_scroll_multiplier: {Options.wheel_scroll_multiplier},
Shell: "{Options.shell}", Url_excluded_characters: "{Options.url_excluded_characters}",
}}
const OptionNames = {option_names}
'''  # }}}


# Boilerplate {{{


@contextmanager
def replace_if_needed(path: str, show_diff: bool = False) -> Iterator[io.StringIO]:
    buf = io.StringIO()
    origb = sys.stdout
    sys.stdout = buf
    try:
        yield buf
    finally:
        sys.stdout = origb
    orig = ''
    with suppress(FileNotFoundError), open(path, 'r') as f:
        orig = f.read()
    new = buf.getvalue()
    new = f'// Code generated by {os.path.basename(__file__)}; DO NOT EDIT.\n\n' + new
    if orig != new:
        changed.append(path)
        if show_diff:
            with open(path + '.new', 'w') as f:
                f.write(new)
                subprocess.run(['diff', '-Naurp', path, f.name], stdout=open('/dev/tty', 'w'))
                os.remove(f.name)
        with open(path, 'w') as f:
            f.write(new)


def define_enum(package_name: str, type_name: str, items: str, underlying_type: str = 'uint') -> str:
    actions = []
    for x in items.splitlines():
        x = x.strip()
        if x:
            actions.append(x)
    ans = [f'package {package_name}', 'import "strconv"', f'type {type_name} {underlying_type}', 'const (']
    stringer = [f'func (ac {type_name}) String() string ' '{', 'switch(ac) {']
    for i, ac in enumerate(actions):
        stringer.append(f'case {ac}: return "{ac}"')
        if i == 0:
            ac = ac + f' {type_name} = iota'
        ans.append(ac)
    ans.append(')')
    stringer.append('}\nreturn strconv.Itoa(int(ac)) }')
    return '\n'.join(ans + stringer)


def generate_readline_actions() -> str:
    return define_enum(
        'readline',
        'Action',
        '''\
        ActionNil

        ActionBackspace
        ActionDelete
        ActionMoveToStartOfLine
        ActionMoveToEndOfLine
        ActionMoveToStartOfDocument
        ActionMoveToEndOfDocument
        ActionMoveToEndOfWord
        ActionMoveToStartOfWord
        ActionCursorLeft
        ActionCursorRight
        ActionEndInput
        ActionAcceptInput
        ActionCursorUp
        ActionCursorDown
        ActionClearScreen
        ActionAddText
        ActionAbortCurrentLine

        ActionStartKillActions
        ActionKillToEndOfLine
        ActionKillToStartOfLine
        ActionKillNextWord
        ActionKillPreviousWord
        ActionKillPreviousSpaceDelimitedWord
        ActionEndKillActions
        ActionYank
        ActionPopYank

        ActionNumericArgumentDigit0
        ActionNumericArgumentDigit1
        ActionNumericArgumentDigit2
        ActionNumericArgumentDigit3
        ActionNumericArgumentDigit4
        ActionNumericArgumentDigit5
        ActionNumericArgumentDigit6
        ActionNumericArgumentDigit7
        ActionNumericArgumentDigit8
        ActionNumericArgumentDigit9
        ActionNumericArgumentDigitMinus
    ''',
    )


def main(args: List[str] = sys.argv) -> None:
    with replace_if_needed('constants_generated.go') as f:
        f.write(generate_constants())
    with replace_if_needed('tools/tui/readline/actions_generated.go') as f:
        f.write(generate_readline_actions())

    kitten_clis()
    print(json.dumps(changed, indent=2))


if __name__ == '__main__':
    import runpy

    m = runpy.run_path(os.path.dirname(os.path.abspath(__file__)))
    m['main']([sys.executable, 'go-code'])
# }}}
