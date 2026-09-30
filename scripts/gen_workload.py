
import argparse
import os
import random

from common import QUANTUM

WORDS = ("the quick brown fox jumps over the lazy dog while packets "
         "traverse the network stack and the scheduler drains its queue "
         "in careful deterministic order").split()

def _fit_to_length(words: list, content_len: int) -> str:
    line = " ".join(words)
    if len(line) < content_len:
        line = line + " " * (content_len - len(line))
    else:
        line = line[:content_len]
    return line

def make_short_line(rng: random.Random, target_len: int) -> str:
    words = []
    length = 0
    while length < target_len - 1:
        w = rng.choice(WORDS)
        words.append(w)
        length += len(w) + 1
    return _fit_to_length(words, target_len - 1) + "\n"

def make_long_line(rng: random.Random, target_len: int) -> str:
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

    total_short_bytes = target_bytes - num_long_lines * long_line_len
    gap_bytes = total_short_bytes // (num_long_lines + 1)

    with open(path, "w", newline="\n") as f:
        written = 0
        short_written = 0

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

    write_large_with_long_lines(
        os.path.join(args.out_dir, "large.txt"), 150 * 1024, seed=3,
        num_long_lines=6, long_line_len=20000,
    )

    for name in ("small.txt", "medium.txt", "large.txt"):
        p = os.path.join(args.out_dir, name)
        print(f"{p}: {os.path.getsize(p)} bytes")

if __name__ == "__main__":
    main()
