#!/usr/bin/env python3
"""Builds the Part A submission zip:  <entry1>_<entry2>_A.zip

    python3 scripts/package.py <entry1> <entry2>

Contents (spec A.9): all sources, the Makefile and the scripts; the README; the
report PDF; the plots as PNG files at the top level of the zip; the workload
(test files) and the metrics CSVs of every run. Left out on purpose: build
artefacts, the server-side data directories the experiments create, Python
caches, and internal notes (the team plan document).
"""
import glob
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TOP_FILES = ["Makefile", "config.json", "README.md", "report_A.pdf"]
TOP_GLOBS = ["fig*.png"]
DIRS = ["src", "scripts", "workload", "tests", "results"]


def skip(rel):
    parts = rel.split("/")
    name = parts[-1]
    if "__pycache__" in parts or name.endswith(".pyc"):
        return True
    if any(p.endswith(".data") or p == "_warmup" for p in parts):
        return True  # server storage dirs created by the experiment runs
    if rel == "tests/test_slice":  # compiled unit-test binary
        return True
    return False


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: package.py <entry1> <entry2>")
    entry1, entry2 = sys.argv[1:3]
    out = os.path.join(ROOT, f"{entry1}_{entry2}_A.zip")

    files = []
    for f in TOP_FILES:
        if not os.path.exists(os.path.join(ROOT, f)):
            sys.exit(f"missing required file: {f}")
        files.append(f)
    for g in TOP_GLOBS:
        files += sorted(os.path.basename(p) for p in glob.glob(os.path.join(ROOT, g)))
    for d in DIRS:
        for base, _, names in os.walk(os.path.join(ROOT, d)):
            for n in sorted(names):
                rel = os.path.relpath(os.path.join(base, n), ROOT).replace(os.sep, "/")
                if not skip(rel):
                    files.append(rel)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for rel in files:
            z.write(os.path.join(ROOT, rel), rel)
    print(f"{out}: {len(files)} files, {os.path.getsize(out) / 1024:.0f} KB")


if __name__ == "__main__":
    main()
