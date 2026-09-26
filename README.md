# COL724/7524 Assignment 2, Part A: Link Scheduling

A multithreaded C++17 TCP file server with an explicit request queue and four
selectable scheduling policies (`fcfs`, `sjf`, `rr`, `drr`), a client that can
upload, download and generate experiment load, and the scripts, workload and
report used to measure how the policy changes who waits.

Part B (the load balancer) is a separate submission and is not in this tree.

## Build

Linux, `g++` with C++17 and POSIX threads (developed and tested on Ubuntu 24.04
under WSL2). No external libraries; the JSON parser is the vendored
single-header `nlohmann/json.hpp` (`src/common/third_party/`).

```
make            # ./server and ./client at the repo root  (g++ -std=c++17 -pthread)
make test       # unit tests of the scheduling core (72 checks; needs tests/ from the git repo)
make clean
```

The sources live under `src/` so that the binaries can be `./server` and
`./client` at the repo root, as the assignment invokes them.

## Run

```
./server --sched fcfs --file ./data
./server --sched rr   --file ./data --quantum 8192 --p 1 --config config.json --metrics-out metrics.csv
./client put text/notes.txt
./client get notes.txt
./client load ./workload --requests 2000 --config config.json
```

The client is invoked as an operation followed by its target. `put` sends only
the base name of the local path; `get` writes the file to the current directory
under the name it was requested by.

### Server flags

| Flag | Meaning |
|---|---|
| `--sched <policy>` | required: `fcfs`, `sjf`, `rr` or `drr` |
| `--quantum <Q>` | bytes per round, a positive integer; required with `rr`/`drr`, rejected otherwise |
| `--file <path>` | required: an existing directory the server serves files from and stores PUTs into |
| `--p <N>` | whole lines grouped into one write on the GET path (positive integer, default 1) |
| `--config <path>` | config file (default `config.json`) |
| `--metrics-out <path>` | per-request CSV written at shutdown (default `metrics.csv`) |

A missing required flag, an unknown flag, `--quantum` with `fcfs`/`sjf` (or `--quantum 0`), a
non-positive `--p`, a `--file` that is not an existing directory, or a bad config all print
one clear line and exit non-zero.

### Client flags

`--config <path>` (default `config.json`), and `--requests <N>`, required by
`load` and rejected by `put`/`get`. `put`/`get` exit non-zero on failure; `load` exits
non-zero if the workload directory is unusable or any request failed (it prints no
metrics, A4).

### config.json

| Field | Meaning |
|---|---|
| `server.ip`, `server.port` | address the server listens on / the client connects to (port 1-65535) |
| `server.server_threads` | number of worker threads that serve requests (1-10000) |
| `server.client_threads` | concurrent client threads used by `client load` (1-10000) |
| `load_balancer.*` | used by Part B only (validated when present, ignored here) |

Every field is required. A missing field, a field of the wrong type, a value out of
range (a thread count of 0 would start a server that never serves) or malformed JSON
exits non-zero naming the field, e.g.
`error: missing required field 'server.port'`. The committed `config.json` is
the reference configuration of the experiments: 4 server threads, 8 client
threads.

### Stop

SIGINT or SIGTERM: the server stops accepting, answers every request already
accepted (including ones accepted just before the signal), joins its threads,
prints `requests_served / bytes_served / send_calls`, writes the CSV and exits.
`SO_REUSEADDR` is set, so it can be restarted on the same port immediately.

## Wire protocol

`GET <name>\n`, `PUT <name> <bytes>\n` and `HEALTH\n`; responses `OK <n>\n` and
`ERR <reason>\n`. One request per connection; the server closes after its last
byte. A GET replies `OK <size>\n` then exactly `size` bytes. A PUT replies
`OK 0\n`, reads exactly `bytes` bytes, then replies `OK 0\n`. `HEALTH` replies
with the scheduler queue depth (admitted-but-unserved requests, preempted ones
included). A request the server cannot fulfil gets an `ERR <reason>` reply,
never a silent close: malformed or unknown request line, missing or
non-numeric byte count, a rejected name, a missing file, an absurd declared
size.

## Design

### Threads and the queue (A5)

* One **acceptor** thread only calls `accept()`.
* Each connection goes to a short-lived **admission** thread: it reads the
  header line (under a 5 s *total* deadline, so neither a silent nor a trickling client pins anything), answers
  `HEALTH` immediately, validates the file name, and enqueues the request. For
  a GET it opens the file and takes the size from `fstat`, so the size is known
  when the request enters the queue and refers to one fixed version of the file.
* `server_threads` **worker** threads only loop on `next()`, then
  `serve_slice()` for one round, then requeue (if preempted) or finish.

Admission never waits for a worker, so requests genuinely accumulate in the
scheduler queue and the policy, not TCP accept order, decides who is served.
`HEALTH` never waits behind the backlog and never appears in the CSV.

### The policies

| Policy | Queue order | Rounds |
|---|---|---|
| `fcfs` | arrival order | 1 (never preempts) |
| `sjf` | smallest declared size first (GET: file size, PUT: count in the request line), earliest arrival on ties, no aging | 1 |
| `rr` | arrival order; a preempted request goes to the tail | each round allows at most Q bytes |
| `drr` | same queue as `rr` | each round allows deficit + Q bytes; unused allowance is kept as deficit |

### One round (`src/server/slice.cpp`, `serve_slice`)

* **GET** is transferred in whole lines (A6): the line is found by scanning to
  the next `\n` (a last line without one still counts) and the terminating `\n`
  counts toward every byte total (A7). Lines are grouped `--p` per `write()`
  (A8); `--p` changes only how bytes are batched, not which are sent, their
  order, or the accounting.
* A round ends as soon as the next line would exceed the remaining allowance
  (A13). Under `rr` the unused remainder is added to `forfeited_bytes`; under
  `drr` it becomes the deficit and `forfeited_bytes` stays 0.
* **A14 (rr only).** If a line longer than Q *starts* a round (nothing sent yet
  in it), it is sent in full and the round ends, overrunning the allowance; one
  `A14 request_id=.. filename=.. line_bytes=.. quantum=..` line goes to stderr.
  A long line reached mid-round is handled by A13 (round ends, remainder
  forfeited) and goes out first in the next round. `drr` has no escape: the
  deficit grows by Q per round until it covers the line (A16).
* **PUT** is byte-exact per round (A9, A13): a round reads
  `min(allowance, remaining)` bytes from the socket into a temp file
  (`<name>.tmp.<id>`) and the file is `rename()`d onto the destination when the
  last byte arrives, so a concurrent GET never sees a half-written file.
* `rounds` counts how many times a request was scheduled; `OK <n>\n` response
  lines are protocol overhead and are not charged to the allowance.
* **Preemption state (A17)** lives in the `Request`: byte offset, the file
  descriptor opened at admission, bytes read from the socket but not yet
  consumed, deficit, `rounds`, `forfeited_bytes`, temp path.

### Metrics CSV (A23)

Header `request_id,op,filename,bytes,rounds,forfeited_bytes,arrival_ns,start_ns,finish_ns`,
one row per completed request. Timestamps are `CLOCK_MONOTONIC`: `arrival` =
header parsed and request enqueued, `start` = first pick-up by a worker,
`finish` = last byte transferred. `rounds` is 1 under fcfs/sjf and the number
of slices under rr/drr; `forfeited_bytes` is non-zero only for rr GETs.

### Choices the spec left open

* **A14 reading** as above (fires only when the long line starts a round); the
  report explains why, and what the alternative would change.
* **Robustness:** the header must arrive within 5 s in total and each 64 KB of a PUT body within 10 s in
  total (a per-`recv()` timeout alone would let a client sending one byte every few seconds hold a thread
  for hours); `MSG_NOSIGNAL` and `SIGPIPE` ignored (a client hanging up
  must not kill the server); a PUT declaring more than 1 GiB is rejected with
  `ERR`; an exception while serving one request is caught and answered with
  `ERR`; `TCP_NODELAY` (each `--p` group is really one segment) and a send
  timeout on accepted sockets.
* **Filenames:** empty, containing `/`, or exactly `.`/`..` are rejected (A25).
  Names cannot contain spaces (the request line has no escaping).
* **Excluding seed requests from metrics (A4):** `load` seeds strictly
  sequentially before any concurrent request starts, so the first
  `<workload file count>` CSV rows by `arrival_ns` are the seeds; the analysis
  scripts drop them (`--seed-count`). `load`'s GETs discard their body.
* **Quantum Q = 8192 bytes** (2-16 KB as required).

## Workload (A27)

`workload/` is generated deterministically by `scripts/gen_workload.py`.

* Size axis: `small.txt` 1,044 B, `medium.txt` 30,774 B, `large.txt` 153,613 B.
* Line axis: small and medium have only ordinary 60-79 byte lines; `large.txt`
  has the same ordinary lines plus six 20,000-byte lines (> Q) spread evenly
  through it, so it is both the large file and the long-line file.

## Tests

```
make test                        # 72 unit checks on serve_slice and the four queue policies
SCHED=rr tests/integration_test.sh   # ~40 end-to-end checks; SCHED = fcfs | sjf | rr | drr (default fcfs)
```

The unit tests (`tests/test_slice.cpp`) drive `serve_slice` over a message-
preserving socketpair and compare rounds, forfeited bytes, A14 counts, write
grouping and PUT resumption against values worked out by hand. The integration
suite covers byte-exact transfer, `HEALTH` behind a silent client, surviving
client hang-ups, queue build-up, the SIGTERM race, absurd PUT sizes, torn-read
freedom under concurrent PUT+GET, a PUT whose body shares a segment with its header,
zero-byte files, `HEALTH` absent from the CSV, SIGINT and an immediate restart on the
same port, byte-exact transfer of every workload file under every `--p` with a small
quantum, clients that trickle bytes, bad configs and flags, and that each policy orders
a queued large and small GET the way it should. `tests/test_*.py` are manual
raw-socket scripts (start a server on port 9000 first).

## Reproducing the experiments

```
scripts/run_all.sh                 # ~2 min: 5 interleaved blocks x 8 cells + the --p aside, then a summary
python3 scripts/summarize.py       # every number, median [min-max] over the runs
python  scripts/make_report.py     # figures + report_A.pdf (needs matplotlib and reportlab)
```

`run_experiments.py` runs the six cells required by A28 (`fcfs`, `sjf`, `rr`,
`drr` at 4 server threads; `fcfs`, `rr` at 1) with 1000 counted requests each
and 8 client threads, generating a config per cell; `--with-extras` adds two
supplementary cells (`drr`, `sjf` at 1 thread). Single cells:
`python3 scripts/run_experiments.py --only rr_ref`. `analysis.py` and
`compare_rr_drr.py` compute the A19-A22 and A29 numbers for one CSV / one
rr-drr pair.

The environment (WSL2 on a laptop, loopback) is noisy, so every cell is run 5
times and the report uses medians; see the report for details. `results/`
holds every run: `results/run1..run5/<cell>.csv` (the metrics CSVs), and
`results/aside_p*` for the `--p` aside (A30). `run_all.sh` also writes each run's server
log and config next to its CSV; those are kept in the git repository, not the zip.

## Layout

```
Makefile  config.json  README.md  report_A.pdf  fig*.png
src/common/   protocol, config, csv_writer, client_ops, request, clock, logging, third_party/json.hpp
src/server/   main.cpp (threads, CLI, shutdown), scheduler.h, queue.h, slice.cpp (serve_slice),
              scheduler_{fcfs,sjf,rr,drr}.cpp, scheduler_factory.cpp
src/client/   main.cpp (put/get/load CLI), load.{h,cpp} (experiment driver)
scripts/      gen_workload.py, run_experiments.py, run_all.sh, analysis.py, compare_rr_drr.py,
              metrics.py, summarize.py, make_report.py, package.py, common.py
workload/     small.txt medium.txt large.txt
tests/        test_slice.cpp, integration_test.sh, manual test_*.py   (git repo only, not in the submission zip)
results/      metrics CSVs of all runs (the git repo also keeps each run's server log and config)
```

## Packaging

`python3 scripts/package.py <entry1> <entry2>` writes `<entry1>_<entry2>_A.zip` with
exactly what the assignment asks for: sources, Makefile, scripts, README, report,
plots (top level), the workload and the metrics CSVs. The tests (`tests/`), and the
per-run server logs and config copies that `run_all.sh` writes next to each CSV, live
in the git repository only; the logs hold the `A14 ...` lines the A14 counts in the
report come from, so those counts show as `n/a` if the report is regenerated from the
zip without re-running the experiments.
