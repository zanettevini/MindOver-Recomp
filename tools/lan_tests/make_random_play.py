"""make_random_play.py <out.txt> <minutes> <seed> [--guest] -- a recording of
seeded random play for lan_one_pc.ps1 (console `replay` format): runs in
random directions, jumps (A), light / heavy attacks (X / Y), B now and then,
one change every 150 / 250 / 400 ms, like lan_host_5min.txt. --guest: START
at 3 s (joins as the mask) and B at 5 s (on foot) first, then the play.
--lb: LB now and then too (Simple Spinning's spin, held for a while)."""
import math
import random
import sys

out, minutes, seed = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
guest = '--guest' in sys.argv[4:]
lb = '--lb' in sys.argv[4:]
rng = random.Random(seed)
lines = [f"# LAN test, the {'guest (its own socket): START at 3 s joins, B at 5 s leaves the mask, then ' if guest else 'host: '}"
         f"{minutes:g} minutes of random runs, jumps, light / heavy attacks, B{', LB' if lb else ''} (seed {seed}).",
         "# <ms> <player> <buttons hex> <LT> <RT> <LX> <LY> <RX> <RY>"]
t = 0
if guest:
    for t in range(0, 6250, 250):
        b = '0010' if t == 3000 else '2000' if t == 5000 else '0000'
        lines.append(f"{t} 1 {b} 0 0 0 0 0 0")
    t = 6250
end = int(minutes * 60000) + t
angle = rng.uniform(-math.pi, math.pi)
while t < end:
    if rng.random() < 0.15:
        x = y = 0  # stand still a moment
    else:
        angle += rng.uniform(-0.9, 0.9)
        x, y = int(32767 * math.cos(angle)), int(32767 * math.sin(angle))
    r = rng.random()
    b = '1000' if r < 0.16 else '4000' if r < 0.31 else '8000' if r < 0.37 else '2000' if r < 0.395 else '0000'
    if lb and rng.random() < 0.06:
        b = '0100'  # LB: a spin (with --simple_spinning), held for this step
    lines.append(f"{t} 1 {b} 0 0 {x} {y} 0 0")
    t += rng.choice((150, 250, 250, 400))
lines.append(f"{t} 1 0000 0 0 0 0 0 0")
open(out, 'w', newline='\n').write('\n'.join(lines) + '\n')
print(out, len(lines) - 2, "entries,", t, "ms")
