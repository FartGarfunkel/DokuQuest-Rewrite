#!/usr/bin/env python3
"""Forged tools: mix-and-match heads and hafts (idempotent; data_patches.py runs it). Our own code and art (the idea of
building tools from parts is Cubyz's; nothing is taken from it):
  python3 tools/port/forging.py <data-dir>
- the Tool Forge (a block): put in a material and pick a head (sim/stations.cpp forgeOptions; a pickaxe or axe head
  takes three, a sword or hoe head two, a shovel head one)
- <material>_<tool>_head for 12 materials x 5 tools, pictured from the iron tool's head recoloured
- a head and a haft make the tool (shapeless, any grid): a stick gives the plain tool (the existing one where there is
  one), a bone haft is sturdier and slower, a cinder rod faster and hot (its blades burn) but wears sooner, a steel haft
  much sturdier and a little slower
- creative tab "Forged" (ui/inventory.cpp: items with "forged")"""
import json, sys, pathlib, colorsys
from PIL import Image

TOOLS = {'pickaxe': 3, 'axe': 3, 'shovel': 1, 'sword': 2, 'hoe': 2}
# material: (ingredient, name, colour, stats when there is no tool set of it yet: tier, speed, damage of a pickaxe, durability, element)
MATS = {
    'flint': ('flint', 'Flint', (78, 78, 88), (2, 4.2, 3, 120, None)),
    'copper': ('copper_ingot', 'Copper', (214, 124, 82), None),
    'iron': ('iron_ingot', 'Iron', (214, 214, 218), None),
    'silver': ('silver_ingot', 'Silver', (228, 234, 244), None),
    'gold': ('gold_ingot', 'Gold', (250, 210, 70), None),
    'steel': ('steel_ingot', 'Steel', (150, 160, 178), None),
    'diamond': ('diamond', 'Diamond', (96, 232, 222), None),
    'ruby': ('ruby', 'Ruby', (222, 40, 64), (4, 8.5, 5, 1300, None)),
    'sapphire': ('sapphire', 'Sapphire', (52, 92, 222), (4, 9, 5, 1400, None)),
    'mithril': ('mithril_ingot', 'Mithril', (168, 222, 244), None),
    'obsidian': ('obsidian', 'Obsidian', (52, 36, 80), (4, 5, 6, 3000, None)),
    'emberglass': ('emberglass_cluster', 'Emberglass', (240, 96, 40), (3, 9.5, 5, 320, 'fire')),
}
# haft: (item, adjective, colour or None = the stick's own, speed x, durability x, element)
HAFTS = {
    'stick': ('stick', '', None, 1.0, 1.0, None),
    'bone': ('bone', 'Bone-Hafted ', (232, 226, 204), 0.95, 1.3, None),
    'cinder': ('cinder_rod', 'Cinder-Hafted ', (210, 92, 40), 1.15, 0.85, 'fire'),
    'steel': ('steel_ingot', 'Steel-Hafted ', (140, 150, 166), 0.9, 1.6, None),
}


def is_haft(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return 0.03 < h < 0.16 and s > 0.25 and v < 0.85


def classify(tp):
    """haft (True) or head (False) for every pixel: bright ones by colour, dark outline ones take their nearest bright neighbour's"""
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
    rj = json.loads((d / 'recipes.json').read_text()); bj = json.loads((d / 'blocks.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    (d / 'textures/item/forged').mkdir(parents=True, exist_ok=True)

    def pic(name):
        return Image.open(d / files[tiles[name]]).convert('RGBA').resize((32, 32), Image.NEAREST)

    def item_tile(name, im):
        rel = 'textures/item/forged/%s.png' % name
        im.save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if name not in sh['itemTiles']: sh['itemTiles'].append(name)

    def recipe(out, n, shape=None, key=None, items_=None):
        if any(r.get('out') == out and r.get('items') == items_ for r in rj['recipes']): return
        r = {'out': out, 'n': n}
        if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
        else: r['items'] = items_
        rj['recipes'].append(r)

    for t, cost in TOOLS.items():
        tmpl = pic('iron_' + t); tp = tmpl.load(); cls = classify(tp)
        head_px = [tp[p] for p, h in cls.items() if not h and max(tp[p][:3]) >= 64]
        haft_px = [tp[p] for p, h in cls.items() if h and max(tp[p][:3]) >= 64]
        lum = lambda L: max(1, sum(0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2] for p in L) / max(1, len(L)))
        href, sref = lum(head_px), lum(haft_px)
        iron = items['iron_' + t]['tool']
        for m, (ing, mname, mrgb, own) in MATS.items():
            base = items.get(m + '_' + t, {}).get('tool')
            if base is None:
                tier, speed, dmg, dur, el = own
                base = {'type': t, 'tier': tier, 'speed': speed, 'damage': iron['damage'] + (dmg - items['iron_pickaxe']['tool']['damage']), 'durability': dur}
                if el and t in ('sword', 'axe'): base['element'] = el
            # the head
            hk = '%s_%s_head' % (m, t)
            hn = 'forged_' + hk
            if True:
                im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
                for y in range(32):
                    for x in range(32):
                        c = tp[x, y]
                        if c[3] and not cls.get((x, y), True): o[x, y] = tint(c, mrgb, href)
                item_tile(hn, im)
            items[hk] = {'key': hk, 'name': '%s %s Head' % (mname, t.title()), 'maxStack': 16, 'icon': hn, 'forged': True,
                         'forgeHead': {'mat': ing, 'cost': cost, 'tool': t}}
            for hf, (hitem, adj, hrgb, sx, dx, hel) in HAFTS.items():
                vanilla = hf == 'stick' and (m + '_' + t) in items and not items[m + '_' + t].get('forged')
                key = (m + '_' + t) if hf == 'stick' else '%s_hafted_%s_%s' % (hf, m, t)
                if not vanilla:
                    tn = 'forged_' + key
                    if True:
                        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); o = im.load()
                        for y in range(32):
                            for x in range(32):
                                c = tp[x, y]
                                if not c[3]: continue
                                o[x, y] = (tint(c, hrgb, sref) if hrgb else c) if cls.get((x, y), False) else tint(c, mrgb, href)
                        item_tile(tn, im)
                    tool = dict(base)
                    tool['speed'] = round(base['speed'] * sx, 2)
                    tool['durability'] = int(base['durability'] * dx)
                    if hel and t in ('sword', 'axe'): tool['element'] = hel
                    items[key] = {'key': key, 'name': '%s%s %s' % (adj, mname, t.title()), 'maxStack': 1, 'icon': tn, 'forged': True, 'tool': tool}
                recipe(key, 1, items_=[hk, hitem])

    # the Tool Forge: a stone hearth with an iron anvil top
    blocks = bj['blocks']
    if not any(b['key'] == 'tool_forge' for b in blocks):
        def rec(name, fn):
            im = pic(name); px = im.load()
            for y in range(32):
                for x in range(32):
                    if px[x, y][3]: px[x, y] = fn(*px[x, y])
            rel = 'textures/block/%s.png' % ('tool_forge_' + name.split('_')[-1])
            im.save(d / rel); nm = 'tool_forge_' + name.split('_')[-1]
            if nm not in tiles: tiles[nm] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
            return tiles[nm]
        warm = lambda r, g, b, a: (min(255, int(r * 1.05 + 18)), int(g * 0.8), int(b * 0.62), a)
        top = rec('smithing_table_top', lambda r, g, b, a: (int(r * 0.9), int(g * 0.9), int(b * 0.95), a))
        side = rec('smithing_table_side', warm)
        front = rec('smithing_table_front', warm)
        bottom = rec('smithing_table_bottom', warm)
        blocks.append({'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 3.5, 'tool': 'pickaxe',
                       'tier': 0, 'drop': 'tool_forge', 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': 'tool_forge', 'name': 'Tool Forge',
                       'forge': True,
                       'tex': {'top': 'tool_forge_top', 'bottom': 'tool_forge_bottom', 'side': 'tool_forge_side', 'front': 'tool_forge_front'}})
        bj['faces'].append([side, side, top, bottom, front, side])
    items['tool_forge'] = {'key': 'tool_forge', 'name': 'Tool Forge', 'maxStack': 64, 'icon': 'block:tool_forge', 'block': 'tool_forge', 'forged': True}
    recipe('tool_forge', 1, ['III', 'BFB', 'BBB'], {'I': 'iron_ingot', 'F': 'furnace', 'B': 'stone_bricks'})

    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
