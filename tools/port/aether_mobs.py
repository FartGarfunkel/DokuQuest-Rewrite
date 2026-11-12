#!/usr/bin/env python3
"""tools/port/aether_mobs.py — The Aether's dungeon keepers and its missing creatures (idempotent; data_patches.py runs it):
  python3 tools/port/aether_mobs.py <data-dir>

Built like tools/port/end_mobs.py: each on the rig of the nearest creature the game has, its model a copy of that one
(scaled) with every picture recoloured (light and shade kept):
  slider (bronze dungeon boss: a big carved-stone cube that hops at you, on the slime), sentry (its small cousin that
  guards the bronze halls), sun_spirit (gold dungeon boss: a burning golden giant, on the Ember Lord), fire_minion (its
  guard, a small one), evil_whirlwind (a dark whirlwind that hunts you, on the whirlwind).
gen/structures_aether.cpp names the bosses; sim/keepers.cpp wakes the Sun Spirit's minions with it; sim/creature_spawn.cpp
spawns sentries in the bronze halls and evil whirlwinds on the islands.
"""
import copy, json, pathlib, sys
import numpy as np
from PIL import Image

MOBS = {
    # key: (base mob, label, default colour, {tile: colour}, model scale, opaque, mob overrides)
    'slider': ('slime', 'the Slider', 0x7C7C7C, {'slime_core': 0x5E5E5E, 'slime_face': 0x8FD3FF}, 2.6, True,
               {'hp': 160, 'w': 1.3, 'h': 2.6, 'damage': 7, 'xp': 120, 'boss': True, 'knockResist': 1.0, 'speed': 2.4, 'special': True,
                'drops': [['carved_stone', 4, 8, 1], ['sentry_stone', 1, 3, 1], ['gravitite_ore', 1, 3, 1], ['golden_apple', 1, 1, 0.6], ['heart_crystal', 1, 1, 0.5]]}),
    'sentry': ('slime', 'a sentry', 0x7A7F86, {'slime_core': 0x5C6066, 'slime_face': 0x7FC8FF}, 1.0, True,
               {'hp': 10, 'damage': 3, 'xp': 5, 'hostile': True, 'glows': 6, 'drops': [['carved_stone', 1, 2, 1], ['sentry_stone', 0, 1, 0.3]]}),
    'sun_spirit': ('ember_lord', 'the Sun Spirit', 0xFFB43C, {}, 1.15, True,
                   {'hp': 360, 'damage': 8, 'xp': 300, 'glows': 15, 'tint': None,
                    'drops': [['gold_block', 1, 3, 1], ['diamond', 2, 4, 1], ['heart_crystal', 1, 1, 1], ['fire_charge', 4, 8, 1], ['emerald', 8, 16, 1]]}),
    'fire_minion': ('cinder_golem', 'a fire minion', 0xE0522A, {}, 0.55, True,
                    {'hp': 40, 'w': 0.5, 'h': 1.7, 'damage': 5, 'xp': 15, 'boss': False, 'glows': 10, 'knockResist': 0.3,
                     'drops': [['fire_charge', 0, 2, 1], ['gold_nugget', 1, 4, 1]]}),
    'evil_whirlwind': ('whirlwind', 'an evil whirlwind', 0x4E4E5C, {}, 1.0, False,
                       {'hostile': True, 'ambient': False, 'damage': 2, 'xp': 4, 'hp': 16, 'drops': [['gust_puff', 1, 2, 0.8], ['gunpowder', 0, 1, 0.4]]}),
}


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def recolour(im, hexcol, opaque):
    a = np.asarray(im.convert('RGBA')).astype(np.float32)
    rgb = np.array([(hexcol >> 16) & 255, (hexcol >> 8) & 255, hexcol & 255], np.float32)
    m = a[..., 3] > 0
    if not m.any(): return im
    l = lum(a); mean = max(1.0, float(l[m].mean()))
    a[..., :3] = np.where(m[..., None], np.clip(rgb[None, None, :] * (l / mean)[..., None], 0, 255), a[..., :3])
    if opaque: a[..., 3] = np.where(m, 255, a[..., 3])
    return Image.fromarray(a.astype(np.uint8), 'RGBA')


def main(d):
    d = pathlib.Path(d)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
    items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    for key, (base, label, col, cols, scale, opaque, over) in MOBS.items():
        if base not in mobs: print('aether_mobs: no', base); continue
        mkey = mobs[base].get('model', base)
        if mkey not in models: print('aether_mobs: no model', mkey); continue
        model = copy.deepcopy(models[mkey])
        def retex(t):
            new = key + '_' + t
            src = d / files[tiles[t]]
            if new not in tiles:
                rel = 'textures/entity/%s.png' % new
                tiles[new] = tj['count']; files.append(rel); tj['res'].append(tj['res'][tiles[t]] if tiles[t] < len(tj['res']) else 32); tj['count'] += 1
            recolour(Image.open(src), cols.get(t, col), opaque).save(d / files[tiles[new]])
            return new
        for p in model['parts']:
            tx = p.get('tex')
            if isinstance(tx, str) and tx in tiles: p['tex'] = retex(tx)
            elif isinstance(tx, dict): p['tex'] = {k: retex(v) if v in tiles else v for k, v in tx.items()}
        for k in ('packBase', 'packTint', 'packTex'): model.pop(k, None)
        model['scale'] = model.get('scale', 1) * scale
        models[key] = model
        m = copy.deepcopy(mobs[base])
        for k in ('variantDefs', 'mini', 'held'): m.pop(k, None)
        m['model'] = key; m['label'] = label; m['aether'] = True
        for k, v in over.items():
            if v is None: m.pop(k, None)
            else: m[k] = v
        if 'drops' in m: m['drops'] = [dr for dr in m['drops'] if dr[0] in items]
        mobs[key] = m
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('aether_mobs: %d creatures' % len(MOBS))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
