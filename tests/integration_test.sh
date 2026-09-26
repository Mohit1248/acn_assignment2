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
  sleep 0.6
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

echo
[ "$FAILS" -eq 0 ] && echo "ALL CHECKS PASSED" || echo "$FAILS CHECK(S) FAILED"
exit "$FAILS"
