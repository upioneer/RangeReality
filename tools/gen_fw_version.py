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


# Version contract: new build, new version. Clean trees stamp their tag
# (v0.4.1): prod-ready, no suffix. Dirty trees auto-bump patch above the
# tag and keep the -dirty suffix (v0.4.2-dirty, v0.4.3-dirty, ...) so every
# dev/alpha/beta flash is unique and never mistaken for prod.
# The dev counter lives in include/.fw_devcount (git-ignored, synced like
# the rest of the tree), keyed by base tag; a newer tag moves it forward.
def parse_triple(text):
    t = text.strip()
    if t.startswith("v"):
        t = t[1:]
    parts = t.split(".")
    if len(parts) != 3:
        return None
    try:
        return (int(parts[0]), int(parts[1]), int(parts[2]))
    except ValueError:
        return None


def read_dev_state(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            saved, last = handle.read().strip().split()
            triple = parse_triple(saved)
            if triple is None:
                return None, -1
            return triple, int(last)
    except (OSError, ValueError):
        return None, -1


def next_dev_version(tag_part, state_path):
    base = parse_triple(tag_part) or (0, 0, 0)
    saved_base, last_patch = read_dev_state(state_path)
    if saved_base != base:
        last_patch = base[2]
    # Monotonic even if a tag lands inside dev-issued numbers.
    next_patch = max(last_patch, base[2]) + 1
    with open(state_path, "w", encoding="utf-8") as handle:
        handle.write("%d.%d.%d %d" % (base[0], base[1], base[2], next_patch))
    return "v%d.%d.%d" % (base[0], base[1], next_patch)


def warn_if_tag_reuses_dev_numbers(tag, state_path):
    triple = parse_triple(tag.split("-")[0])
    if triple is None:
        return
    saved_base, last_patch = read_dev_state(state_path)
    # Tag == last dev patch is a promotion (v0.4.3-dirty validates, v0.4.3
    # ships): same source, suffix dropped. Warn only on a tag below numbers
    # already flashed, which would genuinely collide.
    if (
        saved_base is not None
        and saved_base[0] == triple[0]
        and saved_base[1] == triple[1]
        and last_patch > saved_base[2]
        and triple[2] < last_patch
    ):
        print(
            "[fw_version] WARNING: tag v%d.%d.%d sits below dev builds "
            "already flashed (up to patch %d). Bump higher."
            % (triple[0], triple[1], triple[2], last_patch)
        )


def describe_tag_part(describe):
    if describe.startswith("v"):
        return describe.split("-")[0]
    return describe


version = firmware_version()
state_path = os.path.join(env["PROJECT_DIR"], "include", ".fw_devcount")
if version.endswith("-dirty"):
    if os.environ.get("RR_RELEASE") == "1":
        sys.stderr.write(
            "[fw_version] ERROR: release build refused, tree is dirty. "
            "Commit or stash first.\n"
        )
        env.Exit(1)
    version = next_dev_version(describe_tag_part(version), state_path) + "-dirty"
else:
    warn_if_tag_reuses_dev_numbers(version, state_path)
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
