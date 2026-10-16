"""Calendar test for core.f (>date >time date> time> .date .time leap?).

check.py make      prints Forth test lines for lf
check.py compare F checks lf's output F against Python's datetime
"""
import random
import sys
from datetime import datetime, timezone

def cases():
    rnd = random.Random(2026)
    t = [0, 59, 60, 3599, 3600, 86399, 86400, 951782400, 951868800,
         4107456000, 4107542400, 1767225600, 2**32 - 1]
    for y in (1970, 1971, 1972, 2000, 2024, 2100, 2105):   # month starts and ends
        for m in range(1, 13):
            s = int(datetime(y, m, 1, tzinfo=timezone.utc).timestamp())
            t += [s, s - 1]
    t += [rnd.randrange(2**32) for _ in range(300)]
    return [x for x in t if 0 <= x < 2**32]

def make():
    print("decimal")
    for t in cases():
        # date, time, then the round trip through date> and time> (0 if it agrees)
        print(f"{t} .date space {t} .time space "
              f"{t} >time {t} >date date> 86400 * >r time> r> + {t} xor . cr")
    for y in (1970, 1972, 2000, 2023, 2024, 2100):
        print(f".( leap {y} ) {y} leap? . cr")
    print("bye")

def compare(path):
    want = []
    for t in cases():
        d = datetime.fromtimestamp(t, tz=timezone.utc)
        want.append(d.strftime("%Y-%m-%d %H:%M:%S") + " 0")
    for y in (1970, 1972, 2000, 2023, 2024, 2100):
        leap = y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)
        want.append(f"leap {y} {-1 if leap else 0}")
    got = [l.replace("ok>", "").strip() for l in open(path, encoding="utf-8")]
    got = [l for l in got if l and (l[0].isdigit() or l.startswith("leap"))]
    bad = [(w, g) for w, g in zip(want, got) if w != g]
    if len(got) != len(want) or bad:
        print(f"{len(want)} expected, {len(got)} got, first differences: {bad[:5]}")
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(make() if sys.argv[1] == "make" else compare(sys.argv[2]))
