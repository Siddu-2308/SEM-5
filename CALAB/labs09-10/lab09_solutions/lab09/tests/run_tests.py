#!/usr/bin/env python3
"""Lab 9 test runner -- the cache: mapping and replacement.

    make test                          everything
    python3 tests/run_tests.py unit    just the unit tests
    python3 tests/run_tests.py <name>  just one run, or one program under
                                       every configuration

Three tiers, in the order a failure is worth reading:

    build     does your simulator compile at all?
    unit      does each piece do the right thing when called on its own?
    programs  does the whole simulator report the same figures as ours, over a
              real program on a real machine?

The third tier is different this week. A run is a *program on a machine*, not
just a program: the same three programs are run under four configurations, and
`icache-direct-7-matmul` means 7-matmul on the machine configs/icache-direct.ini
describes. That is the whole method of this week -- hold the program fixed,
change one thing about the memory, and read the difference.

What is compared is the statistics rather than the trace, because a cache
changes what a program costs and never what it computes: a simulator with no
cache at all still retires exactly the same instructions. `instructions` and
`exit_status` are checked for the opposite reason -- a cache that hands back the
wrong bytes does change what the program computes.

The machinery is in simharness.py beside this file, shared by every simulator
week. What gets run and what gets compared is in golden/spec.json, written when
the answer key was recorded.

Writes result.json for the grading script: it records what passed and assigns
no marks. Replaced with a fresh copy before grading, so editing it changes
nothing except what you see while you work.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import simharness

if __name__ == "__main__":
    sys.exit(simharness.main(os.path.dirname(os.path.abspath(__file__)), sys.argv))
