"""Metric computation shared by summarize.py and make_report.py (pure stdlib).

run_metrics() turns one run's CSV into every number the report needs. The
experiments are repeated in several interleaved blocks (results/run1 ...
results/runK, each holding all cells), because this laptop-hosted WSL2 setup
is noisy from one run to the next; across() then gives the median and the
min-max range of any metric over those repeats.
"""
import os
import re
import statistics

from common import (exclude_seed_rows, load_rows, nearest_rank_percentile,
                    size_class)

CLASSES = ("small", "medium", "large")
A14_RE = re.compile(r"^A14 request_id=(\d+) filename=(\S+) line_bytes=(\d+) quantum=(\d+)$")

CELLS = ("fcfs_ref", "sjf_ref", "rr_ref", "drr_ref", "fcfs_st1", "rr_st1")  # the six required by A28
EXTRAS = ("drr_st1", "sjf_st1")  # supplementary, not required by A28
REFERENCE = ("fcfs_ref", "sjf_ref", "rr_ref", "drr_ref")
ALL_CELLS = CELLS + EXTRAS


def _pct(values, p):
    return nearest_rank_percentile(sorted(values), p)


def count_a14(log_path):
    if not os.path.exists(log_path):
        return None
    with open(log_path) as f:
        return sum(1 for line in f if A14_RE.match(line.strip()))


def send_calls(log_path):
    """send() syscall count from the server's shutdown summary line, if present."""
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
    waiting = [r["start_ns"] - r["arrival_ns"] for r in rows]
    response = [r["finish_ns"] - r["arrival_ns"] for r in rows]
    window_s = (max(r["finish_ns"] for r in rows) - min(r["arrival_ns"] for r in rows)) / 1e9

    m = {
        "n": n,
        "wait_p50_ns": _pct(waiting, 50),
        "wait_p99_ns": _pct(waiting, 99),
        "resp_p50_ns": _pct(response, 50),
        "resp_p99_ns": _pct(response, 99),
        "throughput": n / window_s,
        "window_s": window_s,
        "by_class": {},
        "forfeited_total": sum(r["forfeited_bytes"] for r in rows),
        "rounds_mean": sum(r["rounds"] for r in rows) / n,
    }
    for cls in CLASSES:
        crows = [r for r in rows if size_class(r["filename"], r["bytes"]) == cls]
        slow = [(r["finish_ns"] - r["arrival_ns"]) / r["bytes"] for r in crows if r["bytes"] > 0]
        cw = [r["start_ns"] - r["arrival_ns"] for r in crows]
        gets = [r for r in crows if r["op"] == "GET"]
        m["by_class"][cls] = {
            "n": len(crows),
            "n_get": len(gets),
            "slow_med": _pct(slow, 50),
            "slow_p99": _pct(slow, 99),
            "wait_p50_ns": _pct(cw, 50),
            "wait_p99_ns": _pct(cw, 99),
            "wait_max_ns": max(cw),
            "forfeited": sum(r["forfeited_bytes"] for r in crows),
            "rounds_mean_get": (sum(r["rounds"] for r in gets) / len(gets)) if gets else 0.0,
            "rounds_max_get": max((r["rounds"] for r in gets), default=0),
        }
    return m


def load_all(results_dir, seed_count=3):
    """{'runs': [{'name', 'cells': {cell: metrics}, 'a14': {cell: n}}, ...],
        'aside': {name: metrics + send_calls}}"""
    out = {"runs": [], "aside": {}}
    run_dirs = sorted((d for d in os.listdir(results_dir) if re.fullmatch(r"run\d+", d)),
                      key=lambda d: int(d[3:]))
    for d in run_dirs:
        base = os.path.join(results_dir, d)
        run = {"name": d, "cells": {}, "a14": {}}
        for cell in ALL_CELLS:
            p = os.path.join(base, cell + ".csv")
            if os.path.exists(p):
                run["cells"][cell] = run_metrics(p, seed_count)
                run["a14"][cell] = count_a14(os.path.join(base, cell + ".server.log"))
        out["runs"].append(run)
    for name in sorted(os.listdir(results_dir)):
        p = os.path.join(results_dir, name, "fcfs_ref.csv")
        if name.startswith("aside_p") and os.path.exists(p):
            out["aside"][name] = run_metrics(p, seed_count)
            out["aside"][name]["send_calls"] = send_calls(os.path.join(results_dir, name, "fcfs_ref.server.log"))
    return out


def across(data, cell, getter):
    """Median / min / max / all values of getter(metrics) over the repeated runs."""
    vals = [getter(r["cells"][cell]) for r in data["runs"] if cell in r["cells"]]
    return {"median": statistics.median(vals), "min": min(vals), "max": max(vals), "all": vals}


def across_a14(data, cell):
    vals = [r["a14"][cell] for r in data["runs"] if r["a14"].get(cell) is not None]
    return {"median": statistics.median(vals), "min": min(vals), "max": max(vals), "all": vals}
