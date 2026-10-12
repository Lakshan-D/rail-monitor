#!/usr/bin/env python3
"""Ground station for the Rail Monitor.

Reads framed telemetry from the Arduino, checks the CRC, prints a live line,
logs a CSV and can plot the result.

    python3 ground/monitor.py --port /dev/ttyACM0           # real hardware (Linux)
    python3 ground/monitor.py --port COM3                    # real hardware (Windows)
    python3 ground/monitor.py --demo --plot                  # no hardware, synthetic data
    python3 ground/monitor.py --replay capture.bin --plot    # replay a saved capture

Hardware mode needs pyserial:  pip install pyserial
While running, press x + Enter to send 'x' (EEPROM bit-flip test) or h + Enter
for 'h' (hang the board to test the watchdog).
"""

import argparse
import csv
import math
import os
import random
import sys
import threading
import time

import frame

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CSV = os.path.join(HERE, "..", "results", "telemetry.csv")


def demo_stream(n=400):
    """Synthetic 5 V rail with a dip below the UV limit around frames 150..190."""
    rng = random.Random(3)
    uv_active = 0
    data = bytearray()
    for i in range(n):
        mv = 5000 + int(20 * math.sin(i / 10)) + rng.randint(-8, 8)
        if 150 <= i < 190:
            mv -= 900
        ma = 1500 + int(150 * math.sin(i / 25))
        flags = frame.FLAG_UV if 155 <= i < 195 else 0     # 5 sample debounce lag
        data += frame.encode(i, frame.pack_telem(i * 100, mv, ma, flags))
    return bytes(data)


def print_row(t, last_ts):
    note = ""
    if last_ts is not None and t.ts_ms < last_ts:
        note = "  <-- board RESET (timestamp went backwards)"
    print(f"seq {t.seq:3d}  t={t.ts_ms / 1000:8.1f}s  {t.vrail_mv / 1000:6.3f} V  "
          f"{t.irail_ma:5d} mA  [{t.flag_text()}]{note}")


def handle(parser, chunk, rows, writer, state):
    for seq, payload in parser.feed(chunk):
        if len(payload) != frame.TELEM_LEN:
            continue
        t = frame.unpack_telem(seq, payload)
        print_row(t, state["last_ts"])
        state["last_ts"] = t.ts_ms
        rows.append(t)
        writer.writerow([t.seq, t.ts_ms, t.vrail_mv, t.irail_ma, t.flags, t.flag_text()])


def stdin_commands(ser):
    for line in sys.stdin:
        cmd = line.strip()[:1]
        if cmd in ("x", "h"):
            ser.write(cmd.encode())
            print(f"sent command '{cmd}'")


def plot(rows, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t = [r.ts_ms / 1000 for r in rows]
    v = [r.vrail_mv / 1000 for r in rows]
    i = [r.irail_ma for r in rows]
    fault_t = [r.ts_ms / 1000 for r in rows if r.flags & (frame.FLAG_UV | frame.FLAG_OV | frame.FLAG_OC)]

    fig, (a1, a2) = plt.subplots(2, 1, figsize=(8, 5), sharex=True)
    a1.plot(t, v, color="#1f3863")
    a1.set_ylabel("rail (V)")
    if fault_t:
        a1.axvspan(min(fault_t), max(fault_t), color="#c0392b", alpha=0.15, label="fault flag set")
        a1.legend(frameon=False, loc="lower left")
    a2.plot(t, i, color="#595959")
    a2.set_ylabel("current (mA)")
    a2.set_xlabel("time (s)")
    for ax in (a1, a2):
        ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    print(f"wrote {os.path.relpath(path)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port, e.g. /dev/ttyACM0 or COM3")
    src.add_argument("--demo", action="store_true", help="synthetic data, no hardware")
    src.add_argument("--replay", help="binary capture file to replay")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--csv", default=DEFAULT_CSV)
    ap.add_argument("--plot", action="store_true", help="save results/telemetry.png at the end")
    ap.add_argument("--save-raw", help="also save the raw bytes to this file")
    args = ap.parse_args()

    os.makedirs(os.path.dirname(os.path.abspath(args.csv)), exist_ok=True)
    parser = frame.FrameParser()
    rows, state = [], {"last_ts": None}

    with open(args.csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["seq", "ts_ms", "vrail_mv", "irail_ma", "flags", "flag_text"])
        raw_out = open(args.save_raw, "wb") if args.save_raw else None

        try:
            if args.demo or args.replay:
                data = demo_stream() if args.demo else open(args.replay, "rb").read()
                for k in range(0, len(data), 64):      # feed in awkward chunks on purpose
                    handle(parser, data[k:k + 64], rows, writer, state)
            else:
                try:
                    import serial
                except ImportError:
                    sys.exit("pyserial missing. Run: pip install pyserial")
                ser = serial.Serial(args.port, args.baud, timeout=0.2)
                time.sleep(2.0)                          # Uno resets when the port opens
                threading.Thread(target=stdin_commands, args=(ser,), daemon=True).start()
                print("listening, Ctrl+C to stop")
                while True:
                    chunk = ser.read(256)
                    if chunk:
                        if raw_out:
                            raw_out.write(chunk)
                        handle(parser, chunk, rows, writer, state)
        except KeyboardInterrupt:
            pass
        finally:
            if raw_out:
                raw_out.close()

    print(f"\n{parser.good} good frames, {parser.crc_errors} CRC errors, "
          f"{parser.resyncs} bytes skipped while resyncing")
    print(f"wrote {os.path.relpath(args.csv)}")
    if args.plot and rows:
        plot(rows, os.path.join(os.path.dirname(os.path.abspath(args.csv)), "telemetry.png"))


if __name__ == "__main__":
    main()
