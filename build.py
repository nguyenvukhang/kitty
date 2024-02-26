import os, subprocess
from os import path
from sys import stderr


def homebrew_prefix():
    p = os.environ.get("HOMEBREW_PREFIX", None)
    if p is None:
        return subprocess.check_output(["brew", "--prefix"], encoding='utf8').strip()
    return p


HOMEBREW_PREFIX = homebrew_prefix()
PYTHON3 = path.join(HOMEBREW_PREFIX, "opt/python/libexec/bin/python")
cellar = lambda x: path.join(HOMEBREW_PREFIX, "Cellar", x)
BUILD_ARGS = [PYTHON3, "setup.py"]

log = lambda *x: print("[build.py]", *x, file=stderr)


def build():
    BUILD_ARGS.append("alatty.app")
    BUILD_ARGS.append("-I" + cellar("xxhash/0.8.2/include"))
    env = os.environ
    env["LDFLAGS"] = "-L/opt/homebrew/lib"

    # build!
    log("building with args", BUILD_ARGS)
    if subprocess.run(BUILD_ARGS, env=env).returncode != 0:
        exit(1)


def install():
    subprocess.run(["rm", "-rf", '/Applications/Alatty.app'])
    subprocess.run(["cp", '-a', "alatty.app", '/Applications/Alatty.app'])


build()
if "alatty" not in os.environ.get("TERMINFO", ""):
    log("installing...")
    install()
else:
    log("skipping install.")
