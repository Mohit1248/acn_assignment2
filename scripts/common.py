"""Shared helpers/constants for the experiment scripts (C5/C6/C7).

Kept in one place so gen_workload.py, run_experiments.py, analysis.py, and
compare_rr_drr.py can't silently drift apart on things like the quantum
value or how a request's size class is decided.
"""
import csv
import math

QUANTUM = 8192  # bytes; within the 2-16KB range required by A27
N_REQUESTS = 1000  # minimum per A28
REFERENCE_SERVER_THREADS = 4
REFERENCE_CLIENT_THREADS = 8
SINGLE_SERVER_THREADS = 1

_INT_COLUMNS = ("bytes", "rounds", "forfeited_bytes", "arrival_ns", "start_ns", "finish_ns")


def load_rows(csv_path):
    with open(csv_path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in _INT_COLUMNS:
            r[k] = int(r[k])
    return rows


def exclude_seed_rows(rows, seed_count):
    """Drops the first `seed_count` rows by arrival_ns.

    Seeding (load's initial PUT of every workload file) is strictly
    sequential and completes before any load-generated request is even
    admitted, so sorting by arrival_ns and dropping the first
    `seed_count` rows reliably isolates the seed requests - see README,
    "Excluding seed requests from metrics".
    """
    rows_sorted = sorted(rows, key=lambda r: r["arrival_ns"])
    return rows_sorted[seed_count:]


def nearest_rank_percentile(sorted_values, p):
    """A22: index = ceil(p/100 * N) - 1, 0-indexed over the sorted sample."""
    n = len(sorted_values)
    if n == 0:
        return None
    idx = math.ceil(p / 100 * n) - 1
    idx = max(0, min(idx, n - 1))
    return sorted_values[idx]


def size_class(filename, num_bytes):
    """Classifies a request by workload size class (A27: small/medium/large).

    Matches our workload's naming (small.txt/medium.txt/large.txt) first;
    falls back to a byte-size bucket so this still works if the workload
    naming convention ever changes.
    """
    name = filename.lower()
    if "small" in name:
        return "small"
    if "medium" in name:
        return "medium"
    if "large" in name:
        return "large"
    if num_bytes < 5 * 1024:
        return "small"
    if num_bytes < 80 * 1024:
        return "medium"
    return "large"
