#!/usr/bin/env python
# License: GPLv3 Copyright: 2022, Kovid Goyal <kovid at kovidgoyal.net>


import os
import sys
from typing import List


def hold(args: List[str]) -> None:
    from alatty.constants import kitten_exe
    args = ['kitten', '__hold_till_enter__'] + args[1:]
    os.execvp(kitten_exe(), args)


def launch(args: List[str]) -> None:
    import runpy
    sys.argv = args[1:]
    try:
        exe = args[1]
    except IndexError:
        raise SystemExit(
            'usage: alatty +launch script.py [arguments to be passed to script.py ...]\n\n'
            'script.py will be run with full access to alatty code. If script.py is '
            'prefixed with a : it will be searched for in PATH. If script.py is a directory '
            'the __main__.py file inside it is run just as with the normal Python interpreter.'
        )
    if exe.startswith(':'):
        import shutil
        q = shutil.which(exe[1:])
        if not q:
            raise SystemExit(f'{exe[1:]} not found in PATH')
        exe = q
    if not os.path.exists(exe):
        raise SystemExit(f'{exe} does not exist')
    runpy.run_path(exe, run_name='__main__')


def run_kitten(args: List[str]) -> None:
    try:
        kitten = args[1]
    except IndexError:
        print("Invalid kitten")
        raise SystemExit(1)
    sys.argv = args[1:]
    from kittens.runner import run_kitten as rk
    rk(kitten)


def namespaced(args: List[str]) -> None:
    try:
        func = namespaced_entry_points[args[1]]
    except IndexError:
        raise SystemExit('The alatty command line is incomplete')
    except KeyError:
        pass
    else:
        func(args[1:])
        return
    raise SystemExit(f'{args[1]} is not a known entry point. Choices are: ' + ', '.join(namespaced_entry_points))


entry_points = {
    '+': namespaced,
}
namespaced_entry_points = {k: v for k, v in entry_points.items() if k[0] not in '+@'}
namespaced_entry_points['hold'] = hold
namespaced_entry_points['launch'] = launch
namespaced_entry_points['kitten'] = run_kitten


def setup_openssl_environment(ext_dir: str) -> None:
    # Use our bundled CA certificates instead of the system ones, since
    # many systems come with no certificates in a usable form or have various
    # locations for the certificates.
    d = os.path.dirname
    if 'darwin' in sys.platform.lower():
        cert_file = os.path.join(d(d(d(ext_dir))), 'cacert.pem')
    else:
        cert_file = os.path.join(d(ext_dir), 'cacert.pem')
    os.environ['SSL_CERT_FILE'] = cert_file
    setattr(sys, 'alatty_ssl_env_var', 'SSL_CERT_FILE')


def main() -> None:
    if getattr(sys, 'frozen', False):
        ext_dir: str = getattr(sys, 'alatty_run_data').get('extensions_dir')
        if ext_dir:
            setup_openssl_environment(ext_dir)
    first_arg = '' if len(sys.argv) < 2 else sys.argv[1]
    func = entry_points.get(first_arg)
    if func is None:
        if first_arg.startswith('+'):
            namespaced(['+', first_arg[1:]] + sys.argv[2:])
        else:
            from alatty.main import main as alatty_main
            alatty_main()
    else:
        func(sys.argv[1:])
