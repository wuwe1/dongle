#!/usr/bin/env python3
"""Reads candump logs (the dongle prints them; so does candump -L) and finds what changed.

  canlog.py summary LOG                    per ID: frames, period, which bytes move
  canlog.py marks LOG                      the "# mark" lines and when they were set
  canlog.py diff LOG --base A --action B   byte values seen only during the action
  canlog.py diff BASE_LOG ACTION_LOG       the same, across two logs

A and B are a mark's text (its frames run to the next mark) or seconds from the log's first frame,
"10-15". The usual capture: mark idle, wait 10 s; mark lock, press the lock button; mark done.
Then `diff LOG --base idle --action lock` lists the bytes that took a value they never had at idle:
the lock command is among them. A rolling counter or checksum shows up too, but only for values the
idle window happened not to cover, so a longer idle window gives a shorter list.

Lines that are not frames or marks (ESP-IDF log lines, "# stats") are skipped.
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass

FRAME = re.compile(r"^\((\d+)\.(\d{6})\)\s+\S+\s+([0-9A-Fa-f]{3}|[0-9A-Fa-f]{8})#(R|[0-9A-Fa-f]*)\s*$")
MARK = re.compile(r"^# mark (\d+)\.(\d{6}) ?(.*?)\s*$")


@dataclass(frozen=True)
class Frame:
    t: float
    id: int
    extended: bool
    data: bytes | None  # None for a remote frame


@dataclass(frozen=True)
class Mark:
    t: float
    text: str


def parse(lines):
    frames, marks = [], []
    for line in lines:
        if m := FRAME.match(line):
            sec, us, ident, payload = m.groups()
            data = None if payload == "R" else bytes.fromhex(payload) if len(payload) % 2 == 0 else None
            if payload != "R" and data is None:
                continue  # odd number of hex digits: a cut-off line
            frames.append(Frame(int(sec) + int(us) / 1e6, int(ident, 16), len(ident) == 8, data))
        elif m := MARK.match(line):
            sec, us, text = m.groups()
            marks.append(Mark(int(sec) + int(us) / 1e6, text))
    return frames, marks


def load(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return parse(f)


def fmt_id(ident, extended=False):
    return f"{ident:08X}" if extended else f"{ident:03X}"


def window(frames, marks, spec):
    """Frames in a mark's span (to the next mark), or in "start-end" seconds from the first frame."""
    if m := re.fullmatch(r"(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)", spec):
        t0 = frames[0].t if frames else 0
        lo, hi = t0 + float(m[1]), t0 + float(m[2])
    else:
        found = [i for i, k in enumerate(marks) if k.text == spec]
        if not found:
            names = ", ".join(sorted({k.text for k in marks})) or "none"
            sys.exit(f"no mark '{spec}' (marks: {names})")
        i = found[0]
        lo = marks[i].t
        hi = marks[i + 1].t if i + 1 < len(marks) else float("inf")
    return [f for f in frames if lo <= f.t < hi]


def by_id(frames):
    out = defaultdict(list)
    for f in frames:
        out[(f.id, f.extended)].append(f)
    return out


def summary(frames):
    rows = []
    for (ident, ext), fs in sorted(by_id(frames).items()):
        gaps = [b.t - a.t for a, b in zip(fs, fs[1:])]
        period = f"{statistics.median(gaps) * 1000:.0f}" if gaps else "-"
        payloads = [f.data for f in fs if f.data is not None]
        width = max((len(p) for p in payloads), default=0)
        moving = "".join(
            "x" if len({p[i] for p in payloads if len(p) > i}) > 1 else "." for i in range(width)
        )
        rows.append((fmt_id(ident, ext), len(fs), period, width, len(set(payloads)), moving or "R"))
    lines = [f"{'id':>8}  {'frames':>7}  {'ms':>6}  len  {'distinct':>8}  bytes (x = moves)"]
    lines += [f"{i:>8}  {n:>7}  {p:>6}  {w:>3}  {d:>8}  {mv}" for i, n, p, w, d, mv in rows]
    return "\n".join(lines)


@dataclass
class Change:
    id: str
    byte: int | None  # None: the whole ID is new
    base: list[int]
    new: list[int]

    @property
    def was_stable(self):
        return len(self.base) == 1


def diff(base, action):
    """Bytes whose value during the action never appeared at base. Strongest first: an ID the base
    never had, then a byte that sat still at base and then moved, then the rest."""
    b, a = by_id(base), by_id(action)
    changes = []
    for key in sorted(a):
        ident = fmt_id(*key)
        if key not in b:
            changes.append(Change(ident, None, [], []))
            continue
        base_vals, act_vals = defaultdict(set), defaultdict(set)
        for f in b[key]:
            for i, v in enumerate(f.data or b""):
                base_vals[i].add(v)
        for f in a[key]:
            for i, v in enumerate(f.data or b""):
                act_vals[i].add(v)
        for i in sorted(act_vals):
            new = act_vals[i] - base_vals[i]
            if new:
                changes.append(Change(ident, i, sorted(base_vals[i]), sorted(new)))
    changes.sort(key=lambda c: (c.byte is not None, not c.was_stable, c.id, c.byte or 0))
    return changes


def show_values(vals, limit=8):
    s = " ".join(f"{v:02X}" for v in vals[:limit])
    return s + (f" …(+{len(vals) - limit})" if len(vals) > limit else "")


def report(changes, base, action):
    lines = [f"base {len(base)} frames, action {len(action)} frames, {len(changes)} changes"]
    for c in changes:
        if c.byte is None:
            lines.append(f"  {c.id:>8}         new ID: only during the action")
        else:
            tag = "was stable" if c.was_stable else "          "
            lines.append(
                f"  {c.id:>8}  byte {c.byte}  {tag}  base {show_values(c.base)}  ->  new {show_values(c.new)}"
            )
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("summary")
    s.add_argument("log")
    k = sub.add_parser("marks")
    k.add_argument("log")
    d = sub.add_parser("diff")
    d.add_argument("logs", nargs="+", help="one log with --base/--action, or BASE_LOG ACTION_LOG")
    d.add_argument("--base")
    d.add_argument("--action")
    args = ap.parse_args(argv)

    if args.cmd == "summary":
        print(summary(load(args.log)[0]))
    elif args.cmd == "marks":
        frames, marks = load(args.log)
        t0 = frames[0].t if frames else 0
        for m in marks:
            print(f"{m.t - t0:9.3f}s  {m.text}")
    elif len(args.logs) == 2:
        base, action = load(args.logs[0])[0], load(args.logs[1])[0]
        print(report(diff(base, action), base, action))
    elif len(args.logs) == 1 and args.base and args.action:
        frames, marks = load(args.logs[0])
        base, action = window(frames, marks, args.base), window(frames, marks, args.action)
        print(report(diff(base, action), base, action))
    else:
        ap.error("diff takes BASE_LOG ACTION_LOG, or one LOG with --base and --action")


if __name__ == "__main__":
    main()
