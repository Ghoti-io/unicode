#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Unicode.
#
# Ghoti.io Unicode is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""Run one oracle gate, having first proved its reference is reachable.

    oracle_run.py <name>[,<name>...] -- <command> [args...]

Two jobs, from the pattern in `notes/suite/CONTAINERS.md`.

**Prove it, then print it.** The reference is resolved and asked its version
*before* the gate runs, and the version is printed on the line above the gate's
numbers. For this library that line carries the whole claim: a clean
differential against CPython's UCD 16.0 and one against UCD 17.0 are different
statements, and a run that does not say which it made cannot be read a week
later.

**Fail closed.** With GHOTI_ORACLE_REQUIRED=1 an unreachable reference is an
error that names what is missing. Without it the gate still declines to run -
but it declines *loudly*, with the word SKIPPED and a reason, and it does so
having actually tried rather than having read `command -v`.

The distinction matters more than it looks. `command -v python3` answers "is
something called python3 on PATH", which is not the question; the question is
"can this gate reach the reference it names", and the only honest way to answer
it is to reach for the reference. This library's differential is the case in
point: python3 has always been on PATH here, and the reference it reached was
two Unicode releases from the tables it was checking.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env


def main(argv):
    if "--" not in argv:
        sys.stderr.write("usage: oracle_run.py <name>[,<name>] -- <command>\n")
        return 2
    cut = argv.index("--")
    names = [n for n in argv[1:cut][0].split(",") if n]
    command = argv[cut + 1:]
    required = os.environ.get("GHOTI_ORACLE_REQUIRED", "0") == "1"

    try:
        line = oracle_env.provenance(names)
    except oracle_env.OracleUnavailable as why:
        where = " ".join(command[:3])
        if required:
            sys.stderr.write(
                "### %s: the reference is not reachable ###\n%s\n"
                % (where, why))
            return 1
        sys.stderr.write("SKIPPED %s\n  %s\n" % (where, why))
        return 0
    print(line, flush=True)
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
