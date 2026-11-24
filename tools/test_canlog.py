"""python3 -m unittest discover tools   (stdlib only)"""

import io
import unittest
from contextlib import redirect_stdout
from pathlib import Path
import tempfile

import canlog


def simulated_lock_press():
    """20 s of a small bus. Idle 0-10 s, the lock button 10-12 s, idle again after.
      100  every 10 ms: byte 0 a rolling counter 0-15, byte 1 fixed 55
      2B0  every 100 ms: byte 2 is 00, 01 while locking   <- what we are looking for
      3C0  every 50 ms: fixed
      7E8  only while locking: a new message
    """
    lines = []
    for ms in range(0, 20000, 10):
        t = f"({ms // 1000}.{ms % 1000 * 1000:06d}) can0 "
        if ms == 0:
            lines.append("# mark 0.000000 idle")
        if ms == 10000:
            lines.append("# mark 10.000000 lock")
        if ms == 12000:
            lines.append("# mark 12.000000 done")
        locking = 10000 <= ms < 12000
        lines.append(t + f"100#{ms // 10 % 16:02X}55")
        if ms % 100 == 0:
            lines.append(t + f"2B0#1122{1 if locking else 0:02X}33")
            if locking:
                lines.append(t + "7E8#0102")
        if ms % 50 == 0:
            lines.append(t + "3C0#DEADBEEF")
        if ms == 5000:
            lines.append("I (5000) dongle: listening at 500000 bit/s")  # noise, skipped
    return lines


class Parse(unittest.TestCase):
    def test_frames_marks_and_noise(self):
        frames, marks = canlog.parse([
            "(1.000001) can0 1A0#00FF",
            "(1.500000) can0 18DAF110#0102030405060708",
            "(2.000000) can0 7DF#R",
            "(2.100000) can0 123#0",            # odd hex digits: a cut-off line
            "I (123) dongle: something",
            "# stats bitrate=500000",
            "# mark 3.250000 lock",
        ])
        self.assertEqual([(f.id, f.extended, f.data) for f in frames], [
            (0x1A0, False, b"\x00\xff"),
            (0x18DAF110, True, bytes(range(1, 9))),
            (0x7DF, False, None),
        ])
        self.assertAlmostEqual(frames[0].t, 1.000001)
        self.assertEqual([(m.t, m.text) for m in marks], [(3.25, "lock")])


class Diff(unittest.TestCase):
    def setUp(self):
        self.frames, self.marks = canlog.parse(simulated_lock_press())

    def test_finds_the_lock_byte_and_the_new_id_not_the_counter(self):
        base = canlog.window(self.frames, self.marks, "idle")
        action = canlog.window(self.frames, self.marks, "lock")
        changes = canlog.diff(base, action)
        got = [(c.id, c.byte, c.base, c.new) for c in changes]
        self.assertEqual(got, [
            ("7E8", None, [], []),            # new ID first
            ("2B0", 2, [0x00], [0x01]),       # then the byte that was stable
        ])
        self.assertTrue(changes[1].was_stable)

    def test_seconds_window_matches_marks(self):
        a = canlog.window(self.frames, self.marks, "lock")
        b = canlog.window(self.frames, self.marks, "10-12")
        self.assertEqual(a, b)

    def test_a_short_base_lets_the_counter_through(self):
        # 50 ms of idle sees only 5 counter values: the rest look new. Why the base window matters.
        base = canlog.window(self.frames, self.marks, "0-0.05")
        action = canlog.window(self.frames, self.marks, "lock")
        ids = {(c.id, c.byte) for c in canlog.diff(base, action)}
        self.assertIn(("100", 0), ids)

    def test_two_files(self):
        with tempfile.TemporaryDirectory() as d:
            lines = simulated_lock_press()
            cut = lines.index("# mark 10.000000 lock")
            Path(d, "base.log").write_text("\n".join(lines[:cut]))
            Path(d, "action.log").write_text("\n".join(lines[cut:]))
            out = io.StringIO()
            with redirect_stdout(out):
                canlog.main(["diff", str(Path(d, "base.log")), str(Path(d, "action.log"))])
        self.assertIn("2B0  byte 2  was stable  base 00  ->  new 01", out.getvalue())


class Summary(unittest.TestCase):
    def test_periods_and_moving_bytes(self):
        frames, _ = canlog.parse(simulated_lock_press())
        rows = {l.split()[0]: l.split() for l in canlog.summary(frames).splitlines()[1:]}
        self.assertEqual(rows["100"][2], "10")        # ms
        self.assertEqual(rows["100"][5], "x.")        # counter moves, 55 does not
        self.assertEqual(rows["3C0"][5], "....")
        self.assertEqual(rows["2B0"][5], "..x.")


if __name__ == "__main__":
    unittest.main()
