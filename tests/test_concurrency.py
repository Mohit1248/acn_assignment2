import socket
import threading

HOST = "127.0.0.1"
PORT = 9000

def do_request(i, results):
    s = socket.create_connection((HOST, PORT))
    s.sendall(b"GET hello.txt\n")

    # Read the header line first.
    buf = b""
    while b"\n" not in buf:
        buf += s.recv(1)
    header, rest = buf.split(b"\n", 1)

    # header looks like b"OK 24"
    _, size_str = header.split(b" ")
    expected = int(size_str)

    body = rest
    while len(body) < expected:
        chunk = s.recv(4096)
        if not chunk:
            break
        body += chunk

    results[i] = (header, body)
    s.close()

N = 6
results = [None] * N
threads = [threading.Thread(target=do_request, args=(i, results)) for i in range(N)]
for t in threads: t.start()
for t in threads: t.join()
for i, r in enumerate(results):
    print(i, r)