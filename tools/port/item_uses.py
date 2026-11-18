#!/usr/bin/env python3
"""tools/port/item_uses.py — every drop has a use (tools/test_content.cpp "every item has a use" checks it):
- the Minotaur's horn bound into a War Horn (sim/item_uses.cpp: your tamed creatures come to you, quicker and stronger
  for a while);
- each of the seven dragons drops its own scale; dragonscale armour takes four of them a piece at the smithing table and
  becomes that dragon's armour, with a full-set bonus of its own (sim/item_uses.cpp gearSet); any scale still goes back
  to a plain Dragon Scale for the old recipes;
- barley, oats and rye: breads, porridge, oatcakes, ale and mead in the sealed barrel, and the farm animals eat them
  (breeding, the feeding trough);
- the capes' small effects written on them.
Pictures: the base pack's own recoloured, written into the base pack (the art used) and data/textures (the fallback).
Idempotent; data_patches.py runs it.

  python3 tools/port/item_uses.py [data-dir]
"""
import colorsys, json, pathlib, sys
from PIL import Image

DRAGONS = [   # element, name, colour, set bonus (the words on every piece; sim/item_uses.cpp does what they say)
    ('verdant', 'Verdant', (92, 178, 66), 'Full set: Greenwood (wounds close in daylight, and poison never takes)'),
    ('sand', 'Sand', (222, 192, 118), 'Full set: Dunewalker (no heat or burning sand, quicker on sand, and falls hurt half)'),
    ('frost', 'Frost', (150, 214, 244), 'Full set: Rime (frost hurts a fifth, water freezes under your feet, and what strikes you is slowed)'),
    ('bog', 'Bog', (112, 120, 58), 'Full set: Mire (poison never takes, and what strikes you is poisoned)'),
    ('ember', 'Ember', (226, 92, 38), 'Full set: Cinderheart (fire and lava hurt a fifth, and what strikes you burns)'),
    ('storm', 'Storm', (120, 112, 214), 'Full set: Thunderhide (what strikes you is jolted and thrown back, and falls hurt half)'),
    ('tide', 'Tide', (40, 150, 176), 'Full set: Deepcurrent (breathe under water, and swim quicker)'),
]
PIECES = [('helmet', 'Helmet', 0), ('chestplate', 'Chestplate', 1), ('leggings', 'Leggings', 2), ('boots', 'Boots', 3)]


def main(d):
    d = pathlib.Path(d)
    base = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/item'
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text()); sh = json.loads((d / 'itemshapes.json').read_text())
    pm = json.loads((d / 'packmap.json').read_text()); mobs = json.loads((d / 'mobs.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    mapped = {e[0] for e in pm}

    def src(name, fallback_only=False):   # the base pack's picture (else the game's own), and the game's own
        own = Image.open(d / files[tiles[name]]).convert('RGBA')
        bp = base / (name + '.png')
        return (Image.open(bp).convert('RGBA') if bp.exists() and not fallback_only else own), own

    def tile(name, own, pack=None, item=True, folder='item'):
        rel = 'textures/%s/%s.png' % (folder, name)
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        own.save(d / rel)
        if pack is not None: pack.save(base / (name + '.png'))
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if item and name not in sh['itemTiles']: sh['itemTiles'].append(name)
        if pack is not None and name not in mapped: pm.append([name, [name], [name]]); mapped.add(name)

    def recolour(im, rgb, only=None):
        """every coloured pixel (or those `only` picks) to the hue and saturation of rgb, its shading kept"""
        th, ts, tv = colorsys.rgb_to_hsv(*[c / 255 for c in rgb])
        px = im.load(); out = im.copy(); po = out.load()
        vals = [colorsys.rgb_to_hsv(*[c / 255 for c in px[x, y][:3]])[2] for x in range(im.width) for y in range(im.height) if px[x, y][3] > 0 and (not only or only(px[x, y]))]
        mean = sum(vals) / len(vals) if vals else 1
        k = 0.5 + 0.5 * tv / max(0.05, mean)   # half-way to the colour's own brightness (a pale scale stays pale)
        for x in range(im.width):
            for y in range(im.height):
                r, g, b, a = px[x, y]
                if not a or (only and not only(px[x, y])): continue
                h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                nr, ng, nb = colorsys.hsv_to_rgb(th, min(1, ts * (0.5 + s)), min(1, v * k))
                po[x, y] = (int(nr * 255), int(ng * 255), int(nb * 255), a)
        return out

    def recipe(out, n, items_=None, shape=None, key=None, keep=None):
        if any(r.get('out') == out and r.get('items') == items_ and r.get('shape') == shape for r in rj['recipes']): return
        r = {'out': out, 'n': n}
        if keep: r['keep'] = keep
        if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
        else: r.update({'items': items_, 'w': 2, 'h': 2})
        r['idx'] = len(rj['recipes']); rj['recipes'].append(r)

    # ---- the dragons' own scales and armour ----
    sp, so = src('dragon_scale')
    for el, nm, rgb, bonus in DRAGONS:
        key = el + '_dragon_scale'
        tile(key, recolour(so, rgb), recolour(sp, rgb))
        items[key] = {'key': key, 'name': nm + ' Dragon Scale', 'maxStack': 64, 'icon': key, 'rarity': 'epic',
                      'desc': 'From the %s Dragon. Four on a piece of dragonscale armour at the smithing table make it %s armour; or craft it into a plain Dragon Scale.' % (nm, nm)}
        recipe('dragon_scale', 1, [key])
        mob = mobs.get(el + '_dragon')
        if mob:
            for dr in mob.get('drops', []):
                if dr[0] == 'dragon_scale': dr[0] = key
        mat = el + 'scale'
        for piece, pname, slot in PIECES:
            pk = mat + '_' + piece
            p_pack, p_own = src('dragonscale_' + piece)
            tile(pk, recolour(p_own, rgb), recolour(p_pack, rgb))
            arm = dict(items['dragonscale_' + piece]['armor']); arm['set'] = mat
            if el != 'ember': arm['fireResist'] = 0.1
            items[pk] = {'key': pk, 'name': '%s Scale %s' % (nm, pname), 'maxStack': 1, 'icon': pk, 'rarity': 'epic', 'armor': arm, 'desc': bonus}
        for suffix in ('', '_face'):   # the worn armour's pictures (assets/char_model.cpp armor_<material>)
            mo = Image.open(d / ('textures/misc/armor_dragonscale%s.png' % suffix)).convert('RGBA')
            tile('armor_%s%s' % (mat, suffix), recolour(mo, rgb), item=False, folder='misc')
    for piece, _, _ in PIECES:   # the plain set's bonus, now real (sim/item_uses.cpp)
        items['dragonscale_' + piece]['armor']['set'] = 'dragonscale'

    # ---- the Minotaur's horn: a war horn ----
    hp, ho = src('minotaur_horn')
    def banded(im):
        im = im.copy(); px = im.load()
        xs = [x for x in range(im.width) for y in range(im.height) if px[x, y][3] > 0]
        x0, x1 = min(xs), max(xs)
        for x in range(im.width):
            for y in range(im.height):
                if not px[x, y][3]: continue
                f = (x - x0) / max(1, x1 - x0)
                if abs(f - 0.3) < 0.05 or abs(f - 0.62) < 0.05: px[x, y] = (232, 186, 64, 255) if (x + y) % 3 else (176, 128, 30, 255)
                elif f < 0.08: px[x, y] = (150, 104, 24, 255)
        return im
    tile('war_horn', banded(ho), banded(hp))
    items['war_horn'] = {'key': 'war_horn', 'name': 'War Horn', 'maxStack': 1, 'icon': 'war_horn', 'rarity': 'rare', 'warHorn': True,
                         'desc': 'Blow it: your tamed creatures come to you, quicker and stronger for 20 s (once a minute).'}
    recipe('war_horn', 1, ['minotaur_horn', 'gold_ingot', 'leather'])
    items['minotaur_horn']['desc'] = 'A trophy from the heart of the Labyrinth. Bound with gold and leather, it makes a War Horn.'

    # ---- barley, oats and rye ----
    bp, bo = src('bread')
    tile('rye_bread', recolour(bo, (120, 74, 40)), recolour(bp, (120, 74, 40)))
    tile('barley_bread', recolour(bo, (214, 170, 84)), recolour(bp, (214, 170, 84)))
    cp, co = src('cookie')
    tile('oatcake', recolour(co, (206, 168, 110)), recolour(cp, (206, 168, 110)))
    soup = lambda q: colorsys.rgb_to_hsv(*[c / 255 for c in q[:3]])[0] > 0.85 or colorsys.rgb_to_hsv(*[c / 255 for c in q[:3]])[0] < 0.02   # the beetroot in the bowl
    pp, po = src('beetroot_soup')
    tile('porridge', recolour(po, (236, 222, 186), soup), recolour(pp, (236, 222, 186), soup))
    gold = lambda q: q[3] > 0 and 0.05 < colorsys.rgb_to_hsv(*[c / 255 for c in q[:3]])[0] < 0.17 and colorsys.rgb_to_hsv(*[c / 255 for c in q[:3]])[1] > 0.35
    mp, mo = src('honey_bottle')
    tile('mead', recolour(mo, (240, 206, 92), gold), recolour(mp, (240, 206, 92), gold))
    FOODS = {
        'rye_bread': ('Rye Bread', {'heal': 6}, 'Dark and filling.'),
        'barley_bread': ('Barley Bread', {'heal': 5}, 'A flat, sweet loaf.'),
        'oatcake': ('Oatcake', {'heal': 3}, None),
        'porridge': ('Porridge', {'heal': 7, 'effect': ['regen', 8, 0]}, 'Warm oats: it keeps healing you a little after.'),
        'mead': ('Mead', {'heal': 4, 'effect': ['speed', 30, 0]}, 'Honey left sealed with water in a barrel. Quickens you.'),
    }
    for k, (nm, food, desc) in FOODS.items():
        it = {'key': k, 'name': nm, 'maxStack': 1 if k == 'porridge' else 16 if k == 'mead' else 64, 'icon': k, 'food': food}
        if k == 'porridge': it['bowl'] = True
        if k == 'mead': it['returns'] = 'glass_bottle'
        if desc: it['desc'] = desc
        items[k] = it
    items['ale_mug']['food'] = {'heal': 3, 'effect': ['strength', 30, 0]}
    items['ale_mug']['desc'] = 'Barley left sealed with water in a barrel. Makes you stronger for a while.'
    recipe('rye_bread', 1, shape=['RRR'], key={'R': 'rye'})
    recipe('barley_bread', 1, shape=['BBB'], key={'B': 'barley'})
    recipe('oatcake', 3, ['oat', 'oat', 'sugar'])
    recipe('porridge', 1, ['bowl', 'oat', 'oat', 'milk_bucket'], keep={'milk_bucket': 'bucket'})
    for g, desc in (('barley', 'Bake it into bread, grind it to flour, brew it into ale in a sealed barrel, or feed it to farm animals.'),
                    ('oat', 'Porridge and oatcakes, flour at a quern, and the farm animals love it.'),
                    ('rye', 'Rye bread, flour at a quern, and food for farm animals.')):
        items[g]['desc'] = desc
    for k, m in mobs.items():   # what eats wheat eats the other grains (breeding and the feeding trough)
        if not isinstance(m, dict) or not isinstance(m.get('food'), list): continue
        if 'wheat' in m['food'] or 'wheat_seeds' in m['food']:
            for g in ('barley', 'oat', 'rye'):
                if g not in m['food']: m['food'].append(g)

    # ---- the capes ----
    items['ember_cape']['desc'] = 'Woven with a cinderite thread. Worn: fire and lava hurt 20% less, and burning goes out sooner.'
    items['frost_cape']['desc'] = 'Cold to the touch. Worn: frost hurts 30% less, and its chill never slows you.'
    items['royal_cape']['desc'] = 'Purple and gold. Worn: villagers think the better of you, and sell to you for a little less.'

    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), ensure_ascii=False))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    # the sealed barrel: barley to ale, honey to mead (data/barrel.json is kept a recipe a line)
    bt = (d / 'barrel.json').read_text()
    for line in ('{"liquid": "water", "item": "barley", "out": "ale_mug", "hours": 4}', '{"liquid": "water", "item": "honey_bottle", "out": "mead", "hours": 4}'):
        if line not in bt: bt = bt.replace('\n ]\n}', ',\n  ' + line + '\n ]\n}')
    (d / 'barrel.json').write_text(bt)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
