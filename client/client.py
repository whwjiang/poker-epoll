import argparse
import socket
import sys
from time import sleep

shutdown_flag = False

HOST = "127.0.0.1"  # The server's hostname or IP address
PORT = 65432  # The port used by the server

try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        print(f"Connected to server on {HOST}:{PORT}")

        while True:
            user_input = input("-> ")
            s.sendall(user_input.encode())  # Send bytes
            data = s.recv(1024)
            print(f"<- {data.decode()}")

except KeyboardInterrupt:
    print()
    print("Quitting...")
finally:
    sys.exit()
