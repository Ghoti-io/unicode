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
"""How an oracle is spelled, so that no tool here spells one itself.

The pattern and most of this text come from the suite-wide exploration in
`notes/suite/CONTAINERS.md`; this library is the first to land it. What it
solves here is narrower than in `regex` and sharper:
`tools/oracle/unicodedata_diff.py` used to `import unicodedata` in its own
process, so "the reference" was whichever CPython ran the tool. That is the
one pin that cannot be written down. Debian 13 gives 3.13 and UCD 15.1 where
these tables are 17.0.0, and the whole triage list in that differential's
header exists because of the two releases in between.

`command("python")` returns the argv prefix to run the reference with, which
is either the host's interpreter or a `docker run` into an image pinned by
digest in `containers/IMAGES`.

Three properties, in the order they matter:

  1. **It does not fail open.** A missing image, a missing engine, a version
     that does not match its pin - each raises. The mode that uses host tools
     has to be asked for by name. A gate that cannot reach its reference must
     say so and fail; "skipped" printed where a comparison should be is the
     failure this directory exists to avoid.

  2. **It says which instrument answered.** `provenance()` returns the line
     every gate prints beside its numbers. A clean run against CPython is a
     different claim from a clean run against CPython 3.15 - and for this
     library the *UCD* the reference carries is the claim, which is why the
     version field of IMAGES is checked rather than trusted.

  3. **Paths mean the same thing on both sides.** The repository is mounted at
     its own host path, so a path a caller has built already resolves and no
     tool needs translating.

Modes, from GHOTI_ORACLE_MODE:

  container  (default) run the reference in its pinned image
  host                 run this machine's own interpreter, and print what it is
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Two directories up, so this file has to live at <repo>/tools/oracle/.
ROOT = os.path.dirname(os.path.dirname(HERE))
IMAGES = os.path.join(HERE, "containers", "IMAGES")

MODE = os.environ.get("GHOTI_ORACLE_MODE", "container")
ENGINE = os.environ.get("GHOTI_CONTAINER_ENGINE", "docker")


class OracleUnavailable(Exception):
    """The reference cannot be reached. Never caught into a skip."""


# How to ask each reference for its version, and what the answer must contain.
# Host mode has no digest to trust, so it is checked here; container mode is
# checked too, because IMAGES is maintained by hand and a version field that
# has drifted from the image it names is a lie nothing else would catch.
#
# For this library the interpreter's own version is not the interesting half -
# `unicodedata.unidata_version` is - so the probe asks for both and the pin is
# written as both. A CPython whose UCD is not what IMAGES says is exactly the
# failure that matters, and the point of the whole exercise is that it is now
# possible to state it.
PROBE = {
    "python": ([
        "python3", "-c",
        "import sys, unicodedata; print('Python %s, unicodedata %s'"
        " % (sys.version.split()[0], unicodedata.unidata_version))",
    ], "Python "),
}
PROBE["python-next"] = PROBE["python"]

# ICU's probe is a program compiled *into* its image, reporting
# U_UNICODE_VERSION from the headers the driver is compiled against - so the pin
# is checked against the same tables that will answer the questions. The
# alternative was parsing `icuinfo`'s XML, which puts a sed script between the
# check and the fact it checks.
PROBE["icu"] = (["icu-version"], "ICU ")

_pins = None
_cache = {}


def pins():
    """The IMAGES table: name -> (image, version, description)."""
    global _pins
    if _pins is not None:
        return _pins
    _pins = {}
    if not os.path.exists(IMAGES):
        return _pins
    with open(IMAGES, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                raise OracleUnavailable(
                    "containers/IMAGES: not three tab-separated fields: %r"
                    % line)
            name, image, version = parts[0], parts[1], parts[2]
            _pins[name] = (image, version, parts[3] if len(parts) > 3 else "")
    return _pins


def _engine_ok():
    if shutil.which(ENGINE) is None:
        raise OracleUnavailable(
            "%s is not on PATH, and GHOTI_ORACLE_MODE is 'container'.\n"
            "Install it, or run with GHOTI_ORACLE_MODE=host to use this "
            "machine's own interpreter - which answers a different question, "
            "and says so in the line it prints." % ENGINE)


def _have_image(image):
    finished = subprocess.run([ENGINE, "image", "exists", image],
        capture_output=True)
    if finished.returncode == 0:
        return True
    # `image exists` is podman's. Fall back to a docker-portable spelling.
    finished = subprocess.run([ENGINE, "image", "inspect", image],
        capture_output=True)
    return finished.returncode == 0


def ensure(name):
    """Make the reference runnable, or raise saying what is missing."""
    if MODE == "host":
        binary = PROBE.get(name, ([name], ""))[0][0]
        if shutil.which(binary) is None:
            raise OracleUnavailable(
                "GHOTI_ORACLE_MODE=host and %s is not on PATH" % binary)
        return
    if MODE != "container":
        raise OracleUnavailable("GHOTI_ORACLE_MODE=%r is not a mode" % MODE)
    _engine_ok()
    table = pins()
    if name not in table:
        raise OracleUnavailable(
            "no pin for %r in tools/oracle/containers/IMAGES" % name)
    image = table[name][0]
    if _have_image(image):
        return
    if os.environ.get("GHOTI_ORACLE_PULL", "1") != "1":
        raise OracleUnavailable(
            "image for %s is not present and GHOTI_ORACLE_PULL is off: %s"
            % (name, image))
    sys.stderr.write("oracle: pulling %s\n" % image)
    finished = subprocess.run([ENGINE, "pull", image], capture_output=True,
        text=True)
    if finished.returncode != 0:
        raise OracleUnavailable(
            "could not pull the pinned image for %s.\n  %s\n%s"
            % (name, image, finished.stderr.strip()))


# Ask one oracle's questions of a different pin, e.g.
#   GHOTI_ORACLE_ALIAS=python=python-next make check-oracle-unicodedata
# which is why IMAGES carries two CPythons: the gating pin is a released
# interpreter, and the second one carries this library's own UCD version. The
# disagreement between them is a reading of what the Consortium changed, and
# only one CPython can be installed at a time.
ALIAS = dict(
    pair.split("=", 1)
    for pair in os.environ.get("GHOTI_ORACLE_ALIAS", "").split(",")
    if "=" in pair)


def command(name, argv=None, scratch=None):
    """The argv prefix that runs `name`'s reference.

    `argv` is what to run *inside*, defaulting to the reference's own
    interpreter. The repository is bind-mounted at its own path, so any path a
    caller has built already resolves - the driver under this directory is
    named by its absolute path and needs no translation.

    `scratch` is a directory the reference must be able to *write*, named the
    same way. Nothing here needs one today: this library's oracle answers on
    stdout. Passing the directory is deliberate rather than mounting /tmp, so
    that a tool which forgets to declare it fails on a path that does not
    exist instead of writing somewhere nobody reads.

    Everything else is closed. `--network none` because no reference here has
    business reaching the network, and the tree is read-only because a corpus
    quietly edited by the thing being compared against it is not a comparison.
    """
    name = ALIAS.get(name, name)
    ensure(name)
    inner = argv if argv is not None else [PROBE.get(name, ([name],))[0][0]]
    if MODE == "host":
        return list(inner)
    image = pins()[name][0]
    argv_out = [ENGINE, "run", "--rm", "-i",
            "--network", "none",
            "--volume", "%s:%s:ro" % (ROOT, ROOT)]
    for path in ([scratch] if isinstance(scratch, str) else (scratch or [])):
        argv_out += ["--volume", "%s:%s:rw" % (path, path)]
    return argv_out + ["--workdir", ROOT, image] + list(inner)


def version(name):
    """What the reference says it is. Runs it; the answer is cached."""
    name = ALIAS.get(name, name)
    key = ("version", name)
    if key in _cache:
        return _cache[key]
    probe, expect = PROBE.get(name, ([name, "--version"], ""))
    finished = subprocess.run(command(name, probe), capture_output=True,
        text=True)
    # stdout only. `docker` on this machine is a podman shim that prints a
    # banner to stderr on every invocation, and a probe that reads both streams
    # reads the banner. The same trap waits for any driver that merges them:
    # the reference's answers and the engine's chatter would interleave on one
    # stream and the extra line would be scored as a disagreement.
    text = finished.stdout.strip().splitlines()
    text = text[0] if text else ""
    if expect and expect not in text:
        raise OracleUnavailable(
            "%s answered %r, which does not look like a version" %
            (name, text))
    _cache[key] = text
    return text


def check_pin(name):
    """Raise unless the reference's version matches containers/IMAGES.

    **Container mode only, and that is a departure** from the pattern in
    `notes/suite/CONTAINERS.md`, which runs this check in both modes. The
    reason it gives for both is that an image *built here* cannot be pinned by
    digest, so the run-time version check is the only guarantee it has. Every
    image this library names is a stock one pinned by digest, so that reason
    does not apply - and applying the check anyway makes host mode a dead end
    rather than an escape hatch, because this machine's CPython is 3.13 and no
    pin worth having names 3.13.

    Host mode therefore reports rather than asserts, and says `unpinned` in the
    line it prints, which is the claim it is actually entitled to make. The
    differential downstream is skew-aware by construction - it is advisory
    unless `--strict` - so an unpinned reference degrades the reading rather
    than corrupting it.
    """
    name = ALIAS.get(name, name)
    table = pins()
    if name not in table:
        return version(name)
    said = table[name][1]
    got = version(name)
    if MODE != "host" and said not in got:
        raise OracleUnavailable(
            "%s: IMAGES says %s and it answers %r" % (name, said, got))
    return got


def provenance(names):
    """One line naming every reference that answered, and how.

    The name printed is the one that *answered*, not the one the gate asked
    for. Under an alias those differ, and printing the requested name makes the
    line name the wrong pin: `GHOTI_ORACLE_ALIAS=python=python-next` reported
    `oracle(container): python Python 3.15.0rc2`, where the version is right and
    the name is the one a reader would grep for. The alias is shown too, because
    "which gate was this" is the other question the line has to answer.
    """
    where = "container" if MODE == "container" else "host, unpinned"
    parts = []
    for name in names:
        resolved = ALIAS.get(name, name)
        label = resolved if resolved == name else "%s as %s" % (resolved, name)
        parts.append("%s %s" % (label, check_pin(name)))
    return "oracle(%s): %s" % (where, ", ".join(parts))
