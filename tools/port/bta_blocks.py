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


    # painted planks, 16 colours, from the oak planks (a paintbrush and a dye in the other hand paint any planks)
    DYE = {'white': (240, 240, 240), 'orange': (230, 128, 40), 'magenta': (190, 70, 180), 'light_blue': (110, 170, 220), 'yellow': (240, 210, 60),
           'lime': (130, 200, 50), 'pink': (235, 140, 170), 'gray': (90, 92, 96), 'light_gray': (160, 160, 156), 'cyan': (40, 140, 150),
           'purple': (120, 50, 170), 'blue': (50, 70, 170), 'brown': (120, 80, 50), 'green': (80, 110, 40), 'red': (170, 45, 40), 'black': (35, 32, 38)}
    base = src_pic(d, tiles, files, 'oak_planks' if (d / PACK / 'oak_planks.png').exists() or 'oak_planks' in tiles else 'planks')
    lum = [[0] * 32 for _ in range(32)]; bp = base.load()
    for y in range(32):
        for x in range(32):
            r, g, b, a = bp[x, y]; lum[y][x] = (r * 299 + g * 587 + b * 114) / 1000
    mean = sum(map(sum, lum)) / 1024 or 1
    for col, (cr, cg, cb) in DYE.items():
        im = base.copy(); px = im.load()
        for y in range(32):
            for x in range(32):
                k = lum[y][x] / mean   # the grain and the seams, carried over
                px[x, y] = (min(255, int(cr * k)), min(255, int(cg * k)), min(255, int(cb * k)), 255)
        key = col + '_painted_planks'
        t = tile(key, im)
        block(key, ' '.join(w.capitalize() for w in col.split('_')) + ' Painted Planks',
              {'hardness': 2, 'tool': 'axe', 'sound': 'wood', 'fuel': 1.5, 'tex': key, 'painted': col}, [t] * 6)
        if key not in rj['tags']['#planks']: rj['tags']['#planks'].append(key)
    if 'paintbrush' not in items:
        items['paintbrush'] = {'key': 'paintbrush', 'name': 'Paintbrush', 'maxStack': 1, 'icon': 'paintbrush', 'paintbrush': True,
                               'tool': {'type': 'brush', 'tier': 0, 'speed': 1, 'damage': 1, 'durability': 128},
                               'desc': 'Hold a dye in your other hand and paint planks, wool, glass or terracotta its colour.'}
    if 'paintbrush' not in tiles:   # a brush: a wooden handle, a bound ferrule, bristles
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for i in range(4, 22):
            for w in (-1, 0, 1):
                x, y = 27 - i + w, i + 1
                if 0 <= x < 32: px[x, y] = (150 - w * 20, 100 - w * 14, 55, 255)   # the handle, lit along one side
        for i in range(22, 25):
            for w in (-2, -1, 0, 1, 2):
                x, y = 27 - i + w, i + 1
                if 0 <= x < 32: px[x, y] = (190, 190, 200, 255) if w else (225, 225, 235, 255)
        for i in range(25, 31):
            for w in (-2, -1, 0, 1, 2):
                x, y = 27 - i + w, i + 1
                if 0 <= x < 32 and 0 <= y < 32: px[x, y] = (60, 50, 45, 255) if abs(w) < 2 else (85, 72, 60, 255)
        rel = 'textures/item/paintbrush.png'; im.save(d / rel)
        tiles['paintbrush'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    recipe('paintbrush', 1, [' W', 'S '], {'W': 'wool', 'S': 'stick'})

    # glass jars: an empty one catches fireflies over grass at night (sim/usables.cpp); a jar of fireflies glows, and
    # opening it lets them go. Pictures: the jar's side and top as the 8 px box samples them (u = x, v = 16 - y)
    if 'glass_jar_side' not in tiles:
        from PIL import Image as _I
        EDGE, HI, RIM = (205, 232, 238, 255), (250, 252, 255, 255), (170, 205, 214, 255)
        im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for y in range(14, 32):
            for x in (8, 23): px[x, y] = EDGE
        for x in range(8, 24): px[x, 31] = RIM; px[x, 14] = RIM; px[x, 15] = RIM
        for y in range(18, 28): px[11, y] = HI
        for y in range(19, 23): px[13, y] = HI
        tile('glass_jar_side', im)
        im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for i in range(8, 24):
            for j in (8, 23): px[i, j] = RIM; px[j, i] = RIM
        tile('glass_jar_top', im)
    if 'glass_jar' not in sj['shapes']:
        sj['shapes']['glass_jar'] = [{'b': [4, 0, 4, 12, 9, 12], 'side': 'glass_jar_side', 'end': 'glass_jar_top'},
                                     {'b': [5, 9, 5, 11, 10.5, 11], 'side': 'spruce_planks', 'end': 'spruce_planks'}]
    for k, nm, lt in (('glass_jar', 'Glass Jar', 0), ('firefly_jar', 'Jar of Fireflies', 9)):
        block(k, nm, {'render': 6, 'opaque': False, 'hardness': 0.3, 'tool': None, 'sound': 'glass', 'light': lt, 'shape': 'glass_jar',
                      'tex': 'glass_jar_side', 'needsSupport': True, 'sink': True, 'jar': 'fireflies' if lt else 'empty'}, [tiles['glass_jar_side']] * 6)
    recipe('glass_jar', 2, [' P ', 'G G', 'GGG'], {'P': '#planks', 'G': 'glass'})

    # the mesh: an iron grate that stops players and creatures, but dropped items fall through (sim/physics.cpp)
    # the motion sensor: gives a redstone signal while something moves within a few blocks of it (sim/redstone.cpp)
    if 'iron_mesh' not in tiles:
        from PIL import Image as _I
        im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for i in range(32):
            for j in range(32):
                bar = i % 8 in (0, 1) or j % 8 in (0, 1)
                if bar: px[i, j] = (150, 152, 158, 255) if (i % 8 == 0 or j % 8 == 0) else (104, 106, 112, 255)
        tile('iron_mesh', im)
        cob = src_pic(d, tiles, files, 'smooth_stone') if 'smooth_stone' in tiles else src_pic(d, tiles, files, 'stone')
        im = recolour(cob, hsv_map(sat=0.3, val=0.62)); px = im.load()
        for i in range(32):
            for j in range(32):
                r = ((i - 15.5) ** 2 + (j - 15.5) ** 2) ** 0.5
                if r < 7.5: px[i, j] = (24, 22, 28, 255) if r > 6 else (40, 36, 44, 255)
                if r < 4.2: px[i, j] = (200, 40, 36, 255) if r > 2 else (255, 120, 90, 255)
        tile('motion_sensor', im)
    block('iron_mesh', 'Iron Mesh', {'render': 2, 'opaque': False, 'hardness': 3, 'sound': 'metal', 'mesh': True, 'tex': 'iron_mesh'}, [tiles['iron_mesh']] * 6)
    block('motion_sensor', 'Motion Sensor', {'hardness': 2, 'sound': 'stone', 'rs': 'plate', 'plate': 'motion', 'tex': 'motion_sensor'}, [tiles['motion_sensor']] * 6)
    recipe('iron_mesh', 4, ['BB', 'BB'], {'B': 'iron_bars'})
    recipe('motion_sensor', 1, ['CCC', 'RQR', 'CCC'], {'C': 'cobblestone', 'R': 'redstone', 'Q': 'quartz'})

    # the sieve (BTA's trommel, by hand): use gravel, sand, dirt, mud, soul sand, ash or cinder sand on it and what
    # was in it falls out below (sim/sieve.cpp)
    if 'sieve' not in sj['shapes']:
        P = 'spruce_planks'
        legs = [{'b': [x, 0, z, x + 2, 10, z + 2], 'side': P, 'end': P} for x in (1, 13) for z in (1, 13)]
        rim = [{'b': [0, 10, 0, 16, 13, 2], 'side': P, 'end': P}, {'b': [0, 10, 14, 16, 13, 16], 'side': P, 'end': P},
               {'b': [0, 10, 2, 2, 13, 14], 'side': P, 'end': P}, {'b': [14, 10, 2, 16, 13, 14], 'side': P, 'end': P}]
        sj['shapes']['sieve'] = legs + rim + [{'b': [2, 11, 2, 14, 11.5, 14], 'side': 'iron_mesh', 'end': 'iron_mesh'}]
    block('sieve', 'Sieve', {'render': 6, 'opaque': False, 'hardness': 1.5, 'tool': 'axe', 'sound': 'wood', 'shape': 'sieve', 'sieve': True,
                             'tex': 'spruce_planks'}, [tiles['spruce_planks']] * 6)
    recipe('sieve', 1, ['PTP', 'S S'], {'P': '#planks', 'T': 'string', 'S': 'stick'})
    if 'hand_sieve' not in tiles:   # the hand sieve: a hoop of wood strung across
        from PIL import Image as _I
        im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for y in range(32):
            for x in range(32):
                r = ((x - 13.5) ** 2 + (y - 13.5) ** 2) ** 0.5
                if 9 < r < 12: px[x, y] = (140, 98, 56, 255) if r < 11 else (104, 70, 40, 255)
                elif r <= 9 and (x % 3 == 0 or y % 3 == 0): px[x, y] = (220, 214, 196, 255)
        for k in range(8): px[min(31, 22 + k), min(31, 22 + k)] = (120, 84, 48, 255); px[min(31, 23 + k), min(31, 22 + k)] = (96, 66, 38, 255)
        rel = 'textures/item/hand_sieve.png'; im.save(d / rel)
        tiles['hand_sieve'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    if 'hand_sieve' not in items:
        items['hand_sieve'] = {'key': 'hand_sieve', 'name': 'Hand Sieve', 'maxStack': 1, 'icon': 'hand_sieve',
                               'desc': 'Use it on gravel, sand, dirt, mud, soul sand, ash or cinder sand to sift it where it lies.'}
    recipe('hand_sieve', 1, [' T ', 'TST', ' T '], {'T': 'string', 'S': 'stick'})

    # sulphur: the crust of the Nether's Sulphur Springs; it crumbles to sulphur, which makes gunpowder with charcoal
    if 'sulphur_block' not in tiles:
        tile('sulphur_block', recolour(src_pic(d, tiles, files, 'sand'), hsv_map(hue=0.15, sat=1.6, val=1.05)))
        im = recolour(src_pic(d, tiles, files, 'glowstone_dust'), hsv_map(hue=0.15, sat=1.4, val=1.0))
        rel = 'textures/item/sulphur.png'; im.save(d / rel)
        tiles['sulphur'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    block('sulphur_block', 'Sulphur', {'hardness': 0.6, 'tool': 'pickaxe', 'sound': 'gravel', 'drop': 'sulphur', 'dropCount': [2, 4], 'tex': 'sulphur_block'},
          [tiles['sulphur_block']] * 6)
    if 'sulphur' not in items:
        items['sulphur'] = {'key': 'sulphur', 'name': 'Sulphur', 'maxStack': 64, 'icon': 'sulphur', 'desc': 'With charcoal it makes gunpowder.'}
    recipe('gunpowder', 2, items_=['sulphur', 'sulphur', 'charcoal'])
    recipe('sulphur_block', 1, ['SS', 'SS'], {'S': 'sulphur'})

    # the outer End: null stone (the dark, void-scarred stone of craters, spire bands and arches) and end moss (a pale
    # violet growth over end stone)
    if 'null_stone' not in tiles:
        tile('null_stone', recolour(src_pic(d, tiles, files, 'end_stone'), hsv_map(hue=0.76, sat=0.55, val=0.38)))
        tile('end_moss', recolour(src_pic(d, tiles, files, 'moss_block'), hsv_map(hue=0.78, sat=0.55, val=1.15)))
    block('null_stone', 'Null Stone', {'hardness': 3, 'tool': 'pickaxe', 'tier': 0, 'tex': 'null_stone'}, [tiles['null_stone']] * 6)
    block('end_moss', 'End Moss', {'hardness': 3, 'tool': 'pickaxe', 'sound': 'grass', 'drop': 'end_stone', 'tex': {'top': 'end_moss', 'side': 'end_stone', 'bottom': 'end_stone'}},
          [tiles['end_stone'], tiles['end_stone'], tiles['end_moss'], tiles['end_stone'], tiles['end_stone'], tiles['end_stone']])

    # the Skylands (our own take on the Aether): three clouds (a soft puff that breaks any fall, a blue one that
    # throws you up, a golden one that lets you float down), glimmersand (slick pale sand at the islands' rims), the
    # rare sunwood tree with golden leaves, and sky blooms
    if 'bounce_cloud' not in tiles:
        tile('cloud_puff_block', recolour(src_pic(d, tiles, files, 'snow'), lambda r, g, b, a, x, y: (min(255, r + 6), min(255, g + 6), min(255, b + 10), a)))
        tile('bounce_cloud', recolour(src_pic(d, tiles, files, 'snow'), lambda r, g, b, a, x, y: (r * 150 // 255, g * 200 // 255, b, a)))
        tile('sun_cloud', recolour(src_pic(d, tiles, files, 'snow'), lambda r, g, b, a, x, y: (r, g * 215 // 255, b * 120 // 255, a)))
        tile('glimmersand', recolour(src_pic(d, tiles, files, 'sand'), hsv_map(hue=0.13, sat=0.35, val=1.18)))
        lv = src_pic(d, tiles, files, 'leaves')
        tile('sunwood_leaves', recolour(lv, lambda r, g, b, a, x, y: (min(255, 120 + g * 135 // 255), min(255, 90 + g * 120 // 255), 30 + b // 6, 255 if a else 0)))
        tile('sunwood_log', recolour(src_pic(d, tiles, files, 'birch_log_side'), hsv_map(hue=0.1, sat=0.8, val=0.95)))
        tile('sky_bloom', recolour(src_pic(d, tiles, files, 'allium'), hsv_map(hue=0.58, sat=0.7, val=1.15)))
    cloud = {'opaque': False, 'render': 2, 'hardness': 0.3, 'tool': None, 'sound': 'wool', 'cloud': True, 'selfCull': True}
    block('cloud_puff', 'Cloud Puff', dict(cloud, tex='cloud_puff_block'), [tiles['cloud_puff_block']] * 6)
    block('bounce_cloud', 'Bounce Cloud', dict(cloud, tex='bounce_cloud', bouncy=True), [tiles['bounce_cloud']] * 6)
    block('sun_cloud', 'Sun Cloud', dict(cloud, tex='sun_cloud', light=6, sunCloud=True), [tiles['sun_cloud']] * 6)
    block('glimmersand', 'Glimmersand', {'hardness': 0.5, 'tool': 'shovel', 'sound': 'sand', 'slippery': True, 'tex': 'glimmersand'}, [tiles['glimmersand']] * 6)
    block('sunwood_log', 'Sunwood Log', {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5,
                                         'tex': {'top': 'birch_log_top', 'bottom': 'birch_log_top', 'side': 'sunwood_log'}},
          [tiles['sunwood_log'], tiles['sunwood_log'], tiles['birch_log_top'], tiles['birch_log_top'], tiles['sunwood_log'], tiles['sunwood_log']])
    block('sunwood_leaves', 'Sunwood Leaves', {'render': 2, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'lightAtt': 1, 'light': 4,
                                               'isLeaves': True, 'drop': 'stick', 'tex': 'sunwood_leaves'}, [tiles['sunwood_leaves']] * 6)
    block('sky_bloom', 'Sky Bloom', {'render': 3, 'solid': False, 'opaque': False, 'hardness': 0, 'tool': None, 'sound': 'grass', 'needsSupport': True,
                                     'tex': 'sky_bloom'}, [tiles['sky_bloom']] * 6)

    # coloured lamps (a lamp and a dye), the inverted lamp (shines while unpowered), metal bricks, the butterfly jar
    for col, rgb in DYE.items():
        k = col + '_lamp'
        if k + '_off_pic' not in tiles:
            for on in (False, True):
                srcp = src_pic(d, tiles, files, 'redstone_lamp_on' if on else 'redstone_lamp')
                tint = (lambda c, o: (lambda r, g, b, a, x, y: (min(255, r * c[0] // 200), min(255, g * c[1] // 200), min(255, b * c[2] // 200), a)))(rgb, on)
                tile(k + ('_on_pic' if on else '_off_pic'), recolour(srcp, tint))
        block(k, col.replace('_', ' ').title() + ' Lamp', {'hardness': 0.3, 'tool': None, 'sound': 'glass', 'rs': 'lamp', 'lampOn': k + '_on', 'tex': k + '_off_pic'}, [tiles[k + '_off_pic']] * 6)
        block(k + '_on', col.replace('_', ' ').title() + ' Lamp', {'hardness': 0.3, 'tool': None, 'sound': 'glass', 'rs': 'lamp', 'light': 15, 'drop': k, 'tex': k + '_on_pic'}, [tiles[k + '_on_pic']] * 6, item=False)
        recipe(k, 1, items_=['redstone_lamp', col + '_dye'])
    block('inverted_lamp', 'Inverted Lamp', {'hardness': 0.3, 'tool': None, 'sound': 'glass', 'rs': 'lamp', 'lampOn': 'inverted_lamp_on', 'inverted': True, 'tex': 'redstone_lamp'}, [tiles['redstone_lamp']] * 6)
    block('inverted_lamp_on', 'Inverted Lamp', {'hardness': 0.3, 'tool': None, 'sound': 'glass', 'rs': 'lamp', 'light': 15, 'drop': 'inverted_lamp', 'inverted': True, 'tex': 'redstone_lamp_on'}, [tiles['redstone_lamp_on']] * 6, item=False)
    recipe('inverted_lamp', 1, items_=['redstone_lamp', 'redstone_torch'])
    for metal, blk, rgb in (('iron', 'iron_block', (220, 220, 225)), ('gold', 'gold_block', (250, 210, 80)), ('lapis', 'lapis_block', (60, 90, 200)), ('steel', 'steel_block', (150, 160, 175))):
        k = metal + '_bricks'
        if k not in tiles:
            tile(k, recolour(src_pic(d, tiles, files, 'stone_bricks'), (lambda c: (lambda r, g, b, a, x, y: (min(255, r * c[0] // 150), min(255, g * c[1] // 150), min(255, b * c[2] // 150), a)))(rgb)))
        block(k, metal.title() + ' Bricks', {'hardness': 4, 'tool': 'pickaxe', 'tier': 1, 'sound': 'metal', 'tex': k}, [tiles[k]] * 6)
        recipe(k, 4, ['BB', 'BB'], {'B': blk})
    block('butterfly_jar', 'Jar of Butterflies', {'render': 6, 'opaque': False, 'hardness': 0.3, 'tool': None, 'sound': 'glass', 'shape': 'glass_jar',
                                                 'tex': 'glass_jar_side', 'needsSupport': True, 'sink': True, 'jar': 'butterflies'}, [tiles['glass_jar_side']] * 6)

    # the glass door and trapdoor (a steel frame round clear panes); bluebells, marigolds and spinifex; the sugar cane
    # bundle (a thatch block)
    from PIL import Image as _I
    def pane(trap=False):
        im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for y in range(32):
            for x in range(32):
                edge = x < 3 or x > 28 or y < 3 or y > 28 or (trap and (x in (15, 16) or y in (15, 16)))
                if edge: px[x, y] = (170, 176, 186, 255) if (x + y) % 7 else (130, 136, 146, 255)
                elif (x - y) % 11 == 0 and 6 < x < 26: px[x, y] = (222, 240, 250, 255)   # a glint across the pane
        return im
    if 'glass_door_lower' not in tiles:
        tile('glass_door_lower', pane()); tile('glass_door_upper', pane()); tile('glass_trapdoor', pane(True))
        ic = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); ip = ic.load(); low = pane().load()
        for y in range(32):
            for x in range(8, 24):
                c = low[(x - 8) * 2, y]
                if c[3]: ip[x, y] = c
        rel = 'textures/item/glass_door_item.png'; ic.save(d / rel)
        tiles['glass_door_item'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        tile('bluebells', recolour(src_pic(d, tiles, files, 'cornflower'), hsv_map(hue=0.66, sat=1.1, val=1.0)))
        tile('marigold', recolour(src_pic(d, tiles, files, 'flower_yellow'), hsv_map(hue=0.08, sat=1.3, val=1.05)))
        tile('spinifex', recolour(src_pic(d, tiles, files, 'dry_grass'), hsv_map(hue=0.12, sat=0.8, val=1.1)))
        tile('sugar_cane_bundle_side', recolour(src_pic(d, tiles, files, 'hay_side'), hsv_map(hue=0.25, sat=0.9, val=1.0)))
        tile('sugar_cane_bundle_top', recolour(src_pic(d, tiles, files, 'hay_top'), hsv_map(hue=0.25, sat=0.9, val=1.0)))
    block('glass_door', 'Glass Door', {'render': 7, 'opaque': False, 'hardness': 1, 'tool': None, 'sound': 'glass', 'shapeKind': 7, 'door': True,
                                       'tex': {'side': 'glass_door_lower', 'top': 'glass_door_lower'}, 'texUpper': 'glass_door_upper',
                                       'upperTile': tiles['glass_door_upper'], 'itemIcon': 'glass_door_item'}, [tiles['glass_door_lower']] * 6)
    items['glass_door'].update({'icon': 'glass_door_item', 'maxStack': 1})
    block('glass_trapdoor', 'Glass Trapdoor', {'render': 7, 'opaque': False, 'hardness': 1, 'tool': None, 'sound': 'glass', 'shapeKind': 8,
                                               'wlog': True, 'trapdoor': True, 'tex': 'glass_trapdoor'}, [tiles['glass_trapdoor']] * 6)
    recipe('glass_door', 1, ['GG', 'GG', 'GG'], {'G': 'glass'})
    recipe('glass_trapdoor', 2, ['GGG', 'GGG'], {'G': 'glass'})
    # the steel set (BTA's steel tier pieces): a steel door and trapdoor (opened by redstone, like iron, but no blast
    # breaks them), a steel fence, and steel-framed glass (clear, and blast-proof)
    steel = hsv_map(hue=0.6, sat=0.35, val=0.82)
    if 'steel_door_lower' not in tiles:
        tile('steel_door_lower', recolour(src_pic(d, tiles, files, 'iron_door_lower'), steel))
        tile('steel_door_upper', recolour(src_pic(d, tiles, files, 'iron_door_upper'), steel))
        tile('steel_trapdoor', recolour(src_pic(d, tiles, files, 'iron_trapdoor'), steel))
        fg = src_pic(d, tiles, files, 'glass').convert('RGBA'); px = fg.load()
        for y in range(32):
            for x in range(32):
                if x < 2 or x > 29 or y < 2 or y > 29 or x in (15, 16) or y in (15, 16): px[x, y] = (128, 138, 152, 255) if (x * 3 + y) % 9 else (100, 110, 124, 255)
        tile('steel_framed_glass', fg)
        ic = recolour(src_pic(d, tiles, files, 'iron_door_item'), steel)
        rel = 'textures/item/steel_door_item.png'; ic.save(d / rel)
        tiles['steel_door_item'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    block('steel_door', 'Steel Door', {'render': 7, 'opaque': False, 'hardness': 7, 'tool': 'pickaxe', 'tier': 2, 'sound': 'metal', 'shapeKind': 7, 'door': True,
                                       'blastproof': True, 'rsOnly': True, 'tex': {'side': 'steel_door_lower', 'top': 'steel_door_lower'}, 'texUpper': 'steel_door_upper',
                                       'upperTile': tiles['steel_door_upper'], 'itemIcon': 'steel_door_item'}, [tiles['steel_door_lower']] * 6)
    items['steel_door'].update({'icon': 'steel_door_item', 'maxStack': 1})
    block('steel_trapdoor', 'Steel Trapdoor', {'render': 7, 'opaque': False, 'hardness': 7, 'tool': 'pickaxe', 'tier': 2, 'sound': 'metal', 'shapeKind': 8,
                                               'wlog': True, 'trapdoor': True, 'blastproof': True, 'rsOnly': True, 'tex': 'steel_trapdoor'}, [tiles['steel_trapdoor']] * 6)
    block('steel_fence', 'Steel Fence', {'render': 7, 'opaque': False, 'hardness': 6, 'tool': 'pickaxe', 'tier': 2, 'sound': 'metal', 'base': 'steel_block',
                                         'shapeKind': 3, 'wlog': True, 'family': 'fence', 'blastproof': True, 'tex': 'steel_block'}, [tiles['steel_block']] * 6)
    block('steel_framed_glass', 'Steel-Framed Glass', {'render': 2, 'opaque': False, 'hardness': 5, 'tool': 'pickaxe', 'tier': 2, 'sound': 'glass',
                                                       'blastproof': True, 'tex': 'steel_framed_glass'}, [tiles['steel_framed_glass']] * 6)
    recipe('steel_door', 1, ['SS', 'SS', 'SS'], {'S': 'steel_ingot'})
    recipe('steel_trapdoor', 1, ['SS', 'SS'], {'S': 'steel_ingot'})
    recipe('steel_fence', 3, ['SIS', 'SIS'], {'S': 'steel_ingot', 'I': 'iron_ingot'})
    recipe('steel_framed_glass', 4, ['GSG', 'SGS', 'GSG'], {'G': 'glass', 'S': 'steel_ingot'})
    for k, nm in (('bluebells', 'Bluebells'), ('marigold', 'Marigold'), ('spinifex', 'Spinifex')):
        block(k, nm, {'render': 3, 'solid': False, 'opaque': False, 'hardness': 0, 'tool': None, 'sound': 'grass', 'needsSupport': True,
                      'flower': k != 'spinifex', 'tex': k}, [tiles[k]] * 6)
    block('sugar_cane_bundle', 'Sugar Cane Bundle', {'hardness': 0.5, 'tool': None, 'sound': 'grass', 'axisBlock': True, 'fuel': 4,
                                                     'tex': {'top': 'sugar_cane_bundle_top', 'bottom': 'sugar_cane_bundle_top', 'side': 'sugar_cane_bundle_side'}},
          [tiles['sugar_cane_bundle_side'], tiles['sugar_cane_bundle_side'], tiles['sugar_cane_bundle_top'], tiles['sugar_cane_bundle_top'], tiles['sugar_cane_bundle_side'], tiles['sugar_cane_bundle_side']])
    recipe('sugar_cane_bundle', 1, ['SSS', 'SSS', 'SSS'], {'S': 'sugar_cane'})
    recipe('sugar_cane', 9, items_=['sugar_cane_bundle'])

    # palms (BTA's palm trees): warm beaches and the thornscrub; a leaning ringed trunk, a crown of drooping fronds,
    # coconuts falling when one is felled (sim/controller.cpp timber)
    if 'palm_log_side' not in tiles:
        side = recolour(src_pic(d, tiles, files, 'jungle_log_side'), hsv_map(hue=0.09, sat=0.55, val=1.15))
        px = side.load()
        for y in range(0, 32, 6):   # the rings of old fronds
            for x in range(32):
                r, g, b, a = px[x, y]
                if a: px[x, y] = (r * 3 // 4, g * 3 // 4, b * 3 // 4, a)
        tile('palm_log_side', side)
        tile('palm_log_top', recolour(src_pic(d, tiles, files, 'jungle_log_top'), hsv_map(hue=0.09, sat=0.55, val=1.15)))
        tile('palm_leaves', recolour(src_pic(d, tiles, files, 'jungle_leaves'), hsv_map(val=1.12)))
        tile('palm_planks', recolour(src_pic(d, tiles, files, 'birch_planks'), hsv_map(hue=0.1, sat=1.25, val=0.98)))
        tile('palm_sapling', recolour(src_pic(d, tiles, files, 'jungle_sapling'), hsv_map(dh=-0.03, val=1.1)))
        co = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); cp = co.load()
        for y in range(32):
            for x in range(32):
                dx, dy = x - 15.5, y - 16.5
                if dx * dx + dy * dy <= 110:
                    shade = 1.0 - (dx + dy) / 40.0
                    cp[x, y] = (int(min(255, 112 * shade)), int(min(255, 72 * shade)), int(min(255, 40 * shade)), 255)
                    if (x * 7 + y * 3) % 11 == 0: cp[x, y] = (86, 54, 30, 255)   # husk fibres
        for x, y in ((12, 9), (16, 8), (20, 10)): cp[x, y] = (60, 38, 22, 255)   # the three eyes
        rel = 'textures/item/coconut.png'; co.save(d / rel)
        tiles['coconut'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    block('palm_log', 'Palm Log', {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5,
                                   'tex': {'top': 'palm_log_top', 'bottom': 'palm_log_top', 'side': 'palm_log_side'}},
          [tiles['palm_log_side'], tiles['palm_log_side'], tiles['palm_log_top'], tiles['palm_log_top'], tiles['palm_log_side'], tiles['palm_log_side']])
    block('palm_leaves', 'Palm Leaves', {'render': 2, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'lightAtt': 1,
                                         'isLeaves': True, 'tint': 'foliage', 'sapling': 'palm_sapling', 'tex': 'palm_leaves'}, [tiles['palm_leaves']] * 6)
    block('palm_planks', 'Palm Planks', {'hardness': 2, 'tool': 'axe', 'sound': 'wood', 'fuel': 1.5, 'tex': 'palm_planks'}, [tiles['palm_planks']] * 6)
    block('palm_sapling', 'Palm Sapling', {'render': 3, 'solid': False, 'opaque': False, 'hardness': 0, 'tool': None, 'sound': 'grass',
                                           'needsSupport': True, 'fuel': 0.5, 'tree': 'palm', 'tex': 'palm_sapling'}, [tiles['palm_sapling']] * 6)
    items['coconut'] = {'key': 'coconut', 'name': 'Coconut', 'maxStack': 64, 'icon': 'coconut', 'food': {'heal': 3}, 'desc': 'Falls from a felled palm.'}
    recipe('palm_planks', 4, items_=['palm_log'])
    # the quiver (BTA's): a leather case of arrows (sim/quiver.cpp)
    if 'quiver' not in tiles:
        qv = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); qp = qv.load()
        for k in range(22):   # the case, slanting up to the right
            cx, cy = 9 + k * 0.55, 28 - k
            for w in range(-4, 5):
                x, y = int(cx + w * 0.8), int(cy + w * 0.45)
                if 0 <= x < 32 and 0 <= y < 32:
                    edge = abs(w) == 4 or k in (0, 21)
                    band = k in (5, 6, 15, 16)
                    qp[x, y] = (70, 42, 22, 255) if edge else (92, 58, 30, 255) if band else (140, 88, 46, 255) if w < 1 else (118, 72, 38, 255)
        for i, (fx, fy) in enumerate(((19, 4), (22, 3), (25, 5))):   # three arrows' fletching out of the top
            for t in range(5):
                for dx in (-1, 0, 1):
                    x, y = fx + dx + t // 2, fy + t
                    if 0 <= x < 32 and 0 <= y < 32 and not qp[x, y][3]: qp[x, y] = (236, 236, 236, 255) if dx else (90, 70, 50, 255)
        rel = 'textures/item/quiver.png'; qv.save(d / rel)
        tiles['quiver'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    items['quiver'] = {'key': 'quiver', 'name': 'Quiver', 'maxStack': 1, 'icon': 'quiver', 'desc': 'Holds up to 192 arrows. Use it to gather them; crouch and use it to tip them out.'}
    recipe('quiver', 1, ['LS', 'LL', 'LL'], {'L': 'leather', 'S': 'string'})
    # thorn leaves: the Thornscrub's thorn trees (BTA's Caatinga); they prick like cactus
    if 'thorn_leaves' not in tiles:
        im = recolour(src_pic(d, tiles, files, 'acacia_leaves'), lambda r, g, b, a, x, y: (r * 150 // 255, g * 140 // 255, b * 70 // 255, 255 if a else 0))
        px = im.load()
        for k in range(24):   # pale spines among the leaves
            x, y = (k * 13 + 5) % 32, (k * 7 + 11) % 32
            if px[x, y][3]: px[x, y] = (226, 214, 170, 255)
        tile('thorn_leaves', im)
    block('thorn_leaves', 'Thorn Leaves', {'render': 2, 'opaque': False, 'hardness': 0.3, 'tool': None, 'sound': 'leaves', 'lightAtt': 1,
                                           'isLeaves': True, 'thorns': True, 'drop': 'stick', 'tex': 'thorn_leaves'}, [tiles['thorn_leaves']] * 6)
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, separators=(',', ':'), ensure_ascii=False))
    sh = json.loads((d / 'itemshapes.json').read_text())
    if 'hand_sieve' not in sh['itemTiles']: sh['itemTiles'].append('hand_sieve'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'steel_door_item' not in sh['itemTiles']: sh['itemTiles'].append('steel_door_item'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'glass_door_item' not in sh['itemTiles']: sh['itemTiles'].append('glass_door_item'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'coconut' not in sh['itemTiles']: sh['itemTiles'].append('coconut'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'quiver' not in sh['itemTiles']: sh['itemTiles'].append('quiver'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'sulphur' not in sh['itemTiles']: sh['itemTiles'].append('sulphur'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'paintbrush' not in sh['itemTiles']: sh['itemTiles'].append('paintbrush'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    print('bta blocks ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
