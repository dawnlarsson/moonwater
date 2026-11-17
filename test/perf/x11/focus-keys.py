"""xfocus-keys.py [KEYS] [GAP] (ember, round 2): a left click on the X window "xgl" (its place from `moonwater window`, passed in
XFOCUS_WINDOWS; the click focuses it), then KEYS asdfghjkl presses GAP seconds apart through battery.py's keyboard (fd 7) and tablet (fd 8).
Run in the bowl with `7<>/dev/uinput 8<>/dev/uinput`. Prints each press's monotonic ms."""
import os, re, sys, time
sys.path.insert(0, "/tmp/cperf")
import battery as B
N = int(sys.argv[1]) if len(sys.argv) > 1 else 30
GAP = float(sys.argv[2]) if len(sys.argv) > 2 else 0.2
B.Uinput.handed = [7, 8]
text = open(os.environ["XFOCUS_WINDOWS"]).read()
m = re.search(r"at (\d+),(\d+), (\d+) by (\d+): xgl", text)
x, y, w, h = (int(v) for v in m.groups())
p = B.Person(1680, 1050)
p.click(x + w // 2, y + h // 2 + 60)
time.sleep(0.6)
letters = "asdfghjkl"
for i in range(N):
    code = B.KEY[letters[i % len(letters)]]
    print("press %d %.3f" % (i + 1, time.monotonic() * 1000), flush=True)
    p.keyboard.send((B.EV_KEY, code, 1))
    time.sleep(0.012)
    p.keyboard.send((B.EV_KEY, code, 0))
    time.sleep(GAP - 0.012)
p.close()
