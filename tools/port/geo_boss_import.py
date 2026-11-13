#!/usr/bin/env python3
"""A GeckoLib boss (its .geo.json, its picture, its .animation.json, and an attack table) into our game, local only:
data/local_models/<key>.json (git-ignored: the mod's art and moves; the registry merges it into models.json's models and
reads its "clips" and "kit" for the "geo" rig (sim/geo_anim.cpp) and the boss brain (sim/boss_kit.cpp)).
  python3 tools/port/geo_boss_import.py [data-dir]

Each bone is one part of ours: its pivot (from its parent's), its rest turn, and its cubes (turned cubes too) voxelised in
the bone's own space from the picture, a voxel a model pixel. Clips keep every keyframe (rotation in degrees, position in
pixels, linear or Catmull-Rom). Axes: GeckoLib's x is mirrored to ours, its turns applied z, y, x (the model's "rotOrder")."""
import json, math, pathlib, re, sys
import numpy as np
from PIL import Image

S = pathlib.Path('/home/matt/soulrend_extract')
BOSSES = {
    'night_lord': {
        'geo': S / 'mods/souls_like_bosses-1.0.3/assets/souls_like_bosses/geo/night_lord.geo.json',
        'tex': S / 'mods/souls_like_bosses-1.0.3/assets/souls_like_bosses/textures/entity/night_lord.png',
        'glow': S / 'mods/souls_like_bosses-1.0.3/assets/souls_like_bosses/textures/entity/night_lord_glowmask.png',
        'anim': S / 'mods/souls_like_bosses-1.0.3/assets/souls_like_bosses/animations/night_lord.animation.json',
        'attacks': S / 'src/souls_like_bosses-1.0.3/com/slb/nightlord/attack/NightLordAttacks.java',
        'hide': ['light_circle', 'kamehameha_beam', 'lightning_sword2', 'lightning'],   # (effects the mod draws at times: not the body)
    },
    # The Graveyard's Lich ("corrupted champion"): a floating caster; its moves written here from its clips
    'graveyard_lich': {
        'geo': S / 'mods/The_Graveyard_3.1_(FORGE)_for_1.20.1/assets/graveyard/geo/lich.geo.json',
        'tex': S / 'mods/The_Graveyard_3.1_(FORGE)_for_1.20.1/assets/graveyard/textures/entity/lich_texture.png',
        'glow': S / 'mods/The_Graveyard_3.1_(FORGE)_for_1.20.1/assets/graveyard/textures/entity/lich_eye_texture.png',
        'anim': S / 'mods/The_Graveyard_3.1_(FORGE)_for_1.20.1/assets/graveyard/animations/lich/lich.animation.json',
        'alias': {'walking': 'idle', 'phase_change': 'phase_two'},
        'kit': [
            {'id': 'claw', 'clip': 'attack', 'len': 1.96, 'phases': [1], 'ranges': ['close'], 'weight': 3, 'hits': [[0.9, 1.1, 9, 3.2]]},
            {'id': 'corpse_spell', 'clip': 'corpse_spell', 'len': 2, 'phases': [1, 2], 'ranges': ['mid', 'far'], 'weight': 2, 'hits': [[1.0, 1.3, 7, 12, 1]], 'lightning': True, 'cooldown': 3},
            {'id': 'summon', 'clip': 'summon', 'len': 4, 'phases': [1, 2], 'ranges': ['mid', 'far'], 'weight': 1, 'summon': ['skeleton', 3], 'cooldown': 10},
            {'id': 'p2_attack', 'clip': 'phase_two_attack', 'len': 4, 'phases': [2], 'ranges': ['close', 'mid'], 'weight': 3, 'hits': [[1.2, 1.5, 10, 3.5], [2.6, 2.9, 10, 3.5]]},
        ],
    },
    # Iron's Spells' Dead King: a crowned skeleton king with a scythe (Iron's walks it in code: our limbs' walk)
    'dead_king': {
        'geo': S / 'mods/irons_spellbooks-1.20.1-3.15.6/assets/irons_spellbooks/geo/dead_king.geo.json',
        'tex': S / 'mods/irons_spellbooks-1.20.1-3.15.6/assets/irons_spellbooks/textures/entity/dead_king/dead_king.png',
        'glow': S / 'mods/irons_spellbooks-1.20.1-3.15.6/assets/irons_spellbooks/textures/entity/dead_king/dead_king_glowing.png',
        'anim': S / 'mods/irons_spellbooks-1.20.1-3.15.6/assets/irons_spellbooks/animations/casting_animations.json',
        'clipPrefix': 'dead_king_', 'hide': ['stage_2_Head'],
        'kit': [
            {'id': 'melee', 'clip': 'melee', 'len': 1.96, 'phases': [1, 2], 'ranges': ['close'], 'weight': 3, 'hits': [[0.85, 1.05, 10, 3.2]]},
            {'id': 'double_swing', 'clip': 'double_swing', 'len': 2.56, 'phases': [1, 2], 'ranges': ['close'], 'weight': 2, 'hits': [[0.8, 1.0, 8, 3.4], [1.6, 1.8, 8, 3.4]]},
            {'id': 'slam', 'clip': 'slam', 'len': 2.4, 'phases': [1, 2], 'ranges': ['close', 'mid'], 'weight': 2, 'hits': [[1.3, 1.5, 12, 4.5, 1]], 'leap': True, 'cooldown': 2},
        ],
    },
}


def rot_m(deg):   # GeckoLib: z, then y, then x, x and y turned the other way (render/armor3d.cpp rotAbout)
    x, y, z = [math.radians(v) for v in deg]
    def Rx(a): c, s = math.cos(a), math.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    def Ry(a): c, s = math.cos(a), math.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    def Rz(a): c, s = math.cos(a), math.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return Rz(z) @ Ry(-y) @ Rx(-x)


def face_uv(cube, f, s, t, tw, th):
    """the texel for face f (0 -x, 1 +x, 2 top, 3 bottom, 4 front -z, 5 back +z) at s, t in 0..1, Bedrock box uv"""
    (ox, oy, oz), (w, h, d) = cube['origin'], cube['size']
    uv = cube.get('uv', [0, 0])
    if isinstance(uv, dict):   # per-face uv
        names = ['east', 'west', 'up', 'down', 'north', 'south']
        fu = uv.get(names[f]);
        if not fu: return None
        u0, v0 = fu['uv']; us, vs = fu.get('uv_size', [1, 1])
        return u0 + s * us, v0 + t * vs
    u, v = uv
    if f == 4: return u + d + s * w, v + d + t * h
    if f == 5: return u + d + w + d + s * w, v + d + t * h
    if f == 0: return u + s * d, v + d + t * h
    if f == 1: return u + d + w + s * d, v + d + t * h
    if f == 2: return u + d + s * w, v + t * d
    return u + d + w + s * w, v + t * d


def bone_voxels(bone, img, glow, tw, th, step=0.5):
    """the bone's cubes as voxels (model pixels) in model space: [(x, y, z, rgb, lit)]"""
    out = {}
    W, H = img.size
    sx, sy = W / tw, H / th
    for c in bone.get('cubes', []):
        (ox, oy, oz), (w, h, d) = c['origin'], c['size']
        inf = c.get('inflate', 0)
        lo = np.array([ox - inf, oy - inf, oz - inf]); hi = np.array([ox + w + inf, oy + h + inf, oz + d + inf])
        M = rot_m(c['rotation']) if 'rotation' in c else None
        pv = np.array(c.get('pivot', [0, 0, 0]), float)
        n = np.maximum(1, np.round((hi - lo) / step).astype(int))
        for iz in range(n[2]):
            for iy in range(n[1]):
                for ix in range(n[0]):
                    if 0 < ix < n[0] - 1 and 0 < iy < n[1] - 1 and 0 < iz < n[2] - 1: continue   # (the shell)
                    p = lo + (np.array([ix, iy, iz]) + 0.5) * (hi - lo) / n
                    dd = [p[0] - lo[0], hi[0] - p[0], hi[1] - p[1], p[1] - lo[1], p[2] - lo[2], hi[2] - p[2]]
                    ext = [h * d, h * d, w * d, w * d, w * h, w * h]
                    f = min((i for i in range(6) if ext[i] > 0), key=lambda i: dd[i], default=None)
                    if f is None: continue
                    fr = lambda q, a, b: (q - a) / (b - a) if b - a > 1e-4 else 0.5
                    fx, fy, fz = fr(p[0], lo[0], hi[0]), fr(p[1], lo[1], hi[1]), fr(p[2], lo[2], hi[2])
                    st = {0: (1 - fz, 1 - fy), 1: (fz, 1 - fy), 2: (fx, 1 - fz), 3: (fx, fz), 4: (fx, 1 - fy), 5: (1 - fx, 1 - fy)}[f]
                    if c.get('mirror') and f in (0, 1, 2, 3, 4, 5): st = (1 - st[0], st[1])
                    uv = face_uv(c, f, st[0], st[1], tw, th)
                    if uv is None: continue
                    px = (min(W - 1, max(0, int(uv[0] * sx))), min(H - 1, max(0, int(uv[1] * sy))))
                    r, g, b, a = img.getpixel(px)
                    if a < 100: continue
                    lit = False
                    if glow is not None:
                        gr, gg, gb, ga = glow.getpixel((min(glow.width - 1, px[0] * glow.width // W), min(glow.height - 1, px[1] * glow.height // H)))
                        lit = ga > 100 and (gr + gg + gb) > 60
                    q = M @ (p - pv) + pv if M is not None else p
                    key = tuple(np.floor(q / 1.0).astype(int))   # a voxel a model pixel
                    out[key] = ((r << 16) | (g << 8) | b, lit)
    return out


def keyframes(ch):
    """{time: value} or a constant list -> [[t, [x, y, z], lerp]]"""
    if isinstance(ch, list): return [[0.0, [float(v) for v in ch], 'linear']]
    if isinstance(ch, (int, float)): return [[0.0, [float(ch)] * 3, 'linear']]
    if isinstance(ch, dict) and 'vector' in ch:   # (a constant written as {"vector": [...]})
        v = ch['vector']; return [[0.0, [float(x) if isinstance(x, (int, float)) else 0.0 for x in v], 'linear']]
    out = []
    for t, v in ch.items():
        try: float(t)
        except ValueError: continue   # ("vector": a channel's own settings, not a keyframe)
        lerp = 'linear'
        if isinstance(v, dict):
            lerp = v.get('lerp_mode', 'linear'); v = v.get('post') if v.get('post') is not None else v.get('pre')
            if v is None: continue
        if isinstance(v, (int, float)): v = [v, v, v]
        out.append([round(float(t), 4), [float(x) if isinstance(x, (int, float)) else 0.0 for x in v], lerp])
    return sorted(out, key=lambda k: k[0])


def attack_table(java):
    """the boss's moves from its Attack.builder(...) chain: clip, length, phases, ranges, weight, damage windows, cooldown"""
    src = java.read_text()
    out = []
    for m in re.finditer(r'Attack\.builder\("([^"]+)"\)(.*?)\.build\(\)', src, re.S):
        aid, body = m.group(1), m.group(2)
        a = {'id': aid}
        if (x := re.search(r'\.animation\("([^"]+)",\s*([\d.]+)\)', body)): a['clip'] = x.group(1); a['len'] = float(x.group(2))
        if (x := re.search(r'\.phases\(([\d,\s]+)\)', body)): a['phases'] = [int(v) for v in x.group(1).split(',')]
        if (x := re.search(r'\.ranges\(([^)]*)\)', body)): a['ranges'] = [r.split('.')[-1].strip().lower() for r in x.group(1).split(',')]
        if (x := re.search(r'\.weight\(([\d.]+)\)', body)): a['weight'] = float(x.group(1))
        if (x := re.search(r'\.cooldown\(([\d.]+)\)', body)): a['cooldown'] = float(x.group(1))
        wins = [[float(v) for v in w] for w in re.findall(r'damageWindow\(([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)\)', body)]
        wins += [[float(v) for v in w[:4]] + [1] for w in re.findall(r'DamageWindow\.radial\(([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)', body)]
        if wins: a['hits'] = wins   # [start, end, damage, radius(, radial)]
        a['lightning'] = 'Lightning' in body or 'lightning' in body
        a['teleport'] = 'teleportNearTarget' in body
        a['leap'] = 'leapTowardTarget' in body or 'diveForward' in body
        if 'moveset' in body.lower() and 'Moveset.AW' in body and 'Moveset.SOC' not in body: a['moveset'] = 'aw'
        out.append(a)
    return out


def main(d='data'):
    d = pathlib.Path(d); od = d / 'local_models'; od.mkdir(parents=True, exist_ok=True)
    (od / '.gitignore').write_text('*\n!.gitignore\n')
    for key, B in BOSSES.items():
        if not B['geo'].exists(): print('geo_boss_import: no', key); continue
        geo = json.loads(B['geo'].read_text())['minecraft:geometry'][0]
        tw, th = geo['description']['texture_width'], geo['description']['texture_height']
        img = Image.open(B['tex']).convert('RGBA')
        glow = Image.open(B['glow']).convert('RGBA') if B.get('glow') and B['glow'].exists() else None
        bones = geo['bones']; byname = {b['name']: b for b in bones}
        hide = set(B.get('hide', []))
        def hidden(b):
            while b:
                if b['name'] in hide: return True
                b = byname.get(b.get('parent'))
            return False
        parts = []
        absp = {b['name']: np.array(b.get('pivot', [0, 0, 0]), float) for b in bones}
        nvox = 0
        for b in bones:   # (Bedrock lists parents first)
            pv = absp[b['name']]; par = b.get('parent')
            rel = pv - absp[par] if par in absp else pv
            p = {'name': b['name'], 'pivot': [round(-rel[0] / 16, 5), round(rel[1] / 16, 5), round(rel[2] / 16, 5)]}
            if par: p['parent'] = par
            if 'rotation' in b: rot = b['rotation']; p['rest'] = [round(-math.radians(rot[0]), 5), round(math.radians(rot[1]), 5), round(-math.radians(rot[2]), 5)]
            vox = {} if hidden(b) else bone_voxels(b, img, glow, tw, th)
            if vox:
                ks = np.array(list(vox.keys()))
                lo = ks.min(0); hi = ks.max(0) + 1
                # in the bone's space (its pivot the origin), x mirrored: cells [x0, x0+1) -> [-(x0+1), -x0)
                cells = []
                for (x, y, z), (rgb, lit) in vox.items():
                    cells.append([int(-(x + 1) - (-hi[0])), int(y - lo[1]), int(z - lo[2]), 1, 1, 1, rgb] + ([1] if lit else []))
                n = [int(hi[0] - lo[0]), int(hi[1] - lo[1]), int(hi[2] - lo[2])]
                mn = [(-hi[0] + pv[0]) / 16, (lo[1] - pv[1]) / 16, (lo[2] - pv[2]) / 16]
                p['min'] = [round(v, 5) for v in mn]
                p['max'] = [round(mn[0] + n[0] / 16, 5), round(mn[1] + n[1] / 16, 5), round(mn[2] + n[2] / 16, 5)]
                p['vox'] = {'n': n, 'c': cells}; p['res'] = 16
                nvox += len(cells)
            else:
                p['min'] = [0, 0, 0]; p['max'] = [0.001, 0.001, 0.001]; p['vox'] = []
            parts.append(p)
        clips = {}
        if B.get('anim') and B['anim'].exists():
            pre = B.get('clipPrefix', '')
            for name, c in json.loads(B['anim'].read_text())['animations'].items():
                if pre:
                    if not name.startswith(pre): continue
                    name = name[len(pre):]
                cb = {}
                for bn, ch in c.get('bones', {}).items():
                    if bn not in byname: continue
                    e = {}
                    if 'rotation' in ch: e['r'] = keyframes(ch['rotation'])
                    if 'position' in ch: e['p'] = keyframes(ch['position'])
                    if e: cb[bn] = e
                clips[name] = {'len': float(c.get('animation_length', 0) or 0), 'loop': bool(c.get('loop', False)), 'bones': cb}
        for a, src in B.get('alias', {}).items():
            if a not in clips and src in clips: clips[a] = clips[src]
        kit = attack_table(B['attacks']) if B.get('attacks') and B['attacks'].exists() else B.get('kit', [])
        out = {'models': {key: {'rig': 'geo', 'rotOrder': 'zyx', 'scale': 1, 'hipY': 12, 'parts': parts}}, 'clips': {key: clips}, 'kits': {key: kit}}
        (od / (key + '.json')).write_text(json.dumps(out, separators=(',', ':')))
        print('geo_boss_import: %s: %d parts, %d voxels, %d clips, %d attacks' % (key, len(parts), nvox, len(clips), len(kit)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
