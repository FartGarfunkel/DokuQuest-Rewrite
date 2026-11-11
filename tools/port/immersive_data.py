#!/usr/bin/env python3
"""The immersive progression's data (docs/superpowers/specs/2026-11-10-immersive-progression-design.md), after
TerraFirmaCraft. Idempotent: rerun safely, it skips what's there.
  python3 tools/port/immersive_data.py data
Part 2 (stone age):
- knapped heads: stone_{axe,shovel,hoe,knife,hammer,javelin}_head and crude_pickaxe_head (from pebbles), and
  flint_{knife,hammer,javelin}_head (flint's axe/shovel/hoe/pickaxe heads exist already: tools/port/forging.py);
  pictured from the iron tools' heads recoloured (our own art, as forging.py)
- tools: stone_knife, stone_hammer, stone_javelin, flint_knife, flint_hammer, flint_javelin, crude_pickaxe
- head + stick (shapeless) -> the tool
- the classic early tool recipes (wooden tools, cobblestone tools) are marked "classicOnly": an immersive world turns
  them off (sim/immersive.h recipeAllowed)"""
import json, sys, pathlib, colorsys
from PIL import Image

STONE = (128, 126, 122)
FLINT = (78, 78, 88)


def is_haft(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return 0.03 < h < 0.16 and s > 0.25 and v < 0.85


def classify(tp):
    cls, q = {}, []
    for y in range(32):
        for x in range(32):
            c = tp[x, y]
            if c[3] and max(c[:3]) >= 64: cls[(x, y)] = is_haft(*c[:3]); q.append((x, y))
    i = 0
    while i < len(q):
        x, y = q[i]; i += 1
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                n = (x + dx, y + dy)
                if 0 <= n[0] < 32 and 0 <= n[1] < 32 and n not in cls and tp[n][3]: cls[n] = cls[(x, y)]; q.append(n)
    return cls


def tint(px, rgb, ref):
    r, g, b, a = px
    k = (0.3 * r + 0.59 * g + 0.11 * b) / ref
    return (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), a)


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    (d / 'textures/item/immersive').mkdir(parents=True, exist_ok=True)

    def pic(name):
        return Image.open(d / files[tiles[name]]).convert('RGBA').resize((32, 32), Image.NEAREST)

    def item_tile(name, im):
        rel = 'textures/item/immersive/%s.png' % name
        im.save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if name not in sh['itemTiles']: sh['itemTiles'].append(name)

    def recolour(src, rgb, keep_haft, scale=1.0):
        """the iron tool's picture: its head recoloured (and its haft kept, or dropped for a loose head); scale < 1
        shrinks it toward the bottom-left corner (a knife from a sword)"""
        tp = pic(src).load(); cls = classify(tp)
        head = [tp[p] for p, h in cls.items() if not h and max(tp[p][:3]) >= 64]
        ref = max(1, sum(0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2] for p in head) / max(1, len(head)))
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        for y in range(32):
            for x in range(32):
                c = tp[x, y]
                if not c[3]: continue
                isHaft = cls.get((x, y), True)
                if isHaft and not keep_haft: continue
                o[x, y] = c if isHaft else tint(c, rgb, ref)
        if scale != 1.0:
            s = int(32 * scale); im2 = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
            im2.paste(im.resize((s, s), Image.NEAREST), (0, 32 - s)); im = im2
        return im

    def hammer(rgb, with_haft):
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        if with_haft:
            for i in range(4, 22):
                for w in (0, 1):
                    o[i + w, 31 - i] = (110, 76, 40, 255); o[i + w, 30 - i] = (138, 98, 54, 255)
        for y in range(4, 14):          # a squat block of stone across the top of the haft
            for x in range(17, 29):
                k = 0.75 + 0.25 * ((x + y) % 5 == 0) + (0.12 if y == 4 or x == 17 else 0) - (0.2 if y == 13 or x == 28 else 0)
                o[x, y] = (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), 255)
        return im

    def recipe(out, n, items_):
        if any(r.get('out') == out and r.get('items') == items_ for r in rj['recipes']): return
        rj['recipes'].append({'out': out, 'n': n, 'items': items_})

    def item(key, name, extra, icon_im):
        if key in items and items[key].get('icon') != 'imm_' + key: return   # one of ours already (straw...): left as it is
        if key not in items:
            items[key] = dict({'key': key, 'name': name, 'icon': 'imm_' + key}, **extra)
        item_tile('imm_' + key, icon_im)

    for mat, rgb, mname in (('stone', STONE, 'Stone'), ('flint', FLINT, 'Flint')):
        heads = {'knife': ('iron_sword', 0.6), 'javelin': ('iron_sword', 1.0)}
        if mat == 'stone': heads.update({'axe': ('iron_axe', 1.0), 'shovel': ('iron_shovel', 1.0), 'hoe': ('iron_hoe', 1.0)})
        for t, (src, sc) in heads.items():
            item('%s_%s_head' % (mat, t), '%s %s Head' % (mname, t.capitalize()), {'maxStack': 16, 'knapped': True}, recolour(src, rgb, False, sc))
        item('%s_hammer_head' % mat, '%s Hammer Head' % mname, {'maxStack': 16, 'knapped': True}, hammer(rgb, False))
        dur = 80 if mat == 'stone' else 110
        item('%s_knife' % mat, '%s Knife' % mname, {'maxStack': 1, 'knife': True, 'tool': {'type': 'sword', 'tier': 2, 'speed': 1.5, 'damage': 3, 'durability': dur}}, recolour('iron_sword', rgb, True, 0.6))
        item('%s_javelin' % mat, '%s Javelin' % mname, {'maxStack': 1, 'javelin': True, 'tool': {'type': 'sword', 'tier': 2, 'speed': 1, 'damage': 5, 'durability': dur - 10}}, recolour('iron_sword', rgb, True, 1.0))
        item('%s_hammer' % mat, '%s Hammer' % mname, {'maxStack': 1, 'hammer': True, 'tool': {'type': 'hammer', 'tier': 2, 'speed': 2, 'damage': 4, 'durability': dur + 20}}, hammer(rgb, True))
        for t in ('knife', 'javelin', 'hammer'): recipe('%s_%s' % (mat, t), 1, ['%s_%s_head' % (mat, t), 'stick'])
    for t in ('axe', 'shovel', 'hoe'): recipe('stone_%s' % t, 1, ['stone_%s_head' % t, 'stick'])
    item('crude_pickaxe_head', 'Crude Pickaxe Head', {'maxStack': 16, 'knapped': True}, recolour('iron_pickaxe', STONE, False))
    item('crude_pickaxe', 'Crude Pickaxe', {'maxStack': 1, 'tool': {'type': 'pickaxe', 'tier': 2, 'speed': 3, 'damage': 2, 'durability': 40}}, recolour('iron_pickaxe', STONE, True))
    recipe('crude_pickaxe', 1, ['crude_pickaxe_head', 'stick'])

    # Part 3: the firestarter (two sticks, rubbed together; 8 tries)
    def sticks_icon(n):
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        for k in range(n):
            for i in range(4, 28):
                y = 31 - i if k == 0 else i
                for w in (0, 1):
                    xx = min(31, i + w + (3 if k else 0))
                    o[xx, y] = (124, 86, 46, 255) if w == 0 else (92, 62, 32, 255)
        return im
    item('firestarter', 'Firestarter', {'maxStack': 1, 'tool': {'type': 'firestarter', 'tier': 0, 'speed': 1, 'damage': 1, 'durability': 8},
         'desc': 'Hold use on three sticks on the ground (straw helps), an out fire, a pit kiln or a log pile'}, sticks_icon(2))
    if not any(r.get('out') == 'firestarter' for r in rj['recipes']):
        rj['recipes'].append({'out': 'firestarter', 'n': 1, 'shape': ['S ', ' S'], 'key': {'S': 'stick'}, 'w': 2, 'h': 2, 'group': 'tools'})
    for r in rj['recipes']:
        if r.get('out') == 'campfire' and 'shape' in r: r['classicOnly'] = True   # immersive: a firepit is made from sticks

    # Part 3: straw, the scythe, thatch
    def straw_icon():
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        for k, x0 in enumerate((6, 10, 14, 18, 22, 25)):
            for y in range(6, 28):
                x = x0 + (y - 16) * (k % 3 - 1) // 6
                if 0 <= x < 32: o[x, y] = (226, 196, 92, 255) if (y + k) % 5 else (186, 150, 60, 255)
        for x in range(5, 28): o[x, 17] = (150, 112, 46, 255)
        return im
    item('straw', 'Straw', {'maxStack': 64, 'desc': 'Cut from tall grass with a knife or a scythe. Pit kilns, thatch, and it helps a firestarter catch'}, straw_icon())
    for mat, rgb, mname in (('stone', STONE, 'Stone'), ('flint', FLINT, 'Flint')):
        item('%s_scythe_head' % mat, '%s Scythe Head' % mname, {'maxStack': 16, 'knapped': True}, recolour('iron_hoe', rgb, False))
        item('%s_scythe' % mat, '%s Scythe' % mname, {'maxStack': 1, 'knife': True, 'scythe': True, 'tool': {'type': 'hoe', 'tier': 2, 'speed': 2, 'damage': 3, 'durability': 90 if mat == 'stone' else 120}}, recolour('iron_hoe', rgb, True))
        recipe('%s_scythe' % mat, 1, ['%s_scythe_head' % mat, 'stick'])
    if 'thatch' not in items: items['thatch'] = {'key': 'thatch', 'name': 'Thatch', 'maxStack': 64, 'icon': 'block:thatch', 'block': 'thatch'}
    if not any(r.get('out') == 'thatch' for r in rj['recipes']):
        rj['recipes'].append({'out': 'thatch', 'n': 1, 'shape': ['SS', 'SS'], 'key': {'S': 'straw'}, 'w': 2, 'h': 2, 'group': 'blocks'})
        rj['recipes'].append({'out': 'straw', 'n': 4, 'items': ['thatch']})

    # Part 3: clay pottery and molds (data/knapping.json "clay" / "fireclay": cells kept), unfired and fired
    kj_path = d / 'knapping.json'
    kj = json.loads(kj_path.read_text())
    def pad(rows):
        g = [[' '] * 5 for _ in range(5)]
        for y, r in enumerate(rows):
            for x, c in enumerate(r): g[y][x] = c
        return g
    def complement(rows):
        return [''.join('#' if c != '#' else ' ' for c in row) for row in pad(rows)]
    heads = {p['out'][len('stone_'):-len('_head')]: p['rows'] for p in kj['stone'] if p['out'].startswith('stone_') and p['out'].endswith('_head') and p.get('n', 1) == 1}
    heads['pickaxe'] = next(p['rows'] for p in kj['stone'] if p['out'] == 'crude_pickaxe_head')
    heads['sword'] = ['   ##', '  ###', ' ### ', '###  ', '##   ']
    heads['saw'] = ['#    ', '##   ', '###  ', '#### ', '#####']
    heads['propick'] = ['  ###', '#####', '  ###']
    shapes = {'vessel': [' ### ', '#####', '#####', '#####', ' ### '], 'jug': ['  #  ', ' ### ', '#####', '#####', ' ### '],
              'pot': ['#   #', '#   #', '#   #', '#   #', '#####'], 'bowl': ['#   #', '#   #', ' ### '], 'ingot_mold': ['#####', '#   #', '#####']}
    for t, rows in sorted(heads.items()): shapes['%s_mold' % t] = complement(rows)
    names = {'vessel': 'Vessel', 'jug': 'Jug', 'pot': 'Pot', 'bowl': 'Bowl', 'ingot_mold': 'Ingot Mold', 'propick_mold': "Prospector's Pick Mold"}
    CLAY, FIRED = (150, 152, 166), (182, 100, 64)
    def pattern_icon(rows, rgb, mold):
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        g = pad(rows)
        for y in range(5):
            for x in range(5):
                keep = g[y][x] == '#'
                if not keep and not mold: continue
                c = rgb if keep else tuple(int(v * 0.45) for v in rgb)
                for py in range(6):
                    for px in range(6):
                        k = 1.08 if py == 0 or px == 0 else 0.85 if py == 5 or px == 5 else 1.0
                        o[1 + x * 6 + px, 1 + y * 6 + py] = (min(255, int(c[0] * k)), min(255, int(c[1] * k)), min(255, int(c[2] * k)), 255)
        return im
    kj['clay'] = []
    for key, rows in shapes.items():
        mold = key.endswith('_mold')
        nm = names.get(key, key.replace('_mold', '').replace('_', ' ').title() + ' Mold')
        item('unfired_' + key, 'Unfired ' + nm, {'maxStack': 16 if mold else 1, 'fires': 'ceramic_' + key, 'pottery': True}, pattern_icon(rows, CLAY, mold))
        item('ceramic_' + key, ('Ceramic ' if not mold else 'Fired ') + nm, {'maxStack': 16 if mold else 1, 'pottery': True, 'mold': key[:-5] if mold else None} if mold else {'maxStack': 1, 'pottery': True}, pattern_icon(rows, FIRED, mold))
        kj['clay'].append({'out': 'unfired_' + key, 'n': 1, 'rows': rows})
    item('fire_clay', 'Fire Clay', {'maxStack': 64}, pattern_icon(['  #  ', ' ### ', '#####', ' ### '], (196, 186, 170), False))
    recipe('fire_clay', 1, ['clay_ball', 'clay_ball', 'clay_ball', 'clay_ball', 'gravel'])
    kj['fireclay'] = [{'out': 'unfired_crucible', 'n': 1, 'rows': ['#   #', '#   #', '#   #', '#   #', ' ### ']}]
    item('unfired_crucible', 'Unfired Crucible', {'maxStack': 1, 'fires': 'crucible', 'pottery': True}, pattern_icon(kj['fireclay'][0]['rows'], (196, 186, 170), False))
    item('crucible', 'Crucible', {'maxStack': 1, 'pottery': True, 'desc': 'Melts metal in a forge: up to thirty ingots, and the hardest metals'}, pattern_icon(kj['fireclay'][0]['rows'], (210, 200, 180), False))
    kj_path.write_text(json.dumps(kj, indent=1))

    # Part 4: metal of any makeup (core/metals.h): one head item per kind, coloured by its "comp" tag; the extra part-tool
    # kinds; the ingots for zinc, bismuth, the alloys and any mix
    METAL = (190, 190, 196)
    KIND_SRC = {'pickaxe': ('iron_pickaxe', 1.0), 'axe': ('iron_axe', 1.0), 'shovel': ('iron_shovel', 1.0), 'hoe': ('iron_hoe', 1.0),
                'sword': ('iron_sword', 1.0), 'knife': ('iron_sword', 0.6), 'saw': ('iron_sword', 0.85), 'scythe': ('iron_hoe', 1.0),
                'javelin': ('iron_sword', 1.0), 'propick': ('iron_pickaxe', 0.8), 'hammer': None, 'chisel': ('chisel', 1.0)}
    for kind, src in KIND_SRC.items():
        im = hammer(METAL, False) if src is None else recolour(src[0], METAL, False, src[1])
        item('metal_%s_head' % kind, '%s Head' % kind.capitalize(), {'maxStack': 16, 'metalHead': kind}, im)
    NEW = {'knife': ({'type': 'sword', 'tier': 2, 'speed': 1.5, 'damage': 3, 'durability': 200}, {'knife': True}),
           'hammer': ({'type': 'hammer', 'tier': 2, 'speed': 2, 'damage': 4, 'durability': 250}, {'hammer': True}),
           'saw': ({'type': 'axe', 'tier': 2, 'speed': 3, 'damage': 3, 'durability': 220}, {'saw': True}),
           'scythe': ({'type': 'hoe', 'tier': 2, 'speed': 2.5, 'damage': 3, 'durability': 220}, {'knife': True, 'scythe': True}),
           'javelin': ({'type': 'sword', 'tier': 2, 'speed': 1, 'damage': 5, 'durability': 180}, {'javelin': True}),
           'propick': ({'type': 'pickaxe', 'tier': 1, 'speed': 2, 'damage': 2, 'durability': 120}, {'propick': True}),
           'chisel': ({'type': 'chisel', 'tier': 2, 'speed': 4, 'damage': 1, 'durability': 180}, {})}
    for kind, (tool, props) in NEW.items():
        src = KIND_SRC[kind]
        im = hammer(METAL, True) if src is None else recolour(src[0], METAL, True, src[1])
        item('mixed_%s' % kind, 'Mixed %s' % kind.capitalize(), dict({'maxStack': 1, 'tool': tool, 'mixed': True}, **props), im)
    def ingot_icon(rgb):
        return recolour('iron_ingot', rgb, False) if 'iron_ingot' in tiles else hammer(rgb, False)
    for key, name, rgb in (('zinc_ingot', 'Zinc Ingot', (176, 186, 196)), ('bismuth_ingot', 'Bismuth Ingot', (150, 170, 160)), ('brass_ingot', 'Brass Ingot', (222, 186, 90)),
                           ('bismuth_bronze_ingot', 'Bismuth Bronze Ingot', (176, 130, 96)), ('black_bronze_ingot', 'Black Bronze Ingot', (96, 72, 64)),
                           ('rose_gold_ingot', 'Rose Gold Ingot', (236, 150, 130)), ('sterling_silver_ingot', 'Sterling Silver Ingot', (214, 220, 232)),
                           ('alloy_ingot', 'Alloy Ingot', (170, 160, 150)), ('pig_iron', 'Pig Iron', (110, 108, 112))):
        item(key, name, {'maxStack': 64, 'metal': True}, ingot_icon(rgb))

    # ores: zinc (sphalerite) and bismuth (bismuthinite) blocks, their pictures the tin ore's recoloured; ore pieces, samples
    # and powder for every metal; surface samples as ground blocks (like the loose rocks) showing their ore
    bj = json.loads((d / 'blocks.json').read_text()); sj = json.loads((d / 'shapes.json').read_text())
    bkeys = {b['key']: b for b in bj['blocks']}
    def block_tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]
    def recolour_tile(src, rgb):
        im = Image.open(d / files[tiles[src]]).convert('RGBA').resize((32, 32), Image.NEAREST); o = im.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = o[x, y]
                h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                if sat > 0.18:   # the ore's specks (not the grey stone)
                    k = v / 0.8
                    o[x, y] = (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), a)
        return im
    for key, name, rgb in (('zinc_ore', 'Sphalerite (Zinc Ore)', (170, 150, 80)), ('bismuth_ore', 'Bismuthinite (Bismuth Ore)', (120, 150, 140))):
        if key not in bkeys:
            t = block_tile(key, recolour_tile('tin_ore', rgb))
            b = dict(bkeys['tin_ore']); b.update({'key': key, 'name': name, 'drop': key, 'tex': key, 'id': len(bj['blocks'])})
            bj['blocks'].append(b); bj['faces'].append([t] * 6); bkeys[key] = b
        if key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
    ORE_RGB = {'copper': (214, 124, 82), 'tin': (200, 204, 210), 'zinc': (170, 150, 80), 'bismuth': (120, 150, 140), 'iron': (196, 150, 120), 'gold': (250, 210, 70), 'silver': (228, 234, 244)}
    ORE_BLOCK = {'copper': 'copper_ore', 'tin': 'tin_ore', 'zinc': 'zinc_ore', 'bismuth': 'bismuth_ore', 'iron': 'iron_ore', 'gold': 'gold_ore', 'silver': 'silver_ore'}
    def lumps(rgb, n, size):
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        spots = [(9, 18), (20, 14), (14, 23), (22, 23), (12, 10)][:n]
        for cx, cy in spots:
            for y in range(32):
                for x in range(32):
                    dd = (x - cx) ** 2 + (y - cy) ** 2
                    if dd <= size * size:
                        k = 1.15 if x < cx and y < cy else 0.8 if x > cx and y > cy else 1.0
                        g = (110, 106, 102) if (x * 7 + y * 3) % 5 == 0 else rgb
                        o[x, y] = (min(255, int(g[0] * k)), min(255, int(g[1] * k)), min(255, int(g[2] * k)), 255)
        return im
    for m, rgb in ORE_RGB.items():
        nm = m.capitalize()
        item('%s_ore_piece' % m, '%s Ore' % nm, {'maxStack': 64, 'ore': m}, lumps(rgb, 3, 5))
        item('%s_ore_sample' % m, 'Small %s Ore' % nm, {'maxStack': 64, 'ore': m}, lumps(rgb, 1, 5))
        item('%s_ore_powder' % m, 'Powdered %s Ore' % nm, {'maxStack': 64, 'ore': m}, lumps(rgb, 5, 3))
        sk = '%s_sample' % m
        if sk not in bkeys:
            base = dict(bkeys['loose_stones']); base.update({'key': sk, 'name': 'Small %s Ore' % nm, 'drop': '%s_ore_sample' % m, 'shape': sk, 'id': len(bj['blocks'])})
            base['tex'] = bkeys[ORE_BLOCK[m]].get('tex', ORE_BLOCK[m])
            bj['blocks'].append(base); bj['faces'].append(list(bj['faces'][bkeys['loose_stones']['id']])); bkeys[sk] = base
        tex = bkeys[ORE_BLOCK[m]].get('tex', ORE_BLOCK[m]); tex = tex if isinstance(tex, str) else tex.get('side', ORE_BLOCK[m])
        sj['shapes'][sk] = [{'b': [4, 0, 4, 9, 3, 8], 'side': tex, 'end': tex}, {'b': [9, 0, 9, 12, 2, 12], 'side': tex, 'end': tex}]
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, indent=1))
    mj = json.loads((d / 'metals.json').read_text())
    mj['oreBlocks'] = {'copper_ore': 'copper', 'tin_ore': 'tin', 'zinc_ore': 'zinc', 'bismuth_ore': 'bismuth', 'iron_ore': 'iron', 'gold_ore': 'gold',
                       'granite_gold_ore': 'gold', 'nether_gold_ore': 'gold', 'silver_ore': 'silver'}
    for ore, metal in (('mithril_ore', 'mithril'), ('adamantite_ore', 'adamantite'), ('thallasium_ore', 'thallasium'), ('cinderite_ore', 'cinderite')):
        mj['ores'][ore] = {'metal': metal, 'u': 100}   # the fantasy ores: melted in the crucible (bellows, full heat), not smelted
    (d / 'metals.json').write_text(json.dumps(mj, indent=1))

    # the bellows (beside a forge: hotter) and the forge (made from charcoal in a stone-ringed pit; no recipe)
    if 'bellows' not in items: items['bellows'] = {'key': 'bellows', 'name': 'Bellows', 'maxStack': 64, 'icon': 'block:bellows', 'block': 'bellows'}
    if not any(r.get('out') == 'bellows' for r in rj['recipes']):
        rj['recipes'].append({'out': 'bellows', 'n': 1, 'shape': ['PPP', 'LLL', 'PPP'], 'key': {'P': '#planks', 'L': 'leather'}, 'w': 3, 'h': 3, 'group': 'misc'})

    # the anvil's work (data/anvil.json): welded and flattened metal of any makeup, tongs, flux, the bloom; metal anvils
    for key, name, rgb in (('metal_double_ingot', 'Double Ingot', (190, 190, 196)), ('metal_sheet', 'Sheet', (196, 196, 202)), ('metal_double_sheet', 'Double Sheet', (186, 186, 192))):
        item(key, name, {'maxStack': 16, 'metal': True, 'metalPiece': True}, ingot_icon(rgb))
    item('raw_bloom', 'Raw Bloom', {'maxStack': 16, 'desc': 'Iron from the bloomery: refine it on an anvil (hit, hit, hit)'}, lumps((96, 86, 82), 5, 4))
    # TFC's two-step refining: raw bloom -> refined bloom -> wrought iron; pig iron -> high-carbon steel -> steel
    item('refined_bloom', 'Refined Bloom', {'maxStack': 16, 'desc': 'Hammered once: work it again into wrought iron'}, lumps((120, 112, 108), 4, 5))
    item('high_carbon_steel', 'High-Carbon Steel', {'maxStack': 16, 'desc': 'Pig iron worked once: work it again into steel'}, ingot_icon((84, 84, 92)))
    # the anvil makes armour unfinished; padding at the crafting table finishes it (leather, wool cloth or burlap)
    for piece in ('helmet', 'chestplate', 'leggings', 'boots'):
        if 'unfinished_' + piece not in items:
            items['unfinished_' + piece] = {'key': 'unfinished_' + piece, 'name': 'Unfinished ' + piece.capitalize(), 'maxStack': 1, 'icon': 'iron_' + piece,
                                            'desc': 'Pad it at the crafting table: leather, wool cloth or burlap'}
    item('flux', 'Flux', {'maxStack': 64, 'desc': 'Welds metal on the anvil'}, lumps((236, 232, 220), 4, 3))
    recipe('flux', 2, ['bone_meal', 'sand'])
    def tongs_icon():
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
        for i in range(6, 28):
            for w in (0, 1):
                o[i + w, 31 - i] = (120, 120, 126, 255); o[min(31, i + 4 + w), 31 - i] = (150, 150, 156, 255)
        return im
    item('tongs', 'Tongs', {'maxStack': 1, 'tongs': True, 'desc': 'In your other hand: carry hot metal without burning yourself'}, tongs_icon())
    recipe('tongs', 1, ['stick', 'stick', 'stone_knife_head'])
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    sj = json.loads((d / 'shapes.json').read_text())
    def tex_of(k):
        t = bkeys[k].get('tex', k)
        return t if isinstance(t, str) else t.get('side', k)
    for key, name, mb in (('copper_anvil', 'Copper Anvil', 'copper_block'), ('bronze_anvil', 'Bronze Anvil', 'bronze_block'), ('steel_anvil', 'Steel Anvil', 'steel_block'),
                          ('mithril_anvil', 'Mithril Anvil', 'mithril_block'), ('adamantite_anvil', 'Adamantite Anvil', 'adamantite_block')):
        if key not in bkeys:
            b = dict(bkeys['anvil']); b.update({'key': key, 'name': name, 'drop': key, 'shape': key, 'id': len(bj['blocks'])}); b.pop('station', None); b['smithing'] = True
            bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['anvil']['id']])); bkeys[key] = b
        ash = json.loads(json.dumps(sj['shapes']['anvil']))   # (not `sh`: that's itemshapes.json, written at the end)
        for box in ash:
            for f in ('side', 'end', 'top', 'bottom'):
                if f in box and isinstance(box[f], str): box[f] = tex_of(mb)
        sj['shapes'][key] = ash
        if key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key, 'desc': 'Smithing: use it with a hammer, the hot metal in your other hand'}
    if 'stone_anvil' not in bkeys:
        b = dict(bkeys['smooth_stone']); b.update({'key': 'stone_anvil', 'name': 'Stone Anvil', 'drop': 'cobblestone', 'id': len(bj['blocks']), 'smithing': True})
        bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['smooth_stone']['id']])); bkeys['stone_anvil'] = b
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, indent=1))

    # the bloomery (iron): a block under a chimney of stone bricks
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    if 'bloomery' not in bkeys:
        b = dict(bkeys['furnace']); b.update({'key': 'bloomery', 'name': 'Bloomery', 'drop': 'bloomery', 'id': len(bj['blocks'])})
        for k in ('furnace', 'container', 'station'): b.pop(k, None)
        bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['furnace']['id']]))
        (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    if 'bloomery' not in items: items['bloomery'] = {'key': 'bloomery', 'name': 'Bloomery', 'maxStack': 64, 'icon': 'block:bloomery', 'block': 'bloomery', 'desc': 'Iron: build a chimney of stone bricks on it (1-3 high), load iron ore and as much charcoal, light it'}
    if not any(r.get('out') == 'bloomery' for r in rj['recipes']):
        rj['recipes'].append({'out': 'bloomery', 'n': 1, 'shape': ['BBB', 'B B', 'BCB'], 'key': {'B': 'stone_bricks', 'C': 'metal_sheet'}, 'w': 3, 'h': 3, 'group': 'misc'})

    # Part 5: the saw and lumber; log -> 2 planks by hand in an immersive world; support beams
    item('lumber', 'Lumber', {'maxStack': 64, 'desc': 'Sawn from a log: planks, support beams, barrels'}, sticks_icon(2).rotate(90))
    item('flint_saw_head', 'Flint Saw Head', {'maxStack': 16, 'knapped': True}, recolour('iron_sword', FLINT, False, 0.85))
    item('flint_saw', 'Flint Saw', {'maxStack': 1, 'saw': True, 'tool': {'type': 'axe', 'tier': 2, 'speed': 2.5, 'damage': 2, 'durability': 64}}, recolour('iron_sword', FLINT, True, 0.85))
    recipe('flint_saw', 1, ['flint_saw_head', 'stick'])
    if 'mixed_saw' in items: items['mixed_saw']['saw'] = True
    rj['tags']['#saw'] = ['flint_saw', 'mixed_saw']
    if not any(r.get('out') == 'lumber' for r in rj['recipes']):
        rj['recipes'].append({'out': 'lumber', 'n': 8, 'items': ['#log', '#saw'], 'keepTool': 'saw', 'w': 2, 'h': 1})
        rj['recipes'].append({'out': 'planks', 'n': 1, 'shape': ['LL', 'LL'], 'key': {'L': 'lumber'}, 'w': 2, 'h': 2})
        rj['recipes'].append({'out': 'support_beam', 'n': 2, 'shape': ['L', 'L', 'L'], 'key': {'L': 'lumber'}, 'w': 1, 'h': 3})
    for r in list(rj['recipes']):
        if r.get('out', '').endswith('planks') and r.get('n') == 4 and 'items' in r and len(r['items']) == 1 and r['items'][0].startswith('#logs_') and not r.get('classicOnly'):
            r['classicOnly'] = True
    # as in TFC, no planks without a saw: log + saw -> lumber, four lumber -> planks (no log -> planks in the grid)
    rj['recipes'] = [r for r in rj['recipes'] if not (r.get('immersiveOnly') and r.get('out', '').endswith('planks') and r.get('items') and r['items'][0].startswith('#logs_'))]
    kj = json.loads((d / 'knapping.json').read_text())
    if not any(x['out'] == 'flint_saw_head' for x in kj['flint']): kj['flint'].append({'out': 'flint_saw_head', 'n': 1, 'rows': ['#    ', '##   ', '###  ', '#### ']})
    (d / 'knapping.json').write_text(json.dumps(kj, indent=1))
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    if 'support_beam' not in bkeys:
        b = {'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 2, 'tool': 'axe', 'tier': 0, 'drop': 'support_beam', 'light': 0, 'lightAtt': 0,
             'sound': 'wood', 'key': 'support_beam', 'name': 'Support Beam', 'tex': 'log_side', 'shape': 'support_beam', 'supportBeam': True, 'id': len(bj['blocks'])}
        bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['log']['id']]))
        (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
        sj = json.loads((d / 'shapes.json').read_text()); sj['shapes']['support_beam'] = [{'b': [4, 0, 4, 12, 16, 12], 'side': 'log_side', 'end': 'log_top'}]
        (d / 'shapes.json').write_text(json.dumps(sj, indent=1))
    if 'support_beam' not in items: items['support_beam'] = {'key': 'support_beam', 'name': 'Support Beam', 'maxStack': 64, 'icon': 'block:support_beam', 'block': 'support_beam', 'desc': 'Holds up a mine roof: no cave-ins within four blocks'}

    # the quern (a stone base and a handstone)
    item('handstone', 'Handstone', {'maxStack': 16}, lumps((150, 150, 150), 1, 9))
    if not any(r.get('out') == 'handstone' for r in rj['recipes']):
        rj['recipes'].append({'out': 'handstone', 'n': 1, 'shape': ['S', 'K'], 'key': {'S': 'smooth_stone', 'K': 'stick'}, 'w': 1, 'h': 2})
        rj['recipes'].append({'out': 'quern', 'n': 1, 'shape': [' H ', 'SSS'], 'key': {'H': 'handstone', 'S': 'smooth_stone'}, 'w': 3, 'h': 2})
    item('lime', 'Lime', {'maxStack': 64, 'desc': 'In a barrel of water: limewater, for soaking hides'}, lumps((236, 236, 226), 4, 3))
    item('flour', 'Flour', {'maxStack': 64}, lumps((240, 234, 214), 4, 3))
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    if 'quern' not in bkeys:
        b = {'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 2, 'tool': 'pickaxe', 'tier': 0, 'drop': 'quern', 'light': 0, 'lightAtt': 0,
             'sound': 'stone', 'key': 'quern', 'name': 'Quern', 'tex': 'smooth_stone', 'shape': 'quern', 'id': len(bj['blocks'])}
        bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['smooth_stone']['id']]))
        (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
        sj = json.loads((d / 'shapes.json').read_text()); sj['shapes']['quern'] = [{'b': [1, 0, 1, 15, 6, 15], 'side': 'smooth_stone', 'end': 'smooth_stone'}, {'b': [3, 6, 3, 13, 10, 13], 'side': 'stone', 'end': 'stone'}, {'b': [7, 10, 7, 9, 14, 9], 'side': 'log_side', 'end': 'log_top'}]
        (d / 'shapes.json').write_text(json.dumps(sj, indent=1))
    if 'quern' not in items: items['quern'] = {'key': 'quern', 'name': 'Quern', 'maxStack': 64, 'icon': 'block:quern', 'block': 'quern', 'desc': 'Hold use on it with grain, ore, limestone, bone or flowers to grind them'}

    # the sealable barrel and leather
    for key, name, rgb in (('raw_hide', 'Raw Hide', (150, 104, 72)), ('soaked_hide', 'Soaked Hide', (176, 150, 120)), ('scraped_hide', 'Scraped Hide', (200, 176, 140)), ('prepared_hide', 'Prepared Hide', (168, 112, 70))):
        item(key, name, {'maxStack': 16}, lumps(rgb, 1, 11))
    recipe('leather', 2, ['prepared_hide'])
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    if 'sealed_barrel' not in bkeys and 'barrel' in bkeys:
        b = dict(bkeys['barrel']); b.update({'key': 'sealed_barrel', 'name': 'Barrel (sealable)', 'drop': 'sealed_barrel', 'id': len(bj['blocks'])})
        for k in ('container', 'station', 'facingModel'): b.pop(k, None)
        bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['barrel']['id']]))
        (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    if 'sealed_barrel' not in items: items['sealed_barrel'] = {'key': 'sealed_barrel', 'name': 'Barrel (sealable)', 'maxStack': 64, 'icon': 'block:sealed_barrel', 'block': 'sealed_barrel', 'desc': 'Water by the bucket, things in; sneak + use seals it. Hides soak in limewater, then tan in tannin'}
    if not any(r.get('out') == 'sealed_barrel' for r in rj['recipes']):
        rj['recipes'].append({'out': 'sealed_barrel', 'n': 1, 'shape': ['L L', 'L L', 'LLL'], 'key': {'L': 'lumber'}, 'w': 3, 'h': 3})

    # panning and the sluice
    kj = json.loads((d / 'knapping.json').read_text())
    if not any(x['out'] == 'unfired_pan' for x in kj['clay']): kj['clay'].append({'out': 'unfired_pan', 'n': 1, 'rows': ['#   #', ' ### ']})
    (d / 'knapping.json').write_text(json.dumps(kj, indent=1))
    item('unfired_pan', 'Unfired Pan', {'maxStack': 1, 'fires': 'ceramic_pan', 'pottery': True}, pattern_icon(['#   #', ' ### '], CLAY, False))
    item('ceramic_pan', 'Pan', {'maxStack': 1, 'pottery': True, 'desc': 'Hold use on gravel or sand by the water to pan it'}, pattern_icon(['#   #', ' ### '], FIRED, False))
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    if 'sluice' not in bkeys:
        b = {'render': 6, 'solid': False, 'opaque': False, 'replaceable': False, 'hardness': 1, 'tool': 'axe', 'tier': 0, 'drop': 'sluice', 'light': 0, 'lightAtt': 0,
             'sound': 'wood', 'key': 'sluice', 'name': 'Sluice', 'tex': 'oak_planks', 'shape': 'sluice', 'wlog': True, 'id': len(bj['blocks'])}
        bj['blocks'].append(b); bj['faces'].append(list(bj['faces'][bkeys['support_beam']['id']]))
        (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
        sj = json.loads((d / 'shapes.json').read_text()); sj['shapes']['sluice'] = [{'b': [0, 0, 0, 16, 2, 16], 'side': 'log_side', 'end': 'log_side'}, {'b': [0, 2, 0, 16, 6, 2], 'side': 'log_side', 'end': 'log_side'}, {'b': [0, 2, 14, 16, 6, 16], 'side': 'log_side', 'end': 'log_side'}]
        (d / 'shapes.json').write_text(json.dumps(sj, indent=1))
    if 'sluice' not in items: items['sluice'] = {'key': 'sluice', 'name': 'Sluice', 'maxStack': 64, 'icon': 'block:sluice', 'block': 'sluice', 'desc': 'Set it in the water beside gravel or sand: it washes out finds on its own (use with an empty hand to take them)'}
    if not any(r.get('out') == 'sluice' for r in rj['recipes']):
        rj['recipes'].append({'out': 'sluice', 'n': 1, 'shape': ['L L', 'LLL'], 'key': {'L': 'lumber'}, 'w': 3, 'h': 2})

    # Part 7: the spindle, yarn and cloth
    kj = json.loads((d / 'knapping.json').read_text())
    if not any(x['out'] == 'unfired_spindle_head' for x in kj['clay']): kj['clay'].append({'out': 'unfired_spindle_head', 'n': 1, 'rows': [' # ', '###', ' # ']})
    (d / 'knapping.json').write_text(json.dumps(kj, indent=1))
    item('unfired_spindle_head', 'Unfired Spindle Whorl', {'maxStack': 16, 'fires': 'ceramic_spindle_head', 'pottery': True}, pattern_icon([' # ', '###', ' # '], CLAY, False))
    item('ceramic_spindle_head', 'Spindle Whorl', {'maxStack': 16, 'pottery': True}, pattern_icon([' # ', '###', ' # '], FIRED, False))
    item('spindle', 'Spindle', {'maxStack': 1, 'pottery': True, 'desc': 'Hold use with wool in your other hand to spin yarn'}, sticks_icon(1))
    recipe('spindle', 1, ['ceramic_spindle_head', 'stick'])
    item('yarn', 'Yarn', {'maxStack': 64}, lumps((236, 232, 222), 2, 5))
    item('wool_cloth', 'Wool Cloth', {'maxStack': 64, 'desc': 'Woven from yarn: padding, sails, beds'}, lumps((226, 222, 210), 1, 12))
    item('burlap', 'Burlap', {'maxStack': 64, 'desc': 'Woven from jute fibre: sacks and sails'}, lumps((176, 150, 104), 1, 12))
    if not any(r.get('out') == 'wool_cloth' for r in rj['recipes']):
        rj['recipes'].append({'out': 'wool_cloth', 'n': 1, 'shape': ['YY', 'YY'], 'key': {'Y': 'yarn'}, 'w': 2, 'h': 2})

    # Part 6: the rocks TFC's layers need that we haven't (pictures: the stone's, recoloured; they break to cobblestone)
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    ROCKS = {'chalk': (232, 230, 220), 'shale': (92, 96, 104), 'claystone': (170, 120, 90), 'conglomerate': (160, 150, 130), 'gneiss': (150, 146, 140),
             'schist': (120, 132, 120), 'quartzite': (220, 214, 206), 'gabbro': (70, 72, 76), 'rhyolite': (196, 160, 150), 'dacite': (176, 176, 168)}
    stone_tile = files[tiles['stone']]
    for key, rgb in ROCKS.items():
        if key in bkeys: continue
        im = Image.open(d / stone_tile).convert('RGBA').resize((32, 32), Image.NEAREST); o2 = im.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = o2[x, y]; k = (0.3 * r + 0.59 * g + 0.11 * b) / 125.0
                o2[x, y] = (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), a)
        rel = 'textures/block/%s.png' % key; im.save(d / rel)
        if key not in tiles: tiles[key] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        b = dict(bkeys['stone']); b.update({'key': key, 'name': key.capitalize(), 'tex': key, 'drop': 'cobblestone', 'id': len(bj['blocks'])})
        bj['blocks'].append(b); bj['faces'].append([tiles[key]] * 6); bkeys[key] = b
        if key not in items: items[key] = {'key': key, 'name': key.capitalize(), 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
    # each breaks to a cobble of its own (the cobblestone's picture in the rock's colour), which smelts back to the rock
    cob_tile = files[tiles['cobblestone']]
    for key, rgb in ROCKS.items():
        ck = 'cobbled_' + key
        if ck not in bkeys:
            im = Image.open(d / cob_tile).convert('RGBA').resize((32, 32), Image.NEAREST); o2 = im.load()
            for y in range(32):
                for x in range(32):
                    r, g, b, a = o2[x, y]; k = (0.3 * r + 0.59 * g + 0.11 * b) / 125.0
                    o2[x, y] = (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), a)
            rel = 'textures/block/%s.png' % ck; im.save(d / rel)
            if ck not in tiles: tiles[ck] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
            b = dict(bkeys['cobblestone']); b.update({'key': ck, 'name': 'Cobbled ' + key.capitalize(), 'tex': ck, 'drop': ck, 'id': len(bj['blocks'])})
            bj['blocks'].append(b); bj['faces'].append([tiles[ck]] * 6); bkeys[ck] = b
        if ck not in items: items[ck] = {'key': ck, 'name': 'Cobbled ' + key.capitalize(), 'maxStack': 64, 'icon': 'block:' + ck, 'block': ck}
        bkeys[key]['drop'] = ck
        rj['smelting'][ck] = key
        if ck not in rj['tags']['#stone_tool']: rj['tags']['#stone_tool'].append(ck)
    # the same family as granite's (gen bta_extras.py): the rock and its cobble each with a slab, stairs and a wall (the
    # chisel makes slabs and stairs of them too, world/carve_forms), and a mossy cobble (cobble + vine)
    def rock_shape(base, suffix, kind, fam, name):
        k = base + '_' + suffix
        if k not in bkeys:
            bb = bkeys[base]
            nb = {'render': 7, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': bb.get('hardness', 1.5), 'tool': bb.get('tool', 'pickaxe'),
                  'tier': bb.get('tier', 0), 'drop': k, 'light': 0, 'lightAtt': 0, 'sound': bb.get('sound', 'stone'), 'key': k, 'name': name,
                  'base': base, 'shapeKind': kind, 'wlog': True, 'family': fam, 'tex': bb['tex'], 'id': len(bj['blocks'])}
            bj['blocks'].append(nb); bj['faces'].append(list(bj['faces'][bb['id']])); bkeys[k] = nb
        if k not in items: items[k] = {'key': k, 'name': name, 'maxStack': 64, 'icon': 'block:' + k, 'block': k}
        shape = {'slab': (['XXX'], 6), 'stairs': (['X  ', 'XX ', 'XXX'], 4), 'wall': (['XXX', 'XXX'], 6)}[suffix]
        if not any(r.get('out') == k for r in rj['recipes']):
            rj['recipes'].append({'out': k, 'n': shape[1], 'shape': shape[0], 'key': {'X': base}, 'w': len(shape[0][0]), 'h': len(shape[0]), 'group': 'shapes'})
    mcob_tile = files[tiles['mossy_cobblestone']]
    for key, rgb in ROCKS.items():
        ck = 'cobbled_' + key
        for base, nm in ((key, key.capitalize()), (ck, 'Cobbled ' + key.capitalize())):
            rock_shape(base, 'slab', 1, 'slab', nm + ' Slab'); rock_shape(base, 'stairs', 2, 'stairs', nm + ' Stairs'); rock_shape(base, 'wall', 5, 'wall', nm + ' Wall')
        mk = 'mossy_' + ck
        if mk not in bkeys:
            im = Image.open(d / mcob_tile).convert('RGBA').resize((32, 32), Image.NEAREST); o2 = im.load()
            for y in range(32):
                for x in range(32):
                    r, g, b, a = o2[x, y]
                    if g > r * 1.12: continue   # the moss stays green
                    k2 = (0.3 * r + 0.59 * g + 0.11 * b) / 125.0
                    o2[x, y] = (min(255, int(rgb[0] * k2)), min(255, int(rgb[1] * k2)), min(255, int(rgb[2] * k2)), a)
            rel = 'textures/block/%s.png' % mk; im.save(d / rel)
            if mk not in tiles: tiles[mk] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
            b = dict(bkeys[ck]); b.update({'key': mk, 'name': 'Mossy Cobbled ' + key.capitalize(), 'tex': mk, 'drop': mk, 'id': len(bj['blocks'])})
            bj['blocks'].append(b); bj['faces'].append([tiles[mk]] * 6); bkeys[mk] = b
        if mk not in items: items[mk] = {'key': mk, 'name': 'Mossy Cobbled ' + key.capitalize(), 'maxStack': 64, 'icon': 'block:' + mk, 'block': mk}
        if not any(r.get('out') == mk for r in rj['recipes']): rj['recipes'].append({'out': mk, 'n': 1, 'items': [ck, 'vine'], 'w': 2, 'h': 2})
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))

    # Part 6: TFC's crops, berry bushes and fruit trees that we hadn't (pictures recoloured from our own: wheat's stages for
    # the grains and jute, the onion's for garlic, the pepper's for the beans and squash; the berry bush's; the leaves')
    bj = json.loads((d / 'blocks.json').read_text()); bkeys = {b['key']: b for b in bj['blocks']}
    def tint_file(src_name, dst_name, fn):
        im = Image.open(d / files[tiles[src_name]]).convert('RGBA'); o2 = im.load(); W2, H2 = im.size
        for y in range(H2):
            for x in range(W2):
                c = o2[x, y]
                if c[3]: o2[x, y] = fn(c, x, y)
        rel = 'textures/block/%s.png' % dst_name; im.save(d / rel)
        if dst_name not in tiles: tiles[dst_name] = tj['count']; files.append(rel); tj['res'].append(W2); tj['count'] += 1
        return tiles[dst_name]
    def shift(target):   # recolour what isn't green toward target, keep the leaves
        def fn(c, x, y):
            r, g, b, a = c; h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if 0.18 < h < 0.45: return c
            return (min(255, int(target[0] * v * 1.15)), min(255, int(target[1] * v * 1.15)), min(255, int(target[2] * v * 1.15)), a)
        return fn
    def all_to(target):
        def fn(c, x, y):
            r, g, b, a = c; v = (0.3 * r + 0.59 * g + 0.11 * b) / 160
            return (min(255, int(target[0] * v)), min(255, int(target[1] * v)), min(255, int(target[2] * v)), a)
        return fn
    def add_block(key, base_key, upd, faces_tile=None):
        if key in bkeys: return
        b = dict(bkeys[base_key]); b.update(upd); b['key'] = key; b['id'] = len(bj['blocks'])
        bj['blocks'].append(b); bj['faces'].append([faces_tile] * 6 if faces_tile is not None else list(bj['faces'][bkeys[base_key]['id']])); bkeys[key] = b
    CROPS = [('barley', 'wheat_crop', (220, 196, 120), 'grain'), ('oat', 'wheat_crop', (226, 214, 160), 'grain'), ('rye', 'wheat_crop', (170, 140, 90), 'grain'),
             ('jute', 'wheat_crop', (110, 150, 70), 'fibre'), ('garlic', 'onion_crop', (236, 232, 220), 'food'), ('soybean', 'pepper_crop', (150, 170, 80), 'food'),
             ('green_beans', 'pepper_crop', (90, 160, 70), 'food'), ('squash', 'tomato_crop', (226, 150, 50), 'food')]
    for name, base, rgb, kind in CROPS:
        crop = name + '_crop'
        stiles = []
        for i, t in enumerate(bkeys[base]['stageTiles']):
            tn = next(k for k, v in tiles.items() if v == t)
            stiles.append(tint_file(tn, '%s_stage%d' % (crop, i), shift(rgb)))
        produce = name + '_fibre' if kind == 'fibre' else name
        seed = name if base in ('onion_crop',) or name in ('soybean',) else name + '_seeds'
        add_block(crop, base, {'name': name.replace('_', ' ').title(), 'tex': '%s_stage0' % crop, 'stageTiles': stiles, 'crop': {'stages': len(stiles), 'drop': produce, 'seed': seed}}, stiles[0])
        nm = name.replace('_', ' ').title()
        if seed != produce: item(seed, nm + ' Seeds', {'maxStack': 64, 'block': crop}, lumps(rgb, 4, 2))
        extra = {'maxStack': 64}
        if kind == 'food': extra['food'] = {'heal': 3 if name == 'squash' else 2}
        if seed == produce: extra['plants'] = crop
        item(produce, nm + (' Fibre' if kind == 'fibre' else ''), extra, lumps(rgb, 3, 5))
    if not any(r.get('out') == 'burlap' for r in rj['recipes']):
        rj['recipes'].append({'out': 'burlap', 'n': 1, 'shape': ['JJ', 'JJ'], 'key': {'J': 'jute_fibre'}, 'w': 2, 'h': 2})
        rj['recipes'].append({'out': 'rope', 'n': 2, 'items': ['jute_fibre', 'jute_fibre', 'jute_fibre']}) if 'rope' in items else None
    qj = json.loads((d / 'quern.json').read_text())
    for g in ('barley', 'oat', 'rye'): qj['grind'][g] = ['flour', 1]
    (d / 'quern.json').write_text(json.dumps(qj, indent=1))
    BERRIES = {'wintergreen': (200, 40, 50), 'blueberry': (70, 90, 200), 'raspberry': (220, 60, 90), 'strawberry_bush': (230, 50, 50), 'blackberry': (60, 40, 70),
               'bunchberry': (230, 70, 40), 'cranberry': (180, 30, 50), 'snowberry': (240, 240, 245), 'elderberry': (70, 50, 90), 'gooseberry': (170, 200, 90), 'cloudberry': (240, 160, 60)}
    for name, rgb in BERRIES.items():
        if name == 'strawberry_bush': continue   # (strawberries are a crop here already)
        berry = name if name.endswith('berry') else name + '_berry'
        full, empty = name + '_bush', name + '_bush_empty'
        tf = tint_file('berry_bush', full, shift(rgb))
        add_block(full, 'berry_bush', {'name': berry.replace('_', ' ').title() + ' Bush', 'tex': full, 'harvest': berry, 'drop': full}, tf)
        add_block(empty, 'berry_bush_empty', {'name': berry.replace('_', ' ').title() + ' Bush', 'tex': 'berry_bush_empty', 'drop': full, 'refill': full})
        item(berry, berry.replace('_', ' ').title(), {'maxStack': 64, 'food': {'heal': 1}}, lumps(rgb, 4, 3))
        if full not in items: items[full] = {'key': full, 'name': berry.replace('_', ' ').title() + ' Bush', 'maxStack': 64, 'icon': 'block:' + full, 'block': full}
    FRUIT = {'banana': (240, 220, 80), 'lemon': (250, 236, 80), 'olive': (110, 130, 60), 'orange': (250, 150, 40), 'peach': (250, 170, 130), 'plum': (120, 50, 110)}
    for name, rgb in FRUIT.items():
        lv = name + '_leaves'; sap = name + '_sapling'
        def dots(c, x, y, rgb=rgb):
            return (rgb[0], rgb[1], rgb[2], 255) if ((x * 7 + y * 13) % 23 == 0 or (x * 5 + y * 3) % 31 == 0) else (int(c[0] * 0.55), int(c[1] * 0.8), int(c[2] * 0.45), c[3])
        tl = tint_file('leaves', lv, dots)
        add_block(lv, 'flowering_cherry_leaves', {'name': name.title() + ' Leaves', 'tex': lv, 'fruit': name, 'sapling': sap, 'drop': lv}, tl)
        ts = tint_file('sapling', sap, lambda c, x, y: c)
        add_block(sap, 'sapling', {'name': name.title() + ' Sapling', 'tex': sap, 'drop': sap, 'tree': 'oak', 'fruitLeaves': lv}, ts)
        item(name, name.title(), {'maxStack': 64, 'food': {'heal': 1 if name == 'olive' else 2}}, lumps(rgb, 2, 6))
        for k in (lv, sap):
            if k not in items: items[k] = {'key': k, 'name': bkeys[k]['name'], 'maxStack': 64, 'icon': 'block:' + k, 'block': k}
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))

    # Part 8: the gearbox (turns a shaft through any side; redstone power disengages it)
    if 'gearbox' not in items: items['gearbox'] = {'key': 'gearbox', 'name': 'Gearbox', 'maxStack': 64, 'icon': 'block:gearbox', 'block': 'gearbox', 'desc': 'Joins axles at right angles and splits them; a redstone signal disengages it'}
    if not any(r.get('out') == 'gearbox' for r in rj['recipes']):
        rj['recipes'].append({'out': 'gearbox', 'n': 1, 'shape': ['PAP', 'A A', 'PAP'], 'key': {'P': '#planks', 'A': 'axle'}, 'w': 3, 'h': 3})

    # Part 8: the professions trade in the new ways (data/villagers.json shops: what they sell you, [item, n, price, tier])
    vj_path = d / 'villagers.json'
    vj = json.loads(vj_path.read_text())
    ADD = {'toolsmith': [['tongs', 1, 2, 0], ['flux', 4, 1, 0], ['firestarter', 1, 1, 0], ['bronze_ingot', 2, 3, 1], ['copper_ingot', 4, 2, 0], ['bellows', 1, 4, 1]],
           'weaponsmith': [['flux', 4, 1, 0], ['tongs', 1, 2, 0], ['brass_ingot', 2, 3, 1]],
           'armorer': [['flux', 4, 1, 0], ['metal_sheet', 1, 6, 2], ['bellows', 1, 4, 1]],
           'mason': [['ceramic_vessel', 1, 2, 0], ['ceramic_ingot_mold', 2, 2, 0], ['ceramic_pickaxe_mold', 1, 3, 1], ['crucible', 1, 8, 2], ['fire_clay', 4, 2, 1], ['support_beam', 12, 1, 0], ['copper_ore_piece', 8, 2, 0], ['tin_ore_piece', 6, 2, 0]],
           'farmer': [['barley_seeds', 6, 1, 0], ['oat_seeds', 6, 1, 0], ['rye_seeds', 6, 1, 0], ['jute_seeds', 4, 1, 1], ['garlic', 4, 1, 0], ['soybean', 4, 1, 1]],
           'baker': [['flour', 8, 1, 0], ['quern', 1, 5, 1], ['gearbox', 1, 4, 1]],
           'butcher': [['raw_hide', 2, 1, 0], ['prepared_hide', 1, 2, 1], ['sealed_barrel', 1, 4, 1]],
           'merchant': [['lumber', 16, 1, 0], ['support_beam', 8, 1, 0], ['flint_saw', 1, 2, 0], ['ceramic_pan', 1, 2, 1], ['plum', 6, 1, 0], ['orange', 6, 1, 0]]}
    for job, trades in ADD.items():
        shop = vj['shops'].get(job)   # only the jobs that already keep a shop
        if shop is None: continue
        have = {t[0] for t in shop.get('buy', [])}
        for t in trades:
            if t[0] in items and t[0] not in have: shop['buy'].append(t)
    vj_path.write_text(json.dumps(vj, indent=1, ensure_ascii=False) if '\n' in vj_path.read_text()[:200] else json.dumps(vj, separators=(',', ':'), ensure_ascii=False))

    # TFC: no shortcuts past a stage in an immersive world
    for rr in rj['recipes']:
        if rr.get('out') == 'anvil' and 'iron_block' in json.dumps(rr.get('key', {})): rr['classicOnly'] = True    # smith the anvil
        if rr.get('out') == 'leather' and rr.get('items') in (['pelt'], ['croc_scale', 'croc_scale']): rr['classicOnly'] = True   # tan it
    if not any(rr.get('out') == 'raw_hide' and rr.get('items') == ['pelt'] for rr in rj['recipes']):
        rj['recipes'].append({'out': 'raw_hide', 'n': 1, 'items': ['pelt'], 'immersiveOnly': True})
    if not any(rr.get('out') == 'bucket' and 'lumber' in json.dumps(rr.get('key', {})) for rr in rj['recipes']):   # TFC's wooden bucket
        rj['recipes'].append({'out': 'bucket', 'n': 1, 'shape': ['L L', ' L '], 'key': {'L': 'lumber'}, 'w': 3, 'h': 2, 'immersiveOnly': True, 'group': 'misc'})

    # the classic early tools: off in an immersive world
    early = {'wood_pickaxe', 'wood_axe', 'wood_shovel', 'wood_sword', 'wood_hoe', 'stone_pickaxe', 'stone_axe', 'stone_shovel', 'stone_sword', 'stone_hoe'}
    import re as _re
    metal_gear = _re.compile(r'^(copper|tin|bronze|iron|gold|silver|steel|mithril|adamantite|thallasium|cinderite)_(pickaxe|axe|shovel|hoe|sword|helmet|chestplate|leggings|boots)$')
    for r in rj['recipes']:   # (metal tools and armour come off the anvil now, or are cast)
        if 'shape' in r and (r.get('out') in early or metal_gear.match(r.get('out', ''))): r['classicOnly'] = True

    # TerraFirmaCraft's own knapping patterns (its data: recipe/knapping/ceramic and stone, 1.21.x; '#' kept, defOn = TFC's
    # default_on: the cells round a smaller pattern stay kept). Ours that TFC hasn't (the flint saw, the crude pickaxe, the
    # stone scythe...) keep their patterns.
    TFC_CLAY = {
        'vessel': [([' ### ', '#####', '#####', '#####', ' ### '], 1, True)],
        'jug': [([' #   ', '#### ', '### #', '#### ', '###  '], 1, True)],
        'pot': [(['#   #', '#   #', '#   #', '#####', ' ### '], 1, True)],
        'bowl': [(['#   #', ' ### ', '     ', '#   #', ' ### '], 4, True), (['#   #', ' ### '], 2, False)],
        'ingot_mold': [(['####', '#  #', '#  #', '#  #', '####'], 2, True)],
        'axe_mold': [(['# ###', '    #', '     ', '    #', '# ###'], 1, True)],
        'chisel_mold': [(['## ##', '## ##', '## ##', '## ##', '## ##'], 1, True)],
        'hammer_mold': [(['#####', '     ', '     ', '## ##', '#####'], 1, True)],
        'hoe_mold': [(['#####', '     ', '  ###', '#####'], 1, True)],
        'javelin_mold': [(['   ##', '    #', '     ', '#   #', '## ##'], 1, True)],
        'knife_mold': [(['## #', '#  #', '#  #', '#  #', '#  #'], 1, True)],
        'pickaxe_mold': [(['#####', '#   #', ' ### ', '#####'], 1, True)],
        'propick_mold': [(['#####', '    #', ' ### ', ' ####', '#####'], 1, True)],
        'saw_mold': [(['  ###', '   ##', '#   #', '#    ', '###  '], 1, True)],
        'scythe_mold': [(['#####', '#    ', '    #', '  ###', '#####'], 1, True)],
        'shovel_mold': [(['#   #', '#   #', '#   #', '#   #', '## ##'], 1, True)],
        'sword_mold': [(['  ###', '   ##', '#   #', '##  #', '#### '], 1, True)],
        'pan': [(['#   #', '#####', ' ### '], 1, False)],
        'spindle_head': [(['  #  ', '#####', '  #  '], 1, False)],
    }
    TFC_ROCK = {
        'axe_head': [([' #   ', '#### ', '#####', '#### ', ' #   '], 1, True)],
        'hammer_head': [(['#####', '#####', '  #  '], 1, False)],
        'hoe_head': [(['#####', '   ##'], 1, False), (['#####', '##   ', '     ', '#####', '##   '], 2, True), (['#####', '##   ', '     ', '#####', '   ##'], 2, True)],
        'javelin_head': [(['###  ', '#### ', '#####', ' ### ', '  #  '], 1, True)],
        'knife_head': [(['# ', '##', '##', '##', '##'], 1, False), (['#  # ', '## ##', '## ##', '## ##', '## ##'], 2, True),
                       (['#   #', '## ##', '## ##', '## ##', '## ##'], 2, True), ([' # # ', '## ##', '## ##', '## ##', '## ##'], 2, True)],
        'shovel_head': [(['###', '###', '###', '###', ' # '], 1, False)],
    }
    kj = json.loads((d / 'knapping.json').read_text())
    if 'unfired_chisel_mold' not in items or 'ceramic_chisel_mold' not in items:
        rows = TFC_CLAY['chisel_mold'][0][0]
        item('unfired_chisel_mold', 'Unfired Chisel Mold', {'maxStack': 16, 'fires': 'ceramic_chisel_mold', 'pottery': True}, pattern_icon(rows, CLAY, True))
        item('ceramic_chisel_mold', 'Fired Chisel Mold', {'maxStack': 16, 'pottery': True, 'mold': 'chisel'}, pattern_icon(rows, FIRED, True))
    def tfc_list(old, table, prefix):
        out, done = [], set()
        for key, pats in table.items():
            ok = prefix + key
            if ok not in items: continue
            done.add(ok)
            for rows, n, on in pats: out.append({'out': ok, 'n': n, 'rows': rows, 'defOn': on})
        return out + [p for p in old if p['out'] not in done]
    kj['clay'] = tfc_list(kj['clay'], TFC_CLAY, 'unfired_')
    kj['stone'] = tfc_list(kj['stone'], TFC_ROCK, 'stone_')
    kj['flint'] = tfc_list(kj['flint'], TFC_ROCK, 'flint_')
    kj['fireclay'] = [{'out': 'unfired_crucible', 'n': 1, 'rows': ['#   #', '#   #', '#   #', '#   #', '#####'], 'defOn': True}]
    (d / 'knapping.json').write_text(json.dumps(kj, indent=1))
    for r in rj['recipes']:   # a chisel is smithed (or cast) in an immersive world, crafted from an ingot in a classic one
        if r.get('out') == 'iron_chisel': r['classicOnly'] = True
    # the rock layers left little plain stone or cobblestone underground: the working things made of it (levers, pistons,
    # repeaters, buttons...) take any rock's cobble or any rock. (Decorative blocks keep their own stone.)
    LAYER = ['limestone', 'chalk', 'shale', 'claystone', 'conglomerate', 'sandstone', 'marble', 'slate', 'gneiss', 'schist', 'quartzite',
             'granite', 'diorite', 'gabbro', 'basalt', 'andesite', 'rhyolite', 'dacite']
    rj['tags']['#cobblestone'] = ['cobblestone'] + sorted(k for k in items if k.startswith('cobbled_') and k in bkeys and not bkeys[k].get('family'))
    rj['tags']['#stone'] = ['stone'] + [k for k in LAYER if k in items]
    FUNC = {'cobblestone': {'activator', 'dispenser', 'dropper', 'lever', 'motion_sensor', 'observer', 'piston'},
            'stone': {'comparator', 'matcher', 'millstone', 'redstone_timer', 'repeater', 'stone_button', 'stone_pressure_plate', 'stonecutter'}}
    for r in rj['recipes']:
        for lit, outs in FUNC.items():
            if r.get('out') not in outs: continue
            if 'items' in r: r['items'] = ['#' + lit if i == lit else i for i in r['items']]
            if 'key' in r: r['key'] = {k: ('#' + lit if v == lit else v) for k, v in r['key'].items()}

    # classic worlds (no vessels): every metal's ore smelts in the furnace, and every alloy crafts from whole ingots in its mix
    mj2 = json.loads((d / 'metals.json').read_text())
    for ob, m in mj2.get('oreBlocks', {}).items():
        if m + '_ingot' in items and ob in items and ob not in rj['smelting']: rj['smelting'][ob] = m + '_ingot'
    for ak, a in mj2.get('alloys', {}).items():
        out, rg = a.get('ingot'), a.get('ranges', {})
        if not out or out not in items or any(m + '_ingot' not in items for m in rg): continue
        if any(r.get('out') == out and len(r.get('items', [])) > 1 and not r.get('immersiveOnly') for r in rj['recipes']): continue   # (bronze has its own)
        best = None
        for n in range(2, 10):   # the fewest ingots whose shares all sit in the ranges (bronze: 3 copper + 1 tin)
            ms = list(rg); import itertools
            for combo in itertools.product(range(1, n), repeat=len(ms)):
                if sum(combo) != n: continue
                if all(rg[m][0] - 1e-9 <= c / n <= rg[m][1] + 1e-9 for m, c in zip(ms, combo)): best = (n, combo, ms); break
            if best: break
        if not best: continue
        n, combo, ms = best
        rj['recipes'].append({'out': out, 'n': n, 'items': [m + '_ingot' for m, c in zip(ms, combo) for _ in range(c)], 'classicOnly': True, 'w': 3, 'h': 3})
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':')))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    print('immersive data: %d items, %d recipes' % (len(items), len(rj['recipes'])))


main(sys.argv[1] if len(sys.argv) > 1 else 'data')
