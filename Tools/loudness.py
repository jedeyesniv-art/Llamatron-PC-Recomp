# BS.1770 integrated loudness (LUFS, mono) and peak of a WAV (e.g. a --wav dump), whole and per window.
# usage: python -P Tools/loudness.py <wav> [start_s end_s]...   (-P: Tools\dis.py shadows Python's dis)
# Used for games.cpp NormalGainDb: gain = -23 - the game's loudness.
import sys, wave, numpy as np
from scipy.signal import lfilter
def load(p):
    w = wave.open(p); n = w.getnframes(); sw = w.getsampwidth(); ch = w.getnchannels(); r = w.getframerate()
    d = w.readframes(n)
    if sw == 2: x = np.frombuffer(d, np.int16).astype(np.float64) / 32768
    elif sw == 4: x = np.frombuffer(d, np.float32).astype(np.float64)
    else: raise SystemExit(sw)
    return x.reshape(-1, ch).mean(1), r
def kw(x):
    b1=[1.53512485958697,-2.69169618940638,1.19839281085285]; a1=[1,-1.69065929318241,0.73248077421585]
    b2=[1,-2,1]; a2=[1,-1.99004745483398,0.99007225036621]
    return lfilter(b2,a2,lfilter(b1,a1,x))
def lufs(x, r):
    y = kw(x); blk = int(0.4*r); hop = int(0.1*r)
    ms = np.array([np.mean(y[i:i+blk]**2) for i in range(0, len(y)-blk, hop)])
    if len(ms)==0: return float('nan')
    l = -0.691 + 10*np.log10(ms+1e-20)
    g = ms[l > -70]
    if len(g)==0: return -99
    rel = -0.691 + 10*np.log10(g.mean()) - 10
    g2 = ms[(l > -70) & (l > rel)]
    return -0.691 + 10*np.log10(g2.mean())
x, r = load(sys.argv[1])
peak = np.abs(x).max()
out = [f"all {lufs(x,r):6.1f} LUFS peak {20*np.log10(peak+1e-9):5.1f} dB ({len(x)/r:.0f} s)"]
a = sys.argv[2:]
for i in range(0, len(a), 2):
    s, e = float(a[i]), float(a[i+1]); seg = x[int(s*r):int(e*r)]
    out.append(f"{s:.0f}-{e:.0f}s {lufs(seg,r):6.1f}")
print("  ".join(out))
