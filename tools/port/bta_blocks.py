#!/usr/bin/env python3
"""Better than Adventure-inspired blocks, our own Doku designs (idempotent; data_patches.py runs it):
  python3 tools/port/bta_blocks.py <data-dir>
Pictures are made from the nearest existing picture (the DokuQuest Base pack's, else the generated one) recoloured and
retouched, so they sit with the rest of the art; nothing is taken from BTA.
- pumice: soaks up the lava around it when it is put down or lava reaches it (sim/blockfx.cpp), turning molten
- molten pumice: glows and burns long in a furnace; water cools it back to pumice
- cinder sand: black volcanic sand that falls
- emberglass cluster / block: glowing red crystal from the Nether
- brazier: a fire in an iron bowl on legs: light, never spreads, burns what stands in it
- spike trap: iron spikes on a plate: hurts what walks into it; items fall through"""
import json, sys, pathlib, colorsys, random
from PIL import Image

PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'


def src_pic(d, tiles, files, name):
    p = d / PACK / (name + '.png')
    if p.exists():
        im = Image.open(p).convert('RGBA')
        return im.crop((0, 0, im.width, im.width)).resize((32, 32), Image.NEAREST)   # first frame of an animation
    return Image.open(d / files[tiles[name]]).convert('RGBA').resize((32, 32), Image.NEAREST)


def recolour(im, fn):
    out = im.copy(); px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a: px[x, y] = fn(r, g, b, a, x, y)
    return out


def hsv_map(dh=0.0, sat=1.0, val=1.0, add=0, hue=None):
    def f(r, g, b, a, x, y):
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        h = (hue if hue is not None else h + dh) % 1.0; s = min(1, s * sat); v = min(1, v * val + add / 255)
        rr, gg, bb = colorsys.hsv_to_rgb(h, s, v)
        return (int(rr * 255), int(gg * 255), int(bb * 255), a)
    return f


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); sj = json.loads((d / 'shapes.json').read_text())
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; have = {b['key'] for b in blocks}

    def tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    def block(key, name, props, faces, item=True):
        if key in have: return
        b = {'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': 'pickaxe',
             'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name}
        b.update(props); blocks.append(b); bj['faces'].append(faces); have.add(key)
        if item: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    rnd = random.Random(7)
    # pumice: pale, porous, from tuff
    pum = recolour(src_pic(d, tiles, files, 'tuff'), hsv_map(sat=0.35, val=1.25, add=18))
    px = pum.load()
    pores = [(rnd.randrange(32), rnd.randrange(32)) for _ in range(46)]
    for x, y in pores:
        r, g, b, a = px[x, y]; px[x, y] = (r * 55 // 100, g * 55 // 100, b * 55 // 100, a)
    t_pum = tile('pumice', pum)
    mol = pum.copy(); mp = mol.load()
    for x, y in pores: mp[x, y] = (255, 150 + rnd.randrange(60), 40, 255)   # the soaked-up lava glows in the pores
    mol = recolour(mol, lambda r, g, b, a, x, y: (min(255, r + 40), g, max(0, b - 30), a) if (x, y) not in set(pores) else (r, g, b, a))
    t_mol = tile('molten_pumice', mol)
    t_cin = tile('cinder_sand', recolour(src_pic(d, tiles, files, 'sand'), hsv_map(hue=0.78, sat=0.25, val=0.32)))
    t_egc = tile('emberglass_cluster', recolour(src_pic(d, tiles, files, 'amethyst_cluster'), hsv_map(hue=0.03, sat=1.3, val=1.1)))
    t_egb = tile('emberglass_block', recolour(src_pic(d, tiles, files, 'amethyst_block'), hsv_map(hue=0.03, sat=1.2, val=1.05)))

    block('pumice', 'Pumice', {'hardness': 0.8, 'pumice': True}, [t_pum] * 6)
    block('molten_pumice', 'Molten Pumice', {'hardness': 0.8, 'light': 12, 'hot': True, 'drop': 'molten_pumice', 'moltenPumice': True, 'fuel': 60}, [t_mol] * 6)
    block('cinder_sand', 'Cinder Sand', {'hardness': 0.5, 'tool': 'shovel', 'sound': 'sand', 'falls': True}, [t_cin] * 6)
    block('emberglass_cluster', 'Emberglass Cluster', {'render': 3, 'solid': False, 'opaque': False, 'hardness': 1.5, 'light': 9, 'sound': 'glass',
                                                       'tex': 'emberglass_cluster'}, [t_egc] * 6)
    block('emberglass_block', 'Block of Emberglass', {'hardness': 1.5, 'light': 12, 'sound': 'glass'}, [t_egb] * 6)
    for k, tx in (('pumice', 'pumice'), ('molten_pumice', 'molten_pumice'), ('cinder_sand', 'cinder_sand'), ('emberglass_block', 'emberglass_block')):
        next(b for b in blocks if b['key'] == k)['tex'] = tx
    # the brazier: iron legs and bowl, coals, a flame
    if 'brazier' not in sj['shapes']:
        legs = [{'b': [x, 0, z, x + 1, 6, z + 1], 'side': 'polished_blackstone'} for x in (3, 12) for z in (3, 12)]
        sj['shapes']['brazier'] = legs + [{'b': [2, 6, 2, 14, 9, 14], 'side': 'polished_blackstone', 'end': 'polished_blackstone'},
                                          {'b': [3, 8.5, 3, 13, 9.5, 13], 'side': 'magma_block', 'end': 'magma_block', 'nocol': True},
                                          {'b': [5, 9.5, 5, 11, 15, 11], 'side': 'fire', 'end': 'fire', 'nocol': True}]
    block('brazier', 'Brazier', {'render': 6, 'opaque': False, 'hardness': 2.5, 'sound': 'metal', 'light': 15, 'hot': True, 'shape': 'brazier',
                                 'tex': 'polished_blackstone'}, [tiles['polished_blackstone']] * 6)
    # the spike trap: a plate of iron spikes you can walk into (not onto); items drop through
    if 'spike_trap' not in sj['shapes']:
        sp = [{'b': [0, 0, 0, 16, 1, 16], 'side': 'iron_block', 'end': 'iron_block', 'nocol': True}]
        for i, (x, z) in enumerate((x, z) for x in (2, 7, 12) for z in (2, 7, 12)):
            h = 9 + (i * 5) % 4
            sp.append({'b': [x + 0.5, 1, z + 0.5, x + 1.5, h, z + 1.5], 'side': 'iron_block', 'end': 'iron_block', 'nocol': True})
        sj['shapes']['spike_trap'] = sp
    block('spike_trap', 'Spike Trap', {'render': 6, 'solid': False, 'opaque': False, 'hardness': 3, 'sound': 'metal', 'shape': 'spike_trap',
                                       'spikes': True, 'tex': 'iron_block'}, [tiles['iron_block']] * 6)

    def recipe(out, n, shape=None, key=None, items_=None):
        if any(r.get('out') == out for r in rj['recipes']): return
        r = {'out': out, 'n': n}
        if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
        else: r['items'] = items_
        rj['recipes'].append(r)
    recipe('pumice', 8, ['BBB', 'BMB', 'BBB'], {'B': 'basalt', 'M': 'magma_cream'})
    recipe('cinder_sand', 2, items_=['sand', 'sand', 'charcoal'])
    recipe('emberglass_block', 1, ['EE', 'EE'], {'E': 'emberglass_cluster'})
    recipe('brazier', 1, ['IcI', 'III'], {'I': 'iron_ingot', 'c': 'coal'})
    recipe('spike_trap', 2, ['NNN', 'III'], {'N': 'iron_nugget', 'I': 'iron_ingot'})

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    print('bta blocks ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
