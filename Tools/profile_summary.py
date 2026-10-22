"""Summarize a --profile file: main program vs interrupts, and the busiest 16-byte blocks.
usage: profile_summary.py <profile.txt> [address offset to add, hex (Gridrunner: 10100)] [rows]"""
import sys

rows = [l.split() for l in open(sys.argv[1])]
rows = [(int(a, 16), int(b), int(c)) for a, b, c in rows]
base = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
n = int(sys.argv[3]) if len(sys.argv) > 3 else 12
tm, ti = sum(r[1] for r in rows), sum(r[2] for r in rows)
print('instructions: main %d (%.0f%%), interrupts %d (%.0f%%)' % (tm, 100 * tm / (tm + ti), ti, 100 * ti / (tm + ti)))
for idx, name in ((1, 'main'), (2, 'interrupts')):
    print('busiest in', name)
    for a, m, i in sorted(rows, key=lambda r: -r[idx])[:n]:
        print('  %06X  main %9d  irq %9d' % (a + base, m, i))
