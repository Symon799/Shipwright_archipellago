"""A bounded localhost AP fixture server exercising the real apclientpp binary.

Only the messages needed by these scenarios are implemented; this is not an
Archipelago server replacement. No external server, credentials or seed are used.
"""

import base64
import hashlib
import http.server
import json
import socket
import struct
import subprocess
import sys
import tempfile
import threading

VERSION = {"major": 0, "minor": 6, "build": 8, "class": "Version"}
GAME = "Ship of Harkinian"
ITEMS = [{"item": 10001, "location": 60001, "player": 1, "flags": 1},
         {"item": 10002, "location": 60002, "player": 1, "flags": 1}]
RECEIVED_ITEMS = [ITEMS[0], {"item": 10002, "location": 70001, "player": 2, "flags": 1}]
PACKAGE = {"cmd": "DataPackage", "data": {"games": {
    GAME: {"item_name_to_id": {"Kokiri Sword": 10001, "Light Arrows": 10002},
           "location_name_to_id": {"Chest": 60001, "Shop": 60002}, "version": 1, "checksum": "fixture"},
    "Archipelago": {"item_name_to_id": {}, "location_name_to_id": {}, "version": 1, "checksum": "fixture"},
    "Fixture Foreign": {"item_name_to_id": {}, "location_name_to_id": {"Foreign Chest": 70001},
                        "version": 1, "checksum": "fixture"}}}}


class FixtureServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, seed):
        super().__init__(("127.0.0.1", 0), Handler)
        self.seed = seed
        self.commands = set()
        self.failures = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def send_packet(self, *messages):
        payload = json.dumps(messages).encode()
        header = b"\x81" + (bytes([len(payload)]) if len(payload) < 126 else b"\x7e" + struct.pack("!H", len(payload)))
        self.wfile.write(header + payload)
        self.wfile.flush()

    def exact(self, size):
        data = self.rfile.read(size)
        if len(data) != size:
            raise EOFError()
        return data

    def read_frame(self):
        first, second = self.exact(2)
        length = second & 127
        if length == 126:
            length = struct.unpack("!H", self.exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.exact(8))[0]
        if length > 1024 * 1024 or not second & 128 or first & 0x70:
            raise ValueError("Unsupported fixture WebSocket frame")
        mask = self.exact(4)
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(self.exact(length)))
        return first & 15, payload

    def do_GET(self):
        self.close_connection = True
        try:
            self.connection.settimeout(15)
            key = self.headers["Sec-WebSocket-Key"]
            accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept.decode())
            self.end_headers()
            self.send_packet({"cmd": "RoomInfo", "time": 0.0, "version": VERSION, "generator_version": VERSION,
                              "seed_name": self.server.seed, "games": [GAME], "password": False,
                              "datapackage_versions": {GAME: 1, "Archipelago": 1}, "permissions": {}})
            while True:
                opcode, payload = self.read_frame()
                if opcode == 8:
                    return
                if opcode != 1:
                    continue
                for command in json.loads(payload):
                    name = command["cmd"]
                    self.server.commands.add(name)
                    if name == "GetDataPackage":
                        self.send_packet(PACKAGE)
                    elif name == "Connect":
                        assert command["name"] == "Validation" and command["game"] == GAME
                        self.send_packet(PACKAGE, {"cmd": "Connected", "team": 0, "slot": 1,
                            "players": [{"team": 0, "slot": 1, "alias": "Validation", "name": "Validation"},
                                        {"team": 0, "slot": 2, "alias": "Foreign", "name": "Foreign"}],
                            "missing_locations": [60001, 60002], "checked_locations": [],
                            "slot_info": {"1": {"name": "Validation", "game": GAME, "type": 1, "group_members": []},
                                          "2": {"name": "Foreign", "game": "Fixture Foreign", "type": 1, "group_members": []}},
                            "slot_data": {"apworld_version": "1.4.0"}})
                    elif name == "LocationScouts":
                        assert set(command["locations"]) == {60001, 60002}
                        self.send_packet({"cmd": "LocationInfo", "locations": ITEMS})
                    elif name == "LocationChecks":
                        assert command["locations"] == [60001]
                        self.send_packet({"cmd": "RoomUpdate", "checked_locations": [60001], "hint_points": 0})
                    elif name == "Sync":
                        self.send_packet({"cmd": "ReceivedItems", "index": 0, "items": RECEIVED_ITEMS},
                                         {"cmd": "ReceivedItems", "index": 0, "items": RECEIVED_ITEMS})
        except (EOFError, ConnectionResetError, BrokenPipeError):
            pass
        except Exception as error:
            self.server.failures.append(str(error))


def main():
    servers = [FixtureServer(seed) for seed in ("validation-seed-a", "validation-seed-a", "validation-seed-b")]
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
    for thread in threads:
        thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="soh-ap-transport-") as directory:
            result = subprocess.run([sys.argv[1], *(str(server.server_port) for server in servers)], cwd=directory,
                                    capture_output=True, text=True, timeout=42)
        print(result.stdout)
        print(result.stderr)
        if result.returncode:
            for server in servers:
                print("Server diagnostics:", server.failures, sorted(server.commands))
            return 1
        for server in servers:
            required = {"Connect", "LocationScouts", "LocationChecks", "Sync"}
            if server.failures or not required <= server.commands:
                print("Fixture server failed:", server.failures, "commands:", sorted(server.commands))
                return 1
        print("PASS: actual apclientpp, scouts, item replay, checks, changed port and seed identity")
        return 0
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    sys.exit(main())
