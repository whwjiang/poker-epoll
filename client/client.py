import selectors
import socket
import struct
import sys

import actions_pb2
import response_pb2


class FrameBuffer:
    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf.extend(data)
        frames = []
        while True:
            if len(self.buf) < 4:
                break
            (size,) = struct.unpack("!I", self.buf[:4])
            if len(self.buf) < 4 + size:
                break
            frames.append(bytes(self.buf[4 : 4 + size]))
            del self.buf[: 4 + size]
        return frames


def frame_message(payload: bytes) -> bytes:
    return struct.pack("!I", len(payload)) + payload


def parse_action(line: str):
    parts = line.strip().split()
    if not parts:
        return None
    action = actions_pb2.Action()
    cmd = parts[0].lower()
    if cmd in ("fold", "f"):
        action.fold.SetInParent()
        return action
    if cmd in ("bet", "b") and len(parts) == 2:
        try:
            amount = int(parts[1])
        except ValueError:
            return None
        action.bet.amount = amount
        return action
    return None


HOST = "127.0.0.1"
PORT = 65432

sel = selectors.DefaultSelector()
out_buf = bytearray()
in_buf = FrameBuffer()


def update_interest(sock):
    events = selectors.EVENT_READ
    if out_buf:
        events |= selectors.EVENT_WRITE
    sel.modify(sock, events, data="sock")


def queue_action(sock, action):
    payload = action.SerializeToString()
    out_buf.extend(frame_message(payload))
    update_interest(sock)


def print_prompt():
    sys.stdout.write("> ")
    sys.stdout.flush()


def handle_socket(sock, mask):
    if mask & selectors.EVENT_READ:
        data = sock.recv(4096)
        if not data:
            raise ConnectionError("server closed")
        for frame in in_buf.feed(data):
            resp = response_pb2.Response()
            resp.ParseFromString(frame)
            sys.stdout.write("\r")
            print(resp)
            print_prompt()
    if mask & selectors.EVENT_WRITE:
        if out_buf:
            sent = sock.send(out_buf)
            del out_buf[:sent]
        if not out_buf:
            update_interest(sock)


def handle_stdin(sock):
    line = sys.stdin.readline()
    if line == "":
        sel.unregister(sys.stdin)
        return
    action = parse_action(line)
    if action is None:
        print("Commands: fold | bet <amount>")
        print_prompt()
        return
    queue_action(sock, action)
    print_prompt()


try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setblocking(False)
        s.connect_ex((HOST, PORT))
        print(f"Connected to server at {HOST}:{PORT}")

        sel.register(s, selectors.EVENT_READ, data="sock")
        sel.register(sys.stdin, selectors.EVENT_READ, data="stdin")
        print_prompt()

        while True:
            for key, mask in sel.select():
                if key.data == "sock":
                    handle_socket(s, mask)
                else:
                    handle_stdin(s)
except KeyboardInterrupt:
    print()
    print("Quitting...")
finally:
    sys.exit()
