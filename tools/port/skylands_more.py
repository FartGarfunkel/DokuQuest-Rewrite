#!/usr/bin/env python3
# tools/port/skylands_more.py — more for the Skylands (the user's list, 2026-11-01): our own takes on sky-island ideas.
#  * driftstone: a pale lilac stone with light in its veins, found in the islands' undersides. It falls UP: with nothing
#    above it, it floats away until it meets something (sim/blockfx.cpp checkFall "floats"), so you can build up into
#    the sky with it. Four make a driftstone lift plate... no: kept simple, it is a building block.
#  * whirlwinds: little spinning columns of cloud that wander the islands and toss whatever they touch up into the air
#    (sim/creature.cpp whirlwindTick); they blow themselves out after a while. They leave a gust puff.
#  * valkyries: the Valkyrie Queen's guard, winged, who wake with her in the sky temple (sim/keepers.cpp).
# Hooked into data_patches.py before the creature looks.   python3 tools/port/skylands_more.py data
import copy, json, pathlib, random, sys
from PIL import Image


def part(name, parent, at, box, tex, alpha=None, role=None, idx=None, rest=None):
    p = {'name': name, 'parent': parent, 'at': list(at), 'box': list(box), 'tex': tex,
         'pivot': [at[0] / 16, at[1] / 16, at[2] / 16], 'min': [box[0] / 16, box[1] / 16, box[2] / 16], 'max': [box[3] / 16, box[4] / 16, box[5] / 16]}
    if alpha is not None: p['alpha'] = alpha
    if role: p['role'] = role
    if idx is not None: p['idx'] = idx
    if rest: p['rest'] = list(rest)
    return p


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    I = items.get('items', items)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text()); M = models.get('models', models)
    tj = json.loads((d / 'tiles.json').read_text()); names = tj['names']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; have = {b['key'] for b in blocks}

    def pic(name):
        i = names.get(name)
        return Image.open(d / tj['files'][i]).convert('RGBA') if i is not None and (d / tj['files'][i]).exists() else None

    def tile(name, im, kind='block'):
        rel = 'textures/%s/%s.png' % (kind, name)
        im.save(d / rel)
        if name not in names:
            names[name] = tj['count']; tj['files'].append(rel); tj['res'].append(im.size[0]); tj['count'] += 1
        return names[name]

    # ---- driftstone: the skystone's grain in lilac, with bright veins
    base = pic('skystone')
    if base is not None:
        im = base.copy(); px = im.load(); W, H = im.size; rnd = random.Random(7)
        for y in range(H):
            for x in range(W):
                r, g, b, a = px[x, y]
                l = (r * .299 + g * .587 + b * .114) / 255
                px[x, y] = (int(150 + 80 * l), int(130 + 70 * l), int(185 + 60 * l), a)
        # veins: a few wandering lines of pale light
        for v in range(4):
            x, y = rnd.random() * W, rnd.random() * H; dx, dy = rnd.random() - 0.5, rnd.random() - 0.5
            for k in range(W):
                xi, yi = int(x) % W, int(y) % H
                px[xi, yi] = (246, 236, 255, 255)
                x += dx + (rnd.random() - 0.5) * 0.8; y += dy + (rnd.random() - 0.5) * 0.8
        t = tile('driftstone', im)
        if 'driftstone' not in have:
            blocks.append({'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.4, 'tool': 'pickaxe', 'tier': 1,
                           'drop': 'driftstone', 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': 'driftstone', 'name': 'Driftstone', 'tex': 'driftstone',
                           'falls': True, 'floats': True})
            bj['faces'].append([t] * 6)
        I['driftstone'] = {'key': 'driftstone', 'name': 'Driftstone', 'maxStack': 64, 'icon': 'block:driftstone', 'block': 'driftstone'}

    # ---- whirlwinds: a column of cloud with puffs whirling round it (the wisp rig's orbit)
    if pic('cloud_puff') is not None:
        parts = [part('core', None, [0, 12, 0], [-1.5, -10, -1.5, 1.5, 10, 1.5], 'cloud_puff', alpha=0.35, role='wcore')]
        for i in range(8):
            y = 3 + i * 2.6; r = 1.5 + i * 0.75
            import math
            a = i * 2.4
            s = 1.2 + i * 0.22
            parts.append(part('rod%d' % i, None, [math.cos(a) * r, y, math.sin(a) * r], [-s, -1.0, -s, s, 1.0, s], 'cloud_puff', alpha=0.55, role='rod', idx=i))
        M['whirlwind'] = {'rig': 'wisp', 'post': 'orbit', 'scale': 1, 'height': 26, 'parts': parts}
        mobs['whirlwind'] = {'model': 'whirlwind', 'label': 'a whirlwind', 'hp': 14, 'w': 0.6, 'h': 1.8, 'speed': 2.2, 'xp': 2, 'ambient': True, 'noPush': True,
                             'whirlwind': True, 'sound': 'zephyr', 'drops': [['gust_puff', 1, 2, 0.8]] if 'gust_puff' in I else []}

    # ---- valkyries: the queen's guard (her model, smaller, with white wings)
    if 'valkyrie_queen' in mobs:
        q = mobs['valkyrie_queen']; qm = q.get('model', 'hollow_archmage')
        if qm in M:
            m = copy.deepcopy(M[qm])
            body = next((p for p in m['parts'] if p['name'] == 'body'), None)
            if body:
                z1 = body['box'][5]
                for s, nm in ((1, 'wingR'), (-1, 'wingL')):
                    m['parts'].append(part('v_' + nm, 'body', [s * 1.5, 10, z1], [min(0, s * 9), -9, 0, max(0, s * 9), 3, 0.5], 'wool', rest=(0, -s * 0.5, 0)))
            M['valkyrie'] = m
            v = {k: q[k] for k in ('w', 'h', 'speed', 'voice', 'tint') if k in q}
            v.update({'model': 'valkyrie', 'label': 'a valkyrie', 'hp': 40, 'hostile': True, 'damage': 4, 'xp': 30, 'knockResist': 0.3, 'held': 'iron_sword',
                      'drops': [['golden_feather', 0, 1, 0.3], ['aether_crystal', 1, 3, 0.6], ['feather', 1, 3, 0.8]]})
            v['h'] = round(q.get('h', 2.4) * 0.85, 2)
            mobs['valkyrie'] = v
            M['valkyrie']['scale'] = M['valkyrie'].get('scale', 1) * 0.85

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    print('skylands more ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
