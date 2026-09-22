"""Logic 2 I2C high level analyzer forwarding frames to the local viewer."""

import socket
from saleae.analyzers import HighLevelAnalyzer


class Hla(HighLevelAnalyzer):
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.reset_sent = False
        self.sequence = 0

    def decode(self, frame):
        kind = frame.type
        if not self.reset_sent:
            self.socket.sendto(b"R", ("127.0.0.1", 48152))
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
        self.socket.sendto(packet.encode("ascii"), ("127.0.0.1", 48152))
        return None
