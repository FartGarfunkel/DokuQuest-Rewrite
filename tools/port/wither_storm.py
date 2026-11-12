#!/usr/bin/env python3
"""The Wither Storm as Minecraft: Story Mode has it: no longer a Wither drawn five times over, but the behemoth it grows
into: a black obsidian mass with a nether-brick maw, three heads with glowing violet eyes and mandibles (the middle one
huge, quartz-white teeth), the command block glowing in its chest, and long tentacles hanging under it that sway as it
drifts (rig "wither_storm": sim/wither.cpp animates the tentacles, the jaws and the heads; it grows with what it eats).
Idempotent; data_patches.py runs it after the Wither."""
import json, math, pathlib

OBS = {'all': 'obsidian', 'top': 'crying_obsidian'}
DARK = 'blackstone'
MAW = 'nether_bricks'
EYE = 'purple_concrete'
TOOTH = 'bone_block_side'
HEART = {'all': 'command_block_side', 'front': 'command_block_front', 'back': 'command_block_front'}


def P(name, parent, at, box, tex, glow=False):
    p = {'name': name, 'parent': parent, 'at': at, 'box': box, 'tex': tex,
         'pivot': [at[0] / 16, at[1] / 16, at[2] / 16], 'min': [b / 16 for b in box[:3]], 'max': [b / 16 for b in box[3:]]}
    if glow: p['glow'] = 1
    return p


def head(name, parent, at, s, out):
    """A head of the Storm: a block of obsidian, two violet eyes, mandibles either side of a nether-brick maw with teeth."""
    w, h, d = 6 * s, 5 * s, 7 * s
    out.append(P(name, parent, at, [-w, -h * 0.4, -d * 2, w, h, 0], OBS))
    out.append(P(name + 'Maw', name, [0, -h * 0.4, -d * 1.2], [-w * 0.7, -h * 0.5, -d * 0.8, w * 0.7, 0.01, d * 0.6], MAW))
    out.append(P(name + 'Teeth', name, [0, -h * 0.4, -d * 1.9], [-w * 0.65, -h * 0.25, -0.6 * s, w * 0.65, 0, 0], TOOTH))
    for side, sx in (('L', -1), ('R', 1)):
        out.append(P(name + 'Eye' + side, name, [sx * w * 0.45, h * 0.35, -d * 2 - 0.05], [-1.3 * s, -0.8 * s, -0.5, 1.3 * s, 0.8 * s, 0], EYE, glow=True))
        out.append(P(name + 'Jaw' + side, name, [sx * w * 0.95, -h * 0.35, -d * 1.4], [-1.3 * s, -h * 0.9, -d * 0.8, 1.3 * s, 0, d * 0.6], OBS))
        out.append(P(name + 'Fang' + side, name + 'Jaw' + side, [0, -h * 0.9, -d * 0.8], [-0.7 * s, -1.6 * s, -0.6 * s, 0.7 * s, 0, 0.6 * s], TOOTH))


def model():
    parts = []
    # the mass: a hulk of obsidian, lumps grown onto it, its maw underneath, spines along the back
    parts.append(P('core', None, [0, 36, 0], [-13, -10, -9, 13, 12, 9], OBS))
    parts.append(P('massL', 'core', [-12, -2, 2], [-6, -7, -7, 4, 8, 7], DARK))
    parts.append(P('massR', 'core', [12, -2, 2], [-4, -7, -7, 6, 8, 7], DARK))
    parts.append(P('hump', 'core', [0, 12, 3], [-9, 0, -6, 9, 6, 6], OBS))
    parts.append(P('belly', 'core', [0, -10, 0], [-10, -5, -7, 10, 0.01, 7], MAW))
    for i, (x, z) in enumerate([(-7, 6), (0, 8), (7, 6), (-4, 1), (4, 1)]):
        parts.append(P('spine%d' % i, 'hump', [x, 6, z - 3], [-1.2, 0, -1.2, 1.2, 4 + (i % 3) * 2, 1.2], DARK))
    # the command block in its chest, glowing
    parts.append(P('heart', 'core', [0, 0, -9.05], [-3.5, -3.5, -1.6, 3.5, 3.5, 0.4], HEART, glow=True))
    # three heads: the middle one huge
    head('head', 'core', [0, 11, -7], 1.6, parts)   # the middle one is the big one
    head('headL', 'core', [-15, 6, -4], 0.8, parts)
    head('headR', 'core', [15, 6, -4], 0.8, parts)
    # tentacles under it: six, four segments each, thinning
    spots = [(-9, -5), (0, -6), (9, -5), (-8, 5), (0, 6), (8, 5)]
    for i, (x, z) in enumerate(spots):
        parent = 'core'
        at = [x, -12, z]
        for j in range(4):
            r = 2.4 - j * 0.45
            L = 11 - j
            name = 'tent%d_%d' % (i, j)
            parts.append(P(name, parent, at, [-r, -L, -r, r, 0, r], OBS if j % 2 == 0 else DARK))
            parent, at = name, [0, -L, 0]
        parts.append(P('tent%d_tip' % i, parent, at, [-0.8, -3, -0.8, 0.8, 0, 0.8], 'crying_obsidian'))
    return {'rig': 'wither_storm', 'scale': 2.4, 'parts': parts}


BONE = 'wskel_bone'
SKULL = {'all': 'wskel_head', 'front': 'wskel_face'}


def young(phase):
    """Its first four phases (sim/wither_storm.cpp LOOK): the Wither it rises as, a command block glowing in a cage of ribs
    in its chest (0); a hump growing on its back (1); the middle head a jawed maw now (2); a tilted mass, tentacles under
    it, the command block grown over (3). Model scale 1.5 like the Wither's; the Storm scales it as it grows."""
    parts = [P('spine', None, [0, 8, 1], [-1.5, 0, -1.5, 1.5, 13, 1.5], BONE),
             P('rib1', None, [0, 12, 1], [-5, 0, -1, 5, 1.5, 1], BONE), P('rib2', None, [0, 15, 1], [-5, 0, -1, 5, 1.5, 1], BONE),
             P('rib3', None, [0, 18, 1], [-5, 0, -1, 5, 1.5, 1], BONE),
             P('shoulders', None, [0, 21, 0], [-10, 0, -1.5, 10, 3, 1.5], BONE),
             P('headL', None, [-8, 23, 0], [-3, 0, -3, 3, 6, 3], SKULL), P('headR', None, [8, 23, 0], [-3, 0, -3, 3, 6, 3], SKULL)]
    if phase < 2: parts.append(P('head', None, [0, 24, 0], [-4, 0, -4, 4, 8, 4], SKULL))
    else: head('head', None, [0, 25, -1], 0.9, parts)   # (a storm head: obsidian, violet eyes, mandibles)
    if phase < 3:   # the cage of ribs in its chest, the command block in it
        for i, (x, z) in enumerate([(-5, -1), (-5, -4.5), (-5, -8), (5, -1), (5, -4.5), (5, -8)]):
            parts.append(P('cage%d' % i, None, [x, 9.5, z], [-0.9, 0, -0.9, 0.9, 10, 0.9], BONE))
        for i, x in enumerate((-5, 5)):
            parts.append(P('cageTop%d' % i, None, [x, 19, -4.5], [-0.9, 0, -4.5, 0.9, 1.5, 4.5], BONE))
        parts.append(P('heart', None, [0, 14.5, -4.5], [-3.5, -3.5, -3.5, 3.5, 3.5, 3.5], HEART, glow=True))
    if phase >= 1:   # the hump
        k = [0, 1.0, 1.35, 1.7][phase]
        parts.append(P('hump', 'shoulders', [0, 0, 3.5], [-6 * k, -6 * k, -1, 6 * k, 6 * k, 6 * k], OBS))
        parts.append(P('humpB', 'hump', [-3 * k, 4 * k, 3 * k], [-3 * k, -2 * k, -2 * k, 3 * k, 3 * k, 3 * k], DARK))
        parts.append(P('humpC', 'hump', [4 * k, -2 * k, 4 * k], [-2.5 * k, -2.5 * k, -2 * k, 2.5 * k, 2.5 * k, 2.5 * k], DARK))
    if phase >= 3:   # the mass it's becoming, and the first tentacles under it
        parts.append(P('core', None, [0, 14, 3], [-9, -7, -6, 9, 9, 7], OBS))
        parts.append(P('belly', 'core', [0, -7, 0], [-7, -3, -5, 7, 0.01, 5], MAW))
        for i, (x, z) in enumerate([(-5, 2), (0, 5), (5, 2)]):
            parent, at = 'core', [x, -8, z]
            for j in range(4):
                r = 1.7 - j * 0.32; L = 14 - j * 2
                name = 'tent%d_%d' % (i, j)
                parts.append(P(name, parent, at, [-r, -L, -r, r, 0, r], OBS if j % 2 == 0 else DARK))
                parent, at = name, [0, -L, 0]
    return {'rig': 'wither_storm', 'scale': 1.5, 'parts': parts}


def core_model():
    """Its command block, bare over the podium while it lies on its back."""
    return {'rig': 'storm_core', 'scale': 1.0, 'parts': [P('block', None, [0, 0, 0], [-6, 0, -6, 6, 12, 6], HEART, glow=True)]}


def bombs(d):
    """Super TNT and the Formidibomb (sim/wither_storm.cpp PrimedBomb; flint and steel lights them): TNT's pictures
    recoloured. Super TNT = 4 TNT + 4 gunpowder round a rift crystal (the mod's needs a command block book, which comes
    from a mini-boss we don't have); the Formidibomb = Super TNT + 4 cinder powder (our blaze powder) + 4 gunpowder."""
    from PIL import Image
    import colorsys
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); rj = json.loads((d / 'recipes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; have = {b['key']: i for i, b in enumerate(blocks)}
    pack = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'

    def pic(name):
        p = pack / (name + '.png')
        im = Image.open(p if p.exists() else d / files[tiles[name]]).convert('RGBA')
        return im.crop((0, 0, im.width, im.width)).resize((32, 32), Image.NEAREST)

    def recolour(im, hue, sat, val, glow=None):
        out = im.copy(); px = out.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = px[x, y]
                if not a: continue
                h, s_, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                bright = v > 0.8 and s_ < 0.3   # the paper band: lit up from inside
                if glow and bright: px[x, y] = glow + (a,); continue
                rr, gg, bb = colorsys.hsv_to_rgb(hue, min(1, max(s_, 0.35) * sat), min(1, v * val))
                px[x, y] = (int(rr * 255), int(gg * 255), int(bb * 255), a)
        return out

    def tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    looks = {'super_tnt': (0.80, 1.1, 0.95, None, 'Super TNT', 'A far bigger bang than TNT. Light it with flint and steel.'),
             'formidibomb': (0.78, 1.3, 0.55, (214, 120, 255), 'Formidibomb',
                             'Light it with flint and steel and let the Wither Storm take it: it goes off in its maw and knocks it out of the sky for half a minute, when everything hurts it twice over.')}
    for key, (hue, sat, val, glow, name, desc) in looks.items():
        t = {f: tile('%s_%s' % (key, f), recolour(pic('tnt_' + f), hue, sat, val, glow)) for f in ('side', 'top', 'bottom')}
        b = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 0, 'tool': None, 'tier': 0, 'drop': key,
             'light': 7 if key == 'formidibomb' else 0, 'lightAtt': 0, 'sound': 'grass', 'key': key, 'name': name,
             'tex': {'top': '%s_top' % key, 'bottom': '%s_bottom' % key, 'side': '%s_side' % key}}
        faces = [t['side'], t['side'], t['top'], t['bottom'], t['side'], t['side']]
        if key in have: i = have[key]; b['id'] = blocks[i]['id']; blocks[i] = b; bj['faces'][i] = faces
        else: b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces); have[key] = len(blocks) - 1
        items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key, 'desc': desc, 'rarity': 'epic' if key == 'formidibomb' else 'rare'}
    rj['recipes'] = [r for r in rj['recipes'] if r.get('out') not in ('super_tnt', 'formidibomb')]
    rj['recipes'].append({'out': 'super_tnt', 'n': 1, 'shape': ['GTG', 'TCT', 'GTG'], 'key': {'G': 'gunpowder', 'T': 'tnt', 'C': 'rift_crystal'}, 'h': 3, 'w': 3})
    rj['recipes'].append({'out': 'formidibomb', 'n': 1, 'shape': ['GPG', 'PSP', 'GPG'], 'key': {'G': 'gunpowder', 'P': 'cinder_powder', 'S': 'super_tnt'}, 'h': 3, 'w': 3})
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))


def main(d):
    d = pathlib.Path(d)
    models = json.loads((d / 'models.json').read_text())
    mobs = json.loads((d / 'mobs.json').read_text())
    # its looks are the mod's own now (tools/port/wither_storm_models.py writes wither_storm_p0..p5); these hand-made
    # ones stand in only until that has been run
    if 'uvTex' not in models.get('wither_storm', {}):
        models['wither_storm'] = model()
        for ph in range(4): models['wither_storm_p%d' % ph] = young(ph)
    models.pop('storm_core', None); mobs.pop('storm_core', None)   # (its command block on a podium: gone)
    if 'wither_storm' in mobs:
        m = mobs['wither_storm']
        m.pop('tint', None)               # its own colours now
        m['hp'] = 400                     # (the mod's; a young one mends ten a second, a grown one takes nothing)
        m['w'], m['h'] = 1.2, 3.6         # (sim/wither_storm.cpp sets its box by phase as it grows)
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    bombs(d)
    print('wither storm: %d parts, young %s' % (len(models['wither_storm']['parts']), [len(models['wither_storm_p%d' % i]['parts']) for i in range(4)]))


if __name__ == '__main__':
    main(pathlib.Path(__file__).resolve().parents[2] / 'data')
