#!/usr/bin/env python3
"""Fidelity check of the C++ terrain generators (src/gen) against the original JS game.

  python3 tools/port/gen_compare.py <js-source-root> <build-dir> <work-dir> [seed ...]

For each seed the original game (headless, via jsdump.py) picks one chunk of each Overworld biome from a
coarse biome map, generates them (full generate(): terrain, rivers, trees, plants, ores, deep dark, crust,
and whatever structures happen to be there) and dumps per-column heights, biomes, climate, the top blocks,
tree lists and block histograms, plus Skylands island tops, End island shapes and Ember Depths solidity.
dq-gentool dumps the same from the C++ generators; the two are compared allowing for the vertical
mapping (docs/port/gen.md). JS results are cached in <work-dir>/js_<seed>.json.
"""
import json, subprocess, sys, pathlib, collections

JS_EXPR = r"""(() => {
  const seedText = %SEED%, seed = seedFromString(seedText), out = { seed, picks: [], ow: [], sky: [], end: [], depths: [] };
  const g = makeWorldGen(seed, 0, GEN_VERSION);
  // one chunk per biome: the first coarse cell (step 64) whose 3x3 neighbourhood is all that biome
  const want = new Set([0, 2, 3, 4, 5, 6]), S = 64, N = 64;
  for (let j = 1; j < N - 1 && want.size; j++) for (let i = 1; i < N - 1 && want.size; i++) {
    const x = (i - N / 2) * S, z = (j - N / 2) * S, b = g.column(x, z).biome;
    if (!want.has(b)) continue;
    let ok = true;
    for (let dj = -1; dj <= 1 && ok; dj++) for (let di = -1; di <= 1; di++) if (g.column(x + di * 24, z + dj * 24).biome !== b) { ok = false; break; }
    if (ok) { want.delete(b); out.picks.push([b, Math.floor(x / 16), Math.floor(z / 16)]); }
  }
  const K = (id) => (BLOCKS[id] && BLOCKS[id].key) || ('#' + id);
  for (const [b, cx, cz] of out.picks) {
    const ch = new Chunk(0, cx, cz); g.generate(ch);
    const cols = [];
    for (let z = 0; z < 16; z++) for (let x = 0; x < 16; x++) {
      const c = g.column(cx * 16 + x, cz * 16 + z), i = z * 16 + x;
      cols.push([c.h, c.biome, K(ch.blocks[cidx(x, c.h, z)]), K(ch.blocks[cidx(x, c.h + 1, z)]), K(ch.blocks[cidx(x, c.h - 1, z)]), ch.climate[i * 2], ch.climate[i * 2 + 1]]);
    }
    const hist = {}; for (let i = 0; i < ch.blocks.length; i++) { const k = K(ch.blocks[i]); hist[k] = (hist[k] || 0) + 1; }
    const trees = g.treesForChunk(cx, cz).map(t => [t.x, t.y, t.z, t.type, t.h, !!t.bare, !!t.bees]);
    let structs = 0; const bx = cx * 16 + 8, bz = cz * 16 + 8;
    try { structs += g.structsNear(bx, bz, 24).length; } catch (e) {}
    try { structs += g.villagesNear(bx, bz, 24).length; } catch (e) {}
    try { structs += g.dungeonsNear(bx, bz, 24).length; } catch (e) {}
    try { structs += g.lairsNear(bx, bz, 24).length; } catch (e) {}
    out.ow.push({ biome: b, cx, cz, cols, hist, trees, structs });
  }
  // heights and biomes over a wide grid (rivers, coasts, mountains)
  out.grid = []; for (let j = 0; j < 60; j++) for (let i = 0; i < 60; i++) { const x = -1800 + i * 61, z = -1800 + j * 61, c = g.column(x, z);
    let v = 0; try { v = g.villagesNear(x, z, 40).length; } catch (e) {}
    out.grid.push([c.h, c.biome, v]); }
  // Skylands: island top (first sky_grass) and lowest block per column of a few chunks
  const gs = makeWorldGen(seed, 2, GEN_VERSION);
  for (const [cx, cz] of [[0, 0], [5, -3], [-7, 9], [12, 12]]) {
    const ch = new Chunk(2, cx, cz); gs.generate(ch); const cols = [];
    for (let z = 0; z < 16; z++) for (let x = 0; x < 16; x++) {
      let top = -1, bot = -1; for (let y = 120; y > 0; y--) { const id = ch.blocks[cidx(x, y, z)]; if (id === B.sky_grass && top < 0) top = y; if (id === B.skystone || id === B.dirt || id === B.sky_grass || id === B.aether_crystal_ore) bot = y; }
      cols.push([top, bot]);
    }
    out.sky.push({ cx, cz, cols });
  }
  // The End: island top/bottom per column (main island, the gulf, the outer islands)
  const ge = makeWorldGen(seed, 5, GEN_VERSION);
  for (const [cx, cz] of [[0, 0], [3, -2], [25, 0], [-30, 12]]) {
    const cols = []; for (let z = 0; z < 16; z++) for (let x = 0; x < 16; x++) { const c = ge.endColumn(cx * 16 + x, cz * 16 + z); cols.push(c ? [c.top, c.bot] : null); }
    out.end.push({ cx, cz, cols });
  }
  out.pillars = ge.endPillars().map(p => [p.x, p.z, p.h, p.rad, p.cage]);
  // Ember Depths: solidity at the original rows for a few columns
  const gd = makeWorldGen(seed, 1, GEN_VERSION);
  for (const [x, z] of [[0, 0], [37, -91], [-210, 64], [513, 300]]) { const s = []; for (let y = 0; y < 100; y++) s.push(gd.depthsSolid(x, y, z) ? 1 : 0); out.depths.push({ x, z, s }); }
  return out;
})()"""

def main():
    if len(sys.argv) < 4: print(__doc__); sys.exit(1)
    jsroot, build, work = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve(), pathlib.Path(sys.argv[3]).resolve()
    seeds = sys.argv[4:] or ['hello', 'dokuquest']
    work.mkdir(parents=True, exist_ok=True)
    here = pathlib.Path(__file__).parent
    ok = True
    for seed in seeds:
        jsf = work / f'js_{seed}.json'
        if not jsf.exists():
            subprocess.run([sys.executable, str(here / 'jsdump.py'), str(jsroot), str(jsf), JS_EXPR.replace('%SEED%', json.dumps(seed))], check=True)
        js = json.loads(jsf.read_text())
        req = {'seed': seed, 'grid': [-1800, -1800, 61, 60], 'ow': [[c['cx'], c['cz']] for c in js['ow']], 'sky': [[c['cx'], c['cz']] for c in js['sky']],
               'end': [[c['cx'], c['cz']] for c in js['end']], 'depths': [[d['x'], d['z']] for d in js['depths']]}
        reqf = work / f'req_{seed}.json'; reqf.write_text(json.dumps(req))
        cf = work / f'cpp_{seed}.json'
        subprocess.run([str(build / 'dq-gentool'), 'dump', str(reqf), str(cf)], check=True, cwd=str(build))
        cpp = json.loads(cf.read_text())
        ok &= compare(seed, js, cpp)
    print('OVERALL', 'OK' if ok else 'DIFFERENCES (see above)')

def owSurface(o):
    d = o - 48
    if d >= 0: return 64 + d + 0.0145 * d * d
    if d >= -4: return 64 + d
    return o * 60 / 44

def compare(seed, js, cpp):
    print(f'=== seed {seed!r} ({js["seed"]}) ===')
    names = ['Ocean', 'Beach', 'Plains', 'Forest', 'Desert', 'Taiga', 'Mountains']
    good = True
    for a, b in zip(js['ow'], cpp['ow']):
        n = len(a['cols']); hEq = bEq = clEq = topEq = aboveEq = mapOk = 0
        for ca, cb in zip(a['cols'], b['cols']):
            hEq += ca[0] == cb[0]; bEq += ca[1] == cb[1]; clEq += ca[5] == cb[5] and ca[6] == cb[6]
            topEq += ca[2] == cb[2]; aboveEq += ca[3] == cb[3]
            lo, hi = int(owSurface(ca[0])), int(owSurface(ca[0] + 1))
            mapOk += lo <= cb[7] <= hi
        ta = [tuple(t[:2]) + (t[3],) for t in a['trees']]; tb = [tuple(t[:2]) + (t[3],) for t in b['trees']]
        treeEq = ta == tb and all(abs(x[4] - y[4]) < 1e-12 and x[5] == y[5] and x[6] == y[6] for x, y in zip(a['trees'], b['trees']))
        tag = ' (structures nearby in the original)' if a['structs'] else ''
        print(f"{names[a['biome']]:9} chunk {a['cx']},{a['cz']}{tag}: height {hEq}/{n} biome {bEq}/{n} climate {clEq}/{n} "
              f"mapped-height {mapOk}/{n} top block {topEq}/{n} block above {aboveEq}/{n} trees {'same' if treeEq else 'DIFFER'} ({len(ta)}/{len(tb)})")
        if hEq < n or bEq < n or clEq < n or mapOk < n or (not treeEq and not a['structs']): good = False
        ha, hb = collections.Counter(a['hist']), collections.Counter(b['hist'])
        keys = [k for k in ('stone', 'slate', 'coal_ore', 'iron_ore', 'copper_ore', 'gold_ore', 'diamond_ore', 'granite', 'water', 'lava') if ha[k] or hb[k]]
        print('           blocks (js -> c++): ' + ', '.join(f'{k} {ha[k]}->{hb[k]}' for k in keys))
    if 'grid' in js:
        pts = [(a, b) for a, b in zip(js['grid'], cpp['grid']) if not (len(a) > 2 and a[2])]   # villages level the land (structures, later)
        gh = sum(a[0] == b[0] for a, b in pts); gb = sum(a[1] == b[1] for a, b in pts)
        print(f"Wide grid (60x60, step 61, {len(js['grid']) - len(pts)} points by villages skipped): height {gh}/{len(pts)} biome {gb}/{len(pts)}")
        good &= gh == gb == len(pts)
    sk = [0, 0, 0]
    for a, b in zip(js['sky'], cpp['sky']):
        for ca, cb in zip(a['cols'], b['cols']):
            sk[0] += 1
            sk[1] += (ca[0] < 0 and cb[0] < 0) or (ca[0] >= 0 and cb[0] == ca[0] + 120)
            sk[2] += (ca[1] < 0 and cb[1] < 0) or (ca[1] >= 0 and cb[1] == ca[1] + 120)
    print(f'Skylands columns: island top {sk[1]}/{sk[0]}, island bottom {sk[2]}/{sk[0]} (original + 120)')
    good &= sk[1] >= sk[0] * 0.99      # the original's own Skylands places (structures) add a few
    en = [0, 0]
    for a, b in zip(js['end'], cpp['end']):
        for ca, cb in zip(a['cols'], b['cols']):
            en[0] += 1; en[1] += (ca is None and cb is None) or (ca is not None and cb is not None and cb == [ca[0] + 64, ca[1] + 64])
    pil = js['pillars'] == [[p[0], p[1], p[2] - 64, p[3], p[4]] for p in cpp['pillars']]
    print(f'End columns: {en[1]}/{en[0]} (original + 64); pillars {"same" if pil else "DIFFER"}')
    good &= en[1] == en[0] and pil
    dp = [0, 0]
    for a, b in zip(js['depths'], cpp['depths']):
        for sa, sb in zip(a['s'], b['s']): dp[0] += 1; dp[1] += sa == sb
    print(f'Ember Depths solidity at the original rows (band rows x2.5, nCave1 on a 4-block grid): {dp[1]}/{dp[0]}')
    return good

if __name__ == '__main__': main()
