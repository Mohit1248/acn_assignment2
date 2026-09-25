import socket, time, threading

HOST, PORT = "127.0.0.1", 9000

def silent_client():
    s = socket.create_connection((HOST, PORT))
    time.sleep(8)  # connects but never sends anything
    s.close()

def health_check():
    start = time.time()
    s = socket.create_connection((HOST, PORT))
    s.sendall(b"HEALTH\n")
    resp = s.recv(4096)
    elapsed = time.time() - start
    print("HEALTH response:", resp, "took %.3fs" % elapsed)
    s.close()

t = threading.Thread(target=silent_client)
t.start()
time.sleep(0.3)  # let the silent client connect first
health_check()
t.join()