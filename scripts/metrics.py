"""Metric computation for the six required cells (A28), one run each - the
spec's default ("No repetition is required"). run_metrics() turns one run's
CSV into every number the report needs, so the report cannot drift from the
data. If a pair of numbers ever needs the "rerun once, report both" exception
(A28), that rerun's CSV is loaded as <cell>_rerun.csv - see load_all().
"""
import os
import re

from common import (exclude_seed_rows, load_rows, nearest_rank_percentile,
                    size_class)

CLASSES = ("small", "medium", "large")
A14_RE = re.compile(r"^A14 request_id=(\d+) filename=(\S+) line_bytes=(\d+) quantum=(\d+)$")

CELLS = ("fcfs_ref", "sjf_ref", "rr_ref", "drr_ref", "fcfs_st1", "rr_st1")  # the six required by A28
REFERENCE = ("fcfs_ref", "sjf_ref", "rr_ref", "drr_ref")


def fmt_count(x):
    return "n/a" if x is None else f"{x:.0f}"


def count_a14(log_path):
    """A14 fire count from a run's server log. The submission zip does not
    ship these logs (only the metrics CSVs, per A.9), so this returns None
    when the log is absent rather than failing."""
    if not os.path.exists(log_path):
        return None
    with open(log_path) as f:
        return sum(1 for line in f if A14_RE.match(line.strip()))


def send_calls(log_path):
    """send() syscall count from a run's server-shutdown summary line, if the
    log is present (the submission zip does not ship server logs)."""
    if not os.path.exists(log_path):
        return None
    with open(log_path) as f:
        for line in f:
            m = re.search(r"send_calls=(\d+)", line)
            if m:
                return int(m.group(1))
    return None


def run_metrics(csv_path, seed_count):
    rows = exclude_seed_rows(load_rows(csv_path), seed_count)
    n = len(rows)
    waiting = sorted(r["start_ns"] - r["arrival_ns"] for r in rows)
    response = sorted(r["finish_ns"] - r["arrival_ns"] for r in rows)
    window_s = (max(r["finish_ns"] for r in rows) - min(r["arrival_ns"] for r in rows)) / 1e9

    m = {
        "n": n,
        "wait_p50_ns": nearest_rank_percentile(waiting, 50),
        "wait_p99_ns": nearest_rank_percentile(waiting, 99),
        "resp_p50_ns": nearest_rank_percentile(response, 50),
        "resp_p99_ns": nearest_rank_percentile(response, 99),
        "throughput": n / window_s,
        "by_class": {},
    }
    for cls in CLASSES:
        crows = [r for r in rows if size_class(r["filename"], r["bytes"]) == cls]
        slow = sorted((r["finish_ns"] - r["arrival_ns"]) / r["bytes"] for r in crows if r["bytes"] > 0)
        cw = sorted(r["start_ns"] - r["arrival_ns"] for r in crows)
        gets = [r for r in crows if r["op"] == "GET"]
        m["by_class"][cls] = {
            "n": len(crows),
            "n_get": len(gets),
            "slow_med": nearest_rank_percentile(slow, 50),
            "slow_p99": nearest_rank_percentile(slow, 99),
            "wait_avg_ns": (sum(cw) / len(cw)) if cw else 0.0,
            "wait_p50_ns": nearest_rank_percentile(cw, 50),
            "wait_p99_ns": nearest_rank_percentile(cw, 99),
            "forfeited": sum(r["forfeited_bytes"] for r in crows),
            "rounds_mean_get": (sum(r["rounds"] for r in gets) / len(gets)) if gets else 0.0,
            "rounds_max_get": max((r["rounds"] for r in gets), default=0),
        }
    return m


def load_all(results_dir, seed_count=3):
    """{'cells': {cell: metrics}, 'a14': {cell: n_or_None}, 'rerun': {cell: metrics}}
    'rerun' is only populated for a cell where <cell>_rerun.csv exists (A28's
    "rerun that pair once" exception)."""
    out = {"cells": {}, "a14": {}, "rerun": {}}
    for cell in CELLS:
        p = os.path.join(results_dir, cell + ".csv")
        if os.path.exists(p):
            out["cells"][cell] = run_metrics(p, seed_count)
            out["a14"][cell] = count_a14(os.path.join(results_dir, cell + ".server.log"))
        rp = os.path.join(results_dir, cell + "_rerun.csv")
        if os.path.exists(rp):
            out["rerun"][cell] = run_metrics(rp, seed_count)
    out["aside"] = {}
    for name in ("aside_p1", "aside_p10"):
        p = os.path.join(results_dir, name, "fcfs_ref.csv")
        if os.path.exists(p):
            out["aside"][name] = run_metrics(p, seed_count)
            out["aside"][name]["send_calls"] = send_calls(os.path.join(results_dir, name, "fcfs_ref.server.log"))
    return out
