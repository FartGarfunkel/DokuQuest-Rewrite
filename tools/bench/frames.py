#!/usr/bin/env python3
# tools/bench/frames.py — summarises a DQ_BENCH file: frame-time percentiles, spikes, the mean of each phase, the
# GPU's time, the input-to-GPU-done latency, and what the main thread was doing in the spikes.
#   python3 tools/bench/frames.py bench.csv [skip_frames=600]
import csv, sys, collections

def pct(v, p):
    if not v: return float('nan')
    v = sorted(v); i = min(len(v) - 1, max(0, int(round(p / 100 * (len(v) - 1)))))
    return v[i]

rows = list(csv.DictReader(open(sys.argv[1])))
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 600
rows = [r for r in rows if int(r['frame']) >= skip]
# frame interval: the time between frame starts (what the screen sees)
iv = [(float(b['t']) - float(a['t'])) * 1000 for a, b in zip(rows, rows[1:])]
print(f"frames {len(iv)}  fps(avg) {1000 / (sum(iv) / len(iv)):.0f}")
print(f"frame ms: avg {sum(iv) / len(iv):.2f}  p50 {pct(iv, 50):.2f}  p95 {pct(iv, 95):.2f}  p99 {pct(iv, 99):.2f}  "
      f"p99.9 {pct(iv, 99.9):.2f}  max {max(iv):.2f}")
print(f"spikes: >16.7 ms {sum(x > 16.7 for x in iv)}  >33 ms {sum(x > 33 for x in iv)}  >50 ms {sum(x > 50 for x in iv)}")
# jitter: how much each frame differs from the one before it
d = [abs(b - a) for a, b in zip(iv, iv[1:])]
print(f"frame-to-frame change: avg {sum(d) / len(d):.2f} ms  p95 {pct(d, 95):.2f}  p99 {pct(d, 99):.2f}")
cols = ['wait', 'input', 'update', 'render', 'ui', 'swap', 'sync', 'sleep']
print("phase ms (avg / p99 / max): " + "  ".join(
    f"{c} {sum(float(r[c]) for r in rows) / len(rows):.2f}/{pct([float(r[c]) for r in rows], 99):.2f}/{max(float(r[c]) for r in rows):.1f}" for c in cols))
g = [float(r['gpu']) for r in rows if float(r['gpu']) >= 0]
l = [float(r['latency']) for r in rows if float(r['latency']) > 0]
if g: print(f"gpu ms: avg {sum(g) / len(g):.2f}  p50 {pct(g, 50):.2f}  p99 {pct(g, 99):.2f}  max {max(g):.1f}")
if l: print(f"input->GPU done ms: avg {sum(l) / len(l):.2f}  p50 {pct(l, 50):.2f}  p95 {pct(l, 95):.2f}  p99 {pct(l, 99):.2f}")
# scopes: the mean of each named piece, and which were biggest in frames over 16.7 ms
tot = collections.Counter(); spikeBlame = collections.Counter(); n = 0
for r, x in zip(rows, iv + [0]):
    for kv in (r['top'] or '').split(' '):
        if '=' not in kv: continue
        k, v = kv.rsplit('=', 1); tot[k] += float(v)
        if x > 16.7: spikeBlame[k] += float(v)
print("top scopes (ms/frame, from each frame's 3 biggest): " + "  ".join(f"{k} {v / len(rows):.2f}" for k, v in tot.most_common(8)))
print("in frames >16.7 ms (total ms): " + "  ".join(f"{k} {v:.0f}" for k, v in spikeBlame.most_common(8)))
