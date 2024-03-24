#!/usr/bin/env python
# License: GPLv3 Copyright: 2021, Kovid Goyal <kovid at kovidgoyal.net>


import re
from typing import Iterator, List, Tuple, get_type_hints

from alatty.conf.types import Definition, MultiOption, ParserFuncType
from alatty.types import _T


def chunks(lst: List[_T], n: int) -> Iterator[List[_T]]:
    for i in range(0, len(lst), n):
        yield lst[i : i + n]


def atoi(text: str) -> str:
    return f'{int(text):08d}' if text.isdigit() else text


def natural_keys(text: str) -> Tuple[str, ...]:
    return tuple(atoi(c) for c in re.split(r'(\d+)', text))


def go_type_data(parser_func: ParserFuncType, ctype: str, is_multiple: bool = False) -> Tuple[str, str]:
    if ctype:
        if ctype == 'string':
            if is_multiple:
                return 'string', '[]string{val}, nil'
            return 'string', 'val, nil'
        if ctype.startswith('strdict_'):
            _, rsep, fsep = ctype.split('_', 2)
            return 'map[string]string', f'config.ParseStrDict(val, `{rsep}`, `{fsep}`)'
        return f'*{ctype}', f'Parse{ctype}(val)'
    p = parser_func.__name__
    if p == 'int':
        return 'int64', 'strconv.ParseInt(val, 10, 64)'
    if p == 'str':
        return 'string', 'val, nil'
    if p == 'float':
        return 'float64', 'strconv.ParseFloat(val, 10, 64)'
    if p == 'to_bool':
        return 'bool', 'config.StringToBool(val), nil'
    if p == 'to_color':
        return 'style.RGBA', 'style.ParseColor(val)'
    if p == 'to_color_or_none':
        return 'style.NullableColor', 'style.ParseColorOrNone(val)'
    if p == 'positive_int':
        return 'uint64', 'strconv.ParseUint(val, 10, 64)'
    if p == 'positive_float':
        return 'float64', 'config.PositiveFloat(val, 10, 64)'
    if p == 'unit_float':
        return 'float64', 'config.UnitFloat(val, 10, 64)'
    if p == 'python_string':
        return 'string', 'config.StringLiteral(val)'
    th = get_type_hints(parser_func)
    rettype = th['return']
    return {int: 'int64', str: 'string', float: 'float64'}[rettype], f'{p}(val)'


mod_map = {
    "shift": "shift",
    "⇧": "shift",
    "alt": "alt",
    "option": "alt",
    "opt": "alt",
    "⌥": "alt",
    "super": "super",
    "command": "super",
    "cmd": "super",
    "⌘": "super",
    "control": "ctrl",
    "ctrl": "ctrl",
    "⌃": "ctrl",
    "hyper": "hyper",
    "meta": "meta",
    "num_lock": "num_lock",
    "caps_lock": "caps_lock",
}


def normalize_shortcut(spec: str) -> str:
    if spec.endswith('+'):
        spec = spec[:-1] + 'plus'
    parts = spec.lower().split('+')
    key = parts[-1]
    if len(parts) == 1:
        return key
    mods = parts[:-1]
    return '+'.join(mod_map.get(x, x) for x in mods) + '+' + key


def normalize_shortcuts(spec: str) -> Iterator[str]:
    spec = spec.replace('++', '+plus')
    spec = re.sub(r'([^+])>', '\\1\0', spec)
    for x in spec.split('\0'):
        yield normalize_shortcut(x)


def gen_go_code(defn: Definition) -> str:
    lines = [
        'import "fmt"',
        'import "strconv"',
        'import "alatty/tools/config"',
        'import "alatty/tools/utils/style"',
        'var _ = fmt.Println',
        'var _ = config.StringToBool',
        'var _ = strconv.Atoi',
        'var _ = style.ParseColor',
    ]
    a = lines.append
    keyboard_shortcuts = tuple(defn.iter_all_maps())
    choices = {}
    go_types = {}
    go_parsers = {}
    defaults = {}
    multiopts = {''}
    for option in sorted(defn.iter_all_options(), key=lambda a: natural_keys(a.name)):
        name = option.name.capitalize()
        if isinstance(option, MultiOption):
            go_types[name], go_parsers[name] = go_type_data(option.parser_func, option.ctype, True)
            multiopts.add(name)
        else:
            defaults[name] = option.parser_func(option.defval_as_string)
            if option.choices:
                choices[name] = option.choices
                go_types[name] = f'{name}_Choice_Type'
                go_parsers[name] = f'Parse_{name}(val)'
                continue
            go_types[name], go_parsers[name] = go_type_data(option.parser_func, option.ctype)

    for oname in choices:
        a(f'type {go_types[oname]} int')
    a('type Config struct {')
    for name, gotype in go_types.items():
        if name in multiopts:
            a(f'{name} []{gotype}')
        else:
            a(f'{name} {gotype}')
    if keyboard_shortcuts:
        a('KeyboardShortcuts []*config.KeyAction')
    a('}')

    def cval(x: str) -> str:
        return x.replace('-', '_')

    a('func NewConfig() *Config {')
    a('return &Config{')
    from alatty.cli import serialize_as_go_string
    from alatty.fast_data_types import Color

    for name, pname in go_parsers.items():
        if name in multiopts:
            continue
        d = defaults[name]
        if not d:
            continue
        if isinstance(d, str):
            dval = f'{name}_{cval(d)}' if name in choices else f'`{d}`'
        elif isinstance(d, bool):
            dval = repr(d).lower()
        elif isinstance(d, dict):
            dval = 'map[string]string{'
            for k, v in d.items():
                dval += f'"{serialize_as_go_string(k)}": "{serialize_as_go_string(v)}",'
            dval += '}'
        elif isinstance(d, Color):
            dval = f'style.RGBA{{Red:{d.red}, Green: {d.green}, Blue: {d.blue}}}'
            if 'NullableColor' in go_types[name]:
                dval = f'style.NullableColor{{IsSet: true, Color:{dval}}}'
        else:
            dval = repr(d)
        a(f'{name}: {dval},')
    if keyboard_shortcuts:
        a('KeyboardShortcuts: []*config.KeyAction{')
        for sc in keyboard_shortcuts:
            aname, aargs = map(serialize_as_go_string, sc.action_def.partition(' ')[::2])
            a('{' f'Name: "{aname}", Args: "{aargs}", Normalized_keys: []string' '{')
            ns = normalize_shortcuts(sc.key_text)
            a(', '.join(f'"{serialize_as_go_string(x)}"' for x in ns) + ',')
            a('}' '},')
        a('},')

    a('}' '}')

    for oname, choice_vals in choices.items():
        a('const (')
        for i, c in enumerate(choice_vals):
            c = cval(c)
            if i == 0:
                a(f'{oname}_{c} {oname}_Choice_Type = iota')
            else:
                a(f'{oname}_{c}')
        a(')')
        a(f'func (x {oname}_Choice_Type) String() string' ' {')
        a('switch x {')
        a('default: return ""')
        for c in choice_vals:
            a(f'case {oname}_{cval(c)}: return "{c}"')
        a('}' '}')
        a(f'func {go_parsers[oname].split("(")[0]}(val string) (ans {go_types[oname]}, err error) ' '{')
        a('switch val {')
        for c in choice_vals:
            a(f'case "{c}": return {oname}_{cval(c)}, nil')
        vals = ', '.join(choice_vals)
        a(f'default: return ans, fmt.Errorf("%#v is not a valid value for %s. Valid values are: %s", val, "{c}", "{vals}")')
        a('}' '}')

    a('func (c *Config) Parse(key, val string) (err error) {')
    a('switch key {')
    a('default: return fmt.Errorf("Unknown configuration key: %#v", key)')
    for oname, pname in go_parsers.items():
        ol = oname.lower()
        is_multiple = oname in multiopts
        a(f'case "{ol}":')
        if is_multiple:
            a(f'var temp_val []{go_types[oname]}')
        else:
            a(f'var temp_val {go_types[oname]}')
        a(f'temp_val, err = {pname}')
        a(f'if err != nil {{ return fmt.Errorf("Failed to parse {ol} = %#v with error: %w", val, err) }}')
        if is_multiple:
            a(f'c.{oname} = append(c.{oname}, temp_val...)')
        else:
            a(f'c.{oname} = temp_val')
    if keyboard_shortcuts:
        a('case "map":')
        a('tempsc, err := config.ParseMap(val)')
        a('if err != nil { return fmt.Errorf("Failed to parse map = %#v with error: %w", val, err) }')
        a('c.KeyboardShortcuts = append(c.KeyboardShortcuts, tempsc)')
    a('}')
    a('return}')
    return '\n'.join(lines)
