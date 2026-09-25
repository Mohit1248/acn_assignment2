#!/usr/bin/env python3
"""C5: runs the 6 required experiment cells (A28) and collects CSVs/logs.

Cells: fcfs/sjf/rr/drr at the reference config (4 server threads, 8 client
threads), plus fcfs/rr again at server_threads=1.

NOTE: until Stage 2 (K1-K4) lands, ./server ignores --sched and always uses
the FIFO stub scheduler, so every cell behaves like fcfs - the orchestration
works, but the numbers are not meaningful for the report yet. Run `make`
first; the binaries are ./server and ./client at the repo root.

Usage:
    python3 run_experiments.py                      # all 6 cells
    python3 run_experiments.py --only fcfs_ref,rr_ref
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

from common import (N_REQUESTS, QUANTUM, REFERENCE_CLIENT_THREADS,
                     REFERENCE_SERVER_THREADS, SINGLE_SERVER_THREADS)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_CONFIG_PATH = os.path.join(REPO_ROOT, "config.json")
WORKLOAD_DIR = os.path.join(REPO_ROOT, "workload")

RUNS = [
    {"name": "fcfs_ref", "sched": "fcfs", "server_threads": REFERENCE_SERVER_THREADS},
    {"name": "sjf_ref", "sched": "sjf", "server_threads": REFERENCE_SERVER_THREADS},
    {"name": "rr_ref", "sched": "rr", "server_threads": REFERENCE_SERVER_THREADS, "quantum": QUANTUM},
    {"name": "drr_ref", "sched": "drr", "server_threads": REFERENCE_SERVER_THREADS, "quantum": QUANTUM},
    {"name": "fcfs_st1", "sched": "fcfs", "server_threads": SINGLE_SERVER_THREADS},
    {"name": "rr_st1", "sched": "rr", "server_threads": SINGLE_SERVER_THREADS, "quantum": QUANTUM},
]


def make_config(server_threads, port, path):
    with open(BASE_CONFIG_PATH) as f:
        cfg = json.load(f)
    cfg["server"]["server_threads"] = server_threads
    cfg["server"]["client_threads"] = REFERENCE_CLIENT_THREADS
    cfg["server"]["port"] = port
    with open(path, "w") as f:
        json.dump(cfg, f, indent=2)


def wait_for_health(ip, port, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with socket.create_connection((ip, port), timeout=1) as s:
                s.sendall(b"HEALTH\n")
                s.settimeout(1)
                reply = s.recv(64)
                if reply.startswith(b"OK "):
                    return True
        except OSError:
            pass
        time.sleep(0.2)
    return False


def run_one(run, index, args, results_dir):
    port = 9100 + index
    config_path = os.path.join(results_dir, run["name"] + ".config.json")
    make_config(run["server_threads"], port, config_path)

    data_dir = os.path.join(results_dir, run["name"] + ".data")
    os.makedirs(data_dir, exist_ok=True)
    csv_path = os.path.join(results_dir, run["name"] + ".csv")
    log_path = os.path.join(results_dir, run["name"] + ".server.log")

    server_cmd = [args.server_bin, "--sched", run["sched"], "--file", data_dir,
                  "--config", config_path, "--metrics-out", csv_path]
    if "quantum" in run:
        server_cmd += ["--quantum", str(run["quantum"])]

    print(f"[{run['name']}] starting server: {' '.join(server_cmd)}")
    with open(log_path, "w") as logf:
        server_proc = subprocess.Popen(server_cmd, stdout=logf, stderr=subprocess.STDOUT)

    healthy = wait_for_health("127.0.0.1", port, args.health_timeout)
    if not healthy:
        print(f"[{run['name']}] server never became healthy on port {port} - "
              f"is the accept loop (S2/S3) implemented yet? see {log_path}", file=sys.stderr)
        if server_proc.poll() is None:
            server_proc.terminate()
            try:
                server_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server_proc.kill()
        return False

    client_cmd = [args.client_bin, "load", WORKLOAD_DIR, "--requests", str(args.requests),
                  "--config", config_path]
    print(f"[{run['name']}] running load: {' '.join(client_cmd)}")
    subprocess.run(client_cmd, check=False)

    print(f"[{run['name']}] shutting down server")
    if server_proc.poll() is None:
        server_proc.terminate()
        try:
            server_proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server_proc.kill()
            server_proc.wait()

    print(f"[{run['name']}] done -> {csv_path}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server-bin", default=os.path.join(REPO_ROOT, "server"))
    ap.add_argument("--client-bin", default=os.path.join(REPO_ROOT, "client"))
    ap.add_argument("--results-dir", default=os.path.join(REPO_ROOT, "results"))
    ap.add_argument("--requests", type=int, default=N_REQUESTS)
    ap.add_argument("--health-timeout", type=float, default=10)
    ap.add_argument("--only", help="comma-separated run names to run (default: all 6)")
    args = ap.parse_args()

    os.makedirs(args.results_dir, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None

    failures = []
    for i, run in enumerate(RUNS):
        if only and run["name"] not in only:
            continue
        if not run_one(run, i, args, args.results_dir):
            failures.append(run["name"])

    if failures:
        print(f"\n{len(failures)} run(s) failed: {', '.join(failures)}", file=sys.stderr)
        sys.exit(1)
    print("\nall runs completed")


if __name__ == "__main__":
    main()
