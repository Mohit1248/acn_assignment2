#!/bin/bash
# Integration regression test for the merged server + client (Stage 1).
# Run from anywhere after `make`:   tests/integration_test.sh
# Exits non-zero if any check fails.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$R"
W="$(mktemp -d)"
# Policy under test: SCHED=fcfs|sjf|rr|drr tests/integration_test.sh  (default fcfs)
SCHED="${SCHED:-fcfs}"
QARG=""
[ "$SCHED" = rr ] || [ "$SCHED" = drr ] && QARG="--quantum 8192"
echo "== policy under test: $SCHED $QARG =="
FAILS=0
SP=""

cleanup() { [ -n "$SP" ] && kill -TERM "$SP" 2>/dev/null; wait 2>/dev/null; rm -rf "$W"; }
trap cleanup EXIT

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILS=$((FAILS + 1)); }

mkdir -p "$W/data" "$W/dl"
cp "$R"/workload/*.txt "$W/data/"

wait_up() {  # poll HEALTH until the server answers (max 5 s)
  for _ in $(seq 1 100); do
    printf "HEALTH\n" | nc -w 1 127.0.0.1 "$1" 2>/dev/null | grep -q "^OK" && return 0
    sleep 0.05
  done
  return 1
}

start_server() {  # port worker_threads csv_path
  python3 - "$1" "$2" "$R" "$W" <<'EOF'
import json, sys
port, threads, root, work = sys.argv[1:5]
c = json.load(open(root + "/config.json"))
c["server"]["port"] = int(port)
c["server"]["server_threads"] = int(threads)
json.dump(c, open("%s/config_%s.json" % (work, port), "w"))
EOF
  "$BIN/server" --sched "$SCHED" $QARG --file "$W/data" --config "$W/config_$1.json" \
      --metrics-out "$3" >"$W/server_$1.log" 2>&1 &
  SP=$!
  wait_up "$1"
}

stop_server() { kill -TERM "$SP" 2>/dev/null; wait "$SP" 2>/dev/null; SP=""; }

alive() { kill -0 "$SP" 2>/dev/null; }

[ -x "$BIN/server" ] && [ -x "$BIN/client" ] || { echo "run 'make' first"; exit 2; }

# ---- 0. byte-exact put/get round trip through the real client ----
start_server 19800 2 "$W/r.csv"
(cd "$W/dl" && cp "$R/workload/large.txt" up.txt \
   && "$BIN/client" put up.txt --config "$W/config_19800.json" \
   && rm up.txt && "$BIN/client" get up.txt --config "$W/config_19800.json")
cmp -s "$R/workload/large.txt" "$W/dl/up.txt" && pass "put/get round trip is byte-exact" \
                                              || fail "put/get round trip is byte-exact"
stop_server

# ---- A. silent client must not delay HEALTH (A5, B8) ----
start_server 19801 1 "$W/a.csv"
T=$(python3 - <<'EOF'
import socket, time
silent = socket.create_connection(("127.0.0.1", 19801)); time.sleep(0.3)
t = time.time()
s = socket.create_connection(("127.0.0.1", 19801)); s.sendall(b"HEALTH\n"); s.recv(64)
print("%.3f" % (time.time() - t))
EOF
)
awk -v t="$T" 'BEGIN{exit !(t < 0.5)}' && pass "HEALTH answered in ${T}s with a silent client connected" \
                                       || fail "HEALTH took ${T}s behind a silent client"
stop_server

# ---- B. clients hanging up mid-transfer must not kill the server ----
start_server 19802 1 "$W/b.csv"
python3 - <<'EOF'
import socket
for i in range(400):
    s = socket.create_connection(("127.0.0.1", 19802)); s.sendall(b"GET large.txt\n"); s.close()
EOF
sleep 1
alive && pass "server survives 400 mid-transfer hang-ups" || fail "server died after client hang-ups"
stop_server

# ---- C. the scheduler queue must actually build up (A5) ----
start_server 19803 1 "$W/c.csv"
D=$(python3 - <<'EOF'
import socket, time, threading
def slow_put():
    s = socket.create_connection(("127.0.0.1", 19803))
    s.sendall(b"PUT slow.txt 100\n"); s.recv(64)
    time.sleep(3); s.sendall(b"x" * 100); s.recv(64); s.close()
threading.Thread(target=slow_put, daemon=True).start()
time.sleep(0.5)
socks = []
for i in range(6):
    s = socket.create_connection(("127.0.0.1", 19803)); s.sendall(b"GET small.txt\n"); socks.append(s)
time.sleep(0.5)
h = socket.create_connection(("127.0.0.1", 19803)); h.sendall(b"HEALTH\n")
print(int(h.recv(64).split()[1]))
EOF
)
[ "${D:-0}" -ge 3 ] && pass "queue depth reached $D behind one busy worker" \
                    || fail "queue depth only $D behind one busy worker (A5)"
sleep 3.5; stop_server

# ---- D. a request accepted just before SIGTERM must still be answered (A26) ----
start_server 19804 2 "$W/d.csv"
REPLY=$(SPID=$SP python3 - <<'EOF'
import socket, time, os, signal
s = socket.create_connection(("127.0.0.1", 19804))
time.sleep(0.3)
os.kill(int(os.environ["SPID"]), signal.SIGTERM)
time.sleep(0.3)
s.sendall(b"GET small.txt\n")
s.settimeout(8)
data = b""
try:
    while True:
        chunk = s.recv(65536)
        if not chunk: break
        data += chunk
except Exception:
    pass
print("OK" if data.startswith(b"OK ") else "NONE")
EOF
)
wait "$SP" 2>/dev/null; STATUS=$?; SP=""
[ "$REPLY" = "OK" ] && pass "request accepted before SIGTERM was still answered" \
                    || fail "request accepted before SIGTERM was dropped (A26)"
[ "$STATUS" = "0" ] && pass "server exited cleanly after SIGTERM" || fail "server exit status $STATUS"

# ---- E. absurd declared PUT size must get ERR, not crash the server (A24) ----
start_server 19805 1 "$W/e.csv"
E=$(python3 - <<'EOF'
import socket
s = socket.create_connection(("127.0.0.1", 19805)); s.sendall(b"PUT big.txt 99999999999999\n")
s.settimeout(5); print(s.recv(64).decode().strip())
EOF
)
sleep 0.3
case "$E" in ERR*) pass "huge PUT rejected with '$E'";; *) fail "huge PUT got '$E'";; esac
alive && pass "server alive after huge PUT" || fail "server died on huge PUT"
stop_server

# ---- F. normal load run: 4 workers, 8 client threads, 200 requests ----
start_server 19806 4 "$W/f.csv"
python3 - "$W" <<'EOF'
import json, sys
p = sys.argv[1] + "/config_19806.json"
c = json.load(open(p)); c["server"]["client_threads"] = 8; json.dump(c, open(p, "w"))
EOF
"$BIN/client" load "$R/workload" --requests 200 --config "$W/config_19806.json"
[ $? -eq 0 ] && pass "load client exited 0" || fail "load client failed"
stop_server
ROWS=$(( $(wc -l < "$W/f.csv") - 1 ))
[ "$ROWS" -eq 203 ] && pass "CSV has 203 rows (3 seed + 200)" || fail "CSV has $ROWS rows, expected 203"
case "$SCHED" in
  fcfs|sjf)
    awk -F, 'NR>1 && $5 != 1 {bad=1} END{exit bad}' "$W/f.csv" \
       && pass "rounds == 1 on every row ($SCHED never preempts, A23)" || fail "rounds != 1 on some row"
    awk -F, 'NR>1 && $6 != 0 {bad=1} END{exit bad}' "$W/f.csv" \
       && pass "forfeited_bytes == 0 on every row" || fail "forfeited_bytes != 0"
    ;;
  rr|drr)
    MAXR=$(awk -F, 'NR>1 && $3=="large.txt" && $2=="GET" && $5>m {m=$5} END{print m+0}' "$W/f.csv")
    [ "$MAXR" -ge 10 ] && pass "large.txt GET was preempted many times (max rounds $MAXR)" \
                       || fail "large.txt GET only took $MAXR rounds"
    awk -F, 'NR>1 && $2=="PUT" && $6 != 0 {bad=1} END{exit bad}' "$W/f.csv" \
       && pass "forfeited_bytes == 0 on every PUT row (A13)" || fail "a PUT forfeited bytes"
    FORF=$(awk -F, 'NR>1 {t+=$6} END{print t+0}' "$W/f.csv")
    A14=$(grep -c "^A14 " "$W/server_19806.log")
    if [ "$SCHED" = rr ]; then
      [ "$FORF" -gt 0 ] && pass "rr forfeited $FORF bytes in total" || fail "rr forfeited nothing"
      [ "$A14" -gt 0 ] && pass "A14 fired $A14 times under rr" || fail "A14 never fired under rr"
    else
      [ "$FORF" -eq 0 ] && pass "drr forfeited nothing" || fail "drr forfeited $FORF bytes"
      [ "$A14" -eq 0 ] && pass "A14 never fires under drr (A16)" || fail "A14 fired $A14 times under drr"
    fi
    ;;
esac

# ---- G. concurrent PUT + GET of the SAME file must never return a torn file (A7) ----
start_server 19807 4 "$W/g.csv"
G=$(python3 - "$R/workload/large.txt" <<'EOF'
import socket, threading, time, sys
orig = open(sys.argv[1], "rb").read()
stop = time.time() + 4
bad, n, lock = [], {"get": 0}, threading.Lock()
def hdr(s):
    b = b""
    while not b.endswith(b"\n"): b += s.recv(1)
    return b.decode().split()
def getter():
    while time.time() < stop:
        s = socket.create_connection(("127.0.0.1", 19807)); s.sendall(b"GET large.txt\n")
        size = int(hdr(s)[1]); data = b""
        while len(data) < size:
            c = s.recv(65536)
            if not c: break
            data += c
        s.close()
        with lock:
            n["get"] += 1
            if size != len(orig) or data != orig: bad.append(size)
def putter():
    while time.time() < stop:
        s = socket.create_connection(("127.0.0.1", 19807)); s.sendall(b"PUT large.txt %d\n" % len(orig)); hdr(s)
        s.sendall(orig); hdr(s); s.close()
ts = [threading.Thread(target=getter) for _ in range(4)] + [threading.Thread(target=putter) for _ in range(4)]
[t.start() for t in ts]; [t.join() for t in ts]
print(len(bad), n["get"])
EOF
)
set -- $G
[ "$1" = "0" ] && [ "${2:-0}" -gt 50 ] && pass "0 torn GETs out of $2 during concurrent PUT+GET of one file" \
                                       || fail "$1 torn GETs out of ${2:-?} during concurrent PUT+GET"
stop_server

# ---- I. with one worker busy, does the policy pick the next request? ----
# Queue = [GET large.txt (arrived first), GET small.txt]. fcfs serves large
# first; sjf serves the smaller one first; rr/drr give large only one quantum
# before small gets its turn, so small finishes first there too.
start_server 19808 1 "$W/i.csv"
ORDER=$(python3 - <<'EOF'
import socket, threading, time
def hdr(s):
    b = b""
    while not b.endswith(b"\n"): b += s.recv(1)
    return b.decode().split()
def slow():
    s = socket.create_connection(("127.0.0.1", 19808)); s.sendall(b"PUT hold.txt 100\n"); hdr(s)
    time.sleep(2); s.sendall(b"x" * 100); hdr(s); s.close()
threading.Thread(target=slow, daemon=True).start()
time.sleep(0.4)                                   # the only worker is now stuck on hold.txt
done = {}
def get(name):
    s = socket.create_connection(("127.0.0.1", 19808)); s.sendall(("GET %s\n" % name).encode())
    size = int(hdr(s)[1]); got = 0
    while got < size:
        c = s.recv(65536)
        if not c: break
        got += len(c)
    done[name] = time.time(); s.close()
a = threading.Thread(target=get, args=("large.txt",)); a.start(); time.sleep(0.3)
b = threading.Thread(target=get, args=("small.txt",)); b.start()
a.join(); b.join()
print("large_first" if done["large.txt"] < done["small.txt"] else "small_first")
EOF
)
if [ "$SCHED" = fcfs ]; then EXP=large_first; else EXP=small_first; fi
[ "$ORDER" = "$EXP" ] && pass "$SCHED served $ORDER, as that policy should" \
                      || fail "$SCHED served $ORDER, expected $EXP"
stop_server

# ---- H. --p must be a positive integer ----
for bad in 0 -3 abc; do
  "$BIN/server" --sched fcfs --file "$W/data" --p "$bad" >/dev/null 2>&1 \
     && fail "--p $bad was accepted" || pass "--p $bad rejected"
done

# ---- J. a PUT whose body arrives in the SAME segment as its header; zero-byte files;
#         HEALTH never logged; SIGINT wrote the CSV; immediate restart on the same port ----
mkdir -p "$W/dj"
python3 - "$W" "$R" <<'EOF'
import json, sys
w, r = sys.argv[1:3]
c = json.load(open(r + "/config.json")); c["server"]["port"] = 19809
json.dump(c, open(w + "/config_19809.json", "w"))
EOF
JQ=""; { [ "$SCHED" = rr ] || [ "$SCHED" = drr ]; } && JQ="--quantum 4"      # 11-byte body = 3 rounds
"$BIN/server" --sched "$SCHED" $JQ --file "$W/dj" --config "$W/config_19809.json" \
    --metrics-out "$W/j.csv" >"$W/server_j.log" 2>&1 &
SP=$!; wait_up 19809
python3 - <<'EOF'
import socket
def rt(req):
    s = socket.create_connection(("127.0.0.1", 19809)); s.sendall(req); s.settimeout(5); out = b""
    while True:
        c = s.recv(4096)
        if not c: break
        out += c
    return out
rt(b"PUT together.txt 11\nhello world")            # header and whole body in one send()
rt(b"PUT empty.txt 0\n")
EOF
[ "$(cat "$W/dj/together.txt" 2>/dev/null)" = "hello world" ] \
    && pass "PUT with header+body in one segment stored intact (leftover bytes kept)" \
    || fail "header+body in one segment: got '$(cat "$W/dj/together.txt" 2>/dev/null)'"
[ -f "$W/dj/empty.txt" ] && [ ! -s "$W/dj/empty.txt" ] && pass "zero-byte PUT creates an empty file" || fail "zero-byte PUT"
Z=$(python3 - <<'EOF'
import socket
s = socket.create_connection(("127.0.0.1", 19809)); s.sendall(b"GET empty.txt\n"); s.settimeout(5)
out = b""
while True:
    c = s.recv(64)
    if not c: break
    out += c
print(out.decode().strip())
EOF
)
[ "$Z" = "OK 0" ] && pass "zero-byte GET replies 'OK 0' and nothing else" || fail "zero-byte GET replied '$Z'"
for i in 1 2 3; do printf "HEALTH\n" | nc -w 1 127.0.0.1 19809 >/dev/null; done
kill -INT "$SP"; wait "$SP" 2>/dev/null; JSTATUS=$?; SP=""
[ "$JSTATUS" = 0 ] && pass "SIGINT: clean exit" || fail "SIGINT exit status $JSTATUS"
JROWS=$(( $(wc -l < "$W/j.csv") - 1 ))
[ "$JROWS" = 3 ] && pass "CSV has 3 rows after SIGINT (2 PUTs + 1 GET; 3 HEALTH probes not logged)" \
                 || fail "CSV has $JROWS rows, expected 3"
grep -q "requests_served=3" "$W/server_j.log" && pass "shutdown summary printed" || fail "no shutdown summary"
"$BIN/server" --sched "$SCHED" $JQ --file "$W/dj" --config "$W/config_19809.json" \
    --metrics-out "$W/j2.csv" >"$W/server_j2.log" 2>&1 &
SP=$!
wait_up 19809 && pass "restart on the same port straight after traffic (SO_REUSEADDR)" || fail "restart refused"
stop_server

# ---- K. byte-exact upload+download of every workload file, small quantum, several --p ----
for PP in 1 7 1000; do
  rm -rf "$W/dk" "$W/ck"; mkdir -p "$W/dk" "$W/ck"
  python3 - "$W" "$R" <<'EOF'
import json, sys
w, r = sys.argv[1:3]
c = json.load(open(r + "/config.json")); c["server"]["port"] = 19810
json.dump(c, open(w + "/config_19810.json", "w"))
EOF
  KQ=""; { [ "$SCHED" = rr ] || [ "$SCHED" = drr ]; } && KQ="--quantum 1000"
  "$BIN/server" --sched "$SCHED" $KQ --p "$PP" --file "$W/dk" --config "$W/config_19810.json" \
      --metrics-out "$W/k.csv" >/dev/null 2>&1 &
  SP=$!; wait_up 19810; KOK=1
  for F in small medium large; do
    "$BIN/client" put "$R/workload/$F.txt" --config "$W/config_19810.json" || KOK=0
    cmp -s "$R/workload/$F.txt" "$W/dk/$F.txt" || KOK=0
    (cd "$W/ck" && "$BIN/client" get "$F.txt" --config "$W/config_19810.json") || KOK=0
    cmp -s "$R/workload/$F.txt" "$W/ck/$F.txt" || KOK=0
  done
  [ "$KOK" = 1 ] && pass "$SCHED --p $PP $KQ: all workload files round-trip byte-exact" \
                 || fail "$SCHED --p $PP $KQ: transfer mismatch"
  stop_server
done

# ---- L. bad configuration must be rejected with a message naming the field (not start a dead server) ----
mkdir -p "$W/dl"
python3 - "$W" "$R" <<'EOF'
import copy, json, sys
w, r = sys.argv[1:3]
base = json.load(open(r + "/config.json"))
def mk(name, mut):
    c = copy.deepcopy(base); mut(c); json.dump(c, open("%s/bad_%s.json" % (w, name), "w"))
mk("threads0", lambda c: c["server"].__setitem__("server_threads", 0))
mk("threadsneg", lambda c: c["server"].__setitem__("server_threads", -2))
mk("client0", lambda c: c["server"].__setitem__("client_threads", 0))
mk("port0", lambda c: c["server"].__setitem__("port", 0))
mk("portstr", lambda c: c["server"].__setitem__("port", "9000"))
mk("noport", lambda c: c["server"].pop("port"))
open(w + "/bad_broken.json", "w").write('{"server": {')
EOF
for CFG_CASE in "threads0:server.server_threads" "threadsneg:server.server_threads" "client0:server.client_threads" \
                "port0:server.port" "portstr:server.port" "noport:server.port" "broken:JSON"; do
  NAME=${CFG_CASE%%:*}; FIELD=${CFG_CASE##*:}
  ERRTXT=$(timeout 3 "$BIN/server" --sched fcfs --file "$W/dl" --config "$W/bad_$NAME.json" --metrics-out "$W/l.csv" 2>&1 >/dev/null)
  RC=$?
  if [ "$RC" -ne 0 ] && [ "$RC" -ne 124 ] && echo "$ERRTXT" | grep -q "$FIELD"; then pass "config '$NAME' rejected (names $FIELD)"
  else fail "config '$NAME': rc=$RC output='$ERRTXT'"; fi
done
for BADQ in 0 -5 abc; do
  "$BIN/server" --sched rr --quantum "$BADQ" --file "$W/dl" >/dev/null 2>&1 \
      && fail "--quantum $BADQ accepted" || pass "--quantum $BADQ rejected"
done
start_server 19813 1 "$W/nul.csv"
NULR=$(python3 - <<'EOF'
import socket
def rt(req):
    s = socket.create_connection(("127.0.0.1", 19813)); s.sendall(req); s.settimeout(5)
    return s.recv(200).decode(errors="replace").strip()
print(rt(b"GET ..\x00x\n") + " | " + rt(b"PUT a\x00b 3\n"))
EOF
)
case "$NULR" in ERR*"| ERR"*) pass "names containing a NUL byte are rejected ($NULR)";; *) fail "NUL byte name: '$NULR'";; esac
stop_server
ERRTXT=$(timeout 3 "$BIN/server" --sched fcfs --file "$W/no_such_dir" --metrics-out "$W/l.csv" 2>&1 >/dev/null); RC=$?
{ [ "$RC" -ne 0 ] && [ "$RC" -ne 124 ] && echo "$ERRTXT" | grep -q -- "--file"; } \
    && pass "--file pointing at a missing directory rejected" || fail "--file missing dir: rc=$RC '$ERRTXT'"
"$BIN/client" load "$W/no_such_workload" --requests 5 --config "$R/config.json" >/dev/null 2>&1 \
    && fail "load on a missing workload dir exited 0" || pass "load on a missing workload dir exits non-zero"

# ---- M. clients that TRICKLE bytes must not pin a thread (A5: total deadline, not per-recv) ----
start_server 19811 1 "$W/m.csv"
HT=$(python3 - <<'EOF'
import socket, time
s = socket.create_connection(("127.0.0.1", 19811)); t = time.time()
try:
    for ch in b"GET small.txt":            # one byte per second, never a newline
        s.sendall(bytes([ch])); time.sleep(1)
except OSError:
    pass                                    # the server hung up on us - that is the point
print("%.1f" % (time.time() - t))
EOF
)
awk -v t="$HT" 'BEGIN{exit !(t < 8)}' && pass "a header dribbled 1 byte/s is cut off after ${HT}s (5 s total deadline)" \
                                      || fail "dribbled header held the connection for ${HT}s"
stop_server

if [ "$SCHED" = fcfs ]; then    # 15 s: run once, the policy is irrelevant to this check
  start_server 19812 1 "$W/m2.csv"
  BT=$(python3 - <<'EOF'
import socket, threading, time
def hdr(s):
    b = b""
    while not b.endswith(b"\n"): b += s.recv(1)
    return b.decode().split()
def dribble():
    s = socket.create_connection(("127.0.0.1", 19812)); s.sendall(b"PUT slow.bin 100000\n"); hdr(s)
    try:
        for _ in range(40):                # 1 byte every 2 s: each recv() is well inside a 5 s timeout
            s.sendall(b"x"); time.sleep(2)
    except OSError:
        pass
threading.Thread(target=dribble, daemon=True).start()
time.sleep(3)                               # the only worker is now stuck reading that body
t = time.time()
s = socket.create_connection(("127.0.0.1", 19812)); s.sendall(b"GET small.txt\n"); s.settimeout(30)
size = int(hdr(s)[1]); got = 0
while got < size:
    c = s.recv(4096)
    if not c: break
    got += len(c)
print("%.1f %d %d" % (time.time() - t, got, size))
EOF
  )
  set -- $BT
  { awk -v t="$1" 'BEGIN{exit !(t < 14)}' && [ "$2" = "$3" ]; } \
      && pass "a trickled PUT body freed the only worker after ~${1}s; the queued GET was then served" \
      || fail "trickled PUT body pinned the worker: GET took ${1:-?}s (got ${2:-?}/${3:-?} bytes)"
  stop_server
fi

echo
[ "$FAILS" -eq 0 ] && echo "ALL CHECKS PASSED" || echo "$FAILS CHECK(S) FAILED"
exit "$FAILS"
