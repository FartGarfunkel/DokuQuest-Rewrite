#!/usr/bin/env python3
"""Fixes on top of the extracted data (run after extract_all.py; idempotent):
  python3 tools/port/data_patches.py <data-dir>
- gate_barrier (a walled town's gate while shut) is drawn: the original drew the gate as a moving model from
  whether the barrier blocks were there; here the barrier blocks themselves are the iron-banded timber gate.
- stacked vines get a body picture (<key>_plant) next to their tip picture, so a strand reads as one plant: the body
  is the tip's strands run the full height of the tile. Packs supply their own *_plant pictures (packmap)."""
import json, sys, pathlib
from PIL import Image

d = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'data')

# ---- blocks ----
bj = json.loads((d / 'blocks.json').read_text())
tnames = json.loads((d / 'tiles.json').read_text())['names']
blocks = bj['blocks']
for b in (blocks.values() if isinstance(blocks, dict) else blocks):
    if isinstance(b, dict) and b.get('key') == 'gate_barrier':
        b['render'] = 2; b['tex'] = 'spruce_door_lower'; b['selfCull'] = True
        bj['faces'][b['id']] = [tnames['spruce_door_lower']] * 6
(d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), sort_keys=False))

# ---- tiles: body pictures for stacked vines ----
tj = json.loads((d / 'tiles.json').read_text())
names, T, per = tj['names'], tj['tile'], tj['perRow']
img = Image.open(d / 'tiles.png').convert('RGBA') if (d / 'tiles.png').exists() else None   # (retired: one file per picture now, the vine bodies among them)
new = []
for tip in ['weeping_vines', 'twisting_vines', 'cave_vines', 'cave_vines_lit']:
    body = tip.replace('cave_vines', 'cave_vines_plant') if tip.startswith('cave_vines') else tip + '_plant'
    if tip not in names or img is None:
        continue
    i = names[tip]; sx, sy = (i % per) * T, (i // per) * T
    src = img.crop((sx, sy, sx + T, sy + T)); px = src.load()
    out = Image.new('RGBA', (T, T), (0, 0, 0, 0)); po = out.load()
    for x in range(T):
        col = [px[x, y] for y in range(T) if px[x, y][3] > 0]
        if len(col) < T // 4:
            continue   # a stray leaf, not a strand
        for y in range(T):
            po[x, y] = col[y % len(col)]
    k = names.get(body, tj['count'])
    if body not in names:
        names[body] = k; tj['count'] += 1
    img.paste(out, ((k % per) * T, (k // per) * T))
    new.append(body)
if img is not None: img.save(d / 'tiles.png', optimize=True)
(d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), sort_keys=False))

# ---- packmap: the pack's own body pictures ----
pm = json.loads((d / 'packmap.json').read_text())
have = {e[0] for e in pm}
for n in new:
    if n not in have:
        pm.append([n, [n], [n]])
(d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), sort_keys=False))
print('patched: gate_barrier drawn; body tiles', new, 'count', tj['count'])

# ---- the enderman's jaw: Minecraft's shrunk "headwear" box, hidden inside the head until it lifts its head in anger ----
mj = json.loads((d / 'models.json').read_text())
en = mj.get('enderman')
if en and not any(pt['name'] == 'jaw' for pt in en['parts']):
    i = next(k for k, pt in enumerate(en['parts']) if pt['name'] == 'head')
    en['parts'].insert(i + 1, {"name": "jaw", "parent": "body", "at": [0, 12, 0], "box": [-3.5, 0.5, -3.5, 3.5, 7.5, 3.5],
                               "tex": "ender_skin", "pivot": [0, 0.75, 0], "min": [-0.21875, 0.03125, -0.21875], "max": [0.21875, 0.46875, 0.21875]})
for pt in (en['parts'] if en else []):   # the jaw: 7x5x7 on the shoulders, its toothed rim just under the lifted head
    if pt['name'] == 'jaw':
        pt['box'] = [-3.5, 0, -3.5, 3.5, 5, 3.5]; pt['min'] = [-0.21875, 0, -0.21875]; pt['max'] = [0.21875, 0.3125, 0.21875]
for pt in (en['parts'] if en else []):   # its legs a little apart (Minecraft's +-2 px), not pressed together
    if pt['name'] in ('legR', 'legL') and abs(pt['at'][0]) == 1:
        sx = 1 if pt['at'][0] > 0 else -1; pt['at'][0] = 2 * sx; pt['pivot'][0] = 0.125 * sx
(d / 'models.json').write_text(json.dumps(mj, separators=(',', ':'), ensure_ascii=False))

# ---- the lava boat: a rowboat of crimson or warped wood (which doesn't burn) that floats on lava ----
from PIL import Image as _I
items = json.loads((d / 'items.json').read_text()); tj2 = json.loads((d / 'tiles.json').read_text())
rj = json.loads((d / 'recipes.json').read_text()); sh2 = json.loads((d / 'itemshapes.json').read_text())
if 'lava_rowboat' not in tj2['names']:
    src = _I.open(d / tj2['files'][tj2['names']['rowboat']]).convert('RGBA'); px = src.load()
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            r, g, b, a = px[x, y]
            if a: l = (r * 299 + g * 587 + b * 114) // 1000; px[x, y] = (min(255, l * 3 // 2 + 30), l * 2 // 5, l * 3 // 5, a)   # crimson wood
    src.save(d / 'textures/item/lava_rowboat.png')
    tj2['res'] += [32] * max(0, tj2['count'] - len(tj2['res']))
    tj2['names']['lava_rowboat'] = tj2['count']; tj2['files'].append('textures/item/lava_rowboat.png'); tj2['res'].append(32); tj2['count'] += 1
    (d / 'tiles.json').write_text(json.dumps(tj2, separators=(',', ':'), ensure_ascii=False))
if 'lava_rowboat' not in sh2['itemTiles']:
    sh2['itemTiles'].append('lava_rowboat'); (d / 'itemshapes.json').write_text(json.dumps(sh2, separators=(',', ':'), ensure_ascii=False))
items['lava_rowboat'] = {'key': 'lava_rowboat', 'name': 'Lava Boat', 'maxStack': 1, 'icon': 'lava_rowboat', 'boat': 'lava_rowboat',
                         'desc': 'Crimson or warped wood floats on lava. Place on lava or water; the lava never reaches you aboard.'}
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
if not any(r.get('out') == 'lava_rowboat' for r in rj['recipes']):
    for wood in ('crimson_planks', 'warped_planks'):
        rj['recipes'].append({'out': 'lava_rowboat', 'n': 1, 'shape': ['PMP', 'PPP'], 'key': {'P': wood, 'M': 'magma_cream'}, 'h': 2, 'w': 3})
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))

# ---- gold coins: the villagers' money (emeralds are a precious ore again; they still pay, at four coins each) ----
items = json.loads((d / 'items.json').read_text()); tj3 = json.loads((d / 'tiles.json').read_text())
sh3 = json.loads((d / 'itemshapes.json').read_text())
if 'gold_coin' not in tj3['names']:
    im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
    for y in range(32):
        for x in range(32):
            dx, dy = x - 15.5, y - 15.5; r = (dx * dx + dy * dy) ** 0.5
            if r > 11.5: continue
            lit = 1 - (dx + dy) / 40                        # lit from the top left
            if r > 10.2: c = (150, 96, 20)                   # the dark milled edge
            elif r > 8.6: c = (255, 214, 92)                 # the raised rim
            else: c = (226, 168, 40)                         # the face
            if 3.2 < r < 4.6 or (abs(dx) < 1 and abs(dy) < 5.5): c = (255, 228, 120)   # a ring and a bar: the crown's stamp
            px[x, y] = (min(255, int(c[0] * lit)), min(255, int(c[1] * lit)), min(255, int(c[2] * lit)), 255)
    im.save(d / 'textures/item/gold_coin.png')
    tj3['res'] += [32] * max(0, tj3['count'] - len(tj3['res']))
    tj3['names']['gold_coin'] = tj3['count']; tj3['files'].append('textures/item/gold_coin.png'); tj3['res'].append(32); tj3['count'] += 1
    (d / 'tiles.json').write_text(json.dumps(tj3, separators=(',', ':'), ensure_ascii=False))
if 'gold_coin' not in sh3['itemTiles']:
    sh3['itemTiles'].append('gold_coin'); (d / 'itemshapes.json').write_text(json.dumps(sh3, separators=(',', ':'), ensure_ascii=False))
items['gold_coin'] = {'key': 'gold_coin', 'name': 'Gold Coin', 'maxStack': 64, 'icon': 'gold_coin',
                      'desc': 'The villages\' money. Shopkeepers still take emeralds, at four coins each.'}
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))

# ---- spawn eggs for every creature (tools/port/spawn_eggs.py) ----
import importlib.util as _u
_s = _u.spec_from_file_location('spawn_eggs', pathlib.Path(__file__).with_name('spawn_eggs.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)

# ---- mob heads (tools/port/mob_heads.py) ----
_s = _u.spec_from_file_location('mob_heads', pathlib.Path(__file__).with_name('mob_heads.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)

# ---- BTA-inspired blocks (tools/port/bta_blocks.py) ----
_s = _u.spec_from_file_location('bta_blocks', pathlib.Path(__file__).with_name('bta_blocks.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)
_s2 = _u.spec_from_file_location('bta_more', pathlib.Path(__file__).with_name('bta_more.py')); _m2b = _u.module_from_spec(_s2); _s2.loader.exec_module(_m2b); _m2b.main(d)
_s3 = _u.spec_from_file_location('nether_bta', pathlib.Path(__file__).with_name('nether_bta.py')); _m3 = _u.module_from_spec(_s3); _s3.loader.exec_module(_m3); _m3.main(d)
_s4 = _u.spec_from_file_location('discs', pathlib.Path(__file__).with_name('discs.py')); _m4 = _u.module_from_spec(_s4); _s4.loader.exec_module(_m4); _m4.main(d)
_s5 = _u.spec_from_file_location('bees', pathlib.Path(__file__).with_name('bees.py')); _m5 = _u.module_from_spec(_s5); _s5.loader.exec_module(_m5); _m5.main(d)
_s7 = _u.spec_from_file_location('travel', pathlib.Path(__file__).with_name('travel.py')); _m7 = _u.module_from_spec(_s7); _s7.loader.exec_module(_m7); _m7.main(d)

# ---- forged tools: heads and hafts, the Tool Forge (tools/port/forging.py) ----
_s = _u.spec_from_file_location('forging', pathlib.Path(__file__).with_name('forging.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)
_s = _u.spec_from_file_location('tool_parts', pathlib.Path(__file__).with_name('tool_parts.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)

# ---- the lead (Minecraft's): leads an animal, ties it to a fence post (sim/leads.cpp) ----
items = json.loads((d / 'items.json').read_text()); tj4 = json.loads((d / 'tiles.json').read_text())
sh4 = json.loads((d / 'itemshapes.json').read_text()); rj4 = json.loads((d / 'recipes.json').read_text())
if 'lead' not in tj4['names']:
    import math as _m
    im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
    def rope(x, y, t):   # a twisted rope cell: light and dark strands
        c = (196, 150, 96) if (int(t) // 2) % 2 == 0 else (150, 104, 60)
        for ox, oy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            if 0 <= x + ox < 32 and 0 <= y + oy < 32: px[x + ox, y + oy] = c + (255,)
    for k in range(80):   # the loop at the top right
        a = k / 80 * 2 * _m.pi; rope(int(20 + 8 * _m.cos(a)), int(10 + 7 * _m.sin(a)), k)
    for k in range(26):   # the tail down to the bottom left
        rope(int(13 - k * 0.42), int(15 + k * 0.55), k + 3)
    for y in range(13, 18):   # the knot: a dab of slime green
        for x in range(10, 15):
            if (x - 12) ** 2 + (y - 15) ** 2 <= 5: px[x, y] = (96, 176, 72, 255) if (x + y) % 3 else (60, 130, 48, 255)
    im.save(d / 'textures/item/lead.png')
    tj4['res'] += [32] * max(0, tj4['count'] - len(tj4['res']))
    tj4['names']['lead'] = tj4['count']; tj4['files'].append('textures/item/lead.png'); tj4['res'].append(32); tj4['count'] += 1
    (d / 'tiles.json').write_text(json.dumps(tj4, separators=(',', ':'), ensure_ascii=False))
if 'lead' not in sh4['itemTiles']:
    sh4['itemTiles'].append('lead'); (d / 'itemshapes.json').write_text(json.dumps(sh4, separators=(',', ':'), ensure_ascii=False))
items['lead'] = {'key': 'lead', 'name': 'Lead', 'maxStack': 64, 'icon': 'lead',
                 'desc': 'Use it on an animal to lead it; use a fence post to tie it there. Use the animal again to let go.'}
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
if not any(r.get('out') == 'lead' for r in rj4['recipes']):
    rj4['recipes'].append({'out': 'lead', 'n': 2, 'shape': ['SS ', 'SB ', '  S'], 'key': {'S': 'string', 'B': 'slime_ball'}, 'h': 3, 'w': 3, 'group': 'tools'})
    (d / 'recipes.json').write_text(json.dumps(rj4, separators=(',', ':'), ensure_ascii=False))

# ---- plant pictures drawn grey for the biome tint (Minecraft-style) must carry the game's "tint me" marker (alpha 253);
#      spruce leaves aren't tinted by biome in Minecraft, so theirs get its fixed colour painted in ----
tj5 = json.loads((d / 'tiles.json').read_text())
def _grey(im):
    op = [p for p in im.getdata() if p[3] >= 128]
    return op and sum(max(p[:3]) - min(p[:3]) for p in op) / len(op) < 8 and not any(p[3] == 253 for p in im.getdata())
for _n in ('acacia_leaves', 'birch_leaves', 'dark_oak_leaves', 'jungle_leaves', 'palm_leaves', 'fern', 'vine', 'spruce_leaves'):
    if _n not in tj5['names']: continue
    _p = d / tj5['files'][tj5['names'][_n]]
    _im = Image.open(_p).convert('RGBA')
    if not _grey(_im): continue
    _px = _im.load()
    for _y in range(_im.size[1]):
        for _x in range(_im.size[0]):
            r, g, b, a = _px[_x, _y]
            if a < 128: continue
            if _n == 'spruce_leaves': _px[_x, _y] = (r * 97 // 255, g * 153 // 255, b * 97 // 255, 255)
            else: _px[_x, _y] = (r, g, b, 253)
    _im.save(_p)
    print('tint marker:', _n)

# ---- the Travel Journal (BTA's guidebook): every thing you've carried, with how it's made (ui/travel_journal.cpp) ----
items = json.loads((d / 'items.json').read_text()); tj6 = json.loads((d / 'tiles.json').read_text())
sh6 = json.loads((d / 'itemshapes.json').read_text()); rj6 = json.loads((d / 'recipes.json').read_text())
if 'travel_journal' not in tj6['names']:
    src = _I.open(d / tj6['files'][tj6['names']['book']]).convert('RGBA'); px = src.load()
    for y in range(src.size[1]):
        for x in range(src.size[0]):
            r, g, b, a = px[x, y]
            if a and r > g + 20: px[x, y] = (r * 70 // 100, g * 85 // 100 + 18, b * 60 // 100, a)   # a weathered green cover
    for x, y in ((15, 13), (16, 13), (15, 14), (16, 14), (14, 12), (17, 15), (17, 12), (14, 15)):   # a compass rose on it
        if px[x, y][3]: px[x, y] = (236, 206, 120, 255)
    src.save(d / 'textures/item/travel_journal.png')
    tj6['res'] += [32] * max(0, tj6['count'] - len(tj6['res']))
    tj6['names']['travel_journal'] = tj6['count']; tj6['files'].append('textures/item/travel_journal.png'); tj6['res'].append(32); tj6['count'] += 1
    (d / 'tiles.json').write_text(json.dumps(tj6, separators=(',', ':'), ensure_ascii=False))
if 'travel_journal' not in sh6['itemTiles']:
    sh6['itemTiles'].append('travel_journal'); (d / 'itemshapes.json').write_text(json.dumps(sh6, separators=(',', ':'), ensure_ascii=False))
items['travel_journal'] = {'key': 'travel_journal', 'name': 'Travel Journal', 'maxStack': 1, 'icon': 'travel_journal',
                           'desc': 'Everything you have come across, and how each is made. Use it to read.'}
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
if not any(r.get('out') == 'travel_journal' for r in rj6['recipes']):
    rj6['recipes'].append({'out': 'travel_journal', 'n': 1, 'items': ['book', 'compass']})
    (d / 'recipes.json').write_text(json.dumps(rj6, separators=(',', ':'), ensure_ascii=False))

# ---- the shulker: a purpur shell clamped to the End cities' walls; it opens to send a seeking bolt that lifts you
#      off your feet (sim/creature_monsters.cpp, the "shulker" bolt in sim/creature_entities.cpp) ----
mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
mg = json.loads((d / 'magic.json').read_text())
def _part(name, at, box, tex):
    return {'name': name, 'parent': None, 'at': at, 'box': box, 'tex': tex, 'pivot': [at[0] / 16, at[1] / 16, at[2] / 16],
            'min': [box[0] / 16, box[1] / 16, box[2] / 16], 'max': [box[3] / 16, box[4] / 16, box[5] / 16]}
models['shulker'] = {'rig': 'shulker', 'scale': 1, 'parts': [
    _part('base', [0, 0, 0], [-8, 0, -8, 8, 8, 8], 'purpur_block'),
    _part('head', [0, 5, 0], [-3, 0, -3, 3, 6, 3], 'end_stone'),
    _part('lid', [0, 4, 8], [-8, 0, -16, 8, 12, 0], 'purpur_block')]}
mobs['shulker'] = {'model': 'shulker', 'label': 'a shulker', 'hp': 30, 'w': 0.5, 'h': 1.0, 'speed': 0, 'xp': 5, 'hostile': True,
                   'special': True, 'damage': 0, 'noPush': True, 'knockResist': 1, 'drops': [['ender_pearl', 0, 1, 0.3], ['purpur_block', 1, 2, 0.6]]}
mg['effects']['levitation'] = {'name': 'Levitation', 'col': '#ceb8f0', 'bad': True}
(d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
(d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
(d / 'magic.json').write_text(json.dumps(mg, separators=(',', ':'), ensure_ascii=False))

# ---- the Wither: summoned (never natural) by three wither skeleton skulls on a T of soul sand; a three-headed
#      flying boss whose skulls burst and wither you (sim/wither.cpp) ----
mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
mg = json.loads((d / 'magic.json').read_text()); items = json.loads((d / 'items.json').read_text())
tj7 = json.loads((d / 'tiles.json').read_text()); sh7 = json.loads((d / 'itemshapes.json').read_text())
B_ = {'all': 'wskel_ribs', 'top': 'wskel_bone', 'bottom': 'wskel_bone'}
H_ = {'all': 'wskel_head', 'front': 'wskel_face'}
models['wither'] = {'rig': 'wither', 'scale': 1.5, 'parts': [
    _part('spine', [0, 8, 1], [-1.5, 0, -1.5, 1.5, 13, 1.5], 'wskel_bone'),
    _part('rib1', [0, 12, 1], [-5, 0, -1, 5, 1.5, 1], B_), _part('rib2', [0, 15, 1], [-5, 0, -1, 5, 1.5, 1], B_), _part('rib3', [0, 18, 1], [-5, 0, -1, 5, 1.5, 1], B_),
    _part('shoulders', [0, 21, 0], [-10, 0, -1.5, 10, 3, 1.5], 'wskel_bone'),
    _part('head', [0, 24, 0], [-4, 0, -4, 4, 8, 4], H_),
    _part('headL', [-8, 23, 0], [-3, 0, -3, 3, 6, 3], H_), _part('headR', [8, 23, 0], [-3, 0, -3, 3, 6, 3], H_)]}
mobs['wither'] = {'model': 'wither', 'label': 'the Wither', 'hp': 300, 'w': 0.9, 'h': 3.0, 'speed': 1.4, 'fly': 6, 'xp': 50, 'hostile': True,
                  'boss': True, 'special': True, 'flyer': True, 'undead': True, 'fireImmune': True, 'knockResist': 1, 'damage': 0,
                  'drops': [['nether_star', 1, 1, 1]]}
mg['effects']['wither'] = {'name': 'Wither', 'col': '#3a3030', 'bad': True}
# the Wither Storm: a Wither grown out of a rift core (sim/wither.cpp stormBrain): the same bones, huge and dark
models['wither_storm'] = dict(models['wither']); models['wither_storm']['scale'] = 5.0
mobs['wither_storm'] = {'model': 'wither_storm', 'label': 'the Wither Storm', 'hp': 900, 'w': 2.8, 'h': 9.5, 'speed': 1.1, 'fly': 5, 'xp': 500, 'hostile': True,
                        'boss': True, 'special': True, 'flyer': True, 'undead': True, 'fireImmune': True, 'knockResist': 1, 'damage': 0,
                        'tint': [0.22, 0.06, 0.32, 0.45], 'drops': [['nether_star', 3, 3, 1], ['rift_crystal', 6, 10, 1]]}
if 'nether_star' not in tj7['names']:
    im = _I.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
    import math as _m2
    for y in range(32):
        for x in range(32):
            dx, dy = x - 15.5, y - 15.5; a = _m2.atan2(dy, dx); r = (dx * dx + dy * dy) ** 0.5
            lim = 6 + 7 * abs(_m2.cos(2 * a)) ** 3   # a four-pointed star
            if r < lim: px[x, y] = (255, 255, 236, 255) if r < 4 else (224, 232, 255, 255) if r < lim - 2 else (160, 176, 220, 255)
    im.save(d / 'textures/item/nether_star.png')
    tj7['res'] += [32] * max(0, tj7['count'] - len(tj7['res']))
    tj7['names']['nether_star'] = tj7['count']; tj7['files'].append('textures/item/nether_star.png'); tj7['res'].append(32); tj7['count'] += 1
    (d / 'tiles.json').write_text(json.dumps(tj7, separators=(',', ':'), ensure_ascii=False))
if 'nether_star' not in sh7['itemTiles']:
    sh7['itemTiles'].append('nether_star'); (d / 'itemshapes.json').write_text(json.dumps(sh7, separators=(',', ':'), ensure_ascii=False))
items['nether_star'] = {'key': 'nether_star', 'name': 'Nether Star', 'maxStack': 64, 'icon': 'nether_star', 'glint': True,
                        'desc': 'All that is left of the Wither. It never burns, never fades.'}
for f, o in (('mobs.json', mobs), ('models.json', models), ('magic.json', mg)): (d / f).write_text(json.dumps(o, separators=(',', ':'), ensure_ascii=False))
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))

# ---- biome fuselings (our own creeper's cousins): a frost one in the snow, a dune one in the desert, a bog one in
#      the swamps; same build, their own colour, and their own burst (sim/creature_monsters.cpp fuseBurst) ----
mobs = json.loads((d / 'mobs.json').read_text())
base = mobs['fuseling']
for k, label, tint in (('frost_fuseling', 'a frost fuseling', [0.62, 0.86, 1.0, 0.55]), ('dune_fuseling', 'a dune fuseling', [1.0, 0.86, 0.55, 0.55]),
                       ('bog_fuseling', 'a bog fuseling', [0.45, 0.6, 0.3, 0.55]), ('ember_fuseling', 'an ember fuseling', [1.0, 0.42, 0.18, 0.55]),
                       ('moss_fuseling', 'a moss fuseling', [0.36, 0.64, 0.28, 0.5]), ('sky_fuseling', 'a sky fuseling', [0.86, 0.93, 1.0, 0.5])):
    v = dict(base); v['label'] = label; v['model'] = base.get('model', 'fuseling'); v['tint'] = tint
    if k == 'ember_fuseling': v['fireImmune'] = True
    mobs[k] = v
(d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))

# ---- village golems take after their land: sandstone in the desert, frost in the snow, mossy in the taiga ----
mobs = json.loads((d / 'mobs.json').read_text())
for k, label, tint in (('sandstone_golem', 'a sandstone golem', [1.0, 0.85, 0.55, 0.5]), ('frost_golem', 'a frost golem', [0.7, 0.88, 1.0, 0.5]),
                       ('moss_golem', 'a mossy golem', [0.5, 0.72, 0.38, 0.5])):
    v = dict(mobs['iron_golem']); v['label'] = label; v['tint'] = tint
    mobs[k] = v
(d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))

# ---- every drop has a use (tools/port/audit.py): pelts and crocodile scales tan into leather, shark teeth tip arrows,
#      glow ink makes a glow item frame (and makes sign writing glow: sim/build.cpp) ----
rj = json.loads((d / 'recipes.json').read_text())
def _recipe(out, n, items_=None, shape=None, key=None):
    if any(r.get('out') == out and r.get('items') == items_ and r.get('shape') == shape for r in rj['recipes']): return
    r = {'out': out, 'n': n}
    if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
    else: r.update({'items': items_, 'w': 2, 'h': 2})
    r['idx'] = len(rj['recipes']); rj['recipes'].append(r)
_recipe('leather', 1, ['pelt'])
_recipe('leather', 1, ['croc_scale', 'croc_scale'])
_recipe('arrow', 4, shape=['T', 'S', 'F'], key={'T': 'shark_tooth', 'S': 'stick', 'F': 'feather'})
_recipe('glow_item_frame', 1, ['item_frame', 'glow_ink_sac'])
(d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':')))

# ---- the armour stand (its item said "set it down and dress it"): a block drawn by render/stand_layer.cpp, its gear in
#      the block entity (sim/build.cpp useBlockAt) ----
bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
tnames = json.loads((d / 'tiles.json').read_text())['names']
if not any(b['key'] == 'armor_stand' for b in bj['blocks']):
    bj['blocks'].append({'id': len(bj['blocks']), 'render': 0, 'solid': False, 'opaque': False, 'replaceable': False, 'hardness': 0.5,
                         'tool': 'axe', 'tier': 0, 'drop': 'armor_stand', 'light': 0, 'lightAtt': 0, 'sound': 'wood', 'key': 'armor_stand',
                         'name': 'Armor Stand', 'armorStand': True, 'facingModel': True, 'sink': True})
    bj['faces'].append([tnames['planks']] * 6)
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), sort_keys=False))
if items['armor_stand'].get('block') != 'armor_stand':
    items['armor_stand']['block'] = 'armor_stand'
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))

# ---- the reinforced piston (BTA's): steel-bound, pushes 16 blocks, and flings whatever stands in front of it
#      (sim/redstone_parts.cpp); its pictures are the piston's, steel-blue with rivets ----
bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
tj = json.loads((d / 'tiles.json').read_text()); rj = json.loads((d / 'recipes.json').read_text())
tn, tf = tj['names'], tj['files']
tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
def _steel(src, dst):
    p = d / 'resourcepacks' / 'DokuQuest Base' / 'assets' / 'minecraft' / 'textures' / 'block' / (src + '.png')   # the base pack's own, else ours
    if not p.exists(): p = d / tf[tn[src]]
    im = Image.open(p).convert('RGBA'); im = im.crop((0, 0, im.width, im.width)).resize((32, 32), Image.NEAREST)
    px = im.load()
    for y in range(32):
        for x in range(32):
            r, g, b, a = px[x, y]
            if not a: continue
            l = (r * 30 + g * 59 + b * 11) // 100
            if r > g + 12 and r > b + 20:   # the wooden parts: dark steel
                px[x, y] = (l * 70 // 100, l * 78 // 100, l * 92 // 100, a)
            else:                            # the stone: blue-grey steel
                px[x, y] = (min(255, l * 88 // 100), min(255, l * 96 // 100), min(255, l * 110 // 100), a)
    for x, y in ((3, 3), (28, 3), (3, 28), (28, 28)):   # rivets
        if px[x, y][3]: px[x, y] = (210, 220, 232, 255); px[x + 1, y + 1] = (60, 66, 78, 255)
    rel = 'textures/block/%s.png' % dst
    im.save(d / rel)
    if dst not in tn: tn[dst] = tj['count']; tf.append(rel); tj['res'].append(32); tj['count'] += 1
    return tn[dst]
t_top = _steel('piston_top', 'reinforced_piston_top'); t_side = _steel('piston_side', 'reinforced_piston_side')
t_bot = _steel('piston_bottom', 'reinforced_piston_bottom'); t_base = _steel('piston_side_base', 'reinforced_piston_side_base')
if not any(b['key'] == 'reinforced_piston' for b in bj['blocks']):
    byk = {b['key']: b for b in bj['blocks']}
    base = dict(byk['piston']); ext = dict(byk['piston_extended'])
    base.update({'id': len(bj['blocks']), 'key': 'reinforced_piston', 'name': 'Reinforced Piston', 'drop': 'reinforced_piston', 'hardness': 3.5,
                 'tex': {'front': 'reinforced_piston_top', 'back': 'reinforced_piston_bottom', 'side': 'reinforced_piston_side'}, 'launcher': True,
                 'f6': {'front': t_top, 'frontV': t_top, 'back': t_bot, 'backOn': t_bot, 'side': t_side, 'top': t_side}})
    bj['blocks'].append(base); bj['faces'].append([t_side, t_side, t_side, t_side, t_top, t_bot])
    ext.update({'id': len(bj['blocks']), 'key': 'reinforced_piston_extended', 'name': 'Reinforced Piston', 'drop': 'reinforced_piston', 'hardness': 3.5,
                'tex': 'reinforced_piston_side_base', 'launcher': True})
    bj['blocks'].append(ext); bj['faces'].append([t_base] * 6)
    items['reinforced_piston'] = {'key': 'reinforced_piston', 'name': 'Reinforced Piston', 'maxStack': 64, 'icon': 'block:reinforced_piston',
                                  'block': 'reinforced_piston', 'desc': 'Pushes up to 16 blocks, and flings whatever stands in front of it.'}
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), sort_keys=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
if not any(r.get('out') == 'reinforced_piston' for r in rj['recipes']):
    rj['recipes'].append({'out': 'reinforced_piston', 'n': 1, 'shape': ['SSS', 'SPS', 'SRS'], 'key': {'S': 'steel_ingot', 'P': 'piston', 'R': 'redstone_block'},
                          'idx': len(rj['recipes']), 'h': 3, 'w': 3})
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':')))

# ---- the Wicker Basket (carries a small animal: sim/basket.cpp) and the Siphon (a copper pipe that moves items along) ----
bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
tj = json.loads((d / 'tiles.json').read_text()); rj = json.loads((d / 'recipes.json').read_text())
sj = json.loads((d / 'shapes.json').read_text()); sh = json.loads((d / 'itemshapes.json').read_text())
tn, tf = tj['names'], tj['files']
tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
_pk = d / 'resourcepacks' / 'DokuQuest Base' / 'assets' / 'minecraft' / 'textures' / 'item' / 'bundle.png'
if _pk.exists():
    im = Image.open(_pk).convert('RGBA'); im = im.crop((0, 0, im.width, im.width)).resize((32, 32), Image.NEAREST)
    px = im.load()
    for yy in range(32):
        for xx in range(32):
            r, g, b, a = px[xx, yy]
            if not a: continue
            l = (r * 30 + g * 59 + b * 11) // 100
            weave = 1.12 if ((xx // 3) + (yy // 3)) % 2 == 0 else 0.86     # woven cane
            px[xx, yy] = (min(255, int((l * 1.05 + 40) * weave)), min(255, int((l * 0.82 + 22) * weave)), min(255, int((l * 0.42 + 6) * weave)), a)
    rel = 'textures/item/wicker_basket.png'; im.save(d / rel)
    if 'wicker_basket' not in tn: tn['wicker_basket'] = tj['count']; tf.append(rel); tj['res'].append(32); tj['count'] += 1
    if 'wicker_basket' not in sh['itemTiles']: sh['itemTiles'].append('wicker_basket')
if 'wicker_basket' not in items and 'wicker_basket' in tn:
    items['wicker_basket'] = {'key': 'wicker_basket', 'name': 'Wicker Basket', 'maxStack': 1, 'icon': 'wicker_basket',
                              'desc': 'Use it on a small animal to lift it in; use it on the ground to set it down again.'}
if 'siphon' not in sj['shapes']:
    sj['shapes']['siphon'] = [{'b': [4, 4, 2, 12, 12, 14], 'side': 'copper_block', 'end': 'copper_block'},
                              {'b': [3, 3, 0, 13, 13, 2], 'side': 'iron_block', 'end': 'iron_block'},
                              {'b': [2, 2, 14, 14, 14, 16], 'side': 'iron_block', 'end': 'hopper_outside' if 'hopper_outside' in tn else 'iron_block'}]
if not any(b['key'] == 'siphon' for b in bj['blocks']):
    bj['blocks'].append({'id': len(bj['blocks']), 'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 2.5, 'tool': 'pickaxe',
                         'tier': 0, 'drop': 'siphon', 'light': 0, 'lightAtt': 0, 'sound': 'metal', 'key': 'siphon', 'name': 'Siphon', 'shape': 'siphon',
                         'facingModel': True, 'siphon': True, 'tex': 'copper_block'})
    bj['faces'].append([tn['copper_block']] * 6)
    items['siphon'] = {'key': 'siphon', 'name': 'Siphon', 'maxStack': 64, 'icon': 'block:siphon', 'block': 'siphon',
                       'desc': 'Draws items out of whatever is behind it (or lying there) and pushes them into whatever is in front.'}
for out, shape, key in (('wicker_basket', ['S S', 'CCC'], {'S': 'stick', 'C': 'sugar_cane'}),
                        ('siphon', ['CCC', 'CHC'], {'C': 'copper_ingot', 'H': 'hopper'})):
    if not any(r.get('out') == out for r in rj['recipes']):
        rj['recipes'].append({'out': out, 'n': 1, 'shape': shape, 'key': key, 'idx': len(rj['recipes']), 'h': len(shape), 'w': len(shape[0])})
for f, o in (('blocks.json', bj), ('shapes.json', sj)): (d / f).write_text(json.dumps(o, separators=(',', ':'), sort_keys=False))
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
(d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'))); (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':')))
(d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':')))

# ---- potions stack to 16 (the stand takes one bottle a slot: ui/magic_screens.cpp); lingering potions (Minecraft's):
#      a splash potion and ectoplasm make one that leaves a cloud of itself on the ground for ten seconds
#      (sim/magic_bolt.cpp PotionCloud) ----
items = json.loads((d / 'items.json').read_text()); rj = json.loads((d / 'recipes.json').read_text())
for k, v in list(items.items()):
    if isinstance(v.get('potion'), dict) and v.get('maxStack', 1) < 16: v['maxStack'] = 16
for k, v in list(items.items()):
    if not k.startswith('splash_potion_') or not isinstance(v.get('potion'), dict): continue
    lk = 'lingering_' + k[len('splash_'):]
    if lk not in items:
        nv = json.loads(json.dumps(v)); nv['key'] = lk
        nv['name'] = v['name'].replace('Splash', 'Lingering', 1) if v['name'].startswith('Splash') else 'Lingering ' + v['name']
        nv['potion']['lingering'] = True; nv['desc'] = 'Use to throw it. It leaves a cloud of itself on the ground for a while.'
        items[lk] = nv
    if not any(r.get('out') == lk for r in rj['recipes']):
        rj['recipes'].append({'out': lk, 'n': 1, 'items': [k, 'ectoplasm'], 'w': 2, 'h': 2, 'idx': len(rj['recipes'])})
(d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
(d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':')))

# ---- the biome fuselings and golems built from their lands' blocks (after the tinted ones above) ----
_s6 = _u.spec_from_file_location('biome_mobs', pathlib.Path(__file__).with_name('biome_mobs.py')); _m6 = _u.module_from_spec(_s6); _s6.loader.exec_module(_m6); _m6.main(d)

# ---- more for the Skylands: driftstone, whirlwinds, valkyries (tools/port/skylands_more.py) ----
_s10 = _u.spec_from_file_location('skylands_more', pathlib.Path(__file__).with_name('skylands_more.py')); _m10 = _u.module_from_spec(_s10); _s10.loader.exec_module(_m10); _m10.main(d)

# ---- crows that settle on the scarecrows (tools/port/crows.py) ----
_s9 = _u.spec_from_file_location('crows', pathlib.Path(__file__).with_name('crows.py')); _m9 = _u.module_from_spec(_s9); _s9.loader.exec_module(_m9); _m9.main(d)
_s = _u.spec_from_file_location('spawn_eggs', pathlib.Path(__file__).with_name('spawn_eggs.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)   # (its egg)

# ---- the art pass on our own creature skins: into the base pack's light and colour (tools/port/mob_restyle.py; the
# pack's own creatures untouched; before the looks, which are made from these) ----
_s11 = _u.spec_from_file_location('mob_restyle', pathlib.Path(__file__).with_name('mob_restyle.py')); _m11 = _u.module_from_spec(_s11); _s11.loader.exec_module(_m11); _m11.main(d)

# ---- sheared sheep keep a short undercoat (tools/port/sheep_undercoat.py) ----
_s12 = _u.spec_from_file_location('sheep_undercoat', pathlib.Path(__file__).with_name('sheep_undercoat.py')); _m12 = _u.module_from_spec(_s12); _s12.loader.exec_module(_m12); _m12.main(d)

# ---- every creature's looks with stories of their own (tools/port/mob_variants.py; after the biome mobs, which get theirs too) ----
_s8 = _u.spec_from_file_location('mob_variants', pathlib.Path(__file__).with_name('mob_variants.py')); _m8 = _u.module_from_spec(_s8); _s8.loader.exec_module(_m8); _m8.main(d)

# ---- stairs, slabs and walls take the tool and tier of their block (plaster's needed a stone pickaxe while plaster
# itself breaks by hand; found by dq-sweep --drops) ----
def _match_family_tools(d):
    bj = json.loads((pathlib.Path(d) / 'blocks.json').read_text())
    by = {b['key']: b for b in bj['blocks']}
    n = 0
    for b in bj['blocks']:
        for suf in ('_stairs', '_slab', '_wall'):
            if not b['key'].endswith(suf): continue
            base = by.get(b['key'][:-len(suf)])
            if base and (b.get('tier', 0) != base.get('tier', 0) or b.get('tool') != base.get('tool')):
                b['tier'] = base.get('tier', 0); b['tool'] = base.get('tool'); n += 1
    (pathlib.Path(d) / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    print('family tools: %d stairs/slabs/walls matched to their block' % n)
_match_family_tools(d)
