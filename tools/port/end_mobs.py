#!/usr/bin/env python3
"""tools/port/end_mobs.py — Better End's creatures for the End's lands (idempotent; data_patches.py runs it):
  python3 tools/port/end_mobs.py <data-dir>

Each is built on the rig of the nearest creature the game already has (so it moves and thinks like it), its own model
a copy of that one with every picture recoloured to the Better End creature's colours (light and shade kept):
  end_slime (on the slime: purple jelly, a green core), dragonfly (on the bee, no sting: cyan), end_fish (on the cod:
  pink and violet), cubozoa (on the glow squid: turquoise jelly that stings), silk_moth (on the bat: cream), shadow_walker
  (on the ghoul: near-black with violet eyes, hostile). sim/creature_spawn.cpp spawns them in their Better End biomes.
"""
import copy, json, pathlib, sys
import numpy as np
from PIL import Image

MOBS = {
    # key: (base model/mob, label, colours {tile-suffix: hex} by base tile, mob overrides)
    'end_slime': ('slime', 'an end slime', {'slime_skin': 0x9A5BD1, 'slime_core': 0x5BD18C, 'slime_face': 0x2A1440}, {'hp': 10}),
    'dragonfly': ('bee', 'a dragonfly', {'bee_body': 0x2BB7C9, 'bee_side': 0x1E8FA8, 'bee_face': 0x0E3A4A, 'bee_sting': 0x2BB7C9, 'bee_wing': 0xBDF4FF},
                  {'stings': False, 'damage': 0, 'pack': False, 'ambient': True, 'sound': 'bee'}),
    'end_fish': ('cod', 'an end fish', {'cod_scales': 0xC157D8, 'cod_fin': 0xF08BE6, 'cod_face': 0x5A1F6E}, {}),
    'cubozoa': ('glow_squid', 'a cubozoa', {'glow_squid_skin': 0x3FD7C9, 'glow_squid_face': 0x166F72}, {'damage': 2, 'glows': 8}),
    'silk_moth': ('bat', 'a silk moth', {'bat_fur': 0xE8DCC0, 'bat_wing': 0xF4ECD8, 'bat_face': 0x6E5A3A}, {'roosts': False}),
    'shadow_walker': ('ghoul', 'a shadow walker', {'ghoul_cloth': 0x1A1424, 'ghoul_skin': 0x241C30, 'ghoul_face': 0x2A2036, 'ghoul_shoe': 0x0E0A14},
                      {'hp': 20, 'damage': 4, 'hostile': True, 'xp': 8, 'sunBurns': False, 'label': 'a shadow walker'}),
}


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def recolour(im, hexcol):
    a = np.asarray(im.convert('RGBA')).astype(np.float32)
    rgb = np.array([(hexcol >> 16) & 255, (hexcol >> 8) & 255, hexcol & 255], np.float32)
    m = a[..., 3] > 0
    if not m.any(): return im
    l = lum(a); mean = max(1.0, float(l[m].mean()))
    k = l / mean
    a[..., :3] = np.where(m[..., None], np.clip(rgb[None, None, :] * k[..., None], 0, 255), a[..., :3])
    return Image.fromarray(a.astype(np.uint8), 'RGBA')


def main(d):
    d = pathlib.Path(d)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    for key, (base, label, cols, over) in MOBS.items():
        if base not in models or base not in mobs: print('end_mobs: no', base); continue
        model = copy.deepcopy(models[base])
        def retex(t):
            if t in cols:
                new = t.replace(base.split('_')[0], key, 1) if t.startswith(base.split('_')[0]) else key + '_' + t
                if new not in tiles:
                    src = d / files[tiles[t]]
                    rel = 'textures/entity/%s.png' % new
                    recolour(Image.open(src), cols[t]).save(d / rel)
                    tiles[new] = tj['count']; files.append(rel); tj['res'].append(tj['res'][tiles[t]] if tiles[t] < len(tj['res']) else 32); tj['count'] += 1
                else:
                    recolour(Image.open(d / files[tiles[t]]), cols[t]).save(d / files[tiles[new]])
                return new
            return t
        for p in model['parts']:
            tx = p.get('tex')
            if isinstance(tx, str): p['tex'] = retex(tx)
            elif isinstance(tx, dict): p['tex'] = {k: retex(v) for k, v in tx.items()}
        for k in ('packBase', 'packTint', 'packTex'): model.pop(k, None)   # its own pictures, never the base's pack skin
        models[key] = model
        m = copy.deepcopy(mobs[base])
        m.pop('variantDefs', None)
        m['model'] = key; m['label'] = label; m['betterEnd'] = True
        m.update(over)
        mobs[key] = m
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('end_mobs: %d creatures' % len(MOBS))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
