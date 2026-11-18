"""same_track.py a.csv b.csv -- are two `track` files of the same replay IDENTICAL?
       same_track.py --summary a.csv -- what one track saw (players, the world row).

Rows are matched on (replay_frame, player); the absolute frame column is
ignored (it counts from launch, and loading time differs). Player rows: body /
ground / steep are guest addresses, reported apart from the values. Player 0 =
the world row (debug console `track`, findings/68 s.6): count, sums and two
hashes of every other damageable character: vy:vz without their addresses
(compared as a value), ground:steep with them (reported with the addresses)."""
import csv, sys

POINTERS = ('body', 'ground', 'steep')

def rows(path):
    out = {}
    with open(path) as f:
        lines = [l for l in f if not l.startswith('#')]
    for r in csv.DictReader(lines):
        if r['replay_frame'] in ('', '-1'):
            continue
        out[(int(r['replay_frame']), r['player'])] = {k: v for k, v in r.items() if k != 'frame'}
    return out

WORLD_POINTERS = ('ground', 'steep')  # the world row's address-including hash

def values(key, r):
    skip = WORLD_POINTERS if key[1] == '0' else POINTERS
    return {c: v for c, v in r.items() if c not in skip}

def pointers(key, r):
    keep = WORLD_POINTERS if key[1] == '0' else POINTERS
    return {c: r[c] for c in keep}

def summary(path):
    a = rows(path)
    frames = sorted({k[0] for k in a})
    print(f"{path}: replay frames {frames[0]}..{frames[-1]}" if frames else f"{path}: no replay rows")
    world = [a[k] for k in sorted(a) if k[1] == '0']
    if world:
        counts = [int(w['body'], 16) for w in world]
        hps = [float(w['vx']) for w in world]
        print(f"  other damageable characters: {min(counts)}..{max(counts)}, "
              f"their hitpoints {min(hps):.1f}..{max(hps):.1f} (first {hps[0]:.1f}, last {hps[-1]:.1f})")
    else:
        print("  no world rows (an older build)")

if sys.argv[1] == '--summary':
    summary(sys.argv[2]); sys.exit(0)

a, b = rows(sys.argv[1]), rows(sys.argv[2])
common = sorted(set(a) & set(b))
print(f"A {len(a)} rows, B {len(b)} rows, {len(common)} shared")
if not common:
    print("NOTHING TO COMPARE"); sys.exit(2)
pdiff = [k for k in common if pointers(k, a[k]) != pointers(k, b[k])]
print("guest addresses (players' bodies and contacts, the world hash with addresses):",
      "same" if not pdiff else f"{len(pdiff)} rows differ, first at replay frame {pdiff[0][0]}, player {pdiff[0][1]}")
nworld = sum(1 for k in common if k[1] == '0')
print(f"world rows compared: {nworld}")
diff = [k for k in common if values(k, a[k]) != values(k, b[k])]
if not diff:
    print(f"IDENTICAL: all {len(common)} shared rows (replay frames {common[0][0]}..{common[-1][0]})")
    sys.exit(0)
k = diff[0]
print(f"DIFFERENT: {len(diff)} rows; first at replay frame {k[0]}, player {k[1]}")
for col, v in values(k, a[k]).items():
    if v != b[k][col]:
        print(f"  {col}: {v} vs {b[k][col]}")
sys.exit(1)
