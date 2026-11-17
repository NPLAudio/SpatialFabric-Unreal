"""Offline shared-fixture integrity/generation tests; native behavior is tested in Unreal."""
import json
import struct
import unittest

import generate_osc_ack_tests as generator


class OscAcknowledgementFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads(generator.DEFAULT_FIXTURE.read_text(encoding="utf-8"))

    def test_native_cases_are_current(self):
        self.assertEqual(generator.OUTPUT.read_text(encoding="utf-8"), generator.render(self.fixture))

    def test_every_golden_packet_is_utf8_ssis(self):
        def string(value):
            raw = value.encode("utf-8") + b"\0"
            return raw + b"\0" * (-len(raw) % 4)
        self.assertEqual(self.fixture["ack_tags"], ",ssis")
        for row in self.fixture["vectors"]:
            with self.subTest(row["name"]):
                packet = (string(row["address"]) + string(",ssis") + string(row["command"])
                          + string(row["result"]) + struct.pack(">i", row["playing"])
                          + string(row["detail"]))
                self.assertEqual(packet.hex(), row["hex"])

    def test_all_decisions_and_packets_are_generated(self):
        output = generator.render(self.fixture)
        self.assertEqual(output.count("CheckDecision("), len(self.fixture["decisions"]))
        self.assertEqual(output.count("CheckPacket("), len(self.fixture["vectors"]))
        for row in self.fixture["vectors"]:
            self.assertIn(generator.text(row["detail"]), output)
            self.assertIn(generator.text(row["hex"]), output)

    def test_mixed_argument_order_is_preserved(self):
        output = generator.render({"decisions": [{"kind": "playback", "op": "play",
            "args": [["i", 0], ["f", 0.9]], "result": "received", "acts": True}], "vectors": []})
        self.assertLess(output.index("AddInt32"), output.index("AddFloat"))


if __name__ == "__main__":
    unittest.main()
