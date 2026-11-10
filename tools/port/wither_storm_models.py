#!/usr/bin/env python3
"""The Wither Storm's looks, phase by phase, taken from Cracker's Wither Storm Mod's own models (the user's copy at
~/Documents/Projects/witherstorm; "All rights reserved": local builds only, never released without the author's leave).

The mod builds its models in Java (Minecraft's PartDefinition / CubeListBuilder); this reads the decompiled source
(CFR: java -jar cfr.jar --outputdir <dir> <the jar's .class files>) and runs those builder calls, then writes our rig
models (data/models.json, rig "wither_storm") and copies its texture (data/textures/entity/wither_storm.png, the
emissive eyes painted in):
  wither_storm_p0  the Wither it rises as, the command block caged in its ribs     (WitherStormCommandBlockModel)
  wither_storm_p1  a hump on its back                                              (WitherStormHunchbackModel)
  wither_storm_p2  the middle head a storm head, the hump grown                    (WitherStormGrowingHunchbackModel)
  wither_storm_p3  the mass tilted over it, three tentacles, no command block       (WitherStormPregnantHunchbackModel)
  wither_storm     the destroyer: a mass of voxels, three great heads, five tentacles (WitherStormDestroyerModel)
  wither_storm_p5  the evolved destroyer, seven tentacles                          (WitherStormEvolvedDestroyerModel)
Minecraft model space is y-down with the feet at y 24 and the right hand at -x; ours is y-up from the feet with the
right hand at +x (so x and y flip, and rotations X and Y flip sign), and our rig turns a part Y, then X, then Z where
Minecraft turns Z, Y, X: each rotation is re-solved. Each piece's own scale (the mass x10, the heads, the tentacles) is
baked into its parts; the whole is drawn at x2 like the mod's renderer (model scale 2). The mass (hundreds of 1-unit
cubes all cut from one texel) becomes one voxel part.
  python3 tools/port/wither_storm_models.py <decompiled src dir> <mod assets/witherstormmod> [data]"""
import json, math, pathlib, re, sys
import numpy as np
from PIL import Image

MOD = 'nonamecrackers2/witherstormmod/client/renderer/entity/model/witherstorm'


# ---- a little Java: just what the model builders use ---------------------------------------------------------------
def balanced(s, i):
    """s[i] == '(' -> index of its ')'"""
    d = 0
    for j in range(i, len(s)):
        if s[j] == '(': d += 1
        elif s[j] == ')':
            d -= 1
            if d == 0: return j
    raise ValueError('unbalanced')


def split_top(s, sep=','):
    out, d, cur = [], 0, ''
    for ch in s:
        if ch in '([{': d += 1
        elif ch in ')]}': d -= 1
        if ch == sep and d == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [x.strip() for x in out]


def num(e, env):
    e = re.sub(r'\(float\)|\(double\)|\(int\)', '', e)
    e = re.sub(r'(\d+\.?\d*)[fFdD]\b', r'\1', e)
    e = e.replace('Mth.m_14089_', 'math.cos').replace('Mth.m_14031_', 'math.sin').replace('(float)Math.PI', 'math.pi').replace('Math.PI', 'math.pi')
    e = e.replace('Math.toRadians', 'math.radians').replace('Math.toDegrees', 'math.degrees')
    return float(eval(e, {'math': math}, dict(env)))


def method_body(path, name):
    s = path.read_text()
    m = re.search(r'static [\w<>\[\]]+ ' + re.escape(name) + r'\(([^)]*)\)\s*\{', s)
    if not m: raise KeyError('%s in %s' % (name, path.name))
    i = m.end() - 1; d = 0
    for j in range(i, len(s)):
        if s[j] == '{': d += 1
        elif s[j] == '}':
            d -= 1
            if d == 0: return [p.strip().split()[-1] for p in m.group(1).split(',') if p.strip()], s[i + 1:j]
    raise ValueError


class Part:
    def __init__(self, name, parent=None):
        self.name, self.parent, self.cubes, self.kids = name, parent, [], {}
        self.pose = [0, 0, 0, 0, 0, 0]; self.k = 1.0; self.pre = None
        if parent is not None: parent.kids[name] = self


def builder(e, env):
    """CubeListBuilder.m_171558_().m_171514_(u, v).m_171496_(...)... -> cubes"""
    e = e.strip()
    if re.fullmatch(r'\w+', e): return env[e]
    m0 = re.match(r'(\w+)\.m_', e)
    if e.startswith('CubeListBuilder.m_171558_()'): i = len('CubeListBuilder.m_171558_()'); cubes = []
    elif m0 and isinstance(env.get(m0.group(1)), list): i = len(m0.group(1)); cubes = env[m0.group(1)]   # (a builder carried on)
    else: raise ValueError('builder: ' + e[:60])
    uv = (0, 0); mirror = False
    while i < len(e):
        m = re.match(r'\.(m_\d+_)\(', e[i:])
        if not m: raise ValueError('builder: ' + e[i:i + 60])
        o = i + m.end() - 1; c = balanced(e, o); args = split_top(e[o + 1:c]); f = m.group(1); i = c + 1
        if f == 'm_171514_': uv = (int(num(args[0], env)), int(num(args[1], env)))
        elif f == 'm_171480_': mirror = True
        elif f == 'm_171555_': mirror = args[0] == 'true'
        elif f in ('m_171488_', 'm_171506_', 'm_171496_'):
            x, y, z, w, h, d = (num(a, env) for a in args[:6])
            ts = (num(args[7], env), num(args[8], env)) if f == 'm_171496_' else (1.0, 1.0)
            mir = (args[6] == 'true') if f == 'm_171506_' else mirror
            cubes.append({'uv': uv, 'box': (x, y, z, w, h, d), 'mirror': mir, 'ts': ts})
        else: raise ValueError('builder method ' + f)
    return cubes


def pose(e, env):
    e = e.strip()
    if e.endswith('f_171404_'): return [0, 0, 0, 0, 0, 0]
    m = re.match(r'PartPose\.(m_171419_|m_171423_)\(', e)
    args = [num(a, env) for a in split_top(e[m.end():balanced(e, m.end() - 1)])]
    return args + [0, 0, 0] if len(args) == 3 else args


def call(e):
    """'TARGET.m_xxx_(ARGS)' at the end of e -> (target, method, args)"""
    e = e.strip(); assert e.endswith(')'), e[-40:]
    d = 0
    for j in range(len(e) - 1, -1, -1):
        if e[j] == ')': d += 1
        elif e[j] == '(':
            d -= 1
            if d == 0: break
    m = re.search(r'\.(m_\d+_)$', e[:j])
    return e[:m.start()], m.group(1), split_top(e[j + 1:-1])


def part_expr(e, env, mesh):
    e = e.strip()
    if re.fullmatch(r'\w+', e): return env[e]
    t, f, args = call(e)
    if f == 'm_171576_': return mesh                       # mesh.getRoot()
    p = part_expr(t, env, mesh)
    if f == 'm_171597_': return p.kids[json.loads(args[0])]   # getChild
    if f == 'm_171599_':                                  # addOrReplaceChild(name, builder, pose)
        name = json.loads(args[0]); q = Part(name, p); q.cubes = builder(args[1], env); q.pose = pose(args[2], env); return q
    raise ValueError('part method ' + f)


def run(path, name, args, mesh):
    params, body = method_body(path, name)
    env = dict(zip(params, args))
    run_body(body, env, mesh)


def run_body(body, env, mesh):
    i = 0
    while i < len(body):
        rest = body[i:]
        m = re.match(r'\s*if \((!?)(\w+)\) \{', rest)
        if m:
            o = i + m.end() - 1; d = 0
            for j in range(o, len(body)):
                if body[j] == '{': d += 1
                elif body[j] == '}':
                    d -= 1
                    if d == 0: break
            if bool(env[m.group(2)]) != (m.group(1) == '!'): run_body(body[o + 1:j], env, mesh)
            i = j + 1; continue
        j = i; d = 0
        while j < len(body) and not (body[j] == ';' and d == 0):
            if body[j] in '({': d += 1
            elif body[j] in ')}': d -= 1
            j += 1
        st = body[i:j].strip(); i = j + 1
        if not st or st.startswith('//'): continue
        st = re.sub(r'^return\s+', '', st)
        if re.match(r'CubeDeformation \w+ = ', st): env[st.split()[1]] = 0.0; continue   # (deformations: not used here)
        mb = re.match(r'(\w+)\.m_', st)
        if mb and isinstance(env.get(mb.group(1)), list): builder(st, env); continue   # body.m_171514_(..).m_171496_(..): more cubes
        m = re.match(r'(PartDefinition|CubeListBuilder) (\w+) = (.*)$', st, re.S)
        if m:
            env[m.group(2)] = part_expr(m.group(3), env, mesh) if m.group(1) == 'PartDefinition' else builder(m.group(3), env)
        elif '.m_171599_(' in st or st.endswith('m_171576_()'):
            part_expr(st, env, mesh)
        elif re.fullmatch(r'\w+', st): pass
        else: raise ValueError('statement: ' + st[:100])


# ---- the mod's pieces ---------------------------------------------------------------------------------------------
class Src:
    def __init__(self, d): self.d = pathlib.Path(d) / MOD
    def f(self, rel): return self.d / (rel + '.java')


def abstract_mesh():
    root = Part('root')
    for n in ('mass', 'lowResMass', 'tentacles', 'heads'): Part(n, root)
    return root


def heads_mesh(S, root, poses, k):
    heads = root.kids['heads']
    for i, p in enumerate(poses):
        h = Part('head%d' % i, heads); h.pose = list(p) + [0, 0, 0]; h.k = k
        h.pose[4] = math.pi   # (it looks back over itself until the game turns it: HeadModel yRot = pi + look)
        run(S.f('HeadModel'), 'populateDefinition', [h], root)
        lj = h.kids['lowerJaw']; lj.pose[3] = (0.065 + 0.02) * math.pi - 0.5   # (the jaw at rest, a little open)


def tentacle_cfg(S, impl):
    """configureTentacles: each tentacle's scale and rest angles"""
    s = S.f('impl/' + impl).read_text()
    out = {}
    for i, k in re.findall(r'this\.tentacles\[(\d+)\] = new TentacleModel\(root\.m_171324_\("tentacle\d+"\), ([\d.]+)f\)', s): out[int(i)] = {'scale': float(k)}
    for i, f, v in re.findall(r'this\.tentacles\[(\d+)\]\.(\w+) = ([^;]+);', s): out[int(i)][f] = num(v, {})   # (values like (float)Math.toRadians(90.0) too)
    return out


def tentacles_mesh(root, specs, cfg):
    """TentacleModel.populateDefinition: a base, then six segments thinning out along -z"""
    ts = root.kids['tentacles']
    TEX = [(36, 24, 14, 24), (76, 32, 32, 32), (76, 32, 33, 32), (77, 32, 34, 32), (76, 34, 36, 34), (77, 34, 38, 34)]
    for i, (L, off) in enumerate(specs):
        c = cfg.get(i, {}); k = c.get('scale', 1.0)
        t = Part('tentacle%d' % i, ts); t.k = k
        b = Part('base', t); b.pose = list(off) + [c.get('xRotationalOffset', 0), c.get('yRotationalOffset', 0), 0]
        prev = b
        for s in range(6):
            a = 6 - s; u0, ul, v0, vl = TEX[s]
            g = Part('segment%d' % (s + 1), prev)
            g.cubes = [{'uv': (u0 + (ul - L[s]), v0 + (vl - L[s])), 'box': (-a, -a, -L[s], 2 * a, 2 * a, L[s]), 'mirror': False, 'ts': (1, 1)}]
            g.pose = [0, 0, -L[s - 1] if s else 0, c.get('xAngularOffset', 0) if s else 0, c.get('yAngularOffset', 0) if s else 0, 0]   # (segments 2-6 curl)
            prev = g


def stage(S, which):
    root = abstract_mesh(); m = root.kids
    base = lambda center, rib, tail: run(S.f('impl/WitherStormCommandBlockModel'), 'createBaseMesh', [root, 0.0, center, rib, tail], root)
    if which == 'p0': base(True, True, True); return root, 1.0, []
    if which == 'p1': base(True, True, True); run(S.f('mass/HunchbackBodyModel'), 'createBodyModel', [root, 1.0], root); return root, 1.0, []
    if which == 'p2':
        heads_mesh(S, root, [(0, 0, 0)], 0.7); base(False, True, True)
        run(S.f('mass/GrowingHunchbackMassModel'), 'createMassModel', [root, 1.0], root); return root, 1.0, ['head0']
    if which == 'p3':
        heads_mesh(S, root, [(0, 0, 0)], 0.7); base(False, False, False)
        run(S.f('mass/PregnantHunchbackBodyModel'), 'createBodyModel', [root, 1.0], root)
        mass = m.pop('mass'); tilt = Part('massTilt', root); tilt.pose = [0, 0, 0, 20 * math.pi / 180, 0, 0]   # (scaleMass: tipped 20 degrees)
        tilt.kids['mass'] = mass; mass.parent = tilt
        tentacles_mesh(root, [([18, 24, 24, 24, 24, 32], (0, 0, 20)), ([18, 24, 24, 24, 24, 32], (0, 0, 20)), ([18, 24, 24, 24, 24, 32], (0, 20, 20))],
                       tentacle_cfg(S, 'WitherStormPregnantHunchbackModel'))
        return root, 1.0, ['head0']
    poses = [(-22, -65, -40), (0, -32, -23), (32, -60, -24)]
    heads_mesh(S, root, poses, 3.0)
    if which == 'p4':
        run(S.f('mass/DestroyerBodyModel'), 'createBodyModel', [root, 0.2], root)
        tentacles_mesh(root, [([23, 28, 28, 28, 32, 32], (-10, -30, 0)), ([23, 28, 28, 28, 32, 32], (30, -115, -10)), ([23, 28, 28, 28, 32, 32], (10, -40, 10)),
                              ([23, 28, 28, 28, 32, 32], (-50, -100, 0)), ([23, 28, 28, 28, 32, 32], (-10, -95, 15))], tentacle_cfg(S, 'WitherStormDestroyerModel'))
    else:
        run(S.f('mass/EvolvedDestroyerBodyModel'), 'createBodyModel', [root, 0.2], root)
        tentacles_mesh(root, [([18, 24, 24, 28, 28, 32], (-20, -50, 0)), ([18, 24, 24, 24, 28, 38], (20, -55, 0)), ([18, 24, 24, 28, 32, 28], (-10, -40, -10)),
                              ([18, 18, 24, 24, 24, 28], (8, -45, -10)), ([18, 18, 24, 28, 32, 32], (-8, -50, 12)), ([18, 20, 26, 28, 32, 28], (10, -45, 12)),
                              ([18, 20, 26, 28, 28, 24], (-2, 0, 0))], tentacle_cfg(S, 'WitherStormEvolvedDestroyerModel'))
    m['mass'].k = 10.0
    return root, 3.0, ['head0', 'head1', 'head2']


# ---- into our rig ---------------------------------------------------------------------------------------------------
def Rx(a): c, s = math.cos(a), math.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def Ry(a): c, s = math.cos(a), math.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def Rz(a): c, s = math.cos(a), math.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def ours(rx, ry, rz, pre=None):
    """Minecraft's Rz.Ry.Rx in its space -> ours (x, y flipped) as our rig's Ry.Rx.Rz"""
    R = Rz(rz) @ Ry(ry) @ Rx(rx)
    if pre is not None: R = Rz(pre[2]) @ Ry(pre[1]) @ Rx(pre[0]) @ R
    S = np.diag([-1, -1, 1]); R = S @ R @ S
    x = math.asin(max(-1, min(1, -R[1, 2])))
    y = math.atan2(R[0, 2], R[2, 2]); z = math.atan2(R[1, 0], R[1, 1])
    return [round(x, 5), round(y, 5), round(z, 5)]


def texel(img, u, v):
    r, g, b, a = img.getpixel((min(img.width - 1, int(u)), min(img.height - 1, int(v))))
    return (r << 16) | (g << 8) | b


def emit(root, img):
    """parents before children; names by path ("head0/upperJaw", "tentacle2/base/segment3"), the containers (heads,
    tentacles, witherBase) left out of them; a part's extra cubes ride on it as "<name>#<i>" """
    out = []

    def add(name, parent, at, box, extra):
        p = {'name': name, 'parent': parent, 'at': [round(v, 4) for v in at], 'box': [round(v, 4) for v in box],
             'pivot': [round(v / 16, 5) for v in at], 'min': [round(v / 16, 5) for v in box[:3]], 'max': [round(v / 16, 5) for v in box[3:]]}
        p.update(extra); out.append(p)

    def walk(p, parent_name, k, top, path):
        k = k * p.k
        x, y, z, rx, ry, rz = p.pose
        x, y, z = x * k, y * k, z * k
        at = [-x, 24 - y, z] if top else [-x, -y, z]
        name = path + '/' + p.name if path else p.name
        rest = ours(rx, ry, rz, p.pre)
        extra = {'rest': rest} if any(abs(v) > 1e-6 for v in rest) else {}
        if p.name == 'mass' and k >= 5 and p.cubes:   # the voxel mass: one cube a cell, every cell one texel's colour
            cells = []
            for c in p.cubes:
                bx, by, bz, w, h, d = c['box']; tu, tv = c['ts']
                cells.append((-(bx + w), -(by + h), bz, w, h, d, texel(img, c['uv'][0] + (d + w / 2) * tu, c['uv'][1] + (d + h / 2) * tv)))
            lo = [min(c[i] for c in cells) for i in range(3)]; hi = [max(c[i] + c[i + 3] for c in cells) for i in range(3)]
            vox = {'n': [int(round(hi[i] - lo[i])) for i in range(3)],
                   'c': [[int(round(c[0] - lo[0])), int(round(c[1] - lo[1])), int(round(c[2] - lo[2])), int(round(c[3])), int(round(c[4])), int(round(c[5])), c[6]] for c in cells]}
            add(name, parent_name, at, [lo[0] * k, lo[1] * k, lo[2] * k, hi[0] * k, hi[1] * k, hi[2] * k], dict(extra, vox=vox))
        else:
            for ci, c in enumerate(p.cubes or [None]):
                nm = name if ci == 0 else '%s#%d' % (name, ci)
                if c is None:
                    add(nm, parent_name, at, [0, 0, 0, 0.01, 0.01, 0.01], dict(extra, vox=[]))   # a pivot only
                    continue
                bx, by, bz, w, h, d = c['box']; tu, tv = c['ts']
                box = [-(bx + w) * k, -(by + h) * k, bz * k, -bx * k, -by * k, (bz + d) * k]
                e = {'uv': [c['uv'][0], c['uv'][1], max(1, round(w * tu)), max(1, round(h * tv)), max(1, round(d * tu))], 'res': round(max(2.0, 16 / k), 4)}
                if c['mirror']: e['mirror'] = True
                if ci == 0: add(nm, parent_name, at, box, dict(extra, **e))
                else: add(nm, name, [0, 0, 0], box, e)
        sub = '' if p.name in ('heads', 'tentacles', 'witherBase', 'massTilt') else name
        for q in list(p.kids.values()): walk(q, name, k, False, sub)

    for q in list(root.kids.values()):
        if q.name != 'lowResMass': walk(q, None, 1.0, True, '')
    return out


def main(src, assets, data):
    S = Src(src); data = pathlib.Path(data); assets = pathlib.Path(assets)
    tex = Image.open(assets / 'textures/entity/wither_storm/wither_storm.png').convert('RGBA')   # (read for its size only:
    # the picture the game uses is our own, painted to these layouts by tools/port/wither_storm_texture.py below)
    models = json.loads((data / 'models.json').read_text())
    keys = {'p0': 'wither_storm_p0', 'p1': 'wither_storm_p1', 'p2': 'wither_storm_p2', 'p3': 'wither_storm_p3', 'p4': 'wither_storm', 'p5': 'wither_storm_p5'}
    for which, key in keys.items():
        root, hk, heads = stage(S, which)
        parts = emit(root, tex)
        assert len({p['name'] for p in parts}) == len(parts), [n for n in [p['name'] for p in parts] if [q['name'] for q in parts].count(n) > 1][:5]
        models[key] = {'rig': 'wither_storm', 'scale': 2.0, 'uvTex': 'textures/entity/wither_storm.png', 'uvTexW': 160, 'parts': parts}
        print('%-16s %4d parts' % (key, len(parts)))
    (data / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    import importlib.util as _u   # our own picture for them (and the Wither in their likeness)
    _s = _u.spec_from_file_location('wither_storm_texture', pathlib.Path(__file__).with_name('wither_storm_texture.py'))
    _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(data)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'data')
