# COL724/7524 Assignment 2 — Link Scheduling and Load Balancing

Team: Mohit + teammate. Full task split, timeline and ownership live in
`assignment2-team-plan-v2.md` at the repo root (kept out of the submission
zips - see the packaging note in Deliverables - but committed here so both
of us always have the current version). This README covers what's actually
in the repo; **see the "Known error in the plan doc" section below before
touching K4/A23.**

## Status

**Phase 0 (joint contracts) complete.** The directory layout, config schema,
wire protocol parsing, request struct, CSV schema, and scheduler interface
are locked and committed.

- Mohit: `/client/*`, `/common/client_ops.*`, workload + experiment scripts
  (tasks C1–C7 in the plan doc).
- Teammate: `/server/*` (except the scheduler core), plus the socket-I/O
  parts of `common/protocol.cpp` (tasks S1–S9). **Stage 1 (S1–S9) is now
  complete and tested** — see "Stage 1 implementation notes (server-infra
  track)" below for what was built, two real bugs found and fixed along the
  way, and the manual test coverage.

The sjf/rr/drr policies and `serve_slice` are still `TODO` stubs pending
Stage 2 (K1–K4) — search the tree for `TODO(` to find every remaining one,
each tagged with which task and spec section it corresponds to.

## Build

Requires g++ with C++17 and POSIX threads (Linux; we build inside WSL2
Ubuntu on Windows dev machines — POSIX sockets don't exist natively on
Windows).

```
make        # builds ./bin/server and ./bin/client
make clean
```

Binaries build to `bin/` rather than repo root, since the spec's suggested
top-level layout already uses the names `server/` and `client/` for source
directories — a file and a directory can't share a name, so this is our
Phase 0 resolution of that clash (stated per Ground Rules).

## Run (once Stage 1/2 land — today this just parses args and exits)

```
./bin/server --sched fcfs --file ./workload --config config.json --metrics-out metrics.csv
./bin/client put text/notes.txt --config config.json
./bin/client get notes.txt --config config.json
./bin/client load ./workload --requests 2000 --config config.json
```

## Repo layout

```
common/    protocol.{h,cpp}   wire framing + request/response parsing (locked)
           config.{h,cpp}     config.json schema + validation (locked)
           csv_writer.{h,cpp} per-request metrics CSV (locked)
           client_ops.{h,cpp} put/get exchange (C2 - Mohit, Stage 1)
           request.h          the Request struct shared by scheduler + CSV
           clock.h            CLOCK_MONOTONIC timestamp helper
           logging.h          A14 fire-log line format (locked)
           third_party/       vendored nlohmann/json.hpp (v3.11.3)
server/    main.cpp           CLI + wiring (S1 done; accept loop TODO S2/S3/S9)
           scheduler.h        IScheduler interface + serve_slice contract (locked)
           queue.h            shared thread-safe request queue helper (locked)
           scheduler_fcfs.cpp real fcfs implementation (reference for the others)
           scheduler_{sjf,rr,drr}.cpp  TODO stubs (K3 - teammate, Stage 2)
           slice.cpp          TODO stub for serve_slice (K1+K2 - Mohit, Stage 2)
           stub_scheduler.{h,cpp}  throwaway FIFO/whole-file path for early
                              end-to-end testing during Stage 1 - does NOT
                              satisfy A5/A6, never use it for experiments
client/    main.cpp           CLI dispatch (C1 - Mohit, Stage 1)
           load.{h,cpp}       experiment driver (C3 - Mohit, Stage 1)
scripts/   run/analysis/comparison scripts land here (C5/C6/C7)
workload/  experiment workload files land here (C4)
config.json  sample config (matches the schema below)
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
- **`--p` parsing**: any integer `argv` accepts via `atoi`; not clamped here
  — validating "sane" values is left to whoever wires `--p` into
  `serve_slice` (K1).
- **Malformed-JSON message**: the spec's example error string is only given
  for a *missing* field; for a JSON syntax error we print
  `error: malformed JSON: <parser detail>` (nlohmann's own message, which
  includes a byte offset) rather than inventing a field name that may not
  exist for a top-level syntax error.
- **`serve_slice` signature**: the plan doc's Phase 0 section writes it as
  `serve_slice(Request*, fd)`; we added `quantum_bytes` and `p_lines`
  parameters since the function can't know how much to send or whether to
  batch without them. Confirm this doesn't surprise anyone at the Stage 2
  (K1-K4) kickoff.
- **Scheduler interface** (`server/scheduler.h`): `enqueue`/`next`/`requeue`/
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

`server/main.cpp` was fully rewritten this stage: accept loop, per-worker
loop (accept → read header → validate → enqueue → `next()` → serve via the
stub scheduler → CSV row → close), signal-driven graceful shutdown.

**Threading model (design decision):** each of `server_threads` workers
runs the *entire* pipeline itself. A worker can end up serving a connection
it did not itself accept, because `next()` returns whatever the active
scheduling policy's shared queue says should go next. This is intentional —
A5 requires the shared queue to determine serving order, and a naive
per-connection FIFO (a worker only ever serves what it accepted) does
**not** satisfy that requirement.

**Two real bugs found and fixed this stage:**
1. `common/protocol.cpp`'s socket I/O (`read_header_line`, `read_exact`,
   `send_all`) was left as no-op/stub code from Phase 0, even though the
   comment attributed it to the server-infra track. Every request was
   getting "connection reset by peer" because the server never actually
   read the request before closing the socket. Fixed with real
   `recv()`/`send()` loops plus `SO_RCVTIMEO` for the header-read timeout.
2. The CSV `rounds` column always wrote `0`. A23 requires `rounds=1` for
   fcfs/sjf. Fixed by setting `to_serve->rounds = 1;` right before serving,
   in `worker_loop`. This is hardcoded since the stub scheduler is the only
   one active in Stage 1 — **whoever wires K3/K4 needs to replace this with
   real round-tracking** once rr/drr exist.

**Manual test coverage (all passing):**
- S2/S3 (accept loop, thread pool, header timeout): GET/PUT/HEALTH exercised
  end-to-end over raw sockets.
- S4 (filename validation, A25): `GET ../etc/passwd`, `GET ..`,
  `GET /etc/passwd`, `GET ./../hello.txt`, `PUT ../evil.txt`,
  `PUT /etc/foo` all correctly rejected; normal `GET` still works
  afterward (no over-rejection).
- S5/S6 (PUT, HEALTH): confirmed byte-opaque PUT round-trips correctly;
  `HEALTH` answered immediately, out-of-band, never appears in the CSV.
- S7 (CSV/timestamps): arrival/start/finish are monotonic per row across
  both sequential and concurrent load.
- S8 (error paths, A24): malformed request line, unknown file, non-numeric
  and missing byte counts, missing filename, and an empty line all return
  distinct `ERR <reason>` messages — never a silent close, never a crash.
- S9 (graceful shutdown, A26): Ctrl+C stops accepting, drains and flushes
  the CSV writer, joins all workers, prints
  `requests_served=... bytes_served=...`, and exits cleanly with no
  leftover process; `SO_REUSEADDR` allows an immediate restart.
- Concurrency smoke test: 6 simultaneous connections against
  `server_threads=4` all got correct, non-corrupted responses, with unique
  sequential `request_id`s and genuinely overlapping finish times in the
  CSV (real parallel serving, not accidental serialization).

**Testing gotcha worth knowing:** when writing a raw-socket test client,
don't call `recv()` only once and assume you have the full response — TCP
is a byte stream, and the server's `OK <n>\n` header and the body can
legitimately arrive in separate reads, especially under concurrent load.
An early concurrency test looked like 4/6 responses were missing their
body; it was a test-client bug (single `recv()` call), not a server bug.
Read until you have the expected length instead. Also: prefer Python
socket scripts over `nc` for manual testing — OpenBSD netcat on WSL2 had
EOF/timing quirks that made a working server look broken.

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
