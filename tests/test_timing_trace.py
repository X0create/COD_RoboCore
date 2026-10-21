"""Synthetic RAM captures: nested IRQ union, READY consumption, wrap and rejection."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import decode_timing_trace as dec


def capture(events, count=None, reason=2):
    raw = bytearray(dec.HEADER_SIZE + dec.CAPACITY * 16)
    count = len(events) if count is None else count
    header = dict.fromkeys(dec.KEYS, 0)
    header.update(magic=dec.MAGIC, version=1, capacity=dec.CAPACITY, clock_hz=1000000,
                  control_task=0x100, rx_task=0x200, state=3, reason=reason, session=42,
                  count=count, overwritten=max(0, count - dec.CAPACITY))
    struct.pack_into("<24I", raw, 0, *(header[k] for k in dec.KEYS))
    start = count - len(events) + 1
    for seq, (time, kind, context, data) in enumerate(events, start):
        struct.pack_into("<4I", raw, dec.HEADER_SIZE + ((seq - 1) % dec.CAPACITY) * 16,
                         seq, time & 0xFFFFFFFF, (context << 16) | (dec.NAMES.index(kind) + 1), data)
    return raw


class TraceTests(unittest.TestCase):
    def test_ready_nested_irq_union_and_no_stale_ready(self):
        raw = capture([(0, "ARM", 0, 1), (10, "SESSION", 0, 42),
                       (20, "TASK_IN", 14, 0x200), (30, "IRQ_IN", 15, 0),
                       (40, "READY", 15, 0x100), (50, "IRQ_IN", 42, 0),
                       (60, "IRQ_OUT", 42, 0), (70, "IRQ_OUT", 15, 0),
                       (80, "TASK_OUT", 14, 0x200), (90, "TASK_IN", 14, 0x100),
                       (100, "CONTROL_ENTER", 0, 1), (110, "CYCLE_END", 0, 1),
                       (120, "CONTROL_ENTER", 0, 2), (150, "FREEZE", 0, 2)])
        report, _ = dec.decode(raw)
        self.assertEqual(report["irq_union_us"], 40)
        first, second = report["control_cycles"]
        self.assertEqual(first["ready_to_first_in_us"], 50)
        self.assertEqual(first["first_in_to_entry_us"], 10)
        self.assertEqual(first["irq_overlap_us"], 30)
        self.assertIsNone(second["ready_to_entry_us"])
        self.assertTrue(second["no_new_ready_in_window"])

    def test_ring_overwrite_and_prefix(self):
        events = [(n, "CYCLE_END", 0, n) for n in range(dec.CAPACITY - 1)]
        events.append((dec.CAPACITY, "FREEZE", 0, 2))
        report, decoded = dec.decode(capture(events, count=dec.CAPACITY + 20))
        self.assertEqual(decoded[0]["sequence"], 21)
        self.assertEqual(report["header"]["overwritten"], 20)
        prefix = capture([(0, "IRQ_OUT", 42, 0), (1, "CONTROL_ENTER", 0, 7),
                          (2, "FREEZE", 0, 2)])
        self.assertIsNone(dec.decode(prefix)[0]["control_cycles"][0]["ready_to_entry_us"])
        prefix = capture([(0, "IRQ_IN", 15, 0), (1, "READY", 15, 0x100),
                          (2, "IRQ_OUT", 15, 0), (3, "TASK_IN", 14, 0x100),
                          (4, "CONTROL_ENTER", 0, 7), (5, "FREEZE", 0, 2)])
        window = dec.decode(prefix)[0]["control_cycles"][0]
        self.assertEqual(window["ready_to_entry_us"], 3)
        self.assertIsNone(window["irq_overlap_us"])
        self.assertFalse(window["irq_window_complete"])

    def test_counter_wrap_and_phase_intervals(self):
        raw = capture([(0xFFFFFFF0, "ARM", 0, 1), (0xFFFFFFF8, "POLL_BEGIN", 0, 0),
                       (2, "FRAME_BEGIN", 0, (0x10 << 16) | 472),
                       (12, "FRAME_END", 0, 1), (22, "POLL_END", 0, 0),
                       (32, "FREEZE", 0, 2)])
        report, _ = dec.decode(raw)
        self.assertEqual(report["span_us"], 48)
        self.assertEqual(report["poll_wall_us"], [30])
        self.assertEqual(report["frames"][0], {"type": 16, "bytes": 472, "accepted": True, "wall_us": 10})

    def test_uncommitted_slot_and_fault_are_rejected(self):
        raw = capture([(0, "ARM", 0, 1), (1, "FREEZE", 0, 2)])
        struct.pack_into("<I", raw, dec.HEADER_SIZE, 0)
        with self.assertRaisesRegex(ValueError, "Uncommitted"):
            dec.decode(raw)
        with self.assertRaisesRegex(ValueError, "fault/session/motion"):
            dec.decode(capture([(0, "FREEZE", 0, 6)], reason=6))
        with self.assertRaisesRegex(ValueError, "complete"):
            dec.decode(b"short")

    def test_irq_mismatch_and_missing_coverage(self):
        raw = capture([(0, "ARM", 0, 1), (1, "IRQ_IN", 42, 0),
                       (2, "IRQ_OUT", 43, 0), (3, "FREEZE", 0, 2)])
        with self.assertRaisesRegex(ValueError, "nesting"):
            dec.decode(raw)
        raw = capture([(0, "ARM", 0, 1), (1, "FREEZE", 0, 2)])
        struct.pack_into("<I", raw, 96, 0x20)
        self.assertEqual(dec.decode(raw)[0]["uncovered_enabled_irqs"], [5])


if __name__ == "__main__":
    unittest.main()
