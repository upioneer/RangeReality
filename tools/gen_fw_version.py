"""PlatformIO pre-build script: stamp the firmware version header.

Writes include/fw_version.h with RR_FW_VERSION taken from
`git describe --tags --always --dirty`, falling back to a UTC date stamp
when git is unavailable (e.g. source export without .git). The file is
only rewritten when the version actually changes, so dependent sources
do not rebuild needlessly.
"""
Import("env")

import datetime
import os
import subprocess
import sys


def firmware_version():
    try:
        proc = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            capture_output=True,
            text=True,
            cwd=env["PROJECT_DIR"],
            timeout=10,
        )
        version = proc.stdout.strip()
        if version:
            return version
    except Exception:
        pass
    return datetime.datetime.now(datetime.timezone.utc).strftime("dev-%Y%m%d-%H%M")


# Raw `git describe` output (e.g. v0.4.1, v0.4.1-dirty): the on-screen
# version always correlates exactly with GitHub tags downstream.
# Uniqueness across builds comes from bumping the tag per build.
version = firmware_version()
if version.endswith("-dirty"):
    if os.environ.get("RR_RELEASE") == "1":
        sys.stderr.write(
            "[fw_version] ERROR: release build refused, tree is dirty. "
            "Commit or stash first.\n"
        )
        env.Exit(1)
out_path = os.path.join(env["PROJECT_DIR"], "include", "fw_version.h")
content = '#pragma once\n#define RR_FW_VERSION "%s"\n' % version

try:
    with open(out_path, "r", encoding="utf-8") as handle:
        old = handle.read()
except OSError:
    old = None

if old != content:
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(content)
    print("[fw_version] stamped %s" % version)
