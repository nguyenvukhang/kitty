import os, subprocess
from os import path

HOMEBREW_PREFIX = os.environ["HOMEBREW_PREFIX"]
PYTHON3 = path.join(HOMEBREW_PREFIX, "opt/python/libexec/bin/python")
cellar = lambda x: path.join(HOMEBREW_PREFIX, "Cellar", x)

INFO_PLIST = "kitty.app/Contents/Info.plist"

args = [
    PYTHON3,
    "setup.py",
    "kitty.app",
    "-I" + cellar("librsync/2.3.4/include"),
    "-I" + cellar("xxhash/0.8.2/include"),
]
env = os.environ
env["LDFLAGS"] = "-L/opt/homebrew/lib"
subprocess.run(args, env=env)

with open(INFO_PLIST, 'rb') as f:
    info_plist = f.read().splitlines()

info_plist = [i if i.strip() != b"<string>kitty</string>" else b'<string>Alatty</string>' for i in info_plist]

with open(INFO_PLIST, 'wb') as f:
    f.write(b'\n'.join(info_plist))

subprocess.run(["rm", "-rf", "Alatty.app"])
os.rename("kitty.app/Contents/MacOS/kitty", "kitty.app/Contents/MacOS/alatty")
subprocess.run(["mv", "kitty.app", "Alatty.app"])


def install():
    subprocess.run(["rm", "-rf", '/Applications/Alatty.app'])
    subprocess.run(["cp", '-a', "Alatty.app", '/Applications/Alatty.app'])


# install()
