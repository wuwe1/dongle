#!/usr/bin/env bash
# Host tests: the C line format, then tools/canlog.py reading exactly what the C code wrote.
set -euo pipefail
cd "$(dirname "$0")"
out=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -o "$out/test_frame" test_frame.c ../main/frame.c
"$out/test_frame" > "$out/lines.log"
python3 - "$out/lines.log" <<'PY'
import sys; sys.path.insert(0, "../../tools")
import canlog
frames, _ = canlog.load(sys.argv[1])
got = [(hex(f.id), f.extended, f.data.hex() if f.data is not None else "R") for f in frames]
assert got == [("0x1a0", False, "00ff"), ("0x7", False, ""), ("0x18daf110", True, "00ff123456789abc"),
               ("0x7df", False, "R"), ("0x123", False, "00ff123456789abc")], got
print("canlog reads the firmware's lines: ok", file=sys.stderr)
PY
(cd ../.. && python3 -m unittest discover -s tools 2>&1 | tail -1)
