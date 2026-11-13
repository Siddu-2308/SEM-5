#!/usr/bin/env python3
"""Write compile_commands.json, so clangd knows how this folder is compiled.

    python3 tests/compdb.py build/sim build/unit_tests

The Makefile runs this on every `make`. You never need to run it yourself.

clangd -- the C++ extension .vscode/ is set up for -- reads that file to learn
which flags every source file is compiled with, and, more importantly, which
source files there are at all. The second is what makes Go to Definition work
across files: on a call to a function declared in a header, clangd can only
take you to the *body* in another .cpp if it has indexed that .cpp, and it only
indexes the files this list names.

It has to hold absolute paths -- clangd matches it against the paths of the
files you open -- so it cannot be shipped with the folder. It is written here,
on your machine, and rewritten whenever the folder has moved or the build has
changed. When nothing has changed it is left alone, so clangd does not
re-index on every build.

The commands come from make itself, asked to print rather than run what it
would do for the given targets. So the flags in the file are the flags in the
Makefile, whatever they are, and nothing here has to be kept in step with it.

This never prints and never fails. If it cannot do its job, editor navigation
is the only thing lost: a build must never fail, or show a warning, because of
it.
"""

import json
import os
import shlex
import shutil
import subprocess
import sys

OUT = "compile_commands.json"
SOURCES = (".cpp", ".cc", ".cxx", ".c")


def compiles(targets):
    """The compile commands make would run to build `targets` from scratch."""
    # -n prints instead of running, -B pretends everything is out of date so
    # every compile is printed rather than only the stale ones. The output is
    # captured and nothing of it reaches the terminal: a nested make can say
    # "warning: jobserver unavailable", and the harness counts any line with
    # "warning:" in it against the student's build.
    r = subprocess.run(["make", "-nB", "--no-print-directory", *targets],
                       capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        return None
    here = os.getcwd()
    found = []
    for line in r.stdout.splitlines():
        try:
            argv = shlex.split(line)
        except ValueError:
            continue
        if "-c" not in argv:
            continue
        i = argv.index("-c")
        if i + 1 >= len(argv) or not argv[i + 1].endswith(SOURCES):
            continue
        # The compiler by its full path. clangd is told (in .vscode/) to ask
        # g++ where the standard headers are, and the glob it is given is
        # matched against this path.
        compiler = shutil.which(argv[0]) or argv[0]
        found.append({
            "directory": here,
            "file": os.path.join(here, argv[i + 1]),
            "arguments": [compiler] + argv[1:],
        })
    return found


def main():
    entries = compiles(sys.argv[1:])
    if not entries:
        return
    text = json.dumps(entries, indent=2) + "\n"
    try:
        with open(OUT) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    tmp = OUT + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, OUT)


if __name__ == "__main__":
    try:
        main()
    except BaseException:
        pass
    sys.exit(0)
