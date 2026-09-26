#!/bin/bash
# Reproduces every experiment in the report (about 2 minutes):
#   * BLOCKS interleaved blocks (default 5), each running all 8 cells once: the
#     six cells required by A28 plus two supplementary ones (drr and sjf at one
#     server thread). Each block starts with an unrecorded warm-up run.
#   * the --p aside (A30): fcfs at the reference configuration, --p 1 vs --p 10,
#     three alternating pairs.
# Output: results/run1..runK/<cell>.{csv,server.log,config.json} and
#         results/aside_p{1,10}_{a,b,c}/. Existing results/ is replaced.
#
#   scripts/run_all.sh            # then: python3 scripts/summarize.py
#   BLOCKS=3 scripts/run_all.sh   # fewer blocks
set -e
cd "$(dirname "$0")/.."
make
BLOCKS=${BLOCKS:-5}
rm -rf results
for i in $(seq 1 "$BLOCKS"); do
  echo "=== block $i of $BLOCKS ==="
  python3 scripts/run_experiments.py --with-extras --results-dir "results/run$i"
done
for i in a b c; do
  python3 scripts/run_experiments.py --only fcfs_ref --p 1  --results-dir "results/aside_p1_$i"
  python3 scripts/run_experiments.py --only fcfs_ref --p 10 --results-dir "results/aside_p10_$i"
done
python3 scripts/summarize.py
echo
echo "Figures and report_A.pdf: python scripts/make_report.py   (needs matplotlib and reportlab)"
