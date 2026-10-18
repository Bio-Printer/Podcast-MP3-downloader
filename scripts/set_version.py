#!/usr/bin/env python3
"""Stamps the version from the VERSION file (plus a build id) into every
place that shows it, so the app, the helper and the Edge extension always
report the same thing.

    python3 scripts/set_version.py                 # build id = current date/time
    python3 scripts/set_version.py --build dev     # explicit build id

Updates: src/core/version.h, extension/manifest.json (version + version_name).
"""
import argparse
import datetime
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", help="build id, default YYYYMMDD-HHMM")
    args = ap.parse_args()

    version = open(os.path.join(ROOT, "VERSION")).read().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("VERSION must look like 1.2.3, got %r" % version)
    major, minor, patch = version.split(".")
    build = args.build or datetime.datetime.now().strftime("%Y%m%d-%H%M")
    full = "%s (build %s)" % (version, build)

    vh = os.path.join(ROOT, "src", "core", "version.h")
    text = open(vh).read()
    for name, value in [("PMP3_VERSION", '"%s"' % version), ("PMP3_VERSION_MAJOR", major),
                        ("PMP3_VERSION_MINOR", minor), ("PMP3_VERSION_PATCH", patch),
                        ("PMP3_BUILD", '"%s"' % build)]:
        text, n = re.subn(r"(#define %s ).*" % name, lambda m: m.group(1) + value, text)
        if n != 1:
            raise SystemExit("could not update %s in version.h" % name)
    open(vh, "w", newline="\n").write(text)

    mf = os.path.join(ROOT, "extension", "manifest.json")
    manifest = json.load(open(mf, encoding="utf-8"))
    manifest["version"] = version
    manifest["version_name"] = full
    with open(mf, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")

    print(full)


if __name__ == "__main__":
    main()
