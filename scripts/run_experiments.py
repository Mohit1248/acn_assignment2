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
import shutil
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

# Throwaway run executed once before the measured cells and never recorded: the
# first cell of a session otherwise pays a cold-start cost (fresh process, cold
# page cache/CPU) that has nothing to do with the policy being measured.
WARMUP_RUN = {"name": "warmup", "sched": "fcfs", "server_threads": REFERENCE_SERVER_THREADS}
WARMUP_REQUESTS = 300

# Supplementary cells (NOT among the six required by A28). They only run when
# named explicitly, e.g. `--only drr_st1,sjf_st1`; the report labels them as such.
EXTRA_RUNS = [
    {"name": "drr_st1", "sched": "drr", "server_threads": SINGLE_SERVER_THREADS, "quantum": QUANTUM},
    {"name": "sjf_st1", "sched": "sjf", "server_threads": SINGLE_SERVER_THREADS},
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
    os.makedirs(results_dir, exist_ok=True)
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
    if args.p is not None:
        server_cmd += ["--p", str(args.p)]

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
    ap.add_argument("--no-warmup", action="store_true", help="skip the throwaway warm-up run")
    ap.add_argument("--with-extras", action="store_true",
                    help="also run the two supplementary cells (drr_st1, sjf_st1)")
    ap.add_argument("--p", type=int, default=None,
                    help="pass --p N to the server (A30 aside only; not part of the required cells)")
    args = ap.parse_args()

    os.makedirs(args.results_dir, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None

    if not args.no_warmup:
        print("[warmup] throwaway run, not recorded")
        warm = argparse.Namespace(**vars(args))
        warm.requests = WARMUP_REQUESTS
        warm_dir = os.path.join(args.results_dir, "_warmup")
        run_one(WARMUP_RUN, 30, warm, warm_dir)
        shutil.rmtree(warm_dir, ignore_errors=True)

    failures = []
    for i, run in enumerate(RUNS + EXTRA_RUNS):
        if only is None and run in EXTRA_RUNS and not args.with_extras:
            continue  # the six required cells only, unless extras are requested
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
