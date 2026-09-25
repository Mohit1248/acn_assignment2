# COL724/7524 Assignment 2 — Link Scheduling and Load Balancing

Team: Mohit + teammate. Full task split, timeline and ownership live in
`assignment2-team-plan-v2.md` at the repo root (kept out of the submission
zips - see the packaging note in Deliverables - but committed here so both
of us always have the current version). This README covers what's actually
in the repo; **see the "Known error in the plan doc" section below before
touching K4/A23.**

## Status

**Phase 0 (joint contracts) complete. Stage 1 (parallel tracks) is complete
and merged into `main`.** Stage 2 (the scheduler core, K1-K4) is next.

- Mohit: `src/client/*`, `src/common/client_ops.*`, workload + experiment scripts
  (C1-C7): all done. See "Experiment scripts" below for what each script does
  and how it was verified.
- Teammate: `src/server/*` (except the scheduler core) plus the socket
  primitives in `src/common/protocol.cpp` (S1-S9): done and tested - see "Stage 1
  implementation notes (server-infra track)" below.

The sjf/rr/drr policies and `serve_slice` are still `TODO` stubs pending
Stage 2 (K1-K4) - search the tree for `TODO(` to find every remaining one,
each tagged with which task and spec section it corresponds to.

## Build

Requires g++ with C++17 and POSIX threads (Linux; we build inside WSL2
Ubuntu on Windows dev machines — POSIX sockets don't exist natively on
Windows).

```
make        # builds ./server and ./client at the repo root
make clean
```

All sources live under `src/` (`src/common`, `src/server`, `src/client`) so
that the binaries can be `./server` and `./client` at the repo root, exactly
as the assignment's examples invoke them - a file and a directory can't
share a name, so the source folders can't also be called `server/`/`client/`.

## Run (server serves with the stub scheduler until Stage 2 lands)

```
./server --sched fcfs --file ./workload --config config.json --metrics-out metrics.csv
./client put text/notes.txt --config config.json
./client get notes.txt --config config.json
./client load ./workload --requests 2000 --config config.json
```

## Repo layout

```
src/common/ protocol.{h,cpp}   wire framing + request/response parsing, incl.
                              read_header_line/read_exact/send_all (done)
           config.{h,cpp}     config.json schema + validation (locked)
           csv_writer.{h,cpp} per-request metrics CSV (locked)
           client_ops.{h,cpp} put/get exchange (C2 - done, Mohit)
           request.h          the Request struct shared by scheduler + CSV
           clock.h            CLOCK_MONOTONIC timestamp helper
           logging.h          A14 fire-log line format (locked)
           third_party/       vendored nlohmann/json.hpp (v3.11.3)
src/server/ main.cpp           CLI, acceptor/admission/worker threads, shutdown (S1-S9, done)
           scheduler.h        IScheduler interface + serve_slice contract (locked)
           queue.h            shared thread-safe request queue helper (locked)
           scheduler_fcfs.cpp real fcfs implementation (reference for the others)
           scheduler_{sjf,rr,drr}.cpp  TODO stubs (K3 - teammate, Stage 2)
           slice.cpp          TODO stub for serve_slice (K1+K2 - Mohit, Stage 2)
           stub_scheduler.{h,cpp}  throwaway FIFO/whole-file path for early
                              end-to-end testing during Stage 1 - does NOT
                              satisfy A5/A6, never use it for experiments
src/client/ main.cpp           CLI dispatch (C1 - done, Mohit)
           load.{h,cpp}       experiment driver (C3 - done, Mohit)
scripts/   common.py          shared constants/helpers (Q, N, CSV loading,
                              percentile calc, size-class classification)
           gen_workload.py    generates the workload dir (C4, done)
           run_experiments.py runs the 6 required cells, collects CSVs/logs
                              (C5, done - see Status for its current limits)
           analysis.py        waiting p50/p99, throughput, slowdown (C6, done)
           compare_rr_drr.py  forfeited_bytes, A14 fire count, long-line
                              slowdown, rr vs drr (C7, done)
workload/  small.txt (~1KB), medium.txt (~30KB), large.txt (~150KB, also
           the long-line file) - done, see Design choices below (C4)
tests/     integration_test.sh  regression suite for the merged server+client
                              (run `make` then `tests/integration_test.sh`)
           test_*.py          the server-infra track's manual raw-socket tests
                              (default port 9000, start a server first)
           data/              small inputs used by those manual tests
config.json  sample config = the A28 reference config (4 server threads,
           8 client threads); experiment scripts override per run
```

## config.json fields

| Field | Meaning |
|---|---|
| `server.ip` / `server.port` | address the server binds/listens on |
| `server.server_threads` | worker pool size |
| `server.client_threads` | concurrency used by `client load` |
| `load_balancer.ip` / `.port` | address the Part B load balancer binds/listens on |
| `load_balancer.health_interval_ms` | how often the LB probes each backend |
| `load_balancer.backends[]` | exactly 4 `{ip, port}` entries, one per backend server |

Part A only reads `server`; Part B additionally reads `load_balancer`. Every
field is required — a missing/wrong-typed field or malformed JSON exits
non-zero with a message naming the offending field, e.g.:
```
error: missing required field 'server.port'
error: wrong type for field 'server.server_threads'
error: malformed JSON: <parser detail>
```

## Design choices made in Phase 0 (per Ground Rules: state and proceed)

- **Request-line tokenizing**: split on ASCII spaces; wrong token count for
  the verb (e.g. `GET` with no name, `PUT` with no byte count) is
  `MALFORMED`, not tolerated/guessed at.
- **PUT byte count**: must be a plain base-10 non-negative integer, no sign,
  no extra characters — anything else is "missing or non-numeric".
- **Filenames**: empty, `/`-containing, or exactly `.`/`..` are unsafe
  (A25). Filenames may contain spaces on disk, but *not* on the wire — the
  request line format has no escaping, so a workload file with a space in
  its name cannot be transferred. We avoid such names in the workload (C4).
- **A14 fire log**: one line to stderr per firing, format
  `A14 request_id=<id> filename=<name> line_bytes=<L> quantum=<Q>` — the
  rr-vs-drr comparison script (A29) parses this exact prefix.
- **Excluding seed requests from metrics (A4, C3/C6)**: the spec says
  seeding PUTs must not count in any reported metric, but the server's CSV
  logs every completed request unconditionally (A23) - there's no "phase"
  column. `load` seeds strictly sequentially, one PUT per workload file,
  and only *then* spawns the concurrent load threads, so every seed
  request's `arrival_ns`/`request_id` is guaranteed smaller than every
  load-generated request's. The analysis script (C6) excludes seeding by
  sorting the CSV by `arrival_ns` and dropping the first
  `<workload file count>` rows - it does not need any other signal.
- **`load`'s GETs discard their body** (write to `/dev/null`): `load` only
  needs to generate timed traffic for the server to measure (A4 - "the
  client reports nothing"); saving a file for a human is what plain
  `client get` is for, not `load`.
- **`--p` parsing**: any integer `argv` accepts via `atoi`; not clamped here
  — validating "sane" values is left to whoever wires `--p` into
  `serve_slice` (K1).
- **Malformed-JSON message**: the spec's example error string is only given
  for a *missing* field; for a JSON syntax error we print
  `error: malformed JSON: <parser detail>` (nlohmann's own message, which
  includes a byte offset) rather than inventing a field name that may not
  exist for a top-level syntax error.
- **Quantum `Q` = 8192 bytes** (8KB, within the required 2-16KB range): at
  this Q, the ~150KB large file takes ~18 rounds under `rr` if all lines
  were ordinary-length - well into "preempted several times" (A27).
- **Workload (A27, C4)**: `workload/small.txt` (~1KB) and
  `workload/medium.txt` (~30KB) contain only ordinary 60-79 byte lines.
  `workload/large.txt` (~150KB) doubles as *both* the large-size file and
  the required long-line file: it's built from ordinary 60-79 byte lines
  with 6 lines of exactly 20000 bytes (> Q) spread evenly through it, so a
  full transfer under `rr` fires the A14 escape hatch 6 times and under
  `drr` needs `ceil(20000/8192) = 3` rounds of deficit accumulation per
  long line (A16). Generated deterministically by
  `scripts/gen_workload.py` (fixed seeds) so results are reproducible -
  rerun it if the workload ever needs regenerating.
- **`serve_slice` signature**: the plan doc's Phase 0 section writes it as
  `serve_slice(Request*, fd)`; we added `quantum_bytes` and `p_lines`
  parameters since the function can't know how much to send or whether to
  batch without them. Confirm this doesn't surprise anyone at the Stage 2
  (K1-K4) kickoff.
- **Scheduler interface** (`src/server/scheduler.h`): `enqueue`/`next`/`requeue`/
  `queue_depth`/`shutdown` on `IScheduler`, plus a free `serve_slice`
  function (not a method) since it operates on a `Request*` + socket fd
  independent of which policy is active — the four `scheduler_*.cpp` files
  only decide *ordering*, never *how many bytes to send*.
- **Stub scheduler vs. real fcfs**: `stub_scheduler.cpp` is explicitly
  throwaway (whole-file transfer, no line rule, no `--p`, no preemption) so
  Stage 1 has an end-to-end path to test against before Stage 2 lands.
  `scheduler_fcfs.cpp` is the real, spec-conforming fcfs policy and is kept
  as a working reference for how `scheduler_{sjf,rr,drr}.cpp` should be
  structured.

## Stage 1 implementation notes (server-infra track)

`src/server/main.cpp` implements the accept loop, worker pool, and signal-driven
graceful shutdown.

**Threading model (design decision, A5):** three roles, so admission is fully
decoupled from serving:
- one **acceptor** thread that only ever calls `accept()`;
- one short-lived **admission** thread per accepted connection: reads the
  header (with a receive timeout, S3), parses/validates it, answers `HEALTH`
  immediately (S6), and otherwise `enqueue()`s the request;
- `server_threads` **worker** threads that only loop on `next()` -> serve ->
  CSV row -> close, never touching `accept()` or header parsing.

This is what makes the scheduler queue real: requests pile up in it while all
workers are busy, so the active policy (not TCP accept order) decides who is
served next. An earlier version had each worker do accept -> parse -> enqueue
-> `next()` -> serve in one loop; that let the queue hold at most
`server_threads` requests, so sjf/rr/drr could never reorder anything (A5),
and a silent client made `HEALTH` wait behind the header timeout.

**Shutdown order (A26):** stop accepting (`shutdown()` on the listening
socket), join the acceptor, wait for every in-flight admission thread to
finish, only then `sched->shutdown()`, join the workers, and free the
scheduler. Requests accepted just before the signal are therefore still
enqueued, drained and answered.

**Robustness choices made after integration testing:**
- `send_all` uses `MSG_NOSIGNAL` (and `SIGPIPE` is ignored): a client hanging
  up mid-response must never kill the server.
- A PUT declaring more than `kMaxPutBytes` (1 GiB) is rejected at admission
  with `ERR`, and any exception raised while serving one request is caught,
  answered with `ERR`, and does not take down the worker or the server.
- **PUT is atomic**: the body is written to `<name>.tmp.<request id>` and
  `rename()`d over the destination. Writing in place (`ofstream` + truncate)
  let a concurrent GET of the same name see a half-written or empty file:
  27% of GETs (1142 of 4282) came back truncated in a 6 s test with 4 GET and
  4 PUT clients on one file, and `load` does exactly this (50% GET / 50% PUT
  over 3 files, 8 threads), which would have corrupted the `bytes` column and
  SJF's size key. **Stage 2 note (K1/K2):** the real `serve_slice` GET path
  should open the file once, take the size from `fstat` on that descriptor,
  and keep the descriptor in the `Request` across preemptions, so every round
  reads the same version of the file that was sized at admission.
- `--p` must be a positive integer (`--p 0`, `--p -3`, `--p abc` are
  rejected with a clear error, A1).

**Bugs found and fixed during Stage 1:**
1. `src/common/protocol.cpp`'s socket I/O was left as no-op stub code from Phase 0,
   so every request got "connection reset by peer". Fixed with real
   `recv()`/`send()` loops plus `SO_RCVTIMEO` for the header-read timeout.
2. The CSV `rounds` column always wrote `0`. A23 requires `rounds=1` for
   fcfs/sjf. Fixed by setting `to_serve->rounds = 1;` before serving. This is
   hardcoded since the stub scheduler is the only one active in Stage 1 -
   **whoever wires K3/K4 needs to replace it with real round-tracking** once
   rr/drr exist.

**Manual test coverage (all passing):**
- S2/S3 (accept loop, thread pool, header timeout): GET/PUT/HEALTH exercised
  end-to-end over raw sockets.
- S4 (filename validation, A25): `GET ../etc/passwd`, `GET ..`,
  `GET /etc/passwd`, `GET ./../hello.txt`, `PUT ../evil.txt`,
  `PUT /etc/foo` all correctly rejected; normal `GET` still works
  afterward (no over-rejection).
- S5/S6 (PUT, HEALTH): byte-opaque PUT round-trips correctly; `HEALTH`
  answered immediately, out-of-band, never appears in the CSV.
- S7 (CSV/timestamps): arrival/start/finish are monotonic per row across
  both sequential and concurrent load.
- S8 (error paths, A24): malformed request line, unknown file, non-numeric
  and missing byte counts, missing filename, and an empty line all return
  distinct `ERR <reason>` messages - never a silent close, never a crash.
- S9 (graceful shutdown, A26): Ctrl+C stops accepting, drains and flushes
  the CSV writer, joins all workers, prints
  `requests_served=... bytes_served=...`, and exits cleanly; `SO_REUSEADDR`
  allows an immediate restart.
- Concurrency smoke test: simultaneous connections against
  `server_threads=4` all got correct, non-corrupted responses with unique
  sequential `request_id`s and genuinely overlapping finish times.
- Integration tests (run after merging both tracks, see `tests/`): silent
  client vs `HEALTH` (answers in ~1 ms), 400 mid-transfer hang-ups (server
  survives), queue depth building up behind a pinned worker, a request
  accepted just before SIGTERM still being answered, a gigantic declared PUT
  size (rejected with `ERR`), and a 200-request 8-client-thread `load` run
  producing exactly 203 CSV rows (3 seed + 200).

**Testing gotcha worth knowing:** when writing a raw-socket test client,
don't call `recv()` only once and assume you have the full response - TCP
is a byte stream, and the server's `OK <n>\n` header and the body can
legitimately arrive in separate reads. Read until you have the expected
length instead. Also: prefer Python socket scripts over `nc` for manual
testing - OpenBSD netcat on WSL2 had EOF/timing quirks.

## Experiment scripts (C5/C6/C7)

```
python3 scripts/run_experiments.py                       # all 6 A28 cells
python3 scripts/run_experiments.py --only fcfs_ref,rr_ref # a subset
python3 scripts/analysis.py results/fcfs_ref.csv --seed-count 3 --slowdown
python3 scripts/compare_rr_drr.py --rr-csv results/rr_ref.csv \
    --drr-csv results/drr_ref.csv --rr-log results/rr_ref.server.log \
    --seed-count 3
```

`--seed-count` must equal the number of files in the workload directory
used for that run (currently 3: small/medium/large.txt) - see "Excluding
seed requests from metrics" above for why this is sufficient.

**Verification status**: all three scripts have now been run against the real server
(stub scheduler, since sjf/rr/drr do not exist yet): `run_experiments.py`
started the server, waited for `HEALTH`, ran `load`, shut down gracefully and
collected the CSV for both an fcfs and an rr cell; `analysis.py` correctly
reported N=200 from a 203-row CSV (i.e. the 3 seed rows were excluded) and
plausible per-size-class slowdowns. `analysis.py` and `compare_rr_drr.py` were
also checked against small hand-computed synthetic CSVs (waiting/throughput/
slowdown/forfeited_bytes/A14-count values verified by hand).
**None of the numbers produced so far are report material**: the stub
scheduler is FIFO/whole-file, so every policy behaves like fcfs until Stage 2
(K1-K4) replaces it. `compare_rr_drr.py` has not seen a real rr/drr pair yet.

## Known error in the plan doc (flag before Stage 2 / K4)

`assignment2-team-plan-v2.md`'s S7 row says "`rounds` and `forfeited_bytes`
= 0 for fcfs/sjf/drr and all PUTs" - this misreads the assignment. Per the
spec (A23): **`rounds` = 1 for fcfs/sjf** (not 0), = quantum-slice count for
rr/drr; **`forfeited_bytes`** is the one that's 0 for fcfs/sjf/drr and every
PUT. Whoever wires K4 (CSV field population) should follow the assignment
PDF directly for this field, not the plan doc's table.

## Git workflow

- `main` — Phase 0 contracts (this commit) and merge target at each
  checkpoint.
- `feature/server-infra` — teammate's Stage 1 track (S1–S9).
- `feature/client-tools` — Mohit's Stage 1 track (C1–C7).
- Style: 4-space indent, braces on the same line (`if (x) {`), `#ifndef`
  header guards named `<DIR>_<FILE>_H`.

## Deliverables checklist

Not yet applicable — see `assignment2-team-plan-v2.md` for the full A1–A30 /
B1–B14 coverage table and the Part A/B deliverables lists. Packaging into
`<entry1>_<entry2>_A.zip` / `_B.zip` happens at the end, via a
`scripts/package.sh` written closer to each deadline.
