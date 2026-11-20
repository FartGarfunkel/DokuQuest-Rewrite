#!/usr/bin/env python3
"""tools/port/dappled_forest.py — leaf litter and the Dappled Forest (terrain version 26): fallen leaves lying on the ground
(five colours: green, red, orange, yellow, brown: a thin replaceable ground cover a hoe or shovel lifts, which crunches
underfoot), the poplar (tall and slim: log, planks, three autumn leaves, a sapling), the red shrub's leaves, the hollow
poplar log lying on the ground (plain, and with shelf fungus on it), and their worldgen data: the Dappled Forest biome,
the litter's and the logs' features, and which woods get which litter (data/worldgen/ground_cover.json).
Pictures: the DokuQuest Base pack's (its poplar leaves and sapling, its leaf litter: green tinted by the wood's foliage,
the other colours made from it); ours, drawn here, are only data/textures fallbacks. Never writes into the pack. The poplar
wood itself (log, planks and the rest) is tools/port/poplar_wood.py's: run that first. Idempotent;
data_patches.py runs it.

  python3 tools/port/dappled_forest.py [data-dir]
"""
import json, pathlib, random, sys
from PIL import Image, ImageDraw

LITTER = {   # colour: [the leaf's shades, light to dark]
    'green': [(112, 158, 52), (88, 134, 44), (66, 108, 38), (136, 176, 66)],
    'red': [(196, 52, 36), (168, 36, 30), (220, 84, 48), (140, 30, 28)],
    'orange': [(228, 128, 30), (206, 104, 24), (244, 160, 52), (176, 80, 22)],
    'yellow': [(236, 196, 52), (214, 168, 38), (248, 222, 96), (182, 140, 34)],
    'brown': [(146, 98, 52), (118, 78, 42), (170, 122, 70), (92, 60, 34)],
}
SOIL = [(62, 44, 30), (50, 36, 26), (74, 54, 36)]


def px(c, a=255):
    return (c[0], c[1], c[2], a)


def litter_tile(shades, seed):
    """a bed of fallen leaves: overlapping small leaf shapes (an ellipse with a midrib) over dark soil showing in the gaps"""
    rng = random.Random(seed)
    im = Image.new('RGBA', (32, 32), px(SOIL[0])); g = ImageDraw.Draw(im); p = im.load()
    for _ in range(90):
        x, y = rng.randrange(32), rng.randrange(32); p[x, y] = px(rng.choice(SOIL))
    for _ in range(46):
        cx, cy = rng.randrange(32), rng.randrange(32); w, h = rng.choice([(2, 1), (3, 1), (3, 2), (2, 2), (1, 2), (1, 3)])
        c = rng.choice(shades)
        for ox in range(-1 - w, w + 2):
            for oy in range(-1 - h, h + 2):
                if (ox / (w + 0.6)) ** 2 + (oy / (h + 0.6)) ** 2 <= 1.0:
                    p[(cx + ox) % 32, (cy + oy) % 32] = px(c)
        d = tuple(max(0, v - 38) for v in c)   # the midrib, along the leaf's long way
        for k in range(-w - 1, w + 2):
            if w >= h: p[(cx + k) % 32, cy % 32] = px(d)
            else: p[cx % 32, (cy + k) % 32] = px(d)
    return im


def leaves_tile(main, others, seed, hole=0.14):
    """a cutout leaf canopy: clumps of leaf pixels in the main colour with flecks of the others, holes showing the sky"""
    rng = random.Random(seed); im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); p = im.load()
    for x in range(32):
        for y in range(32):
            if rng.random() < hole: continue
            r = rng.random()
            c = main[0] if r < 0.5 else main[1] if r < 0.75 else rng.choice(others) if r < 0.9 else main[2]
            p[x, y] = px(c)
    for _ in range(26):   # darker veins: short runs
        x, y = rng.randrange(32), rng.randrange(32)
        for k in range(rng.randrange(2, 5)):
            if p[(x + k) % 32, y][3]: p[(x + k) % 32, y] = px(main[2])
    return im


def bark_tile(seed):
    rng = random.Random(seed); im = Image.new('RGBA', (32, 32), (0, 0, 0, 255)); p = im.load()
    base = [(206, 200, 178), (192, 186, 164), (214, 208, 188)]
    for x in range(32):
        col = rng.choice(base)
        for y in range(32): p[x, y] = px(col if rng.random() < 0.85 else rng.choice(base))
    for _ in range(12):   # dark furrows running up the trunk, and a few knots
        x = rng.randrange(32); y = rng.randrange(32)
        for k in range(rng.randrange(5, 14)): p[x, (y + k) % 32] = px((104, 98, 82)); p[(x + 1) % 32, (y + k) % 32] = px((150, 144, 126)) if rng.random() < 0.5 else p[(x + 1) % 32, (y + k) % 32]
    for _ in range(3):
        x, y = rng.randrange(2, 30), rng.randrange(2, 30)
        for ox, oy in [(0, 0), (1, 0), (0, 1), (1, 1), (-1, 0), (0, -1)]: p[x + ox, y + oy] = px((72, 66, 54))
    return im


def log_top_tile():
    im = Image.new('RGBA', (32, 32), px((200, 180, 128))); g = ImageDraw.Draw(im)
    for i, c in enumerate([(176, 152, 100), (214, 194, 140), (186, 164, 110), (222, 204, 150)]): g.ellipse([3 + i * 3, 3 + i * 3, 28 - i * 3, 28 - i * 3], outline=px(c))
    g.rectangle([15, 15, 16, 16], fill=px((150, 124, 80)))
    g.rectangle([0, 0, 31, 31], outline=px((192, 186, 164))); g.rectangle([1, 1, 30, 30], outline=px((168, 160, 138)))
    return im


def hollow_tile():
    rng = random.Random(77); im = Image.new('RGBA', (32, 32), px((74, 56, 38))); p = im.load()
    for x in range(32):
        for y in range(32):
            if rng.random() < 0.3: p[x, y] = px(rng.choice([(60, 44, 30), (88, 68, 46), (52, 38, 26)]))
    return im


def planks_tile():
    rng = random.Random(31); im = Image.new('RGBA', (32, 32), px((222, 204, 150))); p = im.load(); g = ImageDraw.Draw(im)
    for r in range(4):
        y0 = r * 8
        for x in range(32):
            for y in range(y0, y0 + 8):
                if rng.random() < 0.18: p[x, y] = px(rng.choice([(212, 192, 138), (230, 214, 162), (204, 184, 130)]))
        g.line([0, y0 + 7, 31, y0 + 7], fill=px((170, 148, 98)))
        ex = (r * 11 + 5) % 28; g.line([ex, y0, ex, y0 + 7], fill=px((176, 154, 104)))
    return im


def sapling_tile():
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); g = ImageDraw.Draw(im); rng = random.Random(5)
    g.rectangle([15, 16, 16, 30], fill=px((190, 184, 160)))
    cols = [(236, 196, 52), (228, 128, 30), (196, 52, 36)]
    for cx, cy, r in [(16, 10, 6), (10, 18, 4), (22, 17, 4), (16, 4, 3)]:
        for x in range(cx - r, cx + r + 1):
            for y in range(cy - r, cy + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and rng.random() < 0.88 and 0 <= x < 32 and 0 <= y < 32: im.putpixel((x, y), px(rng.choice(cols)))
    return im


def fungus_tile():
    rng = random.Random(9); im = Image.new('RGBA', (32, 32), px((232, 216, 176))); g = ImageDraw.Draw(im)
    for i, c in enumerate([(206, 150, 84), (222, 176, 106), (190, 128, 70), (236, 200, 132)]):
        g.rectangle([0, 3 + i * 8, 31, 6 + i * 8], fill=px(c))
    for _ in range(40): im.putpixel((rng.randrange(32), rng.randrange(32)), px((250, 240, 214)))
    return im


def tint(im, c):
    """the pack's grey leaf-litter picture in one colour (its shading kept)"""
    im = im.convert('RGBA'); px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            v = (r + g + b) / 3 / 255
            px[x, y] = (min(255, int(c[0] * v * 1.35)), min(255, int(c[1] * v * 1.35)), min(255, int(c[2] * v * 1.35)), a)
    return im


def main(d):
    d = pathlib.Path(d)
    base = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    bj = json.loads((d / 'blocks.json').read_text()); shp = json.loads((d / 'shapes.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text()); pm = json.loads((d / 'packmap.json').read_text())
    mj = json.loads((d / 'machines.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    (d / 'textures/block').mkdir(parents=True, exist_ok=True)
    entry = {e[0]: e for e in pm}
    if 'poplar_log_side' not in tiles: raise SystemExit('dappled_forest.py: run tools/port/poplar_wood.py first (the poplar wood)')

    def tile(name, im, pack=None):
        """a picture: the pack's (pack: its names there) first, ours (data/textures) the fallback; never written into the pack"""
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        e = [name, (pack or []) + [name], [name]]
        if name in entry: pm[pm.index(entry[name])] = e
        else: pm.append(e)
        entry[name] = e
        return tiles[name]

    # ---- the pictures ----
    T = {}
    grey = base / 'leaf_litter.png'
    for c, sh in LITTER.items():
        # each colour is the pack's grey litter painted that colour, and that picture is what is drawn (flat, like the
        # others). Green used to take the pack's untinted grey leaf_litter and a foliage tint; the tint also built it as a
        # raised plate, so it read as grey stone and stood up off the ground.
        im = tint(Image.open(grey), sh[0]) if grey.exists() else litter_tile(sh, sum(map(ord, c)))
        T['leaf_litter_' + c] = tile('leaf_litter_' + c, im)
    T['poplar_log_side'] = tiles['poplar_log_side']; T['poplar_log_hollow'] = tile('poplar_log_hollow', hollow_tile())
    T['poplar_sapling'] = tile('poplar_sapling', sapling_tile(), ['poplar_sapling']); T['shelf_fungus'] = tile('shelf_fungus', fungus_tile())
    Y, O, R = LITTER['yellow'], LITTER['orange'], LITTER['red']
    T['poplar_leaves_yellow'] = tile('poplar_leaves_yellow', leaves_tile(Y, [O[0], R[0]], 61), ['yellow_poplar_leaves'])
    T['poplar_leaves_orange'] = tile('poplar_leaves_orange', leaves_tile(O, [Y[0], R[0]], 62), ['orange_poplar_leaves'])
    T['poplar_leaves_red'] = tile('poplar_leaves_red', leaves_tile(R, [O[0], Y[1]], 63), ['red_poplar_leaves'])
    T['red_shrub_leaves'] = tile('red_shrub_leaves', leaves_tile([R[1], R[0], R[3]], [R[2], O[3]], 64, 0.1), ['dead_red_poplar_leaves', 'red_poplar_leaves'])
    # ---- the models: a hollow log lying along x (a quarter turn makes it lie along z), and the same with shelf fungus ----
    wall = lambda b: {'b': b, 'side': 'poplar_log_side', 'end': 'poplar_log_side'}
    log = [wall([0, 0, 1, 16, 2, 15]), wall([0, 12, 1, 16, 14, 15]), wall([0, 2, 1, 16, 12, 3]), wall([0, 2, 13, 16, 12, 15]),
           {'b': [0, 2, 3, 16, 3, 13], 'side': 'poplar_log_hollow', 'end': 'poplar_log_hollow'}]
    shf = lambda b: {'b': b, 'side': 'shelf_fungus', 'end': 'shelf_fungus'}
    shp['shapes']['hollow_poplar_log'] = log
    shp['shapes']['hollow_poplar_log_fungus'] = log + [shf([3, 14, 4, 7, 15, 8]), shf([9, 14, 7, 13, 15, 10]), shf([5, 4, 15, 10, 6, 16]), shf([4, 8, 0, 8, 10, 1])]

    blocks, faces = bj['blocks'], bj['faces']
    while len(faces) < len(blocks): faces.append(None)
    byKey = {b['key']: b for b in blocks}

    def block(key, name, face, **kw):
        b = {'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 0.2, 'tool': None, 'tier': 0, 'drop': key,
             'light': 0, 'lightAtt': 0, 'sound': 'leaves', 'key': key, 'name': name, 'natural': True}
        b.update(kw)
        if key in byKey: b['id'] = byKey[key]['id']; blocks[b['id']] = b; faces[b['id']] = face
        else: blocks.append(b); faces.append(face)
        byKey[key] = b

    def item(key, name, icon, **kw):
        items[key] = dict({'key': key, 'name': name, 'maxStack': 64, 'icon': icon, 'block': key}, **kw)

    for c in LITTER:   # leaf litter: ground cover like fallen petals (not solid: the ground's height, spawning and building
        # see the dirt under it), replaceable, drops itself; a hoe or shovel lifts it
        k = 'leaf_litter_' + c
        block(k, c.capitalize() + ' Leaf Litter', [T[k]] * 6, render=7, solid=False, opaque=False, replaceable=True, hardness=0.1, tool='shovel', shapeKind=10, needsSupport=True, tex=k, fuel=0.25)
        item(k, c.capitalize() + ' Leaf Litter', 'block:' + k, fuel=0.25,
             desc='Fallen leaves, a thin layer on the ground. They crunch underfoot. Lift them with a hoe or shovel; they compost.')
    for c in ('yellow', 'orange', 'red'):
        k = 'poplar_leaves_' + c
        block(k, 'Poplar Leaves (%s)' % c, [T[k]] * 6, render=2, opaque=False, lightAtt=1, tex=k, isLeaves=True, sapling='poplar_sapling', drop='poplar_sapling', sound='leaves')
        item(k, 'Poplar Leaves (%s)' % c, 'block:' + k)
    block('red_shrub_leaves', 'Red Shrub Leaves', [T['red_shrub_leaves']] * 6, render=2, opaque=False, lightAtt=1, tex='red_shrub_leaves', isLeaves=True)
    item('red_shrub_leaves', 'Red Shrub Leaves', 'block:red_shrub_leaves')
    block('poplar_sapling', 'Poplar Sapling', [T['poplar_sapling']] * 6, render=3, solid=False, opaque=False, hardness=0, sound='grass', tex='poplar_sapling',
          needsSupport=True, fuel=0.5, tree='poplar')
    item('poplar_sapling', 'Poplar Sapling', 'poplar_sapling', fuel=0.5)
    for k, nm in (('hollow_poplar_log', 'Hollow Poplar Log'), ('hollow_poplar_log_fungus', 'Hollow Poplar Log with Shelf Fungus')):
        block(k, nm, [T['poplar_log_side']] * 6, render=6, opaque=False, hardness=2, tool='axe', sound='log', tex='poplar_log_side', shape=k, facingModel=True, drop='poplar_log', fuel=1.5)
    if 'poplar_sapling' not in json.loads((d / 'itemshapes.json').read_text()).get('itemTiles', []):
        ish = json.loads((d / 'itemshapes.json').read_text()); ish['itemTiles'].append('poplar_sapling')
        (d / 'itemshapes.json').write_text(json.dumps(ish, separators=(',', ':'), ensure_ascii=False))

    # ---- tags, composting ----
    tags = rj['tags']
    for t, ks in (('#leaves', ['poplar_leaves_yellow', 'poplar_leaves_orange', 'poplar_leaves_red', 'red_shrub_leaves']), ('#sapling', ['poplar_sapling'])):
        for k in ks:
            if t in tags and k not in tags[t]: tags[t].append(k)
    comp = mj.setdefault('compost', {})
    for k in ['leaf_litter_' + c for c in LITTER] + ['poplar_leaves_yellow', 'poplar_leaves_orange', 'poplar_leaves_red', 'red_shrub_leaves', 'poplar_sapling']:
        comp[k] = comp.get('aspen_leaves' if 'sapling' not in k else 'aspen_sapling', 0.3)

    (d / 'shapes.json').write_text(json.dumps(shp, indent=1, ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), ensure_ascii=False))
    (d / 'machines.json').write_text(json.dumps(mj, separators=(',', ':'), ensure_ascii=False))
    worldgen(d)


def wjson(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2))


def worldgen(d):
    wg = d / 'worldgen'
    leaves = ['poplar_leaves_yellow', 'poplar_leaves_orange', 'poplar_leaves_red']
    tj = json.loads((wg / 'block_tags.json').read_text())
    tj['main:canopy'] = ['#main:leaves'] + ['main:' + k for k in leaves + ['red_shrub_leaves']]
    (wg / 'block_tags.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    # the litter: patches of one colour lying on soil under a canopy (leaves somewhere above)
    canopy = {'type': 'main:any_of', 'predicates': [{'type': 'main:matching_block_tag', 'offset': [0, k, 0], 'tag': 'main:canopy'} for k in range(2, 11)]}
    for c in LITTER:
        # under a canopy (other woods: the litter lies under their trees), and anywhere on the floor (the Dappled Forest,
        # carpeted: its poplars are slim, so few spots have leaves straight above them)
        for name, extra, tries in (('leaf_litter_%s' % c, [canopy], 56), ('leaf_carpet_%s' % c, [], 96)):
            wjson(wg / ('configured_feature/dappled/%s.json' % name), {'type': 'main:random_patch', 'config': {'tries': tries, 'xz_spread': 6, 'y_spread': 2, 'feature': {
                'feature': {'type': 'main:simple_block', 'config': {'to_place': {'type': 'main:simple_state_provider', 'state': {'Name': 'main:leaf_litter_' + c}}}},
                'placement': [{'type': 'main:block_predicate_filter', 'predicate': {'type': 'main:all_of', 'predicates': [
                    {'type': 'main:matching_blocks', 'offset': [0, 0, 0], 'blocks': ['main:air', 'main:grass', 'main:fern']},
                    {'type': 'main:matching_block_tag', 'offset': [0, -1, 0], 'tag': 'main:dirt'}] + extra}}]}}})
        for how, count, feat in (('dense', 10, 'dappled:leaf_carpet_'), ('common', 3, 'dappled:leaf_litter_')):
            wjson(wg / ('placed_feature/dappled/litter_%s_%s.json' % (c, how)), {'feature': feat + c, 'placement': [
                {'type': 'main:count', 'count': count}, {'type': 'main:in_square'}, {'type': 'main:heightmap', 'heightmap': 'MOTION_BLOCKING'}, {'type': 'main:biome'}]})
        wjson(wg / ('placed_feature/dappled/litter_%s_sparse.json' % c), {'feature': 'dappled:leaf_litter_' + c, 'placement': [
            {'type': 'main:rarity_filter', 'chance': 2}, {'type': 'main:count', 'count': 2}, {'type': 'main:in_square'}, {'type': 'main:heightmap', 'heightmap': 'MOTION_BLOCKING'}, {'type': 'main:biome'}]})
    wjson(wg / 'configured_feature/dappled/fallen_log.json', {'type': 'dappled:fallen_log', 'config': {}})
    wjson(wg / 'placed_feature/dappled/fallen_log.json', {'feature': 'dappled:fallen_log', 'placement': [
        {'type': 'main:rarity_filter', 'chance': 2}, {'type': 'main:in_square'}, {'type': 'main:heightmap', 'heightmap': 'MOTION_BLOCKING'}, {'type': 'main:biome'}]})
    # which woods get which litter: the first group naming the biome (the autumn woods red, orange and yellow; the others a little green and brown)
    wjson(wg / 'ground_cover.json', {'groups': [
        {'match': ['autumnal', 'maple', 'boreal_forest_red', 'boreal_forest_yellow', 'beech'], 'features': ['dappled:litter_red_common', 'dappled:litter_orange_common', 'dappled:litter_yellow_common']},
        {'match': ['main:forest', 'birch_forest', 'flower_forest', 'dark_forest', 'cool_forest', 'forest_edge', 'windswept_forest', 'tangled_forest', 'warm_birch', 'aspen',
                   'temperate_rainforest', 'wisteria', 'sakura', 'cherry', 'phantasmal', 'spring_flower'], 'features': ['dappled:litter_green_sparse', 'dappled:litter_brown_sparse']}]})
    # the Dappled Forest itself: a temperate wood's weather and creatures, its ground carpeted red and orange, fallen logs
    forest = json.loads((wg / 'biome/main/forest.json').read_text())
    forest['temperature'] = 0.6; forest['downfall'] = 0.7
    eff = forest.setdefault('effects', {}); eff['grass_color'] = 0xA6A83C; eff['foliage_color'] = 0xC8902A
    F = forest['features']
    F[9] = ['dappled:fallen_log', 'dappled:litter_red_dense', 'dappled:litter_orange_dense', 'dappled:litter_yellow_common'] + [f for f in F[9] if 'tree' not in f]   # (the logs first: the litter would take their ground)
    wjson(wg / 'biome/dappled/dappled_forest.json', forest)
    rg = json.loads((wg / 'biome_regions.json').read_text())
    if 'dappled:dappled_forest' not in rg['biomes']: rg['biomes'].append('dappled:dappled_forest')
    (wg / 'biome_regions.json').write_text(json.dumps(rg, separators=(',', ':'), ensure_ascii=False))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
