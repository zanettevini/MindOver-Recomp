"""fp_first_diff.py a.txt b.txt -- the first line where two
--debug_net_fingerprint_log files differ (lines matched by their leading
session frame), with the differing fields named."""
import sys

def load(path):
    out = {}
    try:
        for line in open(path, errors='replace'):
            parts = line.split()
            if parts and parts[0].lstrip('-').isdigit():
                out.setdefault(int(parts[0]), line.rstrip('\n'))
    except FileNotFoundError:
        print("missing:", path)
    return out

a, b = load(sys.argv[1]), load(sys.argv[2])
shared = sorted(set(a) & set(b))
print(f"{len(a)} / {len(b)} frames, {len(shared)} shared")
# A line = "<frame> <fingerprint> <its parts> || <controllers> || <behaviours>".
# The fingerprint (field 1) is what the session compares; the behaviour part
# holds pointers that may differ harmlessly (each PC's heap), so the search
# is on the fingerprint and its parts (before the first "||").
def head(line): return line.split('||')[0]
for f in shared:
    if head(a[f]) != head(b[f]):
        print(f"FIRST DIFFERENCE at session frame {f}")
        wa, wb = head(a[f]).split(), head(b[f]).split()
        for i, (x, y) in enumerate(zip(wa, wb)):
            if x != y:
                print(f"  field {i}: {x}  vs  {y}")
        print("  host :", head(a[f])[:400])
        print("  guest:", head(b[f])[:400])
        ctl = [g for g in shared if g <= f and a[g].split('||')[1:2] != b[g].split('||')[1:2]]
        print("  controllers part first differed at frame", ctl[0] if ctl else "never (up to here)")
        break
else:
    print("IDENTICAL on every shared frame")
