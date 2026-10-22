#!/usr/bin/env python3
"""Read a frozen MBT1 RAM dump; no debugger, USB or CAN access.

python3 tools/decode_timing_trace.py capture.bin --csv events.csv --json report.json
Times describe the instrumented firmware. A full DWT wrap between adjacent events
cannot be excluded from raw counts alone: capture without breakpoints or clock changes.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import struct

MAGIC, CAPACITY, HEADER_SIZE = 0x3154424D, 2048, 160
KEYS = ("magic version capacity clock_hz control_task rx_task detect_task request threshold_us "
        "state reason session capture_id count overwritten trigger_sequence trigger_cycle "
        "tail_remaining control_cycle last_start_us last_period_us armed_cycles frozen_cycles "
        "captured_threshold_us").split()
NAMES = ("ARM READY TASK_IN TASK_OUT CONTROL_ENTER SAMPLE SESSION SUBMIT_BEGIN SUBMIT_END "
         "RECORD_BEGIN RECORD_END RX_BEGIN RX_END DISPATCH_BEGIN DISPATCH_END CAN_COUNT "
         "UART_COUNT USB_COUNT POLL_BEGIN POLL_END FRAME_BEGIN FRAME_END IRQ_IN IRQ_OUT "
         "TRIGGER FREEZE CYCLE_END LOG_PEEK_BEGIN LOG_PEEK_END ENCODE_BEGIN ENCODE_END "
         "CRC_BEGIN CRC_END").split()
PHASES = ("LOG_PEEK", "ENCODE", "CRC")


def union_length(intervals, start, end):
    spans = sorted((max(a, start), min(b, end)) for a, b in intervals if a < end and b > start)
    total, previous = 0, start
    for a, b in spans:
        total += max(0, b - max(previous, a))
        previous = max(previous, b)
    return total


def decode_phases(events, irq_intervals, anchor_time, missing_irqs, scale):
    """Pair stages independently: CRC is nested in ENCODE, so do not add their times."""
    opened, prefix = {}, []
    spans = {phase: [] for phase in PHASES}
    seen = set()
    for event in events:
        kind = event["type"]
        phase, _, edge = kind.rpartition("_")
        if phase not in spans or edge not in ("BEGIN", "END"):
            continue
        if edge == "BEGIN":
            if phase in opened:
                raise ValueError(f"Overlapping {phase} calls")
            opened[phase] = event
        elif phase in opened:
            begin = opened.pop(phase)
            a, b = begin["cycles"], event["cycles"]
            complete = anchor_time is not None and a >= anchor_time
            irq = union_length(irq_intervals, a, b) * scale if complete else None
            spans[phase].append({
                "begin_sequence": begin["sequence"], "end_sequence": event["sequence"],
                "begin_us": a * scale, "end_us": b * scale,
                "begin_data": begin["data"], "end_data": event["data"],
                "wall_us": (b - a) * scale, "irq_overlap_us": irq,
                "non_irq_us": (b - a) * scale - irq if complete and not missing_irqs else None,
                "irq_window_complete": complete and not missing_irqs})
        elif phase not in seen:
            prefix.append(phase)  # Ring may begin after the corresponding BEGIN was overwritten.
        else:
            raise ValueError(f"{phase}_END without BEGIN")
        seen.add(phase)
    return spans, prefix, sorted(opened)


def decode(raw):
    if len(raw) != HEADER_SIZE + CAPACITY * 16:
        raise ValueError("Expected the complete 32928-byte rm_trace object")
    header = dict(zip(KEYS, struct.unpack_from("<24I", raw)))
    if (header["magic"], header["version"], header["capacity"]) != (MAGIC, 1, CAPACITY):
        raise ValueError("Unsupported MBT1 header")
    if header["state"] != 3 or header["reason"] not in (1, 2):
        raise ValueError("Capture must be frozen by period or manual request; reject fault/session/motion captures")
    if header["clock_hz"] == 0 or header["count"] == 0:
        raise ValueError("Empty capture or invalid clock")
    if header["overwritten"] != max(0, header["count"] - CAPACITY):
        raise ValueError("Inconsistent overwrite count")
    enabled = struct.unpack_from("<8I", raw, 96)
    covered = struct.unpack_from("<8I", raw, 128)
    header.update(enabled_irqs=list(enabled), covered_irqs=list(covered))
    missing_irqs = [32 * i + bit for i, (a, b) in enumerate(zip(enabled, covered))
                    for bit in range(32) if (a & ~b) & (1 << bit)]
    events, previous, ticks = [], None, 0
    first = max(1, header["count"] - CAPACITY + 1)
    if header["reason"] == 1 and not first <= header["trigger_sequence"] <= header["count"]:
        raise ValueError("Trigger was overwritten; capture cannot explain its cause")
    for sequence in range(first, header["count"] + 1):
        entry = struct.unpack_from("<4I", raw, HEADER_SIZE + ((sequence - 1) % CAPACITY) * 16)
        seq, clock, packed, data = entry
        if seq != sequence:
            raise ValueError(f"Uncommitted/overwritten slot: expected {sequence}, found {seq}")
        kind, context = packed & 0xFFFF, packed >> 16
        if not 1 <= kind <= len(NAMES):
            raise ValueError(f"Unknown event type {kind}")
        if previous is not None:
            ticks += (clock - previous) & 0xFFFFFFFF
        previous = clock
        events.append({"sequence": seq, "cycles": ticks, "type": NAMES[kind - 1],
                       "context": context, "data": data})
    if events[-1]["type"] != "FREEZE" or events[-1]["data"] != header["reason"]:
        raise ValueError("Missing final FREEZE commit")
    scale = 1e6 / header["clock_hz"]
    irq_stack, irq_intervals, anchored = [], [], False
    anchor_time = None
    ready, first_in, controls, polls, frames = None, None, [], [], []
    poll, frame = None, None
    for event in events:
        kind, time, context = event["type"], event["cycles"], event["context"]
        if context == 0:
            if irq_stack and anchored:
                raise ValueError("Thread event inside an unclosed IRQ")
            irq_stack.clear()
            if not anchored:
                anchor_time = time
            anchored = True
        if kind == "IRQ_IN":
            if context < 15:
                raise ValueError("Invalid instrumented IRQ context")
            irq_stack.append((context, time))
        elif kind == "IRQ_OUT":
            if not irq_stack:
                if anchored:
                    raise ValueError("IRQ_OUT without IRQ_IN")
            else:
                irq, begin = irq_stack.pop()
                if irq != context:
                    raise ValueError("IRQ nesting mismatch")
                if anchored:
                    irq_intervals.append((begin, time))
        elif kind == "READY":
            if event["data"] != header["control_task"]:
                raise ValueError("READY does not identify control")
            ready, first_in = time, None
        elif kind == "TASK_IN" and event["data"] == header["control_task"]:
            if ready is not None and first_in is None:
                first_in = time
        elif kind == "CONTROL_ENTER":
            controls.append({"cycle": event["data"], "entry": time, "ready": ready, "first_in": first_in})
            ready, first_in = None, None  # Never carry an old READY into a catch-up cycle.
        elif kind == "POLL_BEGIN":
            if poll is not None:
                raise ValueError("Overlapping POLL calls")
            poll = time
        elif kind == "POLL_END" and poll is not None:
            polls.append((poll, time))
            poll = None
        elif kind == "FRAME_BEGIN":
            if frame is not None:
                raise ValueError("Overlapping FRAME calls")
            frame = (time, event["data"] >> 16, event["data"] & 0xFFFF)
        elif kind == "FRAME_END" and frame is not None:
            begin, ftype, length = frame
            frames.append({"type": ftype, "bytes": length, "accepted": bool(event["data"]),
                           "wall_us": (time - begin) * scale})
            frame = None
    if irq_stack:
        raise ValueError("Capture ends inside an IRQ")
    waits = []
    for item in controls:
        r, s, a = item["ready"], item["first_in"], item["entry"]
        irq_complete = r is not None and anchor_time is not None and r >= anchor_time
        wait = {"cycle": item["cycle"], "entry_us": a * scale,
                "ready_to_first_in_us": (s - r) * scale if r is not None and s is not None else None,
                "first_in_to_entry_us": (a - s) * scale if s is not None else None,
                "ready_to_entry_us": (a - r) * scale if r is not None else None,
                "irq_overlap_us": union_length(irq_intervals, r, a) * scale if irq_complete else None,
                "irq_window_complete": irq_complete and not missing_irqs,
                "no_new_ready_in_window": r is None}
        waits.append(wait)
    phases, phase_prefix, phase_tail = decode_phases(
        events, irq_intervals, anchor_time, missing_irqs, scale)
    report = {"sha256": hashlib.sha256(raw).hexdigest(), "header": header,
              "retained_events": len(events), "span_us": ticks * scale,
              "event_counts": dict(Counter(e["type"] for e in events)),
              "uncovered_enabled_irqs": missing_irqs,
              "irq_union_us": union_length(irq_intervals, 0, ticks) * scale,
              "irq_anchor_us": anchor_time * scale if anchor_time is not None else None,
              "control_cycles": waits,
              "poll_wall_us": [(b - a) * scale for a, b in polls], "frames": frames,
              "partial_poll_at_end": poll is not None, "partial_frame_at_end": frame is not None,
              "phases": phases, "partial_phase_prefix": phase_prefix,
              "partial_phase_at_end": phase_tail,
              "limits": ["Assumes no whole DWT wrap between adjacent events, reset, clock change or breakpoint",
                         "Prefix may start inside a task/IRQ; missing READY is not a zero delay",
                         "IRQ union covers instrumented entries; scheduler/entry/probe overhead is not subtracted",
                         "POLL/FRAME are wall intervals, not pure CPU or CRC time",
                         "Phase non_irq_us still includes probe/scheduler/other-task time; CRC is inside ENCODE"]}
    return report, events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    for out in (args.csv, args.json):
        if out and out.resolve() == args.dump.resolve():
            parser.error("Output cannot overwrite the original dump")
    raw = args.dump.read_bytes()
    report, events = decode(raw)
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(("sequence", "time_us", "type", "context", "data"))
            for event in events:
                writer.writerow((event["sequence"], event["cycles"] * 1e6 / report["header"]["clock_hz"],
                                 event["type"], event["context"], event["data"]))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    assert args.dump.read_bytes() == raw, "Original dump changed"
    print(json.dumps({k: report[k] for k in ("sha256", "retained_events", "span_us", "event_counts",
                                           "uncovered_enabled_irqs", "irq_union_us")}, indent=2))


if __name__ == "__main__":
    main()
