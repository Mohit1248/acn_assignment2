# COL724/7524 Assignment 2 — Team Execution Plan (v2, balanced split)

**Team:** You + Mohit
**Deadlines:** Part A — Sep 30 · Part B — Oct 15
**Rule:** Part A must be fully working and merged *before* Part B starts (Part B runs "your Part A binary unmodified").

**What changed from v1:** Part A is now split **in time**, not by ownership. Each of you works on your own track in parallel (18 weight each), then you converge on the hardest piece, the **scheduler core** (12 weight), and split it along a clean seam. Every spec item (A1–A30, B1–B14) is still covered.

Weights are rough effort estimates (1 = quick, 5 = hardest). Log real hours and compare at Checkpoint A-1.

The main risk in splitting is the **wire protocol, config format, CSV schema and scheduler interface**. If you each guess your own version, nothing will link. So both Part A and Part B start with a short joint contract-definition step.

---

## PHASE 0 — Joint: Lock the shared contracts (~1–2 hrs, before anything else)

Do this together in one sitting. Output: a shared repo with these files stubbed out, agreed and committed before either of you writes logic.

1. **Repo/dir structure**, decided once, e.g.:
   ```
   /common/   protocol.h/.cpp, config.h/.cpp, csv_writer.h/.cpp, client_ops.h/.cpp
   /server/   main.cpp, queue.*, slice.cpp, scheduler_*.cpp, stub_scheduler.cpp
   /client/   main.cpp, load.cpp
   /scripts/  run_experiments, analysis, comparison
   ```
2. **Config schema (struct)** matching the spec exactly: `server.ip/port/server_threads/client_threads`, `load_balancer.ip/port/health_interval_ms/backends[4]`. Decide the JSON library (nlohmann/json.hpp vendored, per Ground Rules) and the exact **error message format** for missing/malformed fields (name the offending field, e.g. `error: missing required field 'server.port'`).
3. **Wire protocol module** (`protocol.h`): read a `\n`-delimited header line off a socket (must retain any body bytes over-read in the same `recv()`, which the spec calls out explicitly), and parse `GET <name>`, `PUT <name> <bytes>`, `HEALTH`, `OK <n>`, `ERR <reason>`.
4. **Request struct** used by the scheduler queue: id, op, filename, byte count, arrival/start/finish timestamps (`CLOCK_MONOTONIC`), deficit counter, byte offset, leftover unconsumed socket bytes.
5. **CSV schema** (A23), one exact header string:
   `request_id,op,filename,bytes,rounds,forfeited_bytes,arrival_ns,start_ns,finish_ns`
6. **Scheduler interface** (NEW): the plug-in point between server infrastructure and the scheduler core:
   - `enqueue(Request*)`
   - `Request* next()`
   - `requeue(Request*)`
   - `serve_slice(Request*, fd)`: does one quantum of work, returns `DONE` or `PREEMPTED`
7. **Log line format** for the A14 escape hatch (needed later by the rr-vs-drr comparison script, A29).
8. **Git workflow**: one branch per person (`feature/server-infra`, `feature/client-tools`), merge into `main` at each checkpoint. Agree on code style (brace style, header guards).

Once committed, you can work in parallel without touching each other's files.

---

## PART A — Link Scheduling

### Stage 1 — Parallel (independent tracks, 18 wt each)

#### Your track: Server infrastructure (18 wt), with a stub scheduler

Owns: `/server/*` (except the scheduler core files), and the implementations of `protocol`, `config` and `csv_writer` in `/common/`.

| # | Task | wt | Spec refs |
|---|---|---|---|
| S1 | Server CLI: `--sched`, `--quantum`, `--file`, `--p`, `--config`, `--metrics-out`; validation (missing flag, or `--quantum` with fcfs/sjf) exits non-zero with a clear message | 1 | A1 |
| S2 | Accept loop + thread pool of `server_threads` workers with a **real shared admission queue** (not accept→serve per thread) | 4 | A5 |
| S3 | Header parsing on accept, with a **receive timeout** so a silent client can't block admission or pin a worker | 2 | A5 |
| S4 | Filename validation: reject names containing `/`, or equal to `.`/`..`; confine all I/O to `--file` dir | 1 | A25 |
| S5 | `PUT` path: byte-opaque, no line scanning | 1 | A9 |
| S6 | `HEALTH` answered **immediately, out-of-band of the queue**, returning current queue depth (admitted-but-unserved, including requeued); never logged to CSV | 2 | A5, B8 |
| S7 | Timestamps (`arrival`/`start`/`finish`, monotonic) and derived `waiting`/`response`; CSV writer with exact header; `rounds` and `forfeited_bytes` = 0 for fcfs/sjf/drr and all PUTs | 3 | A18–A20, A23 |
| S8 | Error responses (`ERR <reason>`, never a silent close) for: malformed/unknown request line, missing/non-numeric byte count, rejected filename, GET of nonexistent file | 2 | A24 |
| S9 | Graceful shutdown on SIGINT/SIGTERM: stop accept, drain admitted requests, join threads, print summary, write CSV; `SO_REUSEADDR` set (needed for B14's restart test) | 2 | A26 |
| | **Total** | **18** | |

**Use a stub scheduler for now** (`stub_scheduler.cpp`, throwaway):
- FIFO order.
- GET sends the whole file in one go with the correct `OK <n>` header and exact byte count. No line-unit rule, no `--p` batching, no preemption.

This lets the server run end to end so you can test everything above against Mohit's client.

**Hand-offs to make early:**
- The **CSV writer (S7)** should be one of your first tasks. Mohit's analysis scripts need real CSVs.
- **Do not run experiments against the stub.** It deliberately does not meet A6.

#### Mohit's track: Client + experiment tooling (18 wt)

Owns: `/client/*`, `/common/client_ops.*`, workload generation, run scripts and analysis scripts.

| # | Task | wt | Spec refs |
|---|---|---|---|
| C1 | Client CLI: `put <local-path>`, `get <name>`, `load <workload-dir> --requests N`, plus `--config`; on `put` send only the file's base name | 2 | A2, A3 |
| C2 | `put`/`get` exchanges exactly per the framed protocol (declare size → OK → send/recv exact byte count), placed in `common/` so the load driver reuses them | 2 | wire protocol section |
| C3 | `load` driver: seed the server once from the workload dir (uncounted), then N requests from a **shared atomic counter** across `client_threads` concurrent threads; each request picks a random workload file, 50/50 GET/PUT, **closed-loop** (wait for the full response before the next request); the client itself reports nothing, all metrics come from the server CSV | 3 | A4 |
| C4 | **Workload directory**: small (~1KB) / medium (~30KB) / large (~150KB) files; at least one file with ordinary lines (60–80B) and at least one with lines longer than the chosen quantum Q (2–16KB range) | 3 | A27 |
| C5 | Scripts to run the 6 required experiment cells (fcfs/sjf/rr/drr at reference config; fcfs and rr at `server_threads=1`) and collect the CSVs; dry-run against the stub server | 2 | A28 |
| C6 | Analysis scripts: waiting p50/p99 (nearest-rank percentile), throughput (`N / (max finish − min arrival)`), and (reference config only) normalised slowdown (`response/bytes`, ns/byte) median and p99 **per size class** | 4 | A19–A22 |
| C7 | rr vs drr comparison script: forfeited bytes per size class, A14 fire-count (parse the log line format from Phase 0), slowdown of the long-line file(s) under each policy | 2 | A29 |
| | **Total** | **18** | |

**Hand-off to make first:** finish `put`/`get` (C1, C2) and share them right away (~1 hr of work), so your server has a real client to test against.

C7 is the lightest thing left on Mohit's list. If you finish early, he can also take the report's rr-vs-drr section (see below).

### Stage 2 — Joint: Scheduler core (12 wt, both of you, starts when both Stage 1 tracks are done)

**Do not start this alone.** Begin only once both of you are free, and agree on the seam first. Status until then: **pending, stubbed** (A5–A6 and A8 are not finished until this stage replaces the stub).

| # | Task | wt | Owner | Spec refs |
|---|---|---|---|---|
| K1 | **`serve_slice`**: GET whole-line rule (never emit a partial line before preempting), `--p` batching (group ≤N lines per write, short group if fewer remain) | 3 | Mohit | A6, A8 |
| K2 | **Preemption-state preservation**: on requeue, keep byte offset, unconsumed-but-read socket bytes, and (drr) deficit | 4 | Mohit | A17 |
| K3 | **Policies** over the shared queue: `fcfs`; `sjf` (order by declared byte count for GET and PUT, no aging); `rr` (byte quantum, line-boundary rounding + the A14 oversized-line escape hatch); `drr` (deficit counter, no A14 escape hatch, starts at 0, discarded on exit) | 5 | You | A10–A16 |
| K4 | Delete the stub, wire the real scheduler into the queue, and fill `rounds` / `forfeited_bytes` in the CSV | — | Joint | A23 |

Mohit takes K1 + K2 (~7) because they are self-contained in `slice.cpp` and testable against a `socketpair` or a plain file. You take K3 (~5) plus the wiring glue in K4. If it feels lopsided, move the boundary slightly (e.g. you take the drr deficit bookkeeping from K2).

The two halves live in different files (`slice.cpp` vs `scheduler_*.cpp`), so there are no merge conflicts.

### Checkpoint A-1 (joint, do together)
- Merge both branches into `main`. Run the client against the server manually for a single `put`/`get` of each size class; verify byte-exact transfer (A7: size field == size on disk), and that a partial line is never sent (A6).
- Verify each policy on a small hand-made case: line boundaries, `--p` batching, rr rounds, drr deficit carry-over, A14 firing on the long-line file.
- Confirm `HEALTH` works via `printf 'HEALTH\n' | nc <ip> <port>`.
- Confirm the missing/malformed config field error messages match the required format.
- **Fairness check:** compare real hours logged by each of you. If it's skewed, rebalance the joint report work (below), not the code tracks.

### Run experiments (can parallelize by policy)
- One of you runs `fcfs`/`sjf`, the other runs `rr`/`drr`, plus the two `server_threads=1` runs, using the same workload dir and same Q, committed to the repo so results are reproducible. Rerun any pair of "different" numbers that come out close (A28).

### Report + deliverables (~half day, split by section)

Report (≤6 pages). Each section is written by the person who built and measured that part:

| Section | Writer |
|---|---|
| Policies as implemented, incl. preemption-state handling (A17), design choices | You |
| Experimental setup (threads, Q, workload) | Mohit |
| Per-run p50/p99/throughput table (A28) + slowdown table (A21) | Mohit |
| rr-vs-drr discussion with forfeited-bytes data (A29) | Mohit |
| Trade-off discussion incl. SJF starvation (A11) | Joint (review each other's) |

- **README** (you): build/run instructions, all flags, config field meanings, your stated design choices, reproduction steps.
- **Package:** source + Makefile, README, report PDF, plots (PNG/PDF), workload files, metrics CSVs. Zip as `<entry1> <entry2> A.zip`.

---

## PART B — Load Balancer
*(Start only once Part A is merged, tagged, and both of you can build/run it identically.)*

### Phase B0 — Joint (~30–45 min)
Agree on: LB process architecture (one thread per client connection is fine, B4), the health-checker's internal state format (per-backend: healthy/down, last queue depth, last RTT), and the interface between the health checker and the selector so the two tracks don't collide on the same struct.

### Track 1 (Health checking + selection algorithms), ~13 wt

| # | Task | Spec refs |
|---|---|---|
| 1 | Health-check loop: probe each of the 4 backends independently every `health_interval_ms` with `HEALTH\n`; one slow probe must not delay others (separate thread/async per backend) | B7 |
| 2 | Failure conditions: connection error, malformed reply, or no complete reply within one interval → mark unhealthy and exclude from selection; recovers on next successful probe | B7, B10 |
| 3 | Health log with exact columns `timestamp,backend,status,rtt_ms,queue_depth`, one row per probe (including failed ones, with whatever `rtt_ms` was observed) | B9 |
| 4 | Selection algorithm #1, **probe-cadence state**: e.g. "least reported queue depth" using the last `HEALTH` reply per backend, refreshed only once per interval | B12 |
| 5 | Selection algorithm #2, **real-time LB-local state**: e.g. "least connections in flight", updated on every dispatch/completion, not on probe cadence | B12 |
| 6 | (Optional baseline) plain round robin for comparison | B12 |
| 7 | Startup flag to select the algorithm | B11 |

### Track 2 (Forwarding core), ~15 wt

| # | Task | Spec refs |
|---|---|---|
| 1 | Accept clients on `load_balancer.port`, forward `GET`/`PUT` transparently; an unmodified Part A client pointed at a config with the LB's ip/port must just work | B1, B2 |
| 2 | Concurrency: `client_threads` concurrent clients must not serialize | B4 |
| 3 | Backend connection + pass-through of the exact framed exchange to whichever backend the selector (Track 1) picks | B3 |
| 4 | Failure handling: if a backend dies **before** any response byte reaches the client → `ERR <reason>` to client; if it dies **after** response bytes have started → close the client connection (no error injected mid-stream) | B6 |
| 5 | Per-backend forwarded-request counters; log + print at shutdown | B5 |
| 6 | Shutdown discipline matching A26: SIGINT/SIGTERM → stop accept, let in-flight exchanges finish, then print per-backend counts | B5 |
| 7 | Helper scripts to launch 4 backend instances, each with its own config file (single `server.port` per config) | B3 |

Part B is already roughly even, so no swap is needed. Pick tracks by comfort with concurrency.

### Checkpoint B-1 (joint)
- Run 4 backends + LB, point an unmodified Part A client at the LB config, do a few `put`/`get`. Confirm per-backend counters increment correctly and the health log is populating.

### Experiments (can split, but B14 needs both of you at once)
- **B13** (one person can run solo): 3 backends at `server_threads=4`, one at `server_threads=1`; drive load through the LB with the A27 workload, once per selection algorithm; record per-backend request share and explain the probe-cadence algorithm's between-probe behavior.
- **B14** (do together: one drives load and watches LB logs, the other kills a backend): `kill -9` one backend mid-run; measure detection time vs `health_interval_ms`, count/explain failed requests (ERR or connection closed before declared bytes, per B6), restart the backend, measure re-entry-to-rotation time. A nonzero failure count is fine; just explain what would get it to zero.

### Report + deliverables (joint)
- Report (≤4 pages): both algorithms and which state class each reads (B12); per-backend distribution for each (B13); fault-injection result (B14); trade-off discussion. Split sections by who built/ran what, same as Part A.
- README: build/run, config format, health-check design, how to select each algorithm.
- Package: LB source + Makefile + backend-launch helper scripts, README, report PDF, health-check logs, plotting scripts. Zip as `<entry1> <entry2> B.zip`.

---

## Suggested timeline (Sep 21 → Sep 30 for Part A)

| Dates | Work |
|---|---|
| Sep 21–22 | Phase 0 (contracts, incl. scheduler interface) + start Stage 1 on both tracks |
| Sep 23–24 | Finish Stage 1 (Mohit's `put`/`get` and your CSV writer shared on day 1) |
| Sep 24–25 | Stage 2: scheduler core (K1–K4), then Checkpoint A-1 + fairness check |
| Sep 26–27 | Run all 6 experiment cells, generate plots |
| Sep 28–29 | Write report + README, package deliverables |
| Sep 30 | Submit (buffer day) |

Part B (Oct 1 → Oct 15) follows the same rhythm: ~3 days contracts + tracks, ~2 days integration, ~2 days experiments (incl. the joint B14 session), ~2 days report, buffer before the 15th.

---

## Coverage checklist (nothing skipped)

- **A1** S1 · **A2, A3** C1 · **A4** C3 · **A5** S2, S3, S6 · **A6, A8** K1 · **A7** Checkpoint A-1 · **A9** S5 · **A10–A16** K3 · **A17** K2 · **A18–A20, A23** S7 · **A19–A22** C6 · **A24** S8 · **A25** S4 · **A26** S9 · **A27** C4 · **A28** C5 + experiments · **A29** C7 · **A30** report/README/package
- **B1–B14**: mapped in the Part B tables above.
