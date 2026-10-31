import socket
import time

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 7000))
s.listen(5)
print("bound 7000", flush=True)
time.sleep(20)
