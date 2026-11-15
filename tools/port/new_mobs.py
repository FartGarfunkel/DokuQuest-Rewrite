#!/usr/bin/env python3
"""tools/port/new_mobs.py — new creatures (idempotent; data_patches.py runs it): our own animals, wholly our own
designs (no outside code or art is used). Each is made from one of our own as its pattern: that
one's way of living (data/mobs.json: grazing, herding, flying, swimming, hopping), its rig and model (data/models.json)
at its own size, its pictures recoloured to the new animal's coat, and parts of its own (a moose's palmate antlers, an
elephant's trunk, tusks and ears, a toucan's great beak). Where they live: "spawnIn" / "spawnInWater" (sim/creature_spawn).

  python3 tools/port/new_mobs.py [data]"""
import copy, json, pathlib, sys
from PIL import Image

# key: template, size, the coat (light colour, dark colour), changes to the mob, extra parts, retextured parts, part boxes
MOBS = {
    'moose': dict(base='deer', scale=1.45, coat=((120, 84, 52), (52, 34, 22)),
                  mob=dict(hp=30, xp=8, skittish=6, drops=[['venison', 2, 4, 1], ['leather', 1, 3, 1]], spawnIn=[['forest', 2, 1, 2], ['taiga', 4, 1, 2], ['snowy_taiga', 4, 1, 2]], rests=True),
                  extra=[('antlerL', 'head', [-2, 2.5, -2], [-7, 0, -1, 0, 1, 2.5], 'antler'), ('antlerR', 'head', [2, 2.5, -2], [0, 0, -1, 7, 1, 2.5], 'antler'),
                         ('tineL', 'antlerL', [-6, 1, 0], [-1, 0, -0.5, 0, 3, 0.5], 'antler'), ('tineR', 'antlerR', [6, 1, 0], [0, 0, -0.5, 1, 3, 0.5], 'antler'),
                         ('bell', 'head', [0, -2.5, -3], [-0.6, -2.5, -0.6, 0.6, 0, 0.6], 'coat')]),
    'bison': dict(base='cow', scale=1.3, even=True, coat=((96, 66, 40), (40, 26, 16)),
                  mob=dict(hp=36, xp=8, drops=[['beef', 2, 4, 1], ['leather', 1, 3, 1]], spawnIn=[['plains', 3, 2, 4], ['savanna', 2, 2, 4], ['snowy_plains', 2, 2, 3]], rests=True),
                  extra=[('hump', 'body', [0, 5, -6], [-5, 0, -3, 5, 4, 5], 'coat'), ('beard', 'head', [0, -4, -4], [-2, -3, -1, 2, 0, 1], 'coat'),
                         ('hornL', 'head', [-4, 3, -3], [-2, 0, -0.5, 0, 1.2, 0.5], 'horn'), ('hornR', 'head', [4, 3, -3], [0, 0, -0.5, 2, 1.2, 0.5], 'horn'),
                         ('hornTipL', 'hornL', [-2, 1, 0], [-0.6, 0, -0.4, 0.4, 1.5, 0.4], 'horn'), ('hornTipR', 'hornR', [2, 1, 0], [-0.4, 0, -0.4, 0.6, 1.5, 0.4], 'horn')]),
    'gazelle': dict(base='deer', scale=0.8, coat=((210, 168, 108), (150, 106, 60)),
                    mob=dict(hp=10, xp=4, skittish=14, run=8.5, drops=[['venison', 1, 2, 1], ['leather', 0, 1, 1]], spawnIn=[['savanna', 6, 2, 5], ['desert', 1, 1, 2]], rests=True),
                    extra=[('hornL', 'head', [-1.2, 2.5, -1], [-0.4, 0, -0.4, 0.4, 5, 0.4], 'horn'), ('hornR', 'head', [1.2, 2.5, -1], [-0.4, 0, -0.4, 0.4, 5, 0.4], 'horn')]),
    'elephant': dict(base='cow', scale=1.9, even=True, coat=((150, 146, 140), (86, 82, 78)),
                     mob=dict(hp=70, xp=14, drops=[['leather', 2, 5, 1]], spawnIn=[['savanna', 2, 2, 3], ['jungle', 1, 1, 2]], rests=True),
                     extra=[('trunk', 'head', [0, -2, -6], [-1.3, -9, -1.3, 1.3, 0, 1.3], 'coat'), ('trunkTip', 'trunk', [0, -9, 0], [-1, -3, -1, 1, 0, 1], 'coat'),
                            ('tuskL', 'head', [-2.5, -3, -5], [-0.6, -0.6, -4, 0.6, 0.6, 0], 'ivory'), ('tuskR', 'head', [2.5, -3, -5], [-0.6, -0.6, -4, 0.6, 0.6, 0], 'ivory'),
                            ('earL', 'head', [-4, 1, -2], [-6, -5, -0.5, 0, 4, 0.5], 'coat'), ('earR', 'head', [4, 1, -2], [0, -5, -0.5, 6, 4, 0.5], 'coat')]),
    'orca': dict(base='dolphin', scale=1.8, coat=((40, 40, 46), (16, 16, 20)),
                 mob=dict(hp=40, xp=10, drops=[['cod', 1, 3, 1]], spawnInWater=[['ocean', 1, 1, 2], ['cold', 2, 1, 2]]),
                 extra=[('patchL', 'body', [-4, 1, -4], [-0.1, -1, -1.5, 0, 1, 1.5], 'ivory'), ('patchR', 'body', [4, 1, -4], [0, -1, -1.5, 0.1, 1, 1.5], 'ivory'),
                        ('belly', 'body', [0, -3.5, 0], [-3, -0.1, -6, 3, 0, 5], 'ivory')]),
    'kangaroo': dict(base='rabbit', scale=2.6, coat=((196, 132, 82), (128, 80, 44)),
                     mob=dict(hp=20, xp=6, drops=[['leather', 1, 2, 1]], spawnIn=[['savanna', 3, 2, 3], ['desert', 2, 1, 3]]),
                     extra=[('tailK', 'body', [0, -1, 4], [-0.8, -0.8, 0, 0.8, 0.8, 6], 'coat')]),
    'toucan': dict(base='parrot', scale=1.0, coat=((40, 40, 44), (12, 12, 14)),
                   mob=dict(hp=6, drops=[['feather', 1, 2, 1]], spawnIn=[['jungle', 6, 1, 2]]),
                   boxes={'beak': [-1, -0.5, -4.5, 1, 2.2, 0]}, retex={'beak': 'beak', 'crest': 'coat'}),
    'hummingbird': dict(base='parrot', scale=0.45, coat=((70, 190, 110), (20, 110, 80)),
                        mob=dict(hp=2, drops=[['feather', 0, 1, 1]], spawnIn=[['jungle', 4, 1, 2], ['plains', 1, 1, 1], ['forest', 1, 1, 1]]),
                        boxes={'beak': [-0.25, 0.6, -3.5, 0.25, 1.1, 0]}),
    'roadrunner': dict(base='crow', scale=1.05, coat=((150, 118, 82), (70, 52, 34)),
                       mob=dict(hp=8, run=9.0, drops=[['feather', 1, 2, 1]], spawnIn=[['desert', 4, 1, 2], ['savanna', 1, 1, 1]]),
                       boxes={'tailP': [-1, -7.5, 0, 1, 0, 0.6]}, extra=[('crest2', 'head', [0, 4, -1], [-0.4, 0, -1, 0.4, 2.5, 2], 'coat')]),
    'bald_eagle': dict(base='crow', scale=1.6, coat=((92, 64, 40), (44, 30, 20)),
                       mob=dict(hp=14, xp=5, drops=[['feather', 1, 3, 1]], spawnIn=[['mountains', 2, 1, 1], ['snowy_peaks', 1, 1, 1], ['taiga', 1, 1, 1]]),
                       retex={'head': 'white', 'beak': 'beak', 'tailP': 'white'}),
    'seagull': dict(base='crow', scale=1.0, coat=((236, 236, 236), (150, 156, 164)),
                    mob=dict(hp=6, drops=[['feather', 1, 2, 1]], spawnIn=[['beach', 6, 2, 4], ['snowy_beach', 2, 1, 2]]),
                    retex={'beak': 'beak'}),
    'lobster': dict(base='scorpion', scale=0.8, coat=((196, 60, 40), (110, 26, 18)),
                    mob=dict(hp=6, hostile=False, drops=[['cod', 0, 1, 1]], spawnIn=[['beach', 4, 1, 3]])),
}
# the terrain-13 Nether lands' creatures (MIT-licensed source, docs/THIRD_PARTY_NOTICES.md): sim/creature_spawn.cpp spawns them
# in their lands (gen/nether_painted.h), not by spawnIn
MOBS.update({
    'firefly': dict(base='dragonfly', scale=0.8, coat=((236, 220, 90), (120, 96, 30)),
                    mob=dict(label='a firefly', hp=1, glows=12, ambient=True, endLands=False, paintedNether=True, drops=[['glowstone_dust', 0, 1, 0.5]])),
    'hydrogen_jellyfish': dict(base='cubozoa', scale=1.0, coat=((214, 120, 200), (110, 50, 120)),
                               mob=dict(label='a hydrogen jellyfish', ai='flyer', hp=1, flyer=True, floaty=True, swims=False, water=False, glows=10,
                                        retaliates=True, damage=3, speed=0.5, fly=0.6, deathBlast=3.0, fireImmune=True, endLands=False, paintedNether=True, drops=[])),
    'naga': dict(base='scorched_skeleton', scale=1.05, coat=((96, 120, 70), (40, 52, 30)),
                 mob=dict(label='a naga', hp=10, damage=1, onHit={'wither': 10}, fireImmune=True, paintedNether=True,
                          drops=[['coal', 0, 1, 0.5], ['bone', 0, 2, 1]]),
                 extra=[('hoodL', 'head', [-4, 2, 0], [-2, -4, -2, 0, 4, 3], 'coat'), ('hoodR', 'head', [4, 2, 0], [0, -4, -2, 2, 4, 3], 'coat')]),
    'flying_pig': dict(base='phyg', scale=0.9, coat=((214, 130, 120), (140, 60, 60)),
                       mob=dict(label='a flying pig', hp=6, farm=False, flyer=True, roosts=True, paintedNether=True,
                                drops=[['porkchop', 1, 2, 1], ['nether_wart', 0, 3, 0.25]])),
    'jungle_skeleton': dict(base='skeleton', scale=1.0, coat=((150, 170, 110), (70, 84, 50)),
                            mob=dict(label='a jungle skeleton', fleesSun=False, fireImmune=True, paintedNether=True, drops=[['bone', 0, 2, 1], ['arrow', 0, 2, 1]])),
    'skull': dict(base='cinder_wisp', scale=0.55, coat=((226, 220, 200), (110, 100, 86)),
                  mob=dict(label='a skull', hp=4, damage=1, onHit={'fire': 3}, fireImmune=True, paintedNether=True,
                           drops=[['coal', 0, 1, 0.5], ['bone', 0, 1, 1]])),
})

SOLID = {'antler': (196, 176, 140), 'horn': (60, 54, 48), 'ivory': (236, 230, 214), 'beak': (240, 130, 30), 'white': (240, 240, 236)}


def lum(c): return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def recolour(im, light, dark, even=False):
    """the picture's shading kept, its colour the new coat's: bright to `light`, dim to `dark`; the darkest (eyes, hooves) kept dark"""
    im = im.convert('RGBA'); px = im.load(); w, h = im.size
    ls = [lum(px[x, y]) for y in range(h) for x in range(w) if px[x, y][3] > 0]
    if not ls: return im
    lo, hi = min(ls), max(ls)
    out = Image.new('RGBA', (w, h)); po = out.load()
    for y in range(h):
        for x in range(w):
            c = px[x, y]
            if c[3] == 0: po[x, y] = c; continue
            l = lum(c)
            if l < 40: po[x, y] = c; continue   # eyes, nostrils, hooves
            t = (l - lo) / max(1.0, hi - lo)
            if even: t = 0.55 + (t - 0.5) * 0.3   # (a cow's patches would show through: an even coat, a little shading kept)
            po[x, y] = tuple(int(dark[k] + (light[k] - dark[k]) * t) for k in range(3)) + (c[3],)
    return out


def main(d):
    d = pathlib.Path(d)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    out = d / 'textures' / 'entity' / 'new_mobs'; out.mkdir(parents=True, exist_ok=True)

    def add_tile(name, im, res):
        rel = 'textures/entity/new_mobs/%s.png' % name
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(res); tj['count'] += 1
        return name

    def solid(name, rgb):
        im = Image.new('RGBA', (16, 16))
        for y in range(16):
            for x in range(16):
                k = 0.88 + 0.12 * (((x * 7 + y * 13) % 5) / 4)
                im.putpixel((x, y), tuple(int(c * k) for c in rgb) + (255,))
        return add_tile(name, im, 16)

    made = 0
    for key, M in MOBS.items():
        base = mobs.get(M['base'])
        if base is None: continue
        bmodel = models.get(base.get('model', M['base']))
        if bmodel is None: continue
        light, dark = M['coat']
        coat_tile = None
        retiled = {}

        def retile(t):
            nonlocal coat_tile
            if not isinstance(t, str) or t not in tiles: return t
            if t in retiled: return retiled[t]
            src = d / files[tiles[t]]
            try: im = Image.open(src)
            except Exception: return t
            n = add_tile('%s_%s' % (key, t.split('_', 1)[-1] if '_' in t else t), recolour(im, light, dark, M.get('even', False)), tj['res'][tiles[t]] if tiles[t] < len(tj['res']) else 32)
            retiled[t] = n
            if coat_tile is None: coat_tile = n
            return n

        mdl = copy.deepcopy(bmodel)
        mdl['scale'] = round(bmodel.get('scale', 1) * M['scale'], 3)
        for p in mdl['parts']:
            if 'tex' in p:
                p['tex'] = {k: retile(v) for k, v in p['tex'].items()} if isinstance(p['tex'], dict) else retile(p['tex'])
        named = {p['name']: p for p in mdl['parts']}
        for pname, box in M.get('boxes', {}).items():
            if pname in named: named[pname]['box'] = box; named[pname]['min'] = [v / 16 for v in box[:3]]; named[pname]['max'] = [v / 16 for v in box[3:]]
        for pname, tex in M.get('retex', {}).items():
            if pname in named: named[pname]['tex'] = solid('%s_%s' % (key, tex), SOLID[tex]) if tex in SOLID else coat_tile
        for (pname, parent, at, box, tex) in M.get('extra', []):
            t = coat_tile if tex == 'coat' else solid('%s_%s' % (key, tex), SOLID[tex])
            mdl['parts'] = [q for q in mdl['parts'] if q['name'] != pname]
            mdl['parts'].append({'name': pname, 'parent': parent, 'at': at, 'box': box, 'tex': t,
                                 'pivot': [v / 16 for v in at], 'min': [v / 16 for v in box[:3]], 'max': [v / 16 for v in box[3:]]})
        models[key] = mdl
        mob = copy.deepcopy(base)
        mob.pop('variantDefs', None)
        mob['model'] = key
        mob['w'] = round(base.get('w', 0.6) * M['scale'], 3); mob['h'] = round(base.get('h', 1) * M['scale'], 3)
        mob['name'] = key.replace('_', ' ').title()
        for k2, v in M['mob'].items(): mob[k2] = v
        mob['drops'] = [dr for dr in mob.get('drops', []) if dr[0] in items]
        mobs[key] = mob
        made += 1

    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('new_mobs: %d creatures' % made)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
