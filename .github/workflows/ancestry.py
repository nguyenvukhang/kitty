from os import listdir
from sys import argv
from subprocess import check_output


def get_ancestry():
    cmd = ("git", "rev-list", "--ancestry-path", "alatty-ci..HEAD")
    return check_output(cmd, encoding='utf8').split()


def stable_commits():
    import requests

    res = requests.get("https://github.com/nguyenvukhang/alatty/wiki/stable_commits")
    return res.text.split()


def __get__(filepath):
    CHECKED_COMMITS = []
    with open(filepath, 'r') as f:
        CHECKED_COMMITS = f.read().strip().split()

    def unchecked(commit):
        return not any(map(commit.startswith, CHECKED_COMMITS))

    x = [x for x in get_ancestry() if unchecked(x)]
    x = '[' + ", ".join([f'"{x}"' for x in x]) + ']'
    print(x)


def __set__():
    all_os = ("linux", "macos")
    results = filter(lambda f: f.endswith(".ok"), listdir())
    results = map(lambda v: v.removesuffix(".ok").split("-"), results)

    commits = {}
    for os, sha in results:
        sha_list = commits.get(sha, [])
        sha_list.append(os)
        commits[sha] = sha_list
        pass

    ok = []

    for sha, os_list in commits.items():
        if all(map(lambda os: os in os_list, all_os)):
            ok.append(sha)

    for i in ok:
        print(i)


def main():
    if len(argv) < 2:
        print("Pls specify a commmand [get|set]")
        return
    if len(argv) == 3 and argv[1] == "get":
        __get__(argv[2])
    if argv[1] == "set":
        __set__()


if __name__ == '__main__':
    main()
