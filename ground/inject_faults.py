#!/usr/bin/env python3
"""Fault injection on the serial link, host simulation.

Builds a stream of N telemetry frames, flips random bits at a chosen bit error
rate (BER), and feeds the damaged stream to two receivers:

  naive  : finds the sync byte, trusts LEN, ignores the CRC
  crc    : the real parser (CRC-16 check, resync on failure)

For each BER it counts how many frames each receiver accepted and, of those,
how many carried a wrong value. This is the link-level version of the
"70/200 bad without TMR vs 0/200 with TMR" result in the buck converter project.

Usage:
    python3 ground/inject_faults.py                 # sweep, writes results/
    python3 ground/inject_faults.py --frames 5000 --seed 7
"""

import argparse
import csv
import math
import os
import random

import frame

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "..", "results")

BERS = [0.0, 1e-4, 1e-3, 3e-3, 1e-2, 3e-2]


def build_stream(n_frames, rng):
    """Frames with a slowly moving 5 V rail. Returns (stream, set_of_sent_payloads)."""
    stream = bytearray()
    sent = set()
    for i in range(n_frames):
        mv = int(5000 + 40 * math.sin(i / 20.0) + rng.randint(-5, 5))
        ma = int(1500 + 100 * math.sin(i / 35.0))
        payload = frame.pack_telem(i * 100, mv, ma, 0)   # ts unique per frame
        sent.add(payload)
        stream += frame.encode(i, payload)
    return bytes(stream), sent


def corrupt(stream, ber, rng):
    """Flip each bit independently with probability ber. Returns (bytes, bits_flipped)."""
    if ber <= 0:
        return stream, 0
    out = bytearray(stream)
    flipped = 0
    n_bits = len(out) * 8
    expected = ber * n_bits
    # draw the number of flips from a normal approximation of the binomial, then
    # place them uniformly; fast and accurate enough for BER << 1
    k = max(0, int(round(rng.gauss(expected, math.sqrt(expected * (1 - ber))))))
    for pos in rng.sample(range(n_bits), min(k, n_bits)):
        out[pos // 8] ^= 1 << (pos % 8)
        flipped += 1
    return bytes(out), flipped


def run_receiver(stream, sent, check_crc):
    parser = frame.FrameParser(check_crc=check_crc)
    accepted = parser.feed(stream)
    wrong = sum(1 for _, payload in accepted if payload not in sent)
    return len(accepted), wrong, parser.crc_errors


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--frames", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-plot", action="store_true")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    os.makedirs(RESULTS, exist_ok=True)
    clean, sent = build_stream(args.frames, rng)

    rows = []
    print(f"{args.frames} frames, {len(clean)} bytes, seed {args.seed}")
    print(f"{'BER':>8} {'flips':>6} | {'naive ok':>8} {'naive WRONG':>11} | "
          f"{'crc ok':>7} {'crc WRONG':>9} {'crc rejected':>12} {'lost':>5}")
    for ber in BERS:
        damaged, flips = corrupt(clean, ber, rng)
        n_ok, n_wrong, _ = run_receiver(damaged, sent, check_crc=False)
        c_ok, c_wrong, c_rej = run_receiver(damaged, sent, check_crc=True)
        lost = args.frames - c_ok
        rows.append(dict(ber=ber, bit_flips=flips, frames=args.frames,
                         naive_accepted=n_ok, naive_wrong=n_wrong,
                         crc_accepted=c_ok, crc_wrong=c_wrong,
                         crc_rejected=c_rej, crc_lost=lost))
        print(f"{ber:8.0e} {flips:6d} | {n_ok:8d} {n_wrong:11d} | "
              f"{c_ok:7d} {c_wrong:9d} {c_rej:12d} {lost:5d}")

    path = os.path.join(RESULTS, "crc_test.csv")
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {os.path.relpath(path)}")

    if not args.no_plot:
        plot(rows, args.frames)


def plot(rows, n_frames):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    xs = [i for i in range(len(rows))]
    labels = [f"{r['ber']:.0e}" if r["ber"] else "0" for r in rows]
    fig, ax = plt.subplots(figsize=(7, 4))
    w = 0.38
    ax.bar([x - w / 2 for x in xs], [r["naive_wrong"] for r in rows], w,
           label="no CRC: wrong values accepted", color="#c0392b")
    ax.bar([x + w / 2 for x in xs], [r["crc_wrong"] for r in rows], w,
           label="CRC-16: wrong values accepted", color="#1f3863")
    ax.set_xticks(xs)
    ax.set_xticklabels(labels)
    ax.set_xlabel("bit error rate on the serial link")
    ax.set_ylabel(f"frames with a wrong value (of {n_frames})")
    ax.set_title("Link fault injection (host simulation)")
    ax.legend(frameon=False)
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    path = os.path.join(RESULTS, "crc_test.png")
    fig.savefig(path, dpi=150)
    print(f"wrote {os.path.relpath(path)}")


if __name__ == "__main__":
    main()
