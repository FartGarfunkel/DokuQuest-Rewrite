#!/usr/bin/env python3
# tools/port/crows.py — crows (the user's list: "crows landing on scarecrows"): small black birds that flock over fields
# and village farms and settle on the scarecrows' heads (a pumpkin on a hay bale on a fence post) and on fence posts by
# the crops (sim/creature_beasts.cpp flyerAI, "scarecrowPerch"). Built from the parrot's bird model: black feathers with a
# blue sheen, a long dark beak, no crest, a wedge of a tail. A caw of their own (sfx.json vx.crow). Hooked into
# data_patches.py before the creature looks.
#   python3 tools/port/crows.py data
import colorsys, copy, json, pathlib, sys
from PIL import Image


def recolor(src, mix, val=1.0):
    im = src.convert('RGBA'); px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if not a: continue
            l = (r * .299 + g * .587 + b * .114) / 255
            k = 0.55 + l * 0.7
            px[x, y] = (min(255, int(mix[0] * 255 * k * val)), min(255, int(mix[1] * 255 * k * val)), min(255, int(mix[2] * 255 * k * val)), a)
    return im


def part(name, parent, at, box, tex):
    return {'name': name, 'parent': parent, 'at': list(at), 'box': list(box), 'tex': tex,
            'pivot': [at[0] / 16, at[1] / 16, at[2] / 16], 'min': [box[0] / 16, box[1] / 16, box[2] / 16], 'max': [box[3] / 16, box[4] / 16, box[5] / 16]}


def caw(f0, f1, at, ln=0.16):
    return ['throat', at, ln, {'f': [[0, f0], [0.3, f0 * 1.04], [1, f1]], 'v': 'aa', 'k': 1.6, 'breath': 0.7, 'att': 0.006, 'rel': 0.5, 'open': 0.75}]


def main(d):
    d = pathlib.Path(d)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
    M = models.get('models', models)
    tj = json.loads((d / 'tiles.json').read_text()); names = tj['names']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    sfx = json.loads((d / 'sfx.json').read_text())

    def tile(name, im):
        rel = 'textures/entity/%s.png' % name
        im.save(d / rel)
        if name not in names:
            names[name] = tj['count']; tj['files'].append(rel); tj['res'].append(im.size[0]); tj['count'] += 1

    def pic(name):
        i = names.get(name)
        return Image.open(d / tj['files'][i]).convert('RGBA') if i is not None and (d / tj['files'][i]).exists() else None

    base = pic('parrot_red'); beak = pic('parrot_beak')
    if base is None or 'parrot_red' not in M: print('crows: no parrot to start from'); return
    tile('crow_feather', recolor(base, (0.16, 0.17, 0.24), 0.75))
    tile('crow_beak', recolor(beak if beak else base, (0.22, 0.22, 0.24), 0.8))
    m = copy.deepcopy(M['parrot_red'])
    out = []
    for p in m['parts']:
        n = p['name']
        if n == 'crest': continue
        if n in ('beak',):
            p['tex'] = 'crow_beak'; p['box'] = [-0.5, 0, -2.4, 0.5, 1.4, 0]
            p['min'] = [x / 16 for x in p['box'][:3]]; p['max'] = [x / 16 for x in p['box'][3:]]
        elif n.startswith('leg'): p['tex'] = 'crow_beak'
        elif n == 'tailP':
            p['tex'] = 'crow_feather'; p['box'] = [-1.2, -4, 0, 1.2, 0, 0.6]
            p['min'] = [x / 16 for x in p['box'][:3]]; p['max'] = [x / 16 for x in p['box'][3:]]
        else: p['tex'] = 'crow_feather'
        out.append(p)
    m['parts'] = out
    M['crow'] = m
    mobs['crow'] = {'label': 'a crow', 'ai': 'flyer', 'hp': 4, 'w': 0.22, 'h': 0.45, 'speed': 1.4, 'fly': 4.5, 'xp': 1, 'perches': True,
                    'ambient': True, 'scarecrowPerch': True, 'food': ['wheat_seeds', 'melon_seeds', 'pumpkin_seeds', 'beetroot_seeds'],
                    'sound': 'crow', 'drops': [['feather', 0, 2, 1]], 'model': 'crow'}
    R = sfx['recipes']
    R['vx.crow'] = {'len': 0.9, 'gain': 0.55, 'pv': None, 'takes': [[caw(760, 560, 0), caw(780, 540, 0.3)], [caw(720, 520, 0)], [caw(800, 590, 0), caw(770, 560, 0.26), caw(750, 530, 0.52)]]}
    R['vx.crow_hurt'] = {'len': 0.4, 'gain': 0.6, 'pv': None, 'takes': [[caw(980, 700, 0, 0.12)]]}
    R['vx.crow_death'] = {'len': 0.6, 'gain': 0.6, 'pv': None, 'takes': [[caw(820, 420, 0, 0.3)]]}
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    (d / 'sfx.json').write_text(json.dumps(sfx, separators=(',', ':'), ensure_ascii=False))
    print('crows ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
