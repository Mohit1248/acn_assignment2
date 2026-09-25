#!/bin/bash
# Integration regression test for the merged server + client (Stage 1).
# Run from anywhere after `make`:   tests/integration_test.sh
# Exits non-zero if any check fails.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$R/bin"
W="$(mktemp -d)"
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
  "$BIN/server" --sched fcfs --file "$W/data" --config "$W/config_$1.json" \
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
awk -F, 'NR>1 && $5 != 1 {bad=1} END{exit bad}' "$W/f.csv" \
   && pass "rounds == 1 on every row (fcfs, A23)" || fail "rounds != 1 on some row"

echo
[ "$FAILS" -eq 0 ] && echo "ALL CHECKS PASSED" || echo "$FAILS CHECK(S) FAILED"
exit "$FAILS"
