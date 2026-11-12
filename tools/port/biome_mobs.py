#!/usr/bin/env python3
# tools/port/biome_mobs.py — the biome fuselings and village golems BUILT from their land's blocks (the user's ask): a
# model each, made from the fuseling's / iron golem's with every part's picture swapped for a block of that land, plus a
# thing or two of its own (a cactus crown, a mushroom cap, a spike, flowers; the golems' shoulder plates, ice spikes, moss
# and mushrooms, acacia horns). No glow: they differ in build, as the user asked. Their own
# model keys, so no pack skin covers them up. Hooked into data_patches.py.
#   python3 tools/port/biome_mobs.py data
import copy, json, pathlib, sys

# key, label, body, head, legs, fronds, thorns, extras [(name, parent, at, box, tex, glow)]
FUSE = [
    ('ember_fuseling', 'an ember fuseling', 'netherrack', 'magma_block', 'blackstone', 'shroomlight', 'magma_block', []),
    ('sky_fuseling', 'a sky fuseling', 'skystone', 'skystone', 'skystone', 'cloud_leaves', 'ice', []),
]

# The land fuselings, creeper-shaped and built from their land (the user's example sheet, 2026-11-01): four legs, a body,
# a head with the creeper face in a darker block, and each one's own build. Units are model pixels (16 a block).
#   legs (tex, height, width, spread x, spread z), body (tex, box), head (tex or {face: tex}, box), face tex,
#   faces: extra creeper faces [(parent, front z, bottom y)], extras [(name, parent, at, box, tex)], height
def _std(body, head=None, legs=None, face='coal_block', bodybox=(-4, 0, -2, 4, 12, 2), headbox=(-4, 0, -4, 4, 8, 4), legw=4, legh=6, sx=2, sz=4):
    return {'legs': (legs or body, legh, legw, sx, sz), 'body': (body, bodybox), 'head': (head or body, headbox), 'face': face, 'faces': [], 'extras': []}
CREEP = {}
c = _std('terracotta', face='brown_terracotta', bodybox=(-4.5, 0, -2.5, 4.5, 12, 2.5), headbox=(-4.5, 0, -4.5, 4.5, 8, 4.5), legw=4.5)
c['label'] = 'a redlands fuseling'
c['extras'] = [('gold%d' % i, par, at, (-0.5, -0.5, -0.12, 0.5, 0.5, 0.0), 'gold_block') for i, (par, at) in enumerate([
    ('body', (-3, 9, -2.5)), ('body', (2, 6, -2.5)), ('body', (-1, 3, -2.5)), ('body', (3, 1.5, -2.5)), ('head', (3, 1.5, -4.5)), ('head', (-3.2, 7, -4.5))])]
c['extras'] += [('gold6', 'body', (4.5, 8, 0), (0, -0.5, -0.5, 0.12, 0.5, 0.5), 'gold_block'), ('gold7', 'body', (-4.5, 4, 1), (-0.12, -0.5, -0.5, 0, 0.5, 0.5), 'gold_block'),
                ('cactus', 'head', (0, 8, 0), (-1.5, 0, -1.5, 1.5, 3, 1.5), {'all': 'cactus_side', 'top': 'cactus_top'}),
                ('bloom', 'head', (0, 11, 0), (-0.6, 0, -0.6, 0.6, 1, 0.6), 'flower_yellow')]
CREEP['redlands_fuseling'] = c
c = {'label': 'a jungle fuseling', 'legs': ('moss_block', 5, 3, 1.5, 1.5), 'body': ('bamboo_block_side', (-1.5, 0, -1.5, 1.5, 16, 1.5)),
     'head': ('bamboo_block_side', (-3.5, 0, -3.5, 3.5, 7, 3.5)), 'face': 'moss_block', 'faces': [],
     'extras': [('node%d' % i, 'body', (0, y, 0), (-1.8, 0, -1.8, 1.8, 0.6, 1.8), 'bamboo_block_top') for i, y in enumerate((4, 9, 14))] +
               [('leaf', 'body', (1.5, 9, 0), (0, 0, -0.2, 3, 1.2, 0.2), 'jungle_leaves')], 'height': 28}
CREEP['jungle_fuseling'] = c
c = _std('sandstone_side', {'all': 'sandstone_side', 'top': 'sandstone_top'}, face='brown_terracotta'); c['label'] = 'a beach fuseling'
c['extras'] = [('mossTop', 'head', (0, 8, 0), (-4.2, -0.6, -4.2, 4.2, 0.3, 4.2), 'moss_block'), ('mossDrip', 'head', (-2.5, 8, -4), (-1.5, -3, -0.3, 1.5, 0, 0), 'moss_block'),
               ('pebble', 'head', (0.5, 8.3, 0.5), (-1.2, 0, -1.2, 1.2, 1.4, 1.2), 'andesite')] +               [('mossLeg%d' % i, 'leg' + n, (0, 0, 0), (-2.2, -1.6, -2.2, 2.2, 0.2, 2.2), 'moss_block') for i, n in enumerate(('FL', 'BR'))]
CREEP['beach_fuseling'] = c
c = _std('gravel', headbox=(-4.5, 0, -4.5, 4.5, 9, 4.5), face='coal_block', legw=4.5); c['label'] = 'a gravel fuseling'
CREEP['gravel_fuseling'] = c
c = _std('sandstone_side', {'all': 'sandstone_side', 'top': 'sandstone_top'}, face='brown_terracotta'); c['label'] = 'a dune fuseling'
c['extras'] = [('cactus', 'head', (0, 8, 0), (-1.5, 0, -1.5, 1.5, 3.5, 1.5), {'all': 'cactus_side', 'top': 'cactus_top'}),
               ('bloom', 'head', (0, 11.5, 0), (-0.6, 0, -0.6, 0.6, 1, 0.6), 'flower_red'),
               ('bush', 'head', (4, 5, 0), (0, 0, -0.2, 3.5, 4, 0.2), 'dead_bush')]
CREEP['dune_fuseling'] = c
c = _std('mud', face='coal_block'); c['label'] = 'a bog fuseling'
c['extras'] = [('spike', 'head', (0, 8, 0), (-1, 0, -1, 1, 4, 1), 'pointed_dripstone'), ('spike2', 'head', (0, 12, 0), (-0.5, 0, -0.5, 0.5, 4, 0.5), 'pointed_dripstone')]
CREEP['bog_fuseling'] = c
c = _std('snow', face='packed_ice'); c['label'] = 'a frost fuseling'
c['extras'] = [('stoneR', 'head', (2.8, 7.5, 0.5), (-1.3, 0, -1.3, 1.3, 2, 1.3), 'andesite'), ('stoneL', 'head', (-2.8, 7.5, 2), (-1.1, 0, -1.1, 1.1, 1.8, 1.1), 'andesite')]
CREEP['frost_fuseling'] = c
c = _std('mossy_cobblestone', face='coal_block'); c['label'] = 'a moss fuseling'
c['extras'] = [('tuft%d' % i, 'head', at, (-1, 0, -0.1, 1, h, 0.1), 'tall_grass') for i, (at, h) in enumerate([((-2.5, 8, -2), 4), ((1, 8, 1), 3), ((2.5, 8, -2.5), 3.5), ((-1, 8, 2.5), 2.5)])]
CREEP['moss_fuseling'] = c
c = {'label': 'a meadow fuseling', 'legs': ('moss_block', 5, 4.5, 2.2, 4), 'body': ('azalea_leaves', (-4, 0, -2.5, 4, 11, 2.5)),
     'head': ('flowering_azalea_leaves', (-5, 0, -5, 5, 9, 5)), 'face': 'log_side', 'faces': [],
     'extras': [('trunk', 'body', (0, 1.5, 0), (-4.3, 0, -2.8, 4.3, 3, 2.8), 'log_side'),
                ('bush', 'head', (0, 9, 0), (-4.5, 0, -4.5, 4.5, 2, 4.5), 'flowering_azalea_leaves'),
                ('fl1', 'head', (-3, 9, -3), (-0.8, 0, -0.8, 0.8, 2.6, 0.8), 'flower_red'), ('fl2', 'head', (2.5, 9, 1.5), (-0.8, 0, -0.8, 0.8, 2.6, 0.8), 'flower_yellow'),
                ('fl3', 'head', (3, 9, -3.5), (-0.8, 0, -0.8, 0.8, 2.2, 0.8), 'flower_red'), ('fl4', 'head', (-2.5, 9, 3), (-0.8, 0, -0.8, 0.8, 2.4, 0.8), 'flower_yellow')]}
CREEP['meadow_fuseling'] = c
c = _std('mushroom_stem', face='brown_terracotta', bodybox=(-3.5, 0, -2.5, 3.5, 12, 2.5)); c['label'] = 'a mushroom fuseling'
c['extras'] = [('cap', 'head', (0, 8, 0), (-7.5, 0, -7.5, 7.5, 4, 7.5), 'red_mushroom_block'), ('gills', 'head', (0, 7.6, 0), (-7.2, 0, -7.2, 7.2, 0.4, 7.2), 'mushroom_stem')]
CREEP['mushroom_fuseling'] = c
c = {'label': 'a savanna fuseling', 'legs': ('cobblestone', 3, 4, 2, 2.2), 'body': ('orange_terracotta', (-4, 0, -4, 4, 16, 4)),
     'head': ('orange_terracotta', (-4, 0, -4, 4, 8, 4)), 'face': 'brown_terracotta', 'faces': [('body', -4, 0), ('body', -4, 8)],
     'extras': [('apron', 'body', (0, 0, 0), (-4.4, 0, -4.4, 4.4, 1.6, 4.4), 'terracotta'), ('ring', 'body', (0, 8, 0), (-4.2, -0.4, -4.2, 4.2, 0.4, 4.2), 'terracotta')],
     'height': 30}
CREEP['savanna_fuseling'] = c
c = {'label': 'a swamp fuseling', 'legs': ('moss_block', 6, 4, 2, 4), 'body': ('moss_block', (-4, 0, -2, 4, 12, 2)),
     'head': ({'all': 'moss_block', 'front': 'bone_block_side'}, (-4, 0, -4, 4, 8, 4)), 'face': 'coal_block', 'faces': [], 'eyesOnly': True,
     'extras': [('brim', 'head', (0, 8, 0), (-8, 0, -8, 8, 1, 8), 'leaves'), ('crown', 'head', (0, 9, 0), (-4, 0, -4, 4, 3, 4), 'leaves'),
                ('tuftA', 'head', (-2, 12, 0), (-1, 0, -0.1, 1, 3, 0.1), 'tall_grass'), ('tuftB', 'head', (2, 12, 1), (-0.1, 0, -1, 0.1, 3, 1), 'tall_grass'),
                ('jaw', 'head', (0, 0, -4), (-1.5, -2.5, -0.6, 1.5, 0, 0.4), 'bone_block_side'), ('chin', 'head', (0.5, -2.5, -4), (-1, -2, -0.6, 0.5, 0, 0.4), 'bone_block_side'),
                ('vine', 'body', (0, 0, -2), (-4, 0, -0.2, 4, 12, 0), 'vine')]}
CREEP['swamp_fuseling'] = c


def creeper_parts(spec, t):
    lt, lh, lw, sx, sz = spec['legs']
    bt, bb = spec['body']; ht, hb = spec['head']
    tex = lambda x: x if isinstance(x, dict) else t(x)
    parts = [part('body', None, [0, lh, 0], list(bb), tex(bt), False)]
    parts[0]['role'] = 'pbody'
    hp = part('head', 'body', [0, bb[4], 0], list(hb), tex(ht), False); hp['role'] = 'phead'; hp['limb'] = 'head'
    parts.append(hp)
    w2 = lw / 2
    for n, x, z in (('FL', -sx, -sz), ('FR', sx, -sz), ('BL', -sx, sz), ('BR', sx, sz)):
        lp = part('leg' + n, None, [x, lh, z], [-w2, -lh, -w2, w2, 0, w2], tex(lt), False); lp['role'] = 'qleg'; lp['limb'] = 'leg' + n
        parts.append(lp)
    # the creeper face (eyes and mouth, no two boxes overlapping) a hair proud of each face it's on
    def face(par, zf, y0, cx=0.0, k=1.0, eyes_only=False):
        F = [(-3, 4, -1, 6), (1, 4, 3, 6)]
        if not eyes_only: F += [(-1, 3, 1, 4), (-2, 1, 2, 3), (-2, 0, -1, 1), (1, 0, 2, 1)]
        out = []
        for i, (x0, y0b, x1, y1) in enumerate(F):
            out.append(part('face_%s_%d_%d' % (par, int(y0), i), par, [0, 0, 0], [cx + x0 * k, y0 + y0b * k, zf - 0.12, cx + x1 * k, y0 + y1 * k, zf + 0.02], t(spec['face']), False))
        return out
    hy = (hb[4] - hb[1] - 8) / 2.0
    parts += face('head', hb[2], hb[1] + max(0.0, hy), k=min(1.0, (hb[3] - hb[0]) / 8.0) if hb[3] - hb[0] < 8 else 1.0, eyes_only=spec.get('eyesOnly', False))
    for par, zf, y0 in spec.get('faces', []): parts += face(par, zf, y0)
    for name, par, at, box, tx in spec.get('extras', []):
        parts.append(part(name, par, list(at), list(box), tex(tx), False))
    return parts
# key, label, body, chest, head, limbs, eyes
# each golem's own build (name, parent, at, box, tex): shoulder plates, spikes, moss, horns
GOLEM_BUILD = {
    'sandstone_golem': [('padR', 'armR', [0, 2, 0], [-2.8, 0, -3.8, 2.8, 2.4, 3.8], 'chiseled_sandstone'),
                        ('padL', 'armL', [0, 2, 0], [-2.8, 0, -3.8, 2.8, 2.4, 3.8], 'chiseled_sandstone'),
                        ('crest', 'head', [0, 10, 0], [-1, 0, -4.2, 1, 2.5, 3.2], 'cut_sandstone'),
                        ('spine', 'body', [0, 9, 5.5], [-1.5, -6, 0, 1.5, 3, 2], 'cactus_side')],
    'frost_golem': [('spikeR', 'body', [7, 12, 1], [-1, 0, -1, 1, 7, 1], 'blue_ice'), ('spikeR2', 'body', [5, 12, -2], [-0.8, 0, -0.8, 0.8, 4, 0.8], 'blue_ice'),
                    ('spikeL', 'body', [-7, 12, 1], [-1, 0, -1, 1, 7, 1], 'blue_ice'), ('spikeL2', 'body', [-5, 12, -2], [-0.8, 0, -0.8, 0.8, 4, 0.8], 'blue_ice'),
                    ('beard', 'head', [0, 1, -4], [-2.5, -3.5, -0.8, 2.5, 0, 0.2], 'ice')],
    'moss_golem': [('mossR', 'armR', [0, 2.2, 0], [-2.6, -4, -3.4, 2.6, 0.6, 3.4], 'moss_block'),
                   ('mossL', 'armL', [0, 2.2, 0], [-2.6, -4, -3.4, 2.6, 0.6, 3.4], 'moss_block'),
                   ('cap', 'head', [0, 10, 0], [-3.5, 0, -3.5, 3.5, 2, 3.5], 'red_mushroom_block'),
                   ('stem', 'head', [0, 9.5, 0], [-1.2, 0, -1.2, 1.2, 1, 1.2], 'mushroom_stem'),
                   ('vines', 'body', [0, 2, -5.6], [-7, 0, -0.2, 7, 9, 0.2], 'vine')],
    'savanna_golem': [('hornR', 'head', [3.5, 9, 0], [0, 0, -0.8, 3.5, 1.6, 0.8], 'acacia_log_side'), ('tipR', 'head', [6.5, 9, 0], [-0.8, 0, -0.8, 0.8, 4, 0.8], 'acacia_log_side'),
                      ('hornL', 'head', [-3.5, 9, 0], [-3.5, 0, -0.8, 0, 1.6, 0.8], 'acacia_log_side'), ('tipL', 'head', [-6.5, 9, 0], [-0.8, 0, -0.8, 0.8, 4, 0.8], 'acacia_log_side'),
                      ('sash', 'body', [0, 4, 0], [-9.3, 0, -5.8, 9.3, 2, 5.8], 'orange_terracotta')],
}
GOLEM = [
    ('sandstone_golem', 'a sandstone golem', 'sandstone_side', 'chiseled_sandstone', 'cut_sandstone', 'sandstone_side', 'glowstone'),
    ('frost_golem', 'a frost golem', 'packed_ice', 'blue_ice', 'snow', 'packed_ice', 'sea_lantern'),
    ('moss_golem', 'a mossy golem', 'mossy_cobblestone', 'mossy_stone_bricks', 'moss_block', 'spruce_log_side', 'shroomlight'),
    ('savanna_golem', 'a savanna golem', 'acacia_log_side', 'terracotta', 'red_terracotta', 'acacia_log_side', 'glowstone'),
]


def part(name, parent, at, box, tex, glow):
    p = {'name': name, 'parent': parent, 'at': at, 'box': box, 'tex': tex,
         'pivot': [at[0] / 16, at[1] / 16, at[2] / 16], 'min': [box[0] / 16, box[1] / 16, box[2] / 16], 'max': [box[3] / 16, box[4] / 16, box[5] / 16]}
    if glow: p['glow'] = True
    return p


def main(d):
    d = pathlib.Path(d)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
    M = models.get('models', models)
    names = json.loads((d / 'tiles.json').read_text())['names']
    def t(n): return n if n in names else 'stone'
    base = M['fuseling']
    for key, label, body, head, legs, frond, thorn, extras in FUSE:
        m = copy.deepcopy(base)
        for p in m['parts']:
            n = p['name']
            if n == 'body': p['tex'] = t(body)
            elif n == 'head': p['tex'] = {'all': t(head), 'top': t(head)}
            elif n == 'jaw': p['tex'] = {'all': t(head), 'top': 'fuse_maw', 'front': 'fuse_maw'}
            elif n.startswith('frond'): p['tex'] = t(frond)
            elif n.startswith('thorn'): p['tex'] = t(thorn)
            elif n.startswith('leg'): p['tex'] = t(legs)
        for e in extras:
            ex = part(*e); ex['tex'] = ex['tex'] if isinstance(ex['tex'], dict) else t(ex['tex']); m['parts'].append(ex)
        M[key] = m
        v = dict(mobs.get(key) or mobs['fuseling']); v.update({'label': label, 'model': key}); v.pop('tint', None)
        if key == 'ember_fuseling': v['fireImmune'] = True
        mobs[key] = v
    for key, spec in CREEP.items():
        m = {k: v for k, v in base.items() if k != 'parts'}
        m['height'] = spec.get('height', 26)
        m['parts'] = creeper_parts(spec, t)
        M[key] = m
        v = dict(mobs.get(key) or mobs['fuseling']); v.update({'label': spec['label'], 'model': key}); v.pop('tint', None)
        mobs[key] = v
    gbase = M['iron_golem']
    for key, label, body, chest, head, limbs, eyes in GOLEM:
        m = copy.deepcopy(gbase)
        for p in m['parts']:
            n = p['name']
            if n == 'body': p['tex'] = {'all': t(body), 'front': t(chest)}
            elif n == 'head': p['tex'] = t(head)
            elif n in ('nose', 'brow'): p['tex'] = t(chest)
            elif n in ('waist', 'shoulders'): p['tex'] = t(body)
            else: p['tex'] = t(limbs)
        # eyes under the brow (dark, not lit: the user's ask), and a build of their own
        for side, x in (('eyeL', -2.2), ('eyeR', 2.2)):
            m['parts'].append(part(side, 'head', [x, 6.5, -4], [-1, -0.6, -0.15, 1, 0.6, 0.05], t('coal_block'), False))
        for e in GOLEM_BUILD.get(key, []):
            ex = part(*e, False); ex['tex'] = ex['tex'] if isinstance(ex['tex'], dict) else t(ex['tex']); m['parts'].append(ex)
        M[key] = m
        v = dict(mobs.get(key) or mobs['iron_golem']); v.update({'label': label, 'model': key}); v.pop('tint', None)
        mobs[key] = v
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    print('biome mobs ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
