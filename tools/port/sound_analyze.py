#!/usr/bin/env python3
"""Analyse every sound of a Minecraft-style resource pack into compact synthesis PROFILES, so the game can
resynthesise them on the fly (src/audio/resynth.cpp) without shipping a single recording.

A profile (all levels in dB, quantised to 0.5 dB, 0 = silence):
  * fine   — the loudness every 128 samples (2.7 ms at 48 kHz), below and above ~1 kHz: attacks, crunches,
             clicks, the decay;
  * bands  — the noise (what is left once the tones are taken out) in 26 ERB-spaced bands, every hop
             (15 / 20 / 40 ms, longer sounds get the longer hop);
  * tracks — the tones: sinusoidal partials followed frame by frame (frequency + level), which covers
             voices (harmonics), rings (glass, anvils, bells) and pitched pops alike.
Resynthesis = random-phase noise shaped by the bands (overlap-add) + the partials as oscillators, then the
result's own loudness is matched to `fine` block by block. Each render draws new noise, and every play
varies pitch and level like the original's takes.

  python3 tools/port/sound_analyze.py analyze <pack assets/minecraft> data/sounds.json data/sounds.bin
  python3 tools/port/sound_analyze.py eval <pack assets/minecraft> [names...]   # resynth in Python + distances
  python3 tools/port/sound_analyze.py metric a.wav b.wav                        # distance between two renders

Needs numpy and ffmpeg (decoding the .ogg files). The pack's event table (sounds.json) and the original
game's own table of which pack events stand in for which of its sounds (render/packs.js, PACK_SOUND_EVENTS,
taken from data/sfx.json) give the mapping from the game's sound names to profiles.
"""
import sys, json, struct, subprocess, zlib, pathlib
import numpy as np

SR = 48000
NB = 26
NFFT = 2048          # noise analysis/synthesis
NPK = 4096           # partial detection
FINE = 128           # loudness block (above XO)
FINE_LO = 128        # loudness block (below XO)
DB0 = -100.0         # code 0 = silence; code v -> DB0 + v * 0.5

def erb(f): return 21.4 * np.log10(1 + 0.00437 * f)
def ierb(e): return (10 ** (e / 21.4) - 1) / 0.00437
EDGES = ierb(np.linspace(0, erb(SR / 2), NB + 1))
CENT_E = (erb(EDGES[:-1]) + erb(EDGES[1:])) / 2          # band centres on the ERB scale
XO_BAND = int(np.argmin(np.abs(EDGES - 1000)))           # the loudness is followed below and above this band edge
XO = EDGES[XO_BAND]

def decode(path):
    r = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'], capture_output=True)
    if r.returncode: print('cannot decode', path, r.stderr.decode()[:120].strip(), file=sys.stderr); return np.zeros(0)
    raw = r.stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)

def q_db(x):   # linear power/amplitude-in-dB -> code
    return np.clip(np.round((x - DB0) * 2), 0, 255).astype(np.uint8)
def dq_db(c): return np.where(c == 0, -300.0, DB0 + c.astype(np.float64) * 0.5)

def loudness(x, B):
    nf = (len(x) + B - 1) // B
    xp = np.concatenate([x, np.zeros(nf * B - len(x))])
    return 10 * np.log10(np.mean(xp.reshape(nf, B) ** 2, axis=1) + 1e-30)

def hop_for(n):
    d = n / SR
    return 720 if d <= 1.5 else 960 if d <= 4 else 1920

def frames(x, n, hop, nfr):
    pad = np.concatenate([np.zeros(n // 2), x, np.zeros(n)])
    idx = np.arange(nfr)[:, None] * hop + np.arange(n)[None, :]
    return pad[idx]

# ---- analysis ------------------------------------------------------------------------------------------------------
def analyze(x):
    n = len(x); hop = hop_for(n); nfr = n // hop + 1
    # fine loudness, below and above XO
    X = np.fft.rfft(x); lo = np.fft.irfft(X * (np.fft.rfftfreq(n, 1 / SR) < XO), n)
    fine_lo, fine = loudness(lo, FINE_LO), loudness(x - lo, FINE)
    # partials
    wpk = np.hanning(NPK); swp = wpk.sum()
    Fp = np.fft.rfft(frames(x, NPK, hop, nfr) * wpk, axis=1)
    Mp = np.abs(Fp) + 1e-12; Lp = 20 * np.log10(Mp)
    fmax = Lp.max()
    peaks = []
    for i in range(nfr):
        L = Lp[i]
        # local floor: a running median of the level (in 64-bin blocks)
        blk = np.median(L[:len(L) // 64 * 64].reshape(-1, 64), axis=1)
        floor = np.interp(np.arange(len(L)), np.arange(len(blk)) * 64 + 32, blk)
        cand = np.where((L[1:-1] > L[:-2]) & (L[1:-1] >= L[2:]) & (L[1:-1] > floor[1:-1] + 14) & (L[1:-1] > max(L.max() - 50, fmax - 70)))[0] + 1
        cand = cand[cand * SR / NPK > 35]
        cand = cand[np.argsort(-L[cand])][:28]
        pk = []
        for k in cand:
            a, b, c = L[k - 1], L[k], L[k + 1]
            p = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
            f = (k + p) * SR / NPK; lv = b - 0.25 * (a - c) * p
            amp = 2 * 10 ** (lv / 20) / swp
            pk.append((f, amp))
        peaks.append(pk)
    # link peaks into tracks
    tracks, active = [], []
    for i in range(nfr):
        free = list(peaks[i]); nxt = []
        for tr in active:
            f0 = tr['f'][-1]
            best, bd = None, max(15.0, f0 * 0.03)
            for j, (f, a) in enumerate(free):
                if abs(f - f0) < bd: best, bd = j, abs(f - f0)
            if best is not None:
                f, a = free.pop(best); tr['f'].append(f); tr['a'].append(a); nxt.append(tr)
            else: tracks.append(tr)
        for f, a in free: nxt.append({'s': i, 'f': [f], 'a': [a]})
        active = nxt
    tracks += active
    tracks = [t for t in tracks if len(t['f']) >= 4]
    tracks.sort(key=lambda t: -max(t['a']))
    tracks = tracks[:48]
    # noise bands: the spectrum with the partials' bins taken out
    wn = np.hanning(NFFT); s2 = (wn ** 2).sum()
    P = np.abs(np.fft.rfft(frames(x, NFFT, hop, nfr) * wn, axis=1)) ** 2 / s2
    mask = np.ones_like(P, dtype=bool)
    for t in tracks:
        for j, f in enumerate(t['f']):
            k = int(round(f * NFFT / SR)); mask[t['s'] + j, max(0, k - 2):k + 3] = False
    fb = np.arange(P.shape[1]) * SR / NFFT
    bands = np.zeros((nfr, NB))
    for b in range(NB):
        sel = (fb >= EDGES[b]) & (fb < EDGES[b + 1]) if b < NB - 1 else (fb >= EDGES[b])
        if not sel.any(): sel = np.abs(fb - (EDGES[b] + EDGES[b + 1]) / 2) == np.abs(fb - (EDGES[b] + EDGES[b + 1]) / 2).min()
        m = mask[:, sel]; v = P[:, sel]
        cnt = m.sum(axis=1)
        bands[:, b] = np.where(cnt > 0, (v * m).sum(axis=1) / np.maximum(cnt, 1), v.mean(axis=1) * 0.1)
    bands = 10 * np.log10(bands + 1e-30)
    # coarser steps where the ear (and the measure) can't tell: bands in 2 dB, loudness in 1 dB — a third less data
    bands = np.round(bands / 2) * 2; fine = np.round(fine); fine_lo = np.round(fine_lo)
    return {'n': n, 'hop': hop, 'fine': fine, 'fine_lo': fine_lo, 'bands': bands, 'tracks': tracks}

# ---- binary profile ------------------------------------------------------------------------------------------------
# u32 n, u16 hop, u16 nfr, u16 ntracks | fine_lo u8[ceil(n/256)] | fine u8[ceil(n/128)] | bands u8[NB][nfr] (band-major)
# | per track: u16 start, u16 len, u16 logf0, i8 dlogf[len-1], u8 amp0, i8 damp[len-1]
# Every u8 run is delta-coded (each byte = this - previous, mod 256) so zlib packs it tight. logf = log2(f/10)*1024.
def dcode(a): a = np.asarray(a, np.int16); return ((np.diff(np.concatenate([[0], a])) % 256).astype(np.uint8)).tobytes()
def ddecode(b): return np.cumsum(np.frombuffer(b, np.uint8).astype(np.int64)) % 256

def split_tracks(tracks):
    """Cut a track where its pitch jumps too far for one i8 step, so every track codes as deltas."""
    out = []
    for t in tracks:
        lf = np.round(np.log2(np.array(t['f']) / 10) * 1024).astype(int)
        am = q_db(20 * np.log10(np.array(t['a']) + 1e-12)).astype(int)
        s0 = 0
        for j in range(1, len(lf) + 1):
            if j == len(lf) or abs(lf[j] - lf[j - 1]) > 127 or abs(am[j] - am[j - 1]) > 127:
                if j - s0 >= 2: out.append((t['s'] + s0, lf[s0:j], am[s0:j]))
                s0 = j
    return out

def encode(p):
    tr = split_tracks(p['tracks'])
    out = bytearray(struct.pack('<IHHH', p['n'], p['hop'], len(p['bands']), len(tr)))
    out += dcode(q_db(p['fine_lo'])); out += dcode(q_db(p['fine']))
    out += dcode(q_db(p['bands']).T.reshape(-1))
    for s0, lf, am in tr:
        out += struct.pack('<HHH', s0, len(lf), int(np.clip(lf[0], 0, 65535)))
        out += (np.diff(lf).astype(np.int8)).tobytes()
        out += struct.pack('<B', int(am[0])) + (np.diff(am).astype(np.int8)).tobytes()
    return bytes(out)

def decode_profile(b):
    n, hop, nfr, ntr = struct.unpack_from('<IHHH', b, 0); o = 10
    nlo, nhi = (n + FINE_LO - 1) // FINE_LO, (n + FINE - 1) // FINE
    fine_lo = dq_db(ddecode(b[o:o + nlo])); o += nlo
    fine = dq_db(ddecode(b[o:o + nhi])); o += nhi
    bands = dq_db(ddecode(b[o:o + nfr * NB]).reshape(NB, nfr).T); o += nfr * NB
    tracks = []
    for _ in range(ntr):
        s0, L, f0 = struct.unpack_from('<HHH', b, o); o += 6
        lf = f0 + np.concatenate([[0], np.cumsum(np.frombuffer(b, np.int8, L - 1, o))]); o += L - 1
        a0 = b[o]; o += 1
        am = a0 + np.concatenate([[0], np.cumsum(np.frombuffer(b, np.int8, L - 1, o))]); o += L - 1
        tracks.append({'s': s0, 'f': 10 * 2 ** (lf / 1024), 'a': 10 ** (dq_db(am) / 20)})
    return {'n': n, 'hop': hop, 'fine': fine, 'fine_lo': fine_lo, 'bands': bands, 'tracks': tracks}

# ---- reference resynthesis (src/audio/resynth.cpp does the same) -----------------------------------------------------
def resynth(p, seed=1, pitch=1.0):
    rng = np.random.default_rng(seed)
    n, hop = p['n'], p['hop']; nfr = len(p['bands'])
    y = np.zeros((2, n + NFFT * 2))      # [below XO, above XO]
    # noise: overlap-add of random-phase frames every NFFT/4, the band levels interpolated in time and frequency
    hs = NFFT // 4; ws = np.hanning(NFFT); sw2 = (ws ** 2).sum()
    fk = np.arange(NFFT // 2 + 1) * SR / NFFT; kE = erb(fk); low = fk < XO
    for j in range(n // hs + 2):
        t = j * hs / hop; i0 = min(int(t), nfr - 1); i1 = min(i0 + 1, nfr - 1); u = t - int(t)
        lv = p['bands'][i0] * (1 - u) + p['bands'][i1] * u
        Pk = 10 ** (np.interp(kE, CENT_E, lv) / 10)
        M = np.sqrt(Pk * NFFT * hs / sw2)
        Y = M * np.exp(2j * np.pi * rng.random(len(M))); Y[0] = 0
        a = j * hs - NFFT // 2
        for c, sel in ((0, low), (1, ~low)):
            fr = np.fft.irfft(Y * sel, NFFT) * ws
            aa = a
            if aa < 0: fr = fr[-aa:]; aa = 0
            y[c, aa:aa + len(fr)] += fr
    y = y[:, :n]
    # partials
    tt = np.arange(n)
    for t in p['tracks']:
        L = len(t['f']); fpos = (t['s'] + np.arange(L)) * hop
        lo, hi = max(0, int(fpos[0] - hop)), min(n, int(fpos[-1] + hop))
        if hi <= lo: continue
        seg = tt[lo:hi]
        f = np.interp(seg, fpos, t['f']) * pitch
        a = np.interp(seg, np.concatenate([[fpos[0] - hop], fpos, [fpos[-1] + hop]]), np.concatenate([[0], t['a'], [0]]))
        ph = 2 * np.pi * np.cumsum(f) / SR + rng.random() * 2 * np.pi
        y[0 if np.median(t['f']) < XO else 1, lo:hi] += a * np.sin(ph) * (f < SR / 2 * 0.95)
    return match_fine(y[0], p['fine_lo'], FINE_LO) + match_fine(y[1], p['fine'], FINE)

def match_fine(y, fine_db, B=FINE):
    """Scale y block by block (B samples, 3-block smoothing) to the stored loudness."""
    n = len(y); nf = len(fine_db)
    yp = np.concatenate([y, np.zeros(nf * B - n)])
    e = np.mean(yp.reshape(nf, B) ** 2, axis=1)
    es = np.convolve(e, [0.25, 0.5, 0.25], 'same')
    tg = 10 ** (fine_db / 10)
    tgs = np.convolve(tg, [0.25, 0.5, 0.25], 'same')
    g = np.sqrt(tgs / np.maximum(es, 1e-20)); g = np.minimum(g, 30); g[tgs < 1e-12] = 0
    cen = np.arange(nf) * B + B / 2
    return y * np.interp(np.arange(n), cen, g)

# ---- the distance --------------------------------------------------------------------------------------------------
def melspec(x):
    n = 1024; hop = 256
    nfr = max(1, len(x) // hop)
    S = np.abs(np.fft.rfft(frames(x, n, hop, nfr) * np.hanning(n), axis=1)) ** 2
    f = np.arange(S.shape[1]) * SR / n
    mel = lambda f: 2595 * np.log10(1 + f / 700)
    me = np.linspace(mel(30), mel(20000), 66); fe = 700 * (10 ** (me / 2595) - 1)
    W = np.zeros((64, len(f)))
    for i in range(64):
        l, c, r = fe[i], fe[i + 1], fe[i + 2]
        W[i] = np.clip(np.minimum((f - l) / (c - l), (r - f) / (r - c)), 0, None)
    return 10 * np.log10(S @ W.T + 1e-12)

def distance(a, b):
    """Mean absolute difference (dB) of 64-band log-mel spectrograms (1024/256 at 48 kHz), over the cells within 60 dB
    of the original's loudest, levels floored there; and the correlation of the 5 ms loudness envelopes (dB)."""
    n = min(len(a), len(b)); A, B = melspec(a[:n]), melspec(b[:n])
    top = A.max(); fl = top - 60
    A, B = np.maximum(A, fl), np.maximum(B, fl)
    sel = (A > fl) | (B > fl)
    d = float(np.abs(A - B)[sel].mean()) if sel.any() else 0.0
    ea = 10 * np.log10(np.convolve(a[:n] ** 2, np.ones(240) / 240, 'same')[::240] + 1e-10)
    eb = 10 * np.log10(np.convolve(b[:n] ** 2, np.ones(240) / 240, 'same')[::240] + 1e-10)
    m = max(ea.max(), eb.max()) - 50; ea, eb = np.maximum(ea, m), np.maximum(eb, m)
    c = float(np.corrcoef(ea, eb)[0, 1]) if ea.std() > 0 and eb.std() > 0 else 1.0
    return d, c

# ---- the mapping ---------------------------------------------------------------------------------------------------
# The original's table (render/packs.js) plus pack events it never listed but this pack has (the user's request:
# every game sound with an equivalent in the pack uses it). Each: our key -> (pack event, default files).
EXTRA = {
    'fx.anvil': ('block.anvil.use', ['random/anvil_use']), 'fx.anvil_land': ('block.anvil.land', ['random/anvil_land']),
    'fx.fuse': ('entity.creeper.primed', ['random/fuse']), 'fx.drink': ('entity.generic.drink', ['random/drink']),
    'fx.break_tool': ('entity.item.break', ['random/break']), 'fx.portal': ('block.portal.trigger', ['portal/trigger']),
    'fx.portal_travel': ('block.portal.travel', ['portal/travel']), 'fx.portal_ambient': ('block.portal.ambient', ['portal/portal']),
    'fx.thunder': ('entity.lightning_bolt.thunder', ['ambient/weather/thunder1', 'ambient/weather/thunder2', 'ambient/weather/thunder3']),
    'fx.thunder_far': ('entity.lightning_bolt.thunder', ['ambient/weather/thunder1', 'ambient/weather/thunder2', 'ambient/weather/thunder3']),
    'fx.rain': ('weather.rain', ['ambient/weather/rain1', 'ambient/weather/rain2', 'ambient/weather/rain3', 'ambient/weather/rain4']),
    'fx.cave': ('ambient.cave', ['ambient/cave/cave%d' % i for i in range(1, 24)]),
    'fx.firework_launch': ('entity.firework_rocket.launch', ['fireworks/launch1']), 'fx.firework_blast': ('entity.firework_rocket.blast', ['fireworks/blast1']),
    'fx.firework_twinkle': ('entity.firework_rocket.twinkle', ['fireworks/twinkle1']),
    'fx.glass_break': ('block.glass.break', ['random/glass1', 'random/glass2', 'random/glass3']),
    'fx.water_ambient': ('block.water.ambient', ['liquid/water']), 'fx.lava_ambient': ('block.lava.ambient', ['liquid/lava']),
    'fx.hook_throw': ('entity.fishing_bobber.throw', ['random/bow']), 'fx.throw': ('entity.snowball.throw', ['random/bow']),
    'fx.attack': ('entity.player.attack.strong', ['entity/player/attack/strong1']), 'fx.chest_ender': ('block.ender_chest.open', ['block/enderchest/open']),
    'fx.elytra_wind': ('item.elytra.flying', ['item/elytra/elytra_loop']), 'fx.death': ('entity.player.death', ['damage/hit1']),
    'fx.bundle': ('item.bundle.insert', ['item/bundle/insert1']), 'fx.splash_potion': ('entity.splash_potion.break', ['random/glass1']),
    'mob.mstep_cow': ('entity.cow.step', ['mob/cow/step%d' % i for i in range(1, 5)]), 'mob.mstep_pig': ('entity.pig.step', ['mob/pig/step%d' % i for i in range(1, 6)]),
    'mob.mstep_sheep': ('entity.sheep.step', ['mob/sheep/step%d' % i for i in range(1, 6)]), 'mob.mstep_chicken': ('entity.chicken.step', ['mob/chicken/step1', 'mob/chicken/step2']),
    'mob.mstep_golem': ('entity.iron_golem.step', ['mob/irongolem/walk%d' % i for i in range(1, 5)]),
    'mob.mstep_wolf': ('entity.wolf.step', ['mob/wolf/step%d' % i for i in range(1, 6)]), 'mob.wolf_pant': ('entity.wolf.pant', ['mob/wolf/panting']),
    'mob.wolf_whine': ('entity.wolf.whine', ['mob/wolf/whine']), 'mob.cluck_egg': ('entity.chicken.egg', ['mob/chicken/plop']),
}
for m in ['deepslate', 'cobweb', 'vine', 'rooted_dirt', 'iron', 'cherry_wood', 'sweet_berry_bush', 'chain', 'copper']:
    for k, ev, n in [('step', 'step', 6), ('dig', 'hit', 6), ('break', 'break', 4), ('place', 'place', 4)]:
        EXTRA['%s.%s' % (m, k)] = ('block.%s.%s' % (m, ev), ['block/%s/%s%d' % (m, ev, i) for i in range(1, n + 1)])

def resolve(table, root, event, defaults):
    """The files (with the event's own pitch and volume) a pack plays for an event, like render/packs.js does
    (sounds.json first, falling back to the vanilla file names), plus Minecraft's per-entry pitch/volume."""
    def lst(ev, d=0):
        t = table.get(ev)
        if not t or not isinstance(t.get('sounds'), list) or d > 2: return []
        r = []
        for x in t['sounds']:
            if isinstance(x, str): r.append((x, 1.0, 1.0))
            elif isinstance(x, dict) and x.get('type') == 'event': r += lst(str(x['name']).replace('minecraft:', ''), d + 1)
            elif isinstance(x, dict) and x.get('name'): r.append((x['name'], float(x.get('pitch', 1)), float(x.get('volume', 1))))
        return r
    names = [(n.replace('minecraft:', ''), p, v) for n, p, v in lst(event)] if event in table else []
    got = [(n, p, v) for n, p, v in names if (root / 'sounds' / (n + '.ogg')).exists()]
    if not got: got = [(n, 1.0, 1.0) for n in defaults if (root / 'sounds' / (n + '.ogg')).exists()]
    return got

def cmd_analyze(root, out_json, out_bin):
    root = pathlib.Path(root)
    table = json.loads((root / 'sounds.json').read_text()) if (root / 'sounds.json').exists() else {}
    sfx = json.loads(pathlib.Path(out_json).with_name('sfx.json').read_text())
    pe = dict(sfx['packEvents']); pe.update({k: list(v) for k, v in EXTRA.items() if k not in pe})
    files = sorted(p.relative_to(root / 'sounds').with_suffix('').as_posix() for p in (root / 'sounds').rglob('*.ogg'))
    blob, index = bytearray(), {}
    for i, name in enumerate(files):
        x = decode(root / 'sounds' / (name + '.ogg'))
        if len(x) < 64: continue
        b = encode(analyze(x))
        index[name] = [len(blob), len(b)]; blob += b
        if i % 50 == 0: print(i, name, len(b), file=sys.stderr)
    events = {}
    for ours, (ev, defaults) in sorted(pe.items()):
        got = [[n, round(p, 3), round(v, 3)] for n, p, v in resolve(table, root, ev, defaults or []) if n in index]
        if got: events[ours] = got
    z = zlib.compress(bytes(blob), 9)
    pathlib.Path(out_bin).write_bytes(z)
    meta = {'about': 'Synthesis profiles of the GeruDoku resource pack sounds (tools/port/sound_analyze.py). No audio: levels, bands and partials only.',
            'rate': SR, 'bands': NB, 'nfft': NFFT, 'fine': FINE, 'db0': DB0, 'raw': len(blob), 'edges': [round(e, 2) for e in EDGES],
            'sounds': index, 'events': events}
    pathlib.Path(out_json).write_text(json.dumps(meta, separators=(',', ':')))
    print('profiles', len(index), 'events', len(events), 'raw', len(blob), 'zlib', len(z), 'json', pathlib.Path(out_json).stat().st_size)

def cmd_eval(root, names, outdir=None):
    root = pathlib.Path(root)
    if not names: names = sorted(p.relative_to(root / 'sounds').with_suffix('').as_posix() for p in (root / 'sounds').rglob('*.ogg'))
    rows = []
    for name in names:
        x = decode(root / 'sounds' / (name + '.ogg'))
        if len(x) < 64: continue
        p = decode_profile(encode(analyze(x)))
        y = resynth(p, seed=7)
        d, c = distance(x, y)
        # reference points: the floor (two renders of the same profile differ only in their random noise), and the
        # crudest model (white noise with the right overall loudness every 2.7 ms)
        floor = distance(resynth(p, seed=8), y)[0]
        wn = match_fine(np.random.default_rng(3).standard_normal(len(x)), loudness(x, FINE), FINE)
        dn, cn = distance(x, wn)
        rows.append((name, d, c, dn, floor))
        print('%-40s dist %5.2f dB  env r %.3f   (floor %5.2f, noise-with-envelope %5.2f dB)  %d B' % (name, d, c, floor, dn, len(encode(analyze(x)))))
        if outdir:
            import wave
            for tag, s in (('orig', x), ('resyn', y)):
                w = wave.open(str(pathlib.Path(outdir) / (name.replace('/', '_') + '.' + tag + '.wav')), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
                w.writeframes((np.clip(s, -1, 1) * 32767).astype('<i2').tobytes()); w.close()
    if rows:
        D = np.array([r[1] for r in rows]); C = np.array([r[2] for r in rows]); N = np.array([r[3] for r in rows]); F = np.array([r[4] for r in rows])
        print('N=%d  mean dist %.2f dB (median %.2f, p90 %.2f)  mean env r %.3f   floor %.2f dB   noise-with-envelope baseline %.2f dB' % (len(rows), D.mean(), np.median(D), np.percentile(D, 90), C.mean(), F.mean(), N.mean()))
    return rows

def readwav(p):
    if p.endswith('.f32'): return np.fromfile(p, np.float32).astype(np.float64)
    import wave
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth()
    d = np.frombuffer(w.readframes(n), '<i2' if sw == 2 else '<f4').astype(np.float64)
    if sw == 2: d /= 32768
    return d.reshape(-1, ch).mean(axis=1)

if __name__ == '__main__':
    c = sys.argv[1]
    if c == 'analyze': cmd_analyze(sys.argv[2], sys.argv[3], sys.argv[4])
    elif c == 'eval':
        od = None; a = sys.argv[3:]
        if a and a[0].startswith('--out='): od = a[0][6:]; a = a[1:]
        cmd_eval(sys.argv[2], a, od)
    elif c == 'metric':
        d, cc = distance(readwav(sys.argv[2]), readwav(sys.argv[3])); print('dist %.2f dB env r %.3f' % (d, cc))
