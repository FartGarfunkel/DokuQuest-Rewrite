#!/usr/bin/env python3
"""tools/port/soulrend_import.py — Soulrend's pictures into the game's own pack, for the pictures it hasn't got.

The game's built-in pack (data/resourcepacks/DokuQuest Base, git-excluded like every pack) keeps every picture it has;
for each of the game's pictures it lacks (data/packmap.json: our tile -> the pack names it may have), the first
Soulrend source that has one by those names gives it: Excalibur, then its support packs, then Soulrend's other packs,
then the mods' own textures (unpacked by `--mods`). Written into the pack as ordinary files, with a record of what came
in (soulrend_import.json) so `--undo` takes exactly those out again.

  python3 tools/port/soulrend_import.py --soulrend "<instance>/minecraft" [--mods <unpacked mods dir>] [--dry] [--undo]
"""
import argparse, io, json, os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'data', 'resourcepacks', 'DokuQuest Base')
REC = os.path.join(PACK, 'soulrend_import.json')

class Src:
    """one source: a zip pack or a folder; index of picture name -> path, blocks and items apart (minecraft first)"""
    def __init__(self, name, files, read):
        self.name, self.read = name, read
        self.block, self.item = {}, {}
        for f in files:
            if not f.endswith('.png') or '/textures/' not in f or not f.startswith('assets/'): continue
            sub = f.split('/textures/', 1)[1]
            kind = 'block' if sub.startswith(('block/', 'blocks/')) else 'item' if sub.startswith(('item/', 'items/')) else None
            if not kind: continue
            base = f.rsplit('/', 1)[1][:-4]
            M = self.block if kind == 'block' else self.item
            if base not in M or (f.startswith('assets/minecraft/') and not M[base].startswith('assets/minecraft/')): M[base] = f

def zipSrc(path):
    z = zipfile.ZipFile(path)
    names = z.namelist()
    root = ''
    for n in names:
        i = n.find('assets/')
        if i >= 0: root = n[:i]; break
    files = [n[len(root):] for n in names if n.startswith(root) and not n.endswith('/')]
    return Src(os.path.basename(path), files, lambda f: z.read(root + f) if (root + f) in z.NameToInfo else None)

def dirSrc(path, name=None):
    files = []
    for dp, dn, fn in os.walk(path):
        for f in fn: files.append(os.path.relpath(os.path.join(dp, f), path).replace(os.sep, '/'))
    def rd(f):
        p = os.path.join(path, f)
        return open(p, 'rb').read() if os.path.isfile(p) else None
    return Src(name or os.path.basename(path), files, rd)

def sources(inst, mods):
    out = []
    rp = os.path.join(inst, 'config', 'paxi', 'resourcepacks')
    if not os.path.isdir(rp): rp = os.path.join(inst, 'resourcepacks')
    packs = sorted(os.listdir(rp)) if os.path.isdir(rp) else []
    def rank(n):   # Excalibur's main pack, its support packs, the rest
        l = n.lower()
        return (0 if l.startswith('excalibur_v') else 1 if 'excalibur' in l else 2, l)
    for n in sorted(packs, key=rank):
        p = os.path.join(rp, n)
        try: out.append(zipSrc(p) if n.lower().endswith('.zip') else dirSrc(p))
        except Exception as e: print('  (skipped', n, e, ')')
    if mods and os.path.isdir(mods):
        for n in sorted(os.listdir(mods)): out.append(dirSrc(os.path.join(mods, n), 'mod:' + n))
    return [s for s in out if s.block or s.item]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--soulrend', required=True); ap.add_argument('--mods'); ap.add_argument('--dry', action='store_true'); ap.add_argument('--undo', action='store_true')
    a = ap.parse_args()
    if a.undo:
        rec = json.load(open(REC)) if os.path.exists(REC) else []
        for r in rec:
            p = os.path.join(PACK, r['to'])
            if os.path.exists(p): os.remove(p)
            if os.path.exists(p + '.mcmeta') and r.get('mcmeta'): os.remove(p + '.mcmeta')
        if os.path.exists(REC): os.remove(REC)
        print('took out', len(rec)); return
    pm = json.load(open(os.path.join(ROOT, 'data', 'packmap.json')))
    items = set(json.load(open(os.path.join(ROOT, 'data', 'itemshapes.json'))).get('itemTiles', []))
    have = set()
    for dp, dn, fn in os.walk(os.path.join(PACK, 'assets', 'minecraft', 'textures')):
        for f in fn:
            if f.endswith('.png'): have.add((('item' if '/item' in dp.replace(os.sep, '/') else 'block'), f[:-4]))
    srcs = sources(a.soulrend, a.mods)
    print(len(srcs), 'sources')
    rec = json.load(open(REC)) if os.path.exists(REC) else []
    done = {r['to'] for r in rec}
    n = 0; missing = []; bySrc = {}
    for e in pm:
        if not isinstance(e, list) or len(e) < 3: continue
        tname = e[0]; isItem = tname in items
        names = e[2] if isItem else e[1]
        kind = 'item' if isItem else 'block'
        if any((kind, c) in have for c in names): continue
        hit = None
        for s in srcs:
            M = s.item if isItem else s.block
            for c in names:
                if c in M: hit = (s, M[c], c); break
            if hit: break
        if not hit: missing.append(tname); continue
        s, path, c = hit
        to = 'assets/minecraft/textures/%s/%s.png' % (kind, c)
        if to in done: continue
        bySrc[s.name] = bySrc.get(s.name, 0) + 1
        n += 1
        if a.dry: continue
        data = s.read(path)
        if not data: continue
        dst = os.path.join(PACK, to); os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'wb').write(data)
        mm = s.read(path + '.mcmeta')
        if mm: open(dst + '.mcmeta', 'wb').write(mm)
        rec.append({'to': to, 'from': s.name + ':' + path, 'tile': tname, 'mcmeta': bool(mm)})
        have.add((kind, c))
    # ---- second pass: the game's pictures still without one (and those the name table hasn't got), matched to Soulrend's by
    # their names made alike: our port prefixes off, crop stages and the side/top words the same on both sides
    import re
    def norm(n):
        n = n.lower()
        n = re.sub(r'^(bn|be|imm|bta|ae|ff|qk|nat|ns|fd)_', '', n)
        n = re.sub(r'_crops?_stage(\d)$', r'_stage\1', n)
        n = re.sub(r'(?<!_)s_stage(\d)$', r'_stage\1', n)   # (cabbages_stage0 -> cabbage_stage0)
        n = n.replace('_block_side', '_side').replace('_block_top', '_top')
        return n
    # only the pictures blocks and items show (not the painted pieces of the creatures' models, the particles...)
    used = set(items)
    def texNames(t):
        if isinstance(t, str): yield t
        elif isinstance(t, dict):
            for v in t.values(): yield from texNames(v)
        elif isinstance(t, list):
            for v in t: yield from texNames(v)
    for b in json.load(open(os.path.join(ROOT, 'data', 'blocks.json')))['blocks']: used.update(texNames(b.get('tex')))
    for it in json.load(open(os.path.join(ROOT, 'data', 'items.json'))).values():
        ic = it.get('icon') if isinstance(it, dict) else None
        if isinstance(ic, str) and not ic.startswith('block:'): used.add(ic)
    tnames = [t for t in json.load(open(os.path.join(ROOT, 'data', 'tiles.json')))['names'] if t in used]
    inpm = {e[0]: e for e in pm if isinstance(e, list) and len(e) >= 3}
    alike = {}   # normalised -> (src, path, kind)
    for s2 in srcs:
        for kind, M in (('block', s2.block), ('item', s2.item)):
            for base, path in M.items():
                k = norm(base)
                if (k, kind) not in alike: alike[(k, kind)] = (s2, path, base)
    added_pm = 0; n2 = 0
    for tname in tnames:
        isItem = tname in items; kind = 'item' if isItem else 'block'
        e = inpm.get(tname)
        names = ((e[2] if isItem else e[1]) if e else []) or [tname]
        if any((kind, c) in have for c in names): continue
        hit = alike.get((norm(tname), kind)) or alike.get((norm(tname), 'block' if isItem else 'item'))
        if not hit: continue
        s2, path, base = hit
        to = 'assets/minecraft/textures/%s/%s.png' % (kind, names[0])
        if to in done: continue
        n2 += 1; bySrc[s2.name] = bySrc.get(s2.name, 0) + 1
        if a.dry: continue
        data = s2.read(path)
        if not data: continue
        dst = os.path.join(PACK, to); os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'wb').write(data)
        mm = s2.read(path + '.mcmeta')
        if mm: open(dst + '.mcmeta', 'wb').write(mm)
        rec.append({'to': to, 'from': s2.name + ':' + path, 'tile': tname, 'mcmeta': bool(mm), 'alike': base})
        have.add((kind, names[0]))
        if not e:   # (the name table learns it: our tile, by its own name)
            pm.append([tname, [tname], [tname]]); inpm[tname] = pm[-1]; added_pm += 1
    print('by alike names:', n2, '(name table +%d)' % added_pm)
    if not a.dry and added_pm: json.dump(pm, open(os.path.join(ROOT, 'data', 'packmap.json'), 'w'), separators=(',', ':'))
    # ---- block models the game draws from its pack (assets/pack_models.h) and the pictures they name
    MODELS = ['torch', 'wall_torch', 'soul_torch', 'soul_wall_torch', 'redstone_torch', 'redstone_wall_torch', 'redstone_torch_off', 'redstone_wall_torch_off', 'copper_torch', 'copper_wall_torch']
    nm = 0
    for mname in MODELS:
        to = 'assets/minecraft/models/block/%s.json' % mname
        if os.path.exists(os.path.join(PACK, to)) or to in done: continue
        for s2 in srcs:
            if not s2.name.lower().startswith('excalibur_v'): continue
            data = s2.read(to)
            if not data: continue
            mj = json.loads(data)
            for ref in (mj.get('textures') or {}).values():
                if not isinstance(ref, str) or ref.startswith('#'): continue
                ref = ref.replace('minecraft:', '')
                tto = 'assets/minecraft/textures/%s.png' % ref
                if os.path.exists(os.path.join(PACK, tto)) or tto in done: continue
                td = s2.read(tto)
                if td and not a.dry:
                    dst = os.path.join(PACK, tto); os.makedirs(os.path.dirname(dst), exist_ok=True); open(dst, 'wb').write(td)
                    rec.append({'to': tto, 'from': s2.name + ':' + tto, 'tile': 'model:' + mname}); done.add(tto)
            if not a.dry:
                dst = os.path.join(PACK, to); os.makedirs(os.path.dirname(dst), exist_ok=True); open(dst, 'wb').write(data)
                rec.append({'to': to, 'from': s2.name + ':' + to, 'tile': 'model:' + mname}); done.add(to)
            nm += 1
            break
    print('block models:', nm)
    if not a.dry: json.dump(rec, open(REC, 'w'), indent=0)
    print('brought in', n, 'pictures;', len(missing), "of the game's pictures still have none anywhere")
    for k, v in sorted(bySrc.items(), key=lambda x: -x[1])[:15]: print('  ', v, k)
    open('/tmp/soulrend_missing.txt', 'w').write(chr(10).join(missing))

if __name__ == '__main__': main()
