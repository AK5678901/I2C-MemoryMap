"""Exercise the Bridge's emitted bytes without requiring a Logic 2 installation."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


analyzers = types.ModuleType("saleae.analyzers")
analyzers.HighLevelAnalyzer = type("HighLevelAnalyzer", (), {})
analyzers.StringSetting = lambda **kwargs: ""
with patch.dict(sys.modules, {"saleae": types.ModuleType("saleae"), "saleae.analyzers": analyzers}):
    spec = importlib.util.spec_from_file_location(
        "live_bridge", Path(__file__).resolve().parents[1] / "saleae-live" / "HighLevelAnalyzer.py"
    )
    bridge = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bridge)


class BridgeTest(unittest.TestCase):
    def test_independent_senders_and_utf8_envelopes(self):
        with patch.object(bridge.socket, "socket") as socket_factory:
            bridge.Hla.bus_name = ""
            first = bridge.Hla()
            bridge.Hla.bus_name = "I2C [1],日本語"
            second = bridge.Hla()
            timestamp = "2026-09-22T08:20:18.000000000+00:00"
            for sender in (first, second):
                sender.decode(types.SimpleNamespace(type="address", start_time=timestamp,
                                                   data={"address": bytes([80]), "read": False}))
                sender.decode(types.SimpleNamespace(type="data", start_time=timestamp,
                                                   data={"data": bytes([66])}))
                sender.decode(types.SimpleNamespace(type="stop", start_time=timestamp, data={}))
            calls = socket_factory.return_value.sendto.call_args_list
            self.assertEqual(len(calls), 8)
            for offset, name in ((0, "I2C"), (4, "I2C [1],日本語")):
                encoded = name.encode("utf-8")
                prefix = f"B,{len(encoded)},".encode("ascii") + encoded + b","
                expected = [b"R", f"Q,0,A,{timestamp},80,0".encode("ascii"),
                            f"Q,1,D,{timestamp},66".encode("ascii"), b"Q,2,S"]
                for index, payload in enumerate(expected):
                    self.assertEqual(calls[offset + index].args,
                                     (prefix + payload, ("127.0.0.1", 48151)))

    def test_invalid_names(self):
        for name in (" " * 3, "a" * 1025, "あ" * 342):
            bridge.Hla.bus_name = name
            with self.assertRaises(ValueError):
                bridge.Hla()


if __name__ == "__main__":
    unittest.main()
