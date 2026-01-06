import socket
import struct
import sys

import response_pb2


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("socket closed")
        buf.extend(chunk)
    return bytes(buf)


def recv_frame(sock):
    header = recv_exact(sock, 4)
    (size,) = struct.unpack("!I", header)  # network byte order
    return recv_exact(sock, size)


HOST = "127.0.0.1"  # The server's hostname or IP address
PORT = 65432  # The port used by the server

try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        print(f"Connected to server on {HOST}:{PORT}")
        resp = response_pb2.Response()

        while True:
            data = recv_frame(s)
            resp.ParseFromString(data)
            print(resp)

except KeyboardInterrupt:
    print()
    print("Quitting...")
finally:
    sys.exit()
