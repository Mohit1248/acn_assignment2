import socket

HOST = "127.0.0.1"
PORT = 9000

tests = [
    b"FOO bar\n",                    # malformed command
    b"GET nonexistent.txt\n",        # file doesn't exist
    b"PUT uploaded.txt abc\n",       # non-numeric byte count
    b"PUT uploaded.txt\n",           # missing byte count entirely
    b"GET\n",                        # missing filename
    b"\n",                           # empty line
    b"GET hello.txt\n",              # sanity check - should still work
]

for t in tests:
    s = socket.create_connection((HOST, PORT))
    s.sendall(t)
    resp = s.recv(4096)
    print(f"{t[:40]!r:35} -> {resp!r}")
    s.close()