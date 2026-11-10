#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Pins a board's resolved build configuration, so settings can't silently drift.

    config_lock.py check  BUILD/sdkconfig devices/sdkconfig.muse-PROFILE.lock
    config_lock.py update BUILD/sdkconfig devices/sdkconfig.muse-PROFILE.lock

A lock is every CONFIG_ line of a known-good build (set values and "is not set"
ones), sorted, with secrets left out (tokens, passwords, Wi-Fi credentials:
those live in the git-ignored .local overlay). `check` exits 1 and lists every
difference; tools/muse/board.sh runs it after each build of a board that has a
lock. After changing a setting on purpose, run `update` and commit the lock.
"""
import re
import sys

SECRET = re.compile(r"TOKEN|PASSWORD|PASSWD|SECRET|_KEY$|WIFI_SSID")
LINE = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$|^# (CONFIG_[A-Z0-9_]+) is not set$")


def settings(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = LINE.match(line.rstrip("\n"))
            if not m:
                continue
            name = m.group(1) or m.group(3)
            if SECRET.search(name):
                continue
            out[name] = m.group(2) if m.group(1) else "(not set)"
    return out


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("check", "update"):
        sys.exit(__doc__)
    cmd, built, lock = sys.argv[1:]
    have = settings(built)
    if cmd == "update":
        with open(lock, "w", encoding="utf-8") as f:
            f.write("# Resolved build configuration this board is pinned to (tools/muse/config_lock.py).\n")
            f.write("# Secrets are left out. Regenerate with `config_lock.py update` after a deliberate change.\n")
            for name in sorted(have):
                f.write(f"{name}={have[name]}\n")
        print(f"config lock written: {lock} ({len(have)} settings)")
        return
    want = {}
    with open(lock, encoding="utf-8") as f:
        for line in f:
            if line.startswith("CONFIG_") and "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                want[k] = v
    drift = sorted(k for k in set(want) | set(have) if want.get(k) != have.get(k))
    if not drift:
        print(f"config lock ok: {len(want)} settings match {lock}")
        return
    print(f"CONFIG DRIFT: {len(drift)} setting(s) differ from {lock}:", file=sys.stderr)
    for k in drift:
        print(f"  {k}: locked {want.get(k, '(absent)')} -> built {have.get(k, '(absent)')}", file=sys.stderr)
    print("Fix the overlay (devices/sdkconfig.muse-PROFILE-custom), or if the change is deliberate:\n"
          f"  python3 tools/muse/config_lock.py update {built} {lock}", file=sys.stderr)
    sys.exit(1)


if __name__ == "__main__":
    main()
