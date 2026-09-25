import socket, threading, time

HOST, PORT = "127.0.0.1", 9000

def get_big(i, results):
    t0 = time.time()
    s = socket.create_connection((HOST, PORT))
    s.sendall(b"GET big.txt\n")
    buf = b""
    while b"\n" not in buf:
        buf += s.recv(1)
    header, rest = buf.split(b"\n", 1)
    _, size_str = header.split(b" ")
    expected = int(size_str)
    body = rest
    while len(body) < expected:
        chunk = s.recv(65536)
        if not chunk: break
        body += chunk
    results[i] = time.time() - t0
    s.close()

N = 10
results = [None] * N
threads = [threading.Thread(target=get_big, args=(i, results)) for i in range(N)]
for t in threads: t.start()
for t in threads: t.join()
print(results)