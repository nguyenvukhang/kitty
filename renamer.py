import os, shutil
from os import path
from subprocess import PIPE, run

OPERATE = True


def should_skip(f: str):
    return f.startswith(".git/") or f == "renamer.py"


class name:
    lower = ("kitty", 'alatty')
    upper = ("KITTY", 'ALATTY')
    toggle = ("Kitty", 'Alatty')


def process(fp, replacer):
    lines = []
    with open(fp, "r") as f:
        try:
            lines = f.readlines()
        except:
            pass
    lines = [replacer(l) for l in lines]
    if OPERATE:
        with open(fp, "w") as f:
            f.writelines(lines)


def sh(*cmd: list[str]) -> list[str]:
    out = run(cmd, stdout=PIPE)
    return [d.decode('utf8') for d in out.stdout.strip().splitlines()]


def rename_dirs():
    print('\x1b[32m[rename dirs]\x1b[m')
    for d in sh("fd", "-t", "d", '--color=never', name.lower[0]):
        d = d.strip("/")
        t = d.replace(*name.lower)
        print(d, '->', t)
        if OPERATE:
            shutil.move(d, t)
    print()


def rename_files():
    print('\x1b[32m[rename files]\x1b[m')
    for f in sh("fd", "-t", "f", '--color=never', name.lower[0]):
        f = f.strip("/")
        t = f.replace(*name.lower)
        print(f, '->', t)
        if OPERATE:
            os.rename(f, t)
    print()


def search_and_replace():
    print('\x1b[32m[search and replace]\x1b[m')

    def replacer(line):
        o = line
        for x, y in (name.lower, name.toggle, name.upper):
            line = line.replace(x, y)
        if o != line:
            print("<", o, end='')
            print(">", line, end='')
            print()
        return line

    for root, _, files in os.walk("."):
        for f in files:
            f = path.join(root, f)
            f = path.normpath(f).strip()
            if should_skip(f):
                continue
            process(f, replacer)


def fix_term_codes():
    print('\x1b[32m[fix term codes]\x1b[m')

    def replacer(line):
        x, y = name.lower
        line = line.replace(f"@{y}-", f"@{x}-")
        return line

    for root, _, files in os.walk("."):
        for f in files:
            f = path.join(root, f)
            f = path.normpath(f).strip()
            if should_skip(f):
                continue
            process(f, replacer)

    parser_c = f"{name.lower[1]}/parser.c"
    with open(parser_c, 'r') as f:
        text = f.read()

    x, y = name.lower
    text = text.replace(f'"{y}-", sizeof("{y}-")', f'"{x}-", sizeof("{x}-")')
    text = text.replace(f'sizeof("{y}")', f'sizeof("{x}")')
    with open(parser_c, 'w') as f:
        f.write(text)


rename_dirs()
rename_files()
search_and_replace()
fix_term_codes()


run(['python3', 'make.py'], cwd="logo")
run(["rm", "-f", "README.asciidoc"])

with open("README", 'w') as f:
    f.write("alatty.")

# output = run(["fd", "-t", "f", '--color=never', RENAMES[0][0]], stdout=PIPE)
# dirs = [d.decode('utf8') for d in output.stdout.strip().splitlines()]
