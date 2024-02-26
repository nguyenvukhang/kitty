#!/usr/bin/env python
# License: GPLv3 Copyright: 2020, Kovid Goyal <kovid at kovidgoyal.net>


from typing import Sequence


class CLIOptions:
    pass


LaunchCLIOptions = AskCLIOptions = ClipboardCLIOptions = DiffCLIOptions = CLIOptions
HintsCLIOptions = IcatCLIOptions = PanelCLIOptions = ResizeCLIOptions = CLIOptions
ErrorCLIOptions = UnicodeCLIOptions = RCOptions = RemoteFileCLIOptions = CLIOptions
QueryTerminalCLIOptions = BroadcastCLIOptions = ShowKeyCLIOptions = CLIOptions
ThemesCLIOptions = TransferCLIOptions = LoadConfigRCOptions = ActionRCOptions = CLIOptions


def generate_stub() -> None:
    from .cli import as_type_stub, parse_option_spec
    from .conf.utils import save_type_stub
    text = 'import typing\n\n\n'

    def do(otext=None, cls: str = 'CLIOptions', extra_fields: Sequence[str] = ()):
        nonlocal text
        text += as_type_stub(*parse_option_spec(otext), class_name=cls, extra_fields=extra_fields)

    do(extra_fields=('args: typing.List[str]',))

    from .launch import options_spec
    do(options_spec(), 'LaunchCLIOptions')

    from .remote_control import global_options_spec
    do(global_options_spec(), 'RCOptions')

    from kittens.ask.main import option_text
    do(option_text(), 'AskCLIOptions')

    from alatty.rc.base import all_command_names, command_for_name
    for cmd_name in all_command_names():
        cmd = command_for_name(cmd_name)
        if cmd.options_spec:
            do(cmd.options_spec, f'{cmd.__class__.__name__}RCOptions')

    save_type_stub(text, __file__)


if __name__ == '__main__':
    import subprocess
    subprocess.Popen([
        'alatty', '+runpy',
        'from alatty.cli_stub import generate_stub; generate_stub()'
    ])
