"""Logic 2 I2C high level analyzer forwarding frames to the local viewer."""

import socket
from saleae.analyzers import HighLevelAnalyzer, StringSetting


class Hla(HighLevelAnalyzer):
    bus_name = StringSetting(label="Bus name (match folder name under devices; blank = I2C)")

    def __init__(self):
        name = self.bus_name or "I2C"
        encoded_name = name.encode("utf-8")
        if not name.strip() or len(encoded_name) > 1024:
            raise ValueError("Bus name must contain text and be at most 1024 UTF-8 bytes")
        self.prefix = f"B,{len(encoded_name)},".encode("ascii") + encoded_name + b","
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.reset_sent = False
        self.sequence = 0

    def decode(self, frame):
        kind = frame.type
        if not self.reset_sent:
            self.socket.sendto(self.prefix + b"R", ("127.0.0.1", 48151))
            self.reset_sent = True
        timestamp = str(frame.start_time)
        if kind == "stop":
            message = "S"
        elif kind == "address":
            address = frame.data["address"][0]
            message = f"A,{timestamp},{address},{int(frame.data['read'])}"
        elif kind == "data":
            value = frame.data["data"][0]
            message = f"D,{timestamp},{value}"
        else:
            return None
        packet = f"Q,{self.sequence},{message}"
        self.sequence += 1
        self.socket.sendto(self.prefix + packet.encode("ascii"), ("127.0.0.1", 48151))
        return None
