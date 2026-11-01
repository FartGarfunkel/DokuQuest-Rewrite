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
    # the rift core: set in the middle of a Wither's T between two wither skeleton skulls, it wakes the Wither Storm
    if 'rift_core' not in tiles:
        tile('rift_core', recolour(src_pic(d, tiles, files, 'crying_obsidian' if 'crying_obsidian' in tiles else 'obsidian'), hsv_map(hue=0.78, sat=1.2, val=1.15)))
    block('rift_core', 'Rift Core', {'hardness': 10, 'tool': 'pickaxe', 'tier': 3, 'sound': 'stone', 'light': 7, 'tex': 'rift_core'}, [tiles['rift_core']] * 6)
    items['rift_core'].update({'rarity': 'epic', 'desc': 'Humming with the rift. Set it in the middle of a Wither\'s T, between two wither skeleton skulls.'})
    recipe('rift_core', 1, ['CCC', 'CNC', 'CCC'], {'C': 'rift_crystal', 'N': 'nether_star'})
    # the statue (BTA's): a stone plinth; a spawn egg used on it carves that creature standing on it (render/statue_layer.cpp)
    if 'statue_side' not in tiles:
        tile('statue_side', recolour(src_pic(d, tiles, files, 'chiseled_stone_bricks' if 'chiseled_stone_bricks' in tiles else 'stone_bricks'), hsv_map(val=1.05)))
        tile('statue_top', recolour(src_pic(d, tiles, files, 'smooth_stone' if 'smooth_stone' in tiles else 'stone'), hsv_map(val=1.08)))
    block('statue', 'Statue', {'hardness': 2, 'tool': 'pickaxe', 'sound': 'stone', 'statue': True,
                               'tex': {'top': 'statue_top', 'bottom': 'statue_top', 'side': 'statue_side'}},
          [tiles['statue_side'], tiles['statue_side'], tiles['statue_top'], tiles['statue_top'], tiles['statue_side'], tiles['statue_side']])
    items['statue']['desc'] = 'A plinth: use a spawn egg on it to carve that creature in stone. Crouch with an empty hand to clear it.'
    recipe('statue', 1, ['SSS', ' C ', 'SSS'], {'S': 'smooth_stone' if 'smooth_stone' in items else 'stone', 'C': 'chiseled_stone_bricks' if 'chiseled_stone_bricks' in items else 'stone_bricks'})
    # the painting: a framed canvas whose picture is painted by render/painting_layer.cpp (use it to turn to the next)
    if 'painting_frame' not in tiles:
        pf = recolour(src_pic(d, tiles, files, 'dark_oak_planks'), hsv_map(val=0.95)); pp = pf.load()
        for y in range(32):
            for x in range(32):
                if 3 <= x <= 28 and 3 <= y <= 28: pp[x, y] = (46, 40, 34, 255)
                elif x in (2, 29) or y in (2, 29): pp[x, y] = (200, 160, 70, 255)   # a gilt fillet
        tile('painting_frame', pf)
        ic = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); ip = ic.load()
        for y in range(4, 28):
            for x in range(3, 29):
                edge = x in (3, 4, 27, 28) or y in (4, 5, 26, 27)
                if edge: ip[x, y] = (92, 60, 32, 255)
                else:
                    hill = 19 + int(2.5 * __import__('math').sin(x * 0.4))
                    ip[x, y] = (88, 140, 60, 255) if y >= hill else (140, 190, 230, 255) if y > 10 else (170, 210, 240, 255)
        for x, y in ((21, 9), (22, 9), (21, 10), (22, 10)): ip[x, y] = (250, 236, 150, 255)
        rel = 'textures/item/painting_item.png'; ic.save(d / rel)
        tiles['painting_item'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    block('painting', 'Painting', {'render': 7, 'solid': False, 'opaque': False, 'hardness': 0.3, 'tool': None, 'sound': 'wood', 'shapeKind': 18,
                                   'painting': True, 'tex': 'painting_frame', 'itemIcon': 'painting_item', 'fuel': 0.5}, [tiles['painting_frame']] * 6)
    items['painting'].update({'icon': 'painting_item'})
    recipe('painting', 1, ['SSS', 'SWS', 'SSS'], {'S': 'stick', 'W': '#wool'})
    # tree branches (the idea is Cubyz's): slim bark stems that join to logs, other branches and leaves (world/shapes.cpp
    # ShapeKind::Branch = 37); the trees grow them (gen/trees.cpp branch()); they burn and can be built with
    for wood, logk in (('oak', 'log'), ('birch', 'birch_log'), ('spruce', 'spruce_log'), ('jungle', 'jungle_log'), ('acacia', 'acacia_log'),
                       ('dark_oak', 'dark_oak_log'), ('cherry', 'cherry_log'), ('mangrove', 'mangrove_log'), ('pine', 'pine_log'), ('aspen', 'aspen_log'), ('palm', 'palm_log')):
        side = logk + '_side' if logk != 'log' else 'log_side'
        if side not in tiles: continue
        k = wood + '_branch'
        block(k, wood.replace('_', ' ').title() + ' Branch', {'render': 7, 'opaque': False, 'hardness': 1.2, 'tool': 'axe', 'sound': 'log', 'shapeKind': 37,
                                                               'branch': True, 'isLog': True, 'fuel': 1, 'tex': side}, [tiles[side]] * 6)
        recipe(k, 4, ['L', 'L'], {'L': logk})
    # the activator (BTA's): a dispenser that uses what it holds on the block in front (sim/redstone.cpp dispense)
    if 'activator_front' not in tiles:
        hand = lambda r, g, b_, a, x, y: (min(255, b_ + 40), min(255, g + 10), r // 2 + 40, a) if 9 <= x <= 22 and 9 <= y <= 22 else (r, g, b_, a)
        tile('activator_front', recolour(src_pic(d, tiles, files, 'dispenser_front'), hand))
        tile('activator_front_vertical', recolour(src_pic(d, tiles, files, 'dispenser_front_vertical'), hand))
    disp = next(x for x in blocks if x['key'] == 'dispenser')
    f6 = dict(disp.get('f6', {})); f6['front'] = tiles['activator_front']; f6['frontV'] = tiles['activator_front_vertical']
    block('activator', 'Activator', {'hardness': 3.5, 'tool': 'pickaxe', 'sound': 'stone', 'tex': {'front': 'activator_front', 'frontV': 'activator_front_vertical', 'side': 'furnace_side', 'top': 'furnace_top'},
                                     'facing6': True, 'container': 9, 'rs': 'dispenser', 'activator': True, 'f6': f6},
          [tiles['furnace_side'], tiles['furnace_side'], tiles['furnace_top'], tiles['furnace_top'], tiles['activator_front'], tiles['furnace_side']])
    recipe('activator', 1, ['CCC', 'CLC', 'CRC'], {'C': 'cobblestone', 'L': 'lever', 'R': 'redstone'})
    # powder weapons (sim/guns.cpp): the handcannon and its cannonballs, the rocket launcher and its rockets
    if 'handcannon' not in tiles:
        def canvas(): return _I.new('RGBA', (32, 32), (0, 0, 0, 0))
        def save_item(name, im):
            rel = 'textures/item/%s.png' % name; im.save(d / rel)
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        hc = canvas(); hp = hc.load()
        for k in range(20):   # the barrel, slanting up to the right
            for w in range(-2, 3):
                x, y = 9 + k, 19 - k // 2 + w
                if 0 <= x < 32 and 0 <= y < 32: hp[x, y] = (40, 40, 46, 255) if abs(w) == 2 else (92, 94, 104, 255) if w < 0 else (66, 68, 76, 255)
        for x in range(27, 30):
            for y in range(7, 13): hp[x, y] = (30, 30, 34, 255)   # the muzzle
        for k in range(9):   # the wooden grip
            for w in range(-1, 2):
                x, y = 9 + w + k // 3, 19 + k
                if 0 <= x < 32 and 0 <= y < 32: hp[x, y] = (110, 70, 36, 255) if w else (138, 92, 50, 255)
        save_item('handcannon', hc)
        rl = canvas(); rp = rl.load()
        for k in range(26):
            for w in range(-3, 4):
                x, y = 3 + k, 24 - k * 3 // 4 + w
                if 0 <= x < 32 and 0 <= y < 32: rp[x, y] = (40, 46, 54, 255) if abs(w) == 3 else (140, 150, 168, 255) if w < 0 else (104, 114, 130, 255)
        for x, y in ((12, 22), (13, 22), (12, 23), (13, 23), (12, 24), (13, 25)): rp[x, y] = (60, 40, 24, 255)   # the handle
        save_item('rocket_launcher', rl)
        cb = canvas(); cp = cb.load()
        for y in range(32):
            for x in range(32):
                dx, dy = x - 15.5, y - 16.5
                if dx * dx + dy * dy <= 64: sh = 1 - (dx + dy) / 24; cp[x, y] = (int(min(255, 60 * sh)), int(min(255, 60 * sh)), int(min(255, 68 * sh)), 255)
        save_item('cannonball', cb)
        rk = canvas(); kp = rk.load()
        for k in range(18):
            for w in range(-2, 3):
                x, y = 7 + k, 24 - k + w
                if 0 <= x < 32 and 0 <= y < 32: kp[x, y] = (200, 40, 36, 255) if k > 13 else (226, 222, 210, 255) if w < 1 else (184, 180, 168, 255)
        for x, y in ((6, 22), (5, 23), (6, 26), (9, 27), (8, 25)): kp[x, y] = (240, 160, 40, 255)   # the flame
        save_item('rocket', rk)
    items['handcannon'] = {'key': 'handcannon', 'name': 'Handcannon', 'maxStack': 1, 'icon': 'handcannon', 'tool': {'type': 'gun', 'tier': 0, 'speed': 1, 'damage': 2, 'durability': 250},
                           'desc': 'Fires cannonballs that burst where they land without breaking blocks. Fire at your feet to fling yourself.'}
    items['rocket_launcher'] = {'key': 'rocket_launcher', 'name': 'Rocket Launcher', 'maxStack': 1, 'icon': 'rocket_launcher', 'tool': {'type': 'gun', 'tier': 0, 'speed': 1, 'damage': 2, 'durability': 120},
                                'desc': 'Fires rockets that fly straight and blast what they hit.'}
    items['cannonball'] = {'key': 'cannonball', 'name': 'Cannonball', 'maxStack': 64, 'icon': 'cannonball'}
    items['rocket'] = {'key': 'rocket', 'name': 'Rocket', 'maxStack': 16, 'icon': 'rocket'}
    recipe('handcannon', 1, ['I  ', 'IF ', ' IP'], {'I': 'iron_ingot', 'F': 'flint', 'P': 'planks'})
    recipe('rocket_launcher', 1, ['S  ', ' SR', '  S'], {'S': 'steel_ingot', 'R': 'redstone'})
    recipe('cannonball', 4, items_=['iron_ingot', 'gunpowder'])
    recipe('rocket', 2, items_=['firework_rocket', 'gunpowder', 'iron_ingot'])
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
    if 'golden_quiver' not in tiles:
        gq = recolour(_I.open(d / files[tiles['quiver']]).convert('RGBA'), lambda r, g, b_, a, x, y: (min(255, r + 90), min(255, g + 70), b_ // 3, a) if (r, g, b_) != (236, 236, 236) else (r, g, b_, a))
        rel = 'textures/item/golden_quiver.png'; gq.save(d / rel)
        tiles['golden_quiver'] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    items['golden_quiver'] = {'key': 'golden_quiver', 'name': 'Golden Quiver', 'maxStack': 1, 'icon': 'golden_quiver', 'rarity': 'rare',
                              'desc': 'Never runs dry. Its arrows hit a little softer and break where they land.'}
    recipe('golden_quiver', 1, ['GQG', 'GAG', 'GGG'], {'G': 'gold_ingot', 'Q': 'quiver', 'A': 'arrow'})
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
    # BTA's rarer ground and wood: scorched dirt of the Redlands (the rich kind sifts out raw metal), ice that never
    # melts in the glaciers, ember coal in the Depths, moss-grown logs and algae in the Mossmire, dried cane bundles
    if 'scorched_dirt' not in tiles:
        tile('scorched_dirt', recolour(src_pic(d, tiles, files, 'dirt'), hsv_map(hue=0.06, sat=1.25, val=1.12)))
        rich = recolour(src_pic(d, tiles, files, 'dirt'), hsv_map(hue=0.06, sat=1.25, val=1.0)); rp = rich.load()
        for k in range(30):
            x, y = (k * 11 + 3) % 32, (k * 17 + 7) % 32
            rp[x, y] = (214, 170, 96, 255) if k % 3 else (196, 196, 204, 255)   # flecks of metal
        tile('rich_scorched_dirt', rich)
        tile('perma_ice', recolour(src_pic(d, tiles, files, 'packed_ice'), hsv_map(sat=0.7, val=0.9, add=6)))
        eco = recolour(src_pic(d, tiles, files, 'coal_ore'), lambda r, g, b, a, x, y: (min(255, r + 70), g + 10, max(0, b - 10), a) if max(r, g, b) < 70 else (r * 120 // 100, g * 70 // 100, b * 60 // 100, a))
        tile('ember_coal_ore', eco)
        for w in ('dark_oak', 'mangrove'):
            im = src_pic(d, tiles, files, w + '_log_side'); px = im.load()
            for y in range(32):
                for x in range(32):
                    r, g, b, a = px[x, y]
                    if ((x * 7 + y * 13) % 11 < 4 and y > 6 + (x * 5) % 9) or y > 26 - (x * 3) % 5: px[x, y] = (r * 50 // 100 + 30, g * 60 // 100 + 70, b * 40 // 100 + 20, a)
            tile('mossy_%s_log_side' % w, im)
        tile('algae', recolour(src_pic(d, tiles, files, 'lily_pad'), hsv_map(hue=0.22, sat=0.8, val=0.85)))
        tile('dried_sugar_cane_bundle_side', recolour(src_pic(d, tiles, files, 'sugar_cane_bundle_side'), hsv_map(hue=0.13, sat=0.75, val=1.1)))
        tile('dried_sugar_cane_bundle_top', recolour(src_pic(d, tiles, files, 'sugar_cane_bundle_top'), hsv_map(hue=0.13, sat=0.75, val=1.1)))
    block('scorched_dirt', 'Scorched Dirt', {'hardness': 0.6, 'tool': 'shovel', 'sound': 'dirt', 'tex': 'scorched_dirt'}, [tiles['scorched_dirt']] * 6)
    block('rich_scorched_dirt', 'Rich Scorched Dirt', {'hardness': 0.7, 'tool': 'shovel', 'sound': 'dirt', 'tex': 'rich_scorched_dirt'}, [tiles['rich_scorched_dirt']] * 6)
    block('perma_ice', 'Perma-Ice', {'hardness': 0.6, 'sound': 'glass', 'slippery': True, 'noMelt': True, 'tex': 'perma_ice'}, [tiles['perma_ice']] * 6)
    block('ember_coal_ore', 'Ember Coal Ore', {'hardness': 3, 'tier': 1, 'drop': 'ember_coal', 'xp': 2, 'light': 3, 'tex': 'ember_coal_ore'}, [tiles['ember_coal_ore']] * 6)
    if 'ember_coal' not in items:
        items['ember_coal'] = {'key': 'ember_coal', 'name': 'Ember Coal', 'maxStack': 64, 'icon': 'coal', 'fuel': 14,
                               'desc': 'Coal from the Depths. It burns hotter and longer than the ordinary kind.'}
    for w, nm in (('dark_oak', 'Dark Oak'), ('mangrove', 'Mangrove')):
        k = 'mossy_%s_log' % w
        block(k, 'Mossy %s Log' % nm, {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5,
                                       'tex': {'top': w + '_log_top', 'bottom': w + '_log_top', 'side': k + '_side'}},
              [tiles[k + '_side'], tiles[k + '_side'], tiles[w + '_log_top'], tiles[w + '_log_top'], tiles[k + '_side'], tiles[k + '_side']])
    block('algae', 'Algae', {'render': 7, 'solid': True, 'opaque': False, 'hardness': 0, 'tool': None, 'sound': 'grass', 'drop': 'algae',
                             'tint': 'foliage', 'shapeKind': 10, 'onWater': True, 'tex': 'algae'}, [tiles['algae']] * 6)
    block('dried_sugar_cane_bundle', 'Dried Sugar Cane Bundle', {'hardness': 0.5, 'tool': None, 'sound': 'grass', 'axisBlock': True, 'fuel': 6,
                                                                 'tex': {'top': 'dried_sugar_cane_bundle_top', 'bottom': 'dried_sugar_cane_bundle_top', 'side': 'dried_sugar_cane_bundle_side'}},
          [tiles['dried_sugar_cane_bundle_side'], tiles['dried_sugar_cane_bundle_side'], tiles['dried_sugar_cane_bundle_top'], tiles['dried_sugar_cane_bundle_top'],
           tiles['dried_sugar_cane_bundle_side'], tiles['dried_sugar_cane_bundle_side']])
    rj.setdefault('smelting', {}).setdefault('sugar_cane_bundle', 'dried_sugar_cane_bundle')
    rj['smelting'].setdefault('ember_coal_ore', 'ember_coal')
    # BTA's Nether stone family and strata, in our emberstone: cobbled and polished emberstone, soul schist and bone shale
    # in the deep rock, ashen trunks standing petrified on the Ashen Wastes; and gum wood for the Redlands' pale trees
    if 'polished_emberstone' not in tiles:
        tile('cobbled_emberstone', recolour(src_pic(d, tiles, files, 'cobblestone'), hsv_map(hue=0.0, sat=0.9, val=0.75, add=4)))
        tile('polished_emberstone', recolour(src_pic(d, tiles, files, 'polished_andesite'), hsv_map(hue=0.0, sat=0.85, val=0.7)))
        tile('soul_schist', recolour(src_pic(d, tiles, files, 'deepslate'), hsv_map(hue=0.08, sat=0.45, val=0.85)))
        tile('bone_shale', recolour(src_pic(d, tiles, files, 'calcite'), hsv_map(hue=0.1, sat=0.35, val=0.95)))
        tile('ashen_trunk_side', recolour(src_pic(d, tiles, files, 'oak_log_side' if 'oak_log_side' in tiles else 'log_side'), hsv_map(sat=0.08, val=0.62)))
        tile('ashen_trunk_top', recolour(src_pic(d, tiles, files, 'oak_log_top' if 'oak_log_top' in tiles else 'log_top'), hsv_map(sat=0.08, val=0.62)))
        tile('gum_log_side', recolour(src_pic(d, tiles, files, 'birch_log_side'), hsv_map(hue=0.08, sat=0.6, val=1.05)))
        tile('gum_log_top', recolour(src_pic(d, tiles, files, 'birch_log_top'), hsv_map(hue=0.08, sat=0.6, val=1.05)))
        tile('gum_planks', recolour(src_pic(d, tiles, files, 'birch_planks'), hsv_map(hue=0.07, sat=0.8, val=1.0)))
        tile('gum_leaves', recolour(src_pic(d, tiles, files, 'birch_leaves'), hsv_map(val=1.0)))
    for k, nm, tx, hard in (('cobbled_emberstone', 'Cobbled Emberstone', 'cobbled_emberstone', 1.2), ('polished_emberstone', 'Polished Emberstone', 'polished_emberstone', 1.5),
                            ('soul_schist', 'Soul Schist', 'soul_schist', 2.5), ('bone_shale', 'Bone Shale', 'bone_shale', 2.0)):
        block(k, nm, {'hardness': hard, 'tier': 1, 'tex': tx}, [tiles[tx]] * 6)
    block('ashen_trunk', 'Ashen Trunk', {'hardness': 2.5, 'tier': 1, 'sound': 'stone', 'axisBlock': True,
                                         'tex': {'top': 'ashen_trunk_top', 'bottom': 'ashen_trunk_top', 'side': 'ashen_trunk_side'}},
          [tiles['ashen_trunk_side'], tiles['ashen_trunk_side'], tiles['ashen_trunk_top'], tiles['ashen_trunk_top'], tiles['ashen_trunk_side'], tiles['ashen_trunk_side']])
    block('gum_log', 'Gum Log', {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5,
                                 'tex': {'top': 'gum_log_top', 'bottom': 'gum_log_top', 'side': 'gum_log_side'}},
          [tiles['gum_log_side'], tiles['gum_log_side'], tiles['gum_log_top'], tiles['gum_log_top'], tiles['gum_log_side'], tiles['gum_log_side']])
    block('gum_planks', 'Gum Planks', {'hardness': 2, 'tool': 'axe', 'sound': 'wood', 'fuel': 1.5, 'tex': 'gum_planks'}, [tiles['gum_planks']] * 6)
    block('gum_leaves', 'Gum Leaves', {'render': 2, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'lightAtt': 1,
                                       'isLeaves': True, 'tint': 'foliage', 'drop': 'stick', 'tex': 'gum_leaves'}, [tiles['gum_leaves']] * 6)
    recipe('gum_planks', 4, items_=['gum_log'])
    recipe('polished_emberstone', 4, ['EE', 'EE'], {'E': 'emberstone'})
    recipe('cobbled_emberstone', 2, items_=['emberstone', 'gravel'])
    # the wet sponge (Minecraft's): a sponge that soaked up water; dry it in a furnace. Used on dyed wool, terracotta,
    # glass or painted planks it washes the colour out (BTA)
    if 'wet_sponge' not in tiles:
        tile('wet_sponge', recolour(src_pic(d, tiles, files, 'sponge'), lambda r, g, b, a, x, y: (r * 150 // 255, g * 160 // 255, b * 110 // 255 + 20, a)))
    block('wet_sponge', 'Wet Sponge', {'hardness': 0.6, 'tool': 'hoe', 'sound': 'grass', 'wetSponge': True, 'tex': 'wet_sponge'}, [tiles['wet_sponge']] * 6)
    rj.setdefault('smelting', {}).setdefault('wet_sponge', 'sponge')
    # the rest of BTA's stone families: permafrost cut down to cobbles, polished and laid as bricks; a mossy oak log;
    # mossy emberstone. Pictures from the base pack's own (cobblestone, polished andesite, stone bricks, oak log, moss)
    if 'cobbled_permafrost' not in tiles:
        ice = lambda r, g, b, a, x, y: (r * 78 // 100 + 18, g * 88 // 100 + 24, min(255, b * 100 // 100 + 40), a)
        tile('cobbled_permafrost', recolour(src_pic(d, tiles, files, 'cobblestone'), ice))
        tile('polished_permafrost', recolour(src_pic(d, tiles, files, 'polished_andesite'), ice))
        tile('permafrost_bricks', recolour(src_pic(d, tiles, files, 'stone_bricks'), ice))
        im = src_pic(d, tiles, files, 'log_side'); px = im.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = px[x, y]
                if ((x * 7 + y * 13) % 11 < 4 and y > 6 + (x * 5) % 9) or y > 26 - (x * 3) % 5: px[x, y] = (r * 50 // 100 + 30, g * 60 // 100 + 70, b * 40 // 100 + 20, a)
        tile('mossy_oak_log_side', im)
        em = src_pic(d, tiles, files, 'emberstone'); ep = em.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = ep[x, y]
                if (x * 5 + y * 3) % 7 < 2 and (x // 6 + y // 5) % 3 == 0: ep[x, y] = (r * 40 // 100 + 40, g * 50 // 100 + 90, b * 30 // 100 + 24, a)
        tile('mossy_emberstone', em)
    for k, nm in (('cobbled_permafrost', 'Cobbled Permafrost'), ('polished_permafrost', 'Polished Permafrost'), ('permafrost_bricks', 'Permafrost Bricks'),
                  ('mossy_emberstone', 'Mossy Emberstone')):
        block(k, nm, {'hardness': 1.5 if 'permafrost' in k else 0.6, 'tex': k}, [tiles[k]] * 6)
    block('mossy_oak_log', 'Mossy Oak Log', {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5,
                                             'tex': {'top': 'log_top', 'bottom': 'log_top', 'side': 'mossy_oak_log_side'}},
          [tiles['mossy_oak_log_side'], tiles['mossy_oak_log_side'], tiles['log_top'], tiles['log_top'], tiles['mossy_oak_log_side'], tiles['mossy_oak_log_side']])
    pf = next((b for b in blocks if b['key'] == 'permafrost'), None)
    if pf and pf.get('drop') == 'dirt': pf['drop'] = 'cobbled_permafrost'   # BTA: mined permafrost breaks to cobbles
    recipe('polished_permafrost', 4, ['CC', 'CC'], {'C': 'cobbled_permafrost'})
    recipe('permafrost_bricks', 4, ['PP', 'PP'], {'P': 'polished_permafrost'})
    recipe('mossy_oak_log', 1, items_=['log', 'vine'])
    recipe('mossy_emberstone', 1, items_=['emberstone', 'vine'])
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, separators=(',', ':'), ensure_ascii=False))
    sh = json.loads((d / 'itemshapes.json').read_text())
    if 'hand_sieve' not in sh['itemTiles']: sh['itemTiles'].append('hand_sieve'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'steel_door_item' not in sh['itemTiles']: sh['itemTiles'].append('steel_door_item'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'glass_door_item' not in sh['itemTiles']: sh['itemTiles'].append('glass_door_item'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'coconut' not in sh['itemTiles']: sh['itemTiles'].append('coconut'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'golden_quiver' not in sh['itemTiles']: sh['itemTiles'].append('golden_quiver'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'quiver' not in sh['itemTiles']: sh['itemTiles'].append('quiver'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'painting_item' not in sh['itemTiles']: sh['itemTiles'].append('painting_item'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    for _k in ('handcannon', 'rocket_launcher', 'cannonball', 'rocket'):
        if _k not in sh['itemTiles']: sh['itemTiles'].append(_k); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'sulphur' not in sh['itemTiles']: sh['itemTiles'].append('sulphur'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    if 'paintbrush' not in sh['itemTiles']: sh['itemTiles'].append('paintbrush'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    print('bta blocks ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
