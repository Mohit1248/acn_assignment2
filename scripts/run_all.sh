#!/bin/bash
# Reproduces the six required experiment cells (A28: one run per cell, "No
# repetition is required"), plus a one-off --p aside (A30, not required).
#
#   scripts/run_all.sh            # then: python3 scripts/summarize.py
#
# Output: results/<cell>.{csv,server.log,config.json} for the six cells, and
# results/aside_p1/, results/aside_p10/ for the --p aside. Existing results/
# is replaced. An unrecorded warm-up run precedes the six cells, since the
# first cell of a session otherwise pays a cold-start cost unrelated to its
# policy.
set -e
cd "$(dirname "$0")/.."
make
rm -rf results
python3 scripts/run_experiments.py --results-dir results
python3 scripts/run_experiments.py --only fcfs_ref --p 1  --results-dir results/aside_p1
python3 scripts/run_experiments.py --only fcfs_ref --p 10 --results-dir results/aside_p10
python3 scripts/summarize.py
echo
echo "Figures and report_A.pdf: python scripts/make_report.py   (needs matplotlib and reportlab)"
