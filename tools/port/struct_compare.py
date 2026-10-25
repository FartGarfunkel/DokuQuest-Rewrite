#!/usr/bin/env python3
"""Fidelity check of the C++ structures (src/gen/structures_*.cpp) against the original JS game.

  python3 tools/port/struct_compare.py <js-source-root> <build-dir> <work-dir> [seed ...] [--R 2000] [--kinds a,b]

For each seed:
 1. Plans: every structure the original places within R blocks of the origin in each band (STRUCTS kinds,
    villages/towns, dungeons, dragon lairs, strongholds, End cities), compared with dq-gentool's by id, footprint
    and base height (the C++ base + hashDy is the original's base height).
 2. Blocks: for the structure of each kind nearest the origin, both games generate the chunks around it with and
    without structures; the structure's blocks are the positions that changed to a block the structure-less
    terrain doesn't have. Compared as (x, y - base, z, block) tuples: the share of the original's found in the
    C++ and the other way round. Parts that follow the land (footings, paths, stairs up to the surface) sit on
    the stretched terrain (docs/port/gen.md) and so differ in height; the rest should match block for block.
JS results are cached in <work-dir>/sjs_<seed>.json.
"""
import json, subprocess, sys, pathlib, collections, math

JS_EXPR = r"""(() => {
  const seed = seedFromString(%SEED%), R = %R%, out = { seed, list: {}, picks: [] };
  const K = (id) => (BLOCKS[id] && BLOCKS[id].key) || ('#' + id);
  const JD = [0, 1, 2, 5], DIMC = { 0: 2, 1: 1, 2: 3, 5: 0 };
  const gens = {}; for (const d of JD) gens[d] = makeWorldGen(seed, d, GEN_VERSION);
  const found = [];
  for (const d of JD) {
    const g = gens[d], L = out.list[DIMC[d]] = [];
    const add = (kind, S, y, id) => { const e = [kind, id || S.id, S.x0, S.z0, S.x1, S.z1, y, S.name || '']; L.push(e); found.push({ d, kind, S, y, id: e[1] }); };
    const rng = (reg, f) => { const n = Math.ceil(R / reg); for (let rz = -n; rz < n; rz++) for (let rx = -n; rx < n; rx++) f(rx, rz); };
    if (d === 0) {
      rng(VREG, (rx, rz) => { const v = g.villageAt(rx, rz); if (v) add('village', v, v.y); });
      rng(DREG, (rx, rz) => { const v = g.dungeonAt(rx, rz); if (v) add('dungeon', v, v.y); });
    }
    if (d <= 2) rng(LAIR_REG, (rx, rz) => { const l = g.lairAt(rx, rz); if (l) add('dragon_lair', { x0: l.x - 14, z0: l.z - 14, x1: l.x + 14, z1: l.z + 14, name: l.type }, l.y, l.id); });
    if (d === 5) rng(END_CITY_REG, (rx, rz) => { const c = g.endCityAt(rx, rz); if (c) add('end_city', c, c.y, 'e' + rx + '_' + rz); });
    for (let k = 0; k < STRUCTS.length; k++) {
      const T = STRUCTS[k]; if (!T.dims.includes(d)) continue;
      if (T.key === 'stronghold') { for (const S of g.strongholds()) add('stronghold', S, S.y); continue; }
      rng(T.reg, (rx, rz) => { const S = g.structAt(k, rx, rz); if (S) add(T.key, S, S.y !== undefined ? S.y : S.yb !== undefined ? S.yb : S.Y0); });
    }
  }
  // the structure of each kind nearest the origin: its blocks
  const want = %KINDS%;
  const best = {};
  for (const f of found) {
    if (want.length && !want.includes(f.kind)) continue;
    const S = f.S, cx = (S.x0 + S.x1) / 2, cz = (S.z0 + S.z1) / 2, dd = Math.hypot(cx, cz), k = f.kind + '@' + f.d;
    if (!best[k] || dd < best[k].dd) best[k] = { f, dd, cx, cz };
  }
  const offOf = (d) => { const o = makeWorldGen(seed, d, GEN_VERSION); for (const k of ['structsNear', 'structsNearKind', 'villagesNear', 'dungeonsNear', 'lairsNear', 'endCitiesNear']) o[k] = () => [];
    for (const k of ['villageAt', 'dungeonAt', 'lairAt', 'endCityAt']) o[k] = () => null; o.strongholds = () => []; return o; };
  const offs = {};
  for (const k of Object.keys(best)) {
    const { f, cx, cz } = best[k], S = f.S, d = f.d;
    const off = offs[d] || (offs[d] = offOf(d));
    const c = [Math.max(S.x0 >> 4, (Math.floor(cx) >> 4) - %SPAN%), Math.max(S.z0 >> 4, (Math.floor(cz) >> 4) - %SPAN%), Math.min(S.x1 >> 4, (Math.floor(cx) >> 4) + %SPAN%), Math.min(S.z1 >> 4, (Math.floor(cz) >> 4) + %SPAN%)];
    const A = [], Bc = [], natural = new Set();
    for (let qz = c[1]; qz <= c[3]; qz++) for (let qx = c[0]; qx <= c[2]; qx++) {
      const a = new Chunk(d, qx, qz); gens[d].generate(a); A.push(a);
      const b = new Chunk(d, qx, qz); off.generate(b); Bc.push(b);
      for (let i = 0; i < b.blocks.length; i++) natural.add(K(b.blocks[i]));
    }
    const blocks = []; let ents = 0;
    A.forEach((a, n) => {
      const b = Bc[n];
      for (let i = 0; i < a.blocks.length; i++) {
        if (a.blocks[i] === b.blocks[i] || !a.blocks[i]) continue;
        const kk = K(a.blocks[i]); if (natural.has(kk)) continue;
        const x = i & 15, z = (i >> 4) & 15, y = i >> 8;
        blocks.push([a.cx * 16 + x, y - f.y, a.cz * 16 + z, kk]);
      }
      ents += a.blockEntities.size;
    });
    out.picks.push({ kind: f.kind, dim: DIMC[d], id: f.id, base: f.y, c, blocks, entities: ents });
  }
  return out;
})()"""

def main():
    args = [a for a in sys.argv[1:]]
    R, kinds, span = 2000, [], 3
    if '--R' in args: i = args.index('--R'); R = int(args[i + 1]); del args[i:i + 2]
    if '--kinds' in args: i = args.index('--kinds'); kinds = args[i + 1].split(','); del args[i:i + 2]
    if '--span' in args: i = args.index('--span'); span = int(args[i + 1]); del args[i:i + 2]
    if len(args) < 3: print(__doc__); sys.exit(1)
    jsroot, build, work = pathlib.Path(args[0]).resolve(), pathlib.Path(args[1]).resolve(), pathlib.Path(args[2]).resolve()
    seeds = args[3:] or ['hello', 'dokuquest']
    work.mkdir(parents=True, exist_ok=True)
    here = pathlib.Path(__file__).parent
    ok = True
    for seed in seeds:
        tag = f'{seed}_{R}_{span}' + ('_' + '-'.join(kinds) if kinds else '')
        jsf = work / f'sjs_{tag}.json'
        if not jsf.exists():
            expr = JS_EXPR.replace('%SEED%', json.dumps(seed)).replace('%R%', str(R)).replace('%KINDS%', json.dumps(kinds)).replace('%SPAN%', str(span))
            subprocess.run([sys.executable, str(here / 'jsdump.py'), str(jsroot), str(jsf), expr], check=True)
        js = json.loads(jsf.read_text())
        req = {'seed': seed, 'R': R, 'picks': [{'dim': p['dim'], 'id': p['id'], 'c': p['c']} for p in js['picks']]}
        reqf = work / f'sreq_{tag}.json'; reqf.write_text(json.dumps(req))
        cf = work / f'scpp_{tag}.json'
        subprocess.run([str(build / 'dq-gentool'), 'structs', str(reqf), str(cf)], check=True, cwd=str(build))
        cpp = json.loads(cf.read_text())
        ok &= compare(seed, js, cpp, kinds)
    print("OVERALL", "OK" if ok else "DIFFERENCES (see above)")

DIMN = {0: 'End', 1: 'Depths', 2: 'Overworld', 3: 'Skylands'}

def compare(seed, js, cpp, kinds):
    print(f'=== seed {seed!r} ({js["seed"]}) ===')
    good = True
    for d in ('2', '1', '3', '0'):
        a = {e[1]: e for e in js['list'].get(d, []) if not kinds or e[0] in kinds}
        b = {e[1]: e for e in cpp['list'].get(d, []) if not kinds or e[0] in kinds}
        per = collections.defaultdict(lambda: [0, 0, 0, 0, 0])   # js, c++, same id, same footprint, same base
        for i, e in a.items():
            p = per[e[0]]; p[0] += 1
            if i in b:
                p[2] += 1; f = b[i]
                p[3] += e[2:6] == f[2:6] or (e[0] == 'labyrinth' and e[2:5] == f[2:5] and f[5] >= e[5])   # its stair runs longer on the stretched land
                p[4] += e[6] == f[6]
        for i, e in b.items(): per[e[0]][1] += 1
        for k, p in sorted(per.items()):
            flag = '' if p[0] == p[1] == p[2] == p[3] else '   <-- differs'
            print(f'{DIMN[int(d)]:9} {k:15} plans js {p[0]:3} c++ {p[1]:3} same id {p[2]:3} footprint {p[3]:3} base y {p[4]:3}{flag}')
            if p[2] < max(p[0], p[1]) or p[3] < p[2]: good = False
        miss = [i for i in a if i not in b][:4]; extra = [i for i in b if i not in a][:4]
        if miss: print('   only in the original:', [a[i][:7] for i in miss])
        if extra: print('   only in the C++:', [b[i][:7] for i in extra])
        fp = [(a[i], b[i]) for i in a if i in b and a[i][2:6] != b[i][2:6] and a[i][0] != 'labyrinth'][:3]
        for x, y in fp: print('   footprint', x[:7], 'vs', y[:7])
    cp = {(p['dim'], p['id']): p for p in cpp.get('picks', [])}
    for p in js['picks']:
        q = cp.get((p['dim'], p['id']))
        if not q or q.get('missing'): print(f"blocks {p['kind']:15} {p['id']}: not placed by the C++"); good = False; continue
        A = set(tuple(t) for t in p['blocks']); Bs = set(tuple(t) for t in q['blocks'])
        # the same tuples, and the same tuples allowing a vertical offset per column (structures following the land)
        same = len(A & Bs)
        colA = collections.defaultdict(list); colB = collections.defaultdict(list)
        for t in A: colA[(t[0], t[2])].append((t[1], t[3]))
        for t in Bs: colB[(t[0], t[2])].append((t[1], t[3]))
        shifted = 0
        for c, la in colA.items():
            lb = colB.get(c)
            if not lb: continue
            best = 0
            ka = collections.Counter(k for _, k in la); kb = collections.Counter(k for _, k in lb)
            best = sum((ka & kb).values())
            shifted += best
        ka = collections.Counter(t[3] for t in A); kb = collections.Counter(t[3] for t in Bs)
        hist = sum((ka & kb).values())
        n = max(1, len(A))
        print(f"blocks {p['kind']:15} {p['id']:16} {DIMN[p['dim']]:9} js {len(A):6} c++ {len(Bs):6}  exact {same / n * 100:5.1f}%  per-column {shifted / n * 100:5.1f}%  "
              f"block counts {hist / n * 100:5.1f}%  (c++ exact {same / max(1, len(Bs)) * 100:5.1f}%)  base {p.get('base')} vs {q.get('base')}  chests/entities {p['entities']} vs {q['entities']}")
        top = [(k, ka[k], kb[k]) for k in sorted(set(ka) | set(kb), key=lambda k: -abs(ka[k] - kb[k]))[:6] if ka[k] != kb[k]]
        if top: print('           most different:', ', '.join(f'{k} {x}->{y}' for k, x, y in top))
    return good

if __name__ == '__main__': main()
