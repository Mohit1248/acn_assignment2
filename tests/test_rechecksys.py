import socket

HOST, PORT = "127.0.0.1", 9000

def req(line, read_body_len=None):
    s = socket.create_connection((HOST, PORT))
    s.sendall(line)
    buf = b""
    while b"\n" not in buf:
        buf += s.recv(1)
    header, rest = buf.split(b"\n", 1)
    print(line, "->", header)
    if read_body_len is not None:
        body = rest
        while len(body) < read_body_len:
            chunk = s.recv(4096)
            if not chunk: break
            body += chunk
        print("   body:", body)
    s.close()

req(b"HEALTH\n")
req(b"GET hello.txt\n", read_body_len=24)
req(b"PUT sanity.txt 5\nhello", read_body_len=0)
req(b"GET ../etc/passwd\n")   # should still be rejected
req(b"FOO bar\n")             # should still be a clean ERR