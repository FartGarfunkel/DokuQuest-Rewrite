#!/usr/bin/env python3
"""tools/port/new_materials.py — the ores the Aether and Better End brought, made into materials (idempotent; data_patches.py
runs it before forging.py and tool_parts.py, which take the three metals and gems up as forge and mixed-tool materials):

  zanite      (Aether)      zanite_ore drops a zanite gemstone; tools and armour a step past iron
  gravitite   (Aether)      gravitite_ore smelts to a gravitite plate; tools and armour of diamond's tier
  thallasium  (Better End)  thallasium_ore smelts to a thallasium ingot; tools and armour a little under iron
  and the ores that give a thing, not a metal: ender_ore an ender shard, amber_ore an amber gem, ambrosium_ore an
  ambrosium shard (a heal, as in the Aether).

Pictures: each new icon is the matching iron one (the game's own, and the DokuQuest Base pack's into the local overlay
pack "Mod Art (local)"), its metal recoloured to the material's ramp, the handle left as it was; gems and shards from
the diamond and the emerald.
  python3 tools/port/new_materials.py data
"""
import json, pathlib, sys, colorsys
import numpy as np
from PIL import Image

MATS = {   # key: material item, its name, colour, gem?, ore, ore gives the item (else smelts), tool (tier, speed, damage, durability), armour points, armour durability
    'zanite': ('zanite_gemstone', 'Zanite Gemstone', (170, 118, 236), True, 'zanite_ore', True, (3, 7.5, 4, 480), (2, 6, 5, 2), (210, 300, 285, 245)),
    'gravitite': ('gravitite_plate', 'Gravitite Plate', (232, 120, 214), False, 'gravitite_ore', False, (4, 9, 5, 1600), (3, 8, 6, 3), (370, 540, 505, 440)),
    'thallasium': ('thallasium_ingot', 'Thallasium Ingot', (92, 204, 194), False, 'thallasium_ore', False, (2, 6.5, 3, 320), (2, 5, 4, 2), (150, 220, 205, 180)),
}
DROPS = {'ender_ore': ('ender_shard', 'Ender Shard', (72, 196, 176), 'emerald'), 'amber_ore': ('amber_gem', 'Amber Gem', (236, 154, 40), 'emerald'),
         'ambrosium_ore': ('ambrosium_shard', 'Ambrosium Shard', (246, 232, 120), 'diamond')}
TOOLS = {'pickaxe': ['MMM', ' S ', ' S '], 'axe': ['MM', 'MS', ' S'], 'shovel': ['M', 'S', 'S'], 'sword': ['M', 'M', 'S'], 'hoe': ['MM', ' S', ' S']}
ARMOR = {'helmet': ['MMM', 'M M'], 'chestplate': ['M M', 'MMM', 'MMM'], 'leggings': ['MMM', 'M M', 'M M'], 'boots': ['M M', 'M M']}
TITLE = {'pickaxe': 'Pickaxe', 'axe': 'Axe', 'shovel': 'Shovel', 'sword': 'Sword', 'hoe': 'Hoe', 'helmet': 'Helmet', 'chestplate': 'Chestplate', 'leggings': 'Leggings', 'boots': 'Boots'}
OVERLAY = pathlib.Path.home() / '.local/share/DokuQuest/resourcepacks/Mod Art (local)/assets/minecraft/textures/item'


def ramp(rgb):
    r, g, b = [c / 255 for c in rgb]
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    stops = [(0.16, 0.85), (0.32, 0.9), (0.5, 0.85), (0.68, 0.75), (0.86, 0.55)]   # (lightness, saturation scale) dark to light
    return np.array([[c * 255 for c in colorsys.hls_to_rgb(h, L, min(1, s * k))] for L, k in stops], np.float32)


def recolour(img, rgb, all_pixels=False):
    """the metal (the grey, unsaturated pixels; every opaque one if all_pixels) mapped by brightness onto the ramp"""
    a = np.asarray(img.convert('RGBA')).astype(np.float32)
    rgbv = a[..., :3]; mx, mn = rgbv.max(-1), rgbv.min(-1)
    sat = (mx - mn) / np.maximum(mx, 1)
    m = (a[..., 3] > 8) & ((sat < 0.2) | all_pixels)
    if not m.any(): return img
    lum = rgbv[..., 0] * 0.299 + rgbv[..., 1] * 0.587 + rgbv[..., 2] * 0.114
    lo, hi = np.percentile(lum[m], 2), np.percentile(lum[m], 98)
    t = np.clip((lum - lo) / max(1, hi - lo), 0, 1) * 4
    R = ramp(rgb)
    i0 = np.clip(np.floor(t).astype(int), 0, 3); f = (t - i0)[..., None]
    col = R[i0] * (1 - f) + R[i0 + 1] * f
    a[..., :3] = np.where(m[..., None], col, rgbv)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); recipes = json.loads((d / 'recipes.json').read_text())
    bj = json.loads((d / 'blocks.json').read_text()); blocks = {b['key']: b for b in bj['blocks']}
    tj = json.loads((d / 'tiles.json').read_text()); names, files = tj['names'], tj['files']
    pack = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/item'
    made = 0

    def icon(key, src, rgb, all_pixels=False):
        """built-in tile (from the game's own src picture) and the pack-styled one in the overlay (from the pack's src)"""
        nonlocal made
        if key not in names:
            base = d / files[names[src]] if src in names else None
            if base is None or not base.exists(): return
            rel = 'textures/item/%s.png' % key
            (d / rel).parent.mkdir(parents=True, exist_ok=True)
            recolour(Image.open(base), rgb, all_pixels).save(d / rel)
            names[key] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1; made += 1
        pp = pack / (src + '.png')
        if pp.exists():
            OVERLAY.mkdir(parents=True, exist_ok=True)
            im = Image.open(pp)
            if im.size[1] > im.size[0]: im = im.crop((0, 0, im.size[0], im.size[0]))
            recolour(im, rgb, all_pixels).save(OVERLAY / (key + '.png'))

    def recipe(out, shape, mat):
        if any(r.get('out') == out and r.get('shape') for r in recipes['recipes']): return
        recipes['recipes'].append({'out': out, 'n': 1, 'shape': shape, 'key': {'M': mat, 'S': 'stick'} if 'S' in ''.join(shape) else {'M': mat}, 'group': 'tools' if 'S' in ''.join(shape) else 'armor'})

    for mat, (item, iname, rgb, gem, ore, drops, tool, pts, dur) in MATS.items():
        items.setdefault(item, {'key': item, 'name': iname, 'maxStack': 64, 'icon': item, 'rarity': 'rare'})
        icon(item, 'diamond' if gem else 'iron_ingot', rgb, all_pixels=True)
        if ore in blocks:
            if drops: blocks[ore]['drop'] = item
            else: recipes['smelting'][ore] = item
        for t, shape in TOOLS.items():
            k = '%s_%s' % (mat, t)
            items.setdefault(k, {'key': k, 'name': '%s %s' % (iname.split()[0], TITLE[t]), 'maxStack': 1, 'icon': k,
                                 'tool': {'type': t, 'tier': tool[0], 'speed': tool[1], 'damage': tool[2] + (2 if t == 'sword' else 1 if t == 'axe' else 0), 'durability': tool[3]}})
            icon(k, 'iron_' + t, rgb); recipe(k, shape, item)
        for s, (a, shape) in enumerate(ARMOR.items()):
            k = '%s_%s' % (mat, a)
            items.setdefault(k, {'key': k, 'name': '%s %s' % (iname.split()[0], TITLE[a]), 'maxStack': 1, 'icon': k, 'armor': {'slot': s, 'points': pts[s], 'durability': dur[s]}})
            icon(k, 'iron_' + a, rgb); recipe(k, shape, item)
    for ore, (item, iname, rgb, src) in DROPS.items():
        items.setdefault(item, {'key': item, 'name': iname, 'maxStack': 64, 'icon': item})
        if item == 'ambrosium_shard': items[item].setdefault('food', {'heal': 2})
        icon(item, src, rgb, all_pixels=True)
        if ore in blocks: blocks[ore]['drop'] = item
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'recipes.json').write_text(json.dumps(recipes, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('new_materials: %d new pictures, %d tiles now' % (made, tj['count']))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
