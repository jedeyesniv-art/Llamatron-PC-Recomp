"""Compare two --dump RAM images: list the differing ranges (merged when closer than 64 bytes).
usage: ramdiff.py <a.bin> <b.bin> [start hex] [end hex]"""
import sys

a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
start = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
end = int(sys.argv[4], 16) if len(sys.argv) > 4 else len(a)
ranges = []
for i in range(start, end):
    if a[i] != b[i]:
        if ranges and i - ranges[-1][1] < 64:
            ranges[-1][1] = i
        else:
            ranges.append([i, i])
total = sum(e - s + 1 for s, e in ranges)
print('%d differing ranges, %d bytes in them' % (len(ranges), total))
for s, e in ranges[:60]:
    print('  $%06X-$%06X (%d bytes)' % (s, e, e - s + 1))
