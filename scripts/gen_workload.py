#!/usr/bin/env python3
"""Generates the A27 workload directory deterministically (C4).

Covers both axes the spec asks for in one directory:
  - size: small (~1KB), medium (~30KB), large (~150KB)
  - line length: small/medium use ordinary 60-80 byte lines; large also
    contains a handful of lines longer than QUANTUM, so it doubles as the
    "long-line" file required for the rr/drr split (A27, A29).

QUANTUM here (imported from scripts/common.py) must match the --quantum
value used for the rr/drr experiment runs (A28) - both come from the same
constant so they can't drift apart.
"""
import argparse
import os
import random

from common import QUANTUM

WORDS = ("the quick brown fox jumps over the lazy dog while packets "
         "traverse the network stack and the scheduler drains its queue "
         "in careful deterministic order").split()


def _fit_to_length(words: list, content_len: int) -> str:
    """Joins words with single spaces and pads/truncates to exactly content_len chars."""
    line = " ".join(words)
    if len(line) < content_len:
        line = line + " " * (content_len - len(line))
    else:
        line = line[:content_len]
    return line


def make_short_line(rng: random.Random, target_len: int) -> str:
    """Builds one line of exactly target_len bytes including the trailing newline."""
    words = []
    length = 0
    while length < target_len - 1:  # -1 leaves room for '\n'
        w = rng.choice(WORDS)
        words.append(w)
        length += len(w) + 1  # +1 for the space
    return _fit_to_length(words, target_len - 1) + "\n"


def make_long_line(rng: random.Random, target_len: int) -> str:
    """Builds one line of exactly target_len bytes (well over QUANTUM), including '\\n'."""
    words = []
    length = 0
    while length < target_len - 1:
        w = rng.choice(WORDS)
        words.append(w)
        length += len(w) + 1
    return _fit_to_length(words, target_len - 1) + "\n"


def write_ordinary_file(path: str, target_bytes: int, seed: int) -> None:
    rng = random.Random(seed)
    with open(path, "w", newline="\n") as f:
        written = 0
        while written < target_bytes:
            line_len = rng.randint(60, 79)
            line = make_short_line(rng, line_len)
            f.write(line)
            written += len(line)


def write_large_with_long_lines(path: str, target_bytes: int, seed: int,
                                 num_long_lines: int, long_line_len: int) -> None:
    rng = random.Random(seed)
    # Spread the long lines roughly evenly through the file so a policy
    # transferring it in order hits each one at a different point.
    total_short_bytes = target_bytes - num_long_lines * long_line_len
    gap_bytes = total_short_bytes // (num_long_lines + 1)

    with open(path, "w", newline="\n") as f:
        written = 0
        short_written = 0  # counts ordinary-line bytes only, so the long lines
                           # themselves do not advance the position of the next one
        long_lines_left = num_long_lines
        next_long_at = gap_bytes
        while written < target_bytes:
            if long_lines_left > 0 and short_written >= next_long_at:
                line = make_long_line(rng, long_line_len)
                long_lines_left -= 1
                next_long_at += gap_bytes
            else:
                line_len = rng.randint(60, 79)
                line = make_short_line(rng, line_len)
                short_written += len(line)
            f.write(line)
            written += len(line)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="workload")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    write_ordinary_file(os.path.join(args.out_dir, "small.txt"), 1024, seed=1)
    write_ordinary_file(os.path.join(args.out_dir, "medium.txt"), 30 * 1024, seed=2)
    # large.txt is both the "large" size file and the "long-line" file:
    # ~150KB total, with 6 lines well over QUANTUM (8192B) spread through it.
    write_large_with_long_lines(
        os.path.join(args.out_dir, "large.txt"), 150 * 1024, seed=3,
        num_long_lines=6, long_line_len=20000,
    )

    for name in ("small.txt", "medium.txt", "large.txt"):
        p = os.path.join(args.out_dir, name)
        print(f"{p}: {os.path.getsize(p)} bytes")


if __name__ == "__main__":
    main()
