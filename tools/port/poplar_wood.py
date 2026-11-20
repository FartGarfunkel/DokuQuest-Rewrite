#!/usr/bin/env python3
"""tools/port/poplar_wood.py — poplar, a whole wood like the others: everything aspen has (log, wood, stripped log and
wood, planks, stairs, slab, fence, gate, door, trapdoor, button, pressure plate, sign, branch, chair, stool, bench,
table, shelves, bookcase, cabinet) made for poplar from aspen's own entries, with its recipes, smelting, sawing,
composting, tags and item models. Its pictures are the DokuQuest Base pack's poplar ones (the pack is used first:
data/packmap.json); the pictures under data/textures are only fallbacks, aspen's recoloured to the pack's poplar wood.
Poplar's leaves (yellow, orange, red) and sapling are tools/port/dappled_forest.py's. Idempotent; data_patches.py runs
it before dappled_forest.py.

  python3 tools/port/poplar_wood.py [data-dir]
"""
import copy, json, pathlib, sys
from PIL import Image

SKIP = {'aspen_leaves', 'aspen_sapling'}   # (poplar's own: three autumn colours, and its sapling grows the poplar)


def rn(o):
    """aspen -> poplar in every string and key"""
    if isinstance(o, str): return o.replace('aspen', 'poplar')
    if isinstance(o, list): return [rn(x) for x in o]
    if isinstance(o, dict): return {rn(k): rn(v) for k, v in o.items()}
    return o


def has_aspen(o): return 'aspen' in json.dumps(o)


def load(d, f): return json.loads((d / f).read_text())


def save(d, f, o, indent=None, ascii_=False):
    t = json.dumps(o, indent=indent, ensure_ascii=ascii_) if indent else json.dumps(o, separators=(',', ':'), ensure_ascii=ascii_)
    (d / f).write_text(t)


def main(d):
    d = pathlib.Path(d)
    pack = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures'
    bj, items, rj = load(d, 'blocks.json'), load(d, 'items.json'), load(d, 'recipes.json')
    shp, ish, mj = load(d, 'shapes.json'), load(d, 'itemshapes.json'), load(d, 'machines.json')
    tj, pm = load(d, 'tiles.json'), load(d, 'packmap.json')
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))

    # ---- pictures: each of aspen's, for poplar (the pack's art first, aspen's recoloured as the fallback) ----
    def mean(im):
        px = [p for p in im.convert('RGBA').getdata() if p[3] > 0]
        return [sum(c) / len(px) for c in list(zip(*px))[:3]] if px else [1, 1, 1]
    ref = pack / 'block/poplar_planks.png'
    ratio = [1.0, 1.0, 1.0]
    if ref.exists():
        a, p = mean(Image.open(d / files[tiles['aspen_planks']])), mean(Image.open(ref))
        ratio = [p[i] / max(1.0, a[i]) for i in range(3)]

    def recolour(im):
        im = im.convert('RGBA'); px = im.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                r, g, b, al = px[x, y]
                px[x, y] = (min(255, int(r * ratio[0])), min(255, int(g * ratio[1])), min(255, int(b * ratio[2])), al)
        return im

    entry = {e[0]: e for e in pm}
    remap = {}   # aspen's tile index -> poplar's
    for name in [k for k in tiles if 'aspen' in k and k not in SKIP]:
        pn = rn(name)
        rel = rn(files[tiles[name]])
        src = d / files[tiles[name]]
        if src.exists(): recolour(Image.open(src)).save(d / rel)
        if pn not in tiles: tiles[pn] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        else: files[tiles[pn]] = rel
        remap[tiles[name]] = tiles[pn]
        # the pack's names: its own poplar ones only (aspen's list starts with birch's: no borrowing here)
        e = entry.get(name, [name, [name], [name]])
        blocks_ = [n for n in rn(e[1]) if 'birch' not in n]
        items_ = [n for n in rn(e[2]) if 'birch' not in n]
        if pn == 'poplar_log_side': blocks_ = ['poplar_log'] + [n for n in blocks_ if n != 'poplar_log']
        new = [pn, blocks_, items_]
        if pn in entry: pm[pm.index(entry[pn])] = new
        else: pm.append(new)
        entry[pn] = new

    # ---- blocks (new ids; a poplar block already there takes aspen's shape, keeps its id) ----
    blocks, faces = bj['blocks'], bj['faces']
    while len(faces) < len(blocks): faces.append(None)
    byKey = {b['key']: b for b in blocks}
    for b in [b for b in blocks if 'aspen' in b['key'] and b['key'] not in SKIP]:
        nb = rn(copy.deepcopy(b))
        f = faces[b['id']]
        nf = [remap.get(i, i) for i in f] if isinstance(f, list) else f
        if nb['key'] in byKey:
            nb['id'] = byKey[nb['key']]['id']; blocks[nb['id']] = nb; faces[nb['id']] = nf
        else:
            nb['id'] = len(blocks); blocks.append(nb); faces.append(nf)
        byKey[nb['key']] = nb

    # ---- items ----
    for k in [k for k in items if 'aspen' in k and k not in SKIP]:
        items[rn(k)] = rn(copy.deepcopy(items[k]))

    # ---- recipes, smelting, tags ----
    recs = rj['recipes']
    def sig(r): return json.dumps({k: v for k, v in r.items() if k != 'idx'}, sort_keys=True)
    have = {sig(r) for r in recs}
    recs[:] = [r for r in recs if not (r['out'] == 'poplar_planks' and r.get('items') == ['#logs_poplar'] and 'classicOnly' not in r and any('aspen' in json.dumps(x) for x in recs))]
    nidx = max(r.get('idx', 0) for r in recs) + 1
    for r in [r for r in recs if has_aspen(r) and not any(s in json.dumps(r) for s in SKIP)]:
        nr = rn(copy.deepcopy(r))
        if sig(nr) in have: continue
        nr['idx'] = nidx; nidx += 1; recs.append(nr); have.add(sig(nr))
    sm = rj['smelting']
    for k in [k for k in sm if 'aspen' in k]: sm[rn(k)] = rn(sm[k])
    tags = rj['tags']
    for t in list(tags):
        if 'aspen' in t: tags[rn(t)] = rn(tags[t])
        else:
            for k in [k for k in tags[t] if 'aspen' in k and k not in SKIP]:
                if rn(k) not in tags[t]: tags[t].append(rn(k))

    # ---- shapes, item models, machines ----
    for k in [k for k in shp['shapes'] if 'aspen' in k]: shp['shapes'][rn(k)] = rn(copy.deepcopy(shp['shapes'][k]))
    for k in [k for k in ish['itemTiles'] if 'aspen' in k and k not in SKIP]:
        if rn(k) not in ish['itemTiles']: ish['itemTiles'].append(rn(k))
    for k in [k for k in ish['blockItems'] if 'aspen' in k and k not in SKIP]: ish['blockItems'][rn(k)] = rn(copy.deepcopy(ish['blockItems'][k]))
    for sect in ('sawing', 'compost', 'milling', 'crushing'):
        s = mj.get(sect)
        if isinstance(s, dict):
            for k in [k for k in s if 'aspen' in k]: s[rn(k)] = rn(s[k])
    if isinstance(mj.get('compost'), dict):   # (poplar's own leaves and sapling compost like aspen's)
        for k in ('poplar_leaves_yellow', 'poplar_leaves_orange', 'poplar_leaves_red', 'red_shrub_leaves', 'poplar_sapling'):
            if k in byKey: mj['compost'][k] = mj['compost'].get('aspen_leaves' if 'leaves' in k else 'aspen_sapling', 0.3)

    save(d, 'blocks.json', bj); save(d, 'items.json', items, ascii_=True); save(d, 'recipes.json', rj)
    save(d, 'shapes.json', shp, indent=1); save(d, 'itemshapes.json', ish); save(d, 'machines.json', mj)
    save(d, 'tiles.json', tj); save(d, 'packmap.json', pm)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / 'data')
