#!/usr/bin/env python3
# tools/port/nether_bta.py — the blocks of the BTA-style Nether (terrain version 5, gen/nether_bta.cpp), our own designs
# with pictures recoloured from the base pack (so they sit in its style); idempotent, run by data_patches.py:
#   acid (the Sulfur Pools' caustic liquid: it eats at whoever swims in it without armour), red crystal and charged
#   crystal (the Crystal Forest's spires; a charged one holds latent energy: a redstone power source), red crystal
#   clusters, smouldering ash (the Old World's dying embers: it burns whoever steps on it without sneaking), scorched
#   bricks and cracked scorched bricks (the Old World's ruins).
import json, pathlib, sys, importlib.util

_s = importlib.util.spec_from_file_location('bta_blocks', pathlib.Path(__file__).with_name('bta_blocks.py'))
_b = importlib.util.module_from_spec(_s); _s.loader.exec_module(_b)
src_pic = _b.src_pic


def paint(im, rgb, alpha=None, gamma=1.0):
    """the picture's light and shade over a colour (relative to its mean brightness)"""
    im = im.copy(); px = im.load(); w, h = im.size
    lum = [(px[x, y][0] * 299 + px[x, y][1] * 587 + px[x, y][2] * 114) / 1000 for y in range(h) for x in range(w) if px[x, y][3] > 8]
    mean = (sum(lum) / len(lum)) if lum else 1
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a <= 8: continue
            k = (((r * 299 + g * 587 + b * 114) / 1000) / (mean or 1)) ** gamma
            px[x, y] = (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), alpha if alpha is not None else a)
    return im


def speckle(im, cols, every=13, seed=0):
    """a few bright specks (embers, glints) at fixed scattered pixels"""
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            hsh = (x * 73856093 ^ y * 19349663 ^ seed * 83492791) & 0xFFFF
            if hsh % every == 0 and px[x, y][3] > 8:
                px[x, y] = cols[hsh // every % len(cols)] + (px[x, y][3],)
    return im


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text())
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; have = {b['key'] for b in blocks}; B = {b['key']: b for b in blocks}

    def tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)   # (ours: made again each run, so a change to how it's made shows)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    def block(key, name, props, tex, item=True):
        if key in have: B[key].update(props); return B[key]
        b = {'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': 'pickaxe',
             'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name, 'tex': tex}
        b.update(props); blocks.append(b); bj['faces'].append([tiles[tex]] * 6); have.add(key); B[key] = b
        if item: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
        return b

    def pic(name): return src_pic(d, tiles, files, name)

    # ---- acid: still, caustic, faintly glowing ----
    if True: tile('acid', paint(pic('water_still'), (178, 214, 52), alpha=210, gamma=1.2))
    block('acid', 'Acid', {'render': 4, 'solid': False, 'opaque': False, 'replaceable': True, 'hardness': -1, 'tool': None, 'drop': 'air',
                           'light': 4, 'lightAtt': 2, 'liquid': 'acid'}, 'acid', item=False)
    # ---- the Crystal Forest ----
    if True:   # see-through red crystal (a spire of it reads as one great glowing shard)
        tile('red_crystal', speckle(paint(pic('blue_ice'), (218, 46, 58), alpha=172, gamma=1.6), [(255, 150, 150), (255, 200, 190)], every=41, seed=5))
        tile('charged_crystal', speckle(paint(pic('blue_ice'), (255, 104, 74), alpha=212, gamma=1.3), [(255, 214, 120), (255, 250, 200)], every=15, seed=3))
        tile('red_crystal_cluster', paint(pic('amethyst_cluster'), (226, 60, 66)))
    glassy = {'render': 2, 'opaque': False, 'translucent': True, 'selfCull': True, 'sound': 'glass', 'hardness': 1.2, 'crystal': True}
    block('red_crystal', 'Red Crystal', dict(glassy, light=9), 'red_crystal')
    block('charged_crystal', 'Charged Crystal', dict(glassy, light=14, rsBlock=True, desc='It hums with latent energy: a redstone power source.'), 'charged_crystal')
    block('red_crystal_cluster', 'Red Crystal Cluster', {'render': 3, 'solid': False, 'opaque': False, 'hardness': 1.0, 'light': 6, 'sound': 'glass',
                                                         'needsSupport': True}, 'red_crystal_cluster')
    # ---- the Old World ----
    if True:
        tile('smouldering_ash', speckle(pic('ash'), [(255, 120, 30), (255, 170, 60), (220, 70, 20)], every=11, seed=7))
        tile('scorched_bricks', paint(pic('polished_blackstone_bricks'), (104, 90, 116)))
        tile('cracked_scorched_bricks', paint(pic('cracked_nether_bricks'), (98, 86, 110)))
    block('smouldering_ash', 'Smouldering Ash', {'hardness': 0.6, 'tool': 'shovel', 'sound': 'sand', 'light': 6, 'hot': True, 'drop': 'ash'}, 'smouldering_ash')
    block('scorched_bricks', 'Scorched Bricks', {'hardness': 2.0, 'tier': 1}, 'scorched_bricks')
    block('cracked_scorched_bricks', 'Cracked Scorched Bricks', {'hardness': 2.0, 'tier': 1}, 'cracked_scorched_bricks')

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))

    # ---- the lands' own creatures (sim/creature_spawn.cpp spawnRealms): a sulfur slime in the Sulfur Pools (its touch
    #      poisons), a crystal wisp in the Crystal Forest, a scorched skeleton archer in the Old World's ruins ----
    mobs = json.loads((d / 'mobs.json').read_text())
    v = dict(mobs['slime']); v.update({'label': 'a sulfur slime', 'tint': [0.86, 0.9, 0.22, 0.6], 'hp': 14, 'damage': 2, 'fireImmune': True,
                                       'onHit': {'poison': 4}, 'drops': [['slime_ball', 0, 1, 1], ['sulphur', 0, 2, 0.6]]})
    mobs['sulfur_slime'] = v
    v = dict(mobs['cinder_wisp']); v.update({'label': 'a crystal wisp', 'tint': [1.0, 0.32, 0.4, 0.6], 'hp': 16,
                                             'drops': [['red_crystal_cluster', 0, 1, 0.5], ['glowstone_dust', 0, 2, 0.5]]})
    mobs['crystal_wisp'] = v
    v = dict(mobs['skeleton']); v.update({'label': 'a scorched skeleton', 'tint': [0.36, 0.28, 0.3, 0.62], 'hp': 24, 'fireImmune': True,
                                          'drops': [['bone', 0, 2, 1], ['arrow', 0, 2, 1], ['coal', 0, 1, 0.4]]})
    v.pop('fleesSun', None)
    mobs['scorched_skeleton'] = v
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    print('nether bta ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
