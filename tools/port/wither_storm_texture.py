#!/usr/bin/env python3
"""tools/port/wither_storm_texture.py — the Wither Storm's (and the Wither's) own picture, painted here: the Story Mode
look, an obsidian-black body with purple light in its eyes, its mouths and the veins of what it has eaten. Nothing in it
is taken from the mod's picture; only where things go is read, from the models' own box layouts in data/models.json
(each part's uv [u, v, w, h, d], Minecraft's box unwrap: top, bottom, then right, front, left, back), so every face gets
the stuff it is made of: skull, jaw, tooth, bone, tentacle, the mass, the command block. Where two parts read the same
texels the more particular one wins (eyes, then heads, teeth, jaws, the block, bones, tentacles, the mass).

  python3 tools/port/wither_storm_texture.py [data]

Writes data/textures/entity/wither_storm.png and gives the models' voxel masses the body's colour.
"""
import json, pathlib, re, sys
import numpy as np
from PIL import Image

W = 160
FACES = ('top', 'bottom', 'right', 'front', 'left', 'back')
PRI = {'mass': 1, 'tentacle': 2, 'bone': 4, 'block': 5, 'jaw': 6, 'upperJaw': 6, 'lowerJaw': 6, 'teeth': 7, 'head': 8}
MODELS = ('wither_storm', 'wither_storm_p0', 'wither_storm_p1', 'wither_storm_p2', 'wither_storm_p3', 'wither_storm_p5', 'wither')
BODY = (24, 19, 30)
rng = np.random.default_rng(1977)


def kind(name):
    n = re.sub(r'#\d+$', '', name.split('/')[-1])
    if 'Teeth' in n: return 'teeth'
    if 'upperJaw' in n: return 'upperJaw'
    if 'lowerJaw' in n: return 'lowerJaw'
    if 'Jaw' in n: return 'jaw'
    if n in ('center_head', 'right_head', 'left_head', 'head', 'headL', 'headR') or re.fullmatch(r'head\d*', n): return 'head'
    if n == 'block': return 'block'
    if n.startswith('rib') or n in ('shoulders', 'tail', 'spine'): return 'bone'
    if 'segment' in n or 'tentacle' in n or n == 'base': return 'tentacle'
    return 'mass'


def noise(w, h, amp, blur=1):
    n = rng.normal(0, 1, (h + 2, w + 2))
    for _ in range(blur): n = (n + np.roll(n, 1, 0) + np.roll(n, 1, 1) + np.roll(n, -1, 0) + np.roll(n, -1, 1)) / 5
    n = n[1:-1, 1:-1]
    return n / (n.std() + 1e-6) * amp


def shade(base, w, h, amp=7, blur=1):
    a = np.zeros((h, w, 3)); a[:] = base
    return a + noise(w, h, amp, blur)[..., None]


def veins(a, density, colour, glow):
    h, w, _ = a.shape
    for _ in range(max(0, int(w * h * density))):   # a few short purple threads that wander
        x, y = rng.integers(0, w), rng.integers(0, h)
        for _ in range(rng.integers(2, 6)):
            a[y % h, x % w] = colour if rng.random() > 0.25 else glow
            x += rng.integers(-1, 2); y += rng.integers(-1, 2)
    return a


def paint(kind_, face, w, h):
    """the picture of one face, h x w x 3 floats"""
    if kind_ == 'mass':
        a = shade(BODY, w, h, 6, 2)
        a[noise(w, h, 1, 0) > 1.6] = (12, 9, 16)                                   # pits
        sp = rng.random((h, w)); a[sp > 0.985] = (70, 60, 52); a[(sp > 0.975) & (sp <= 0.985)] = (52, 58, 40)   # crumbs of what it ate
        return veins(a, 0.004, (86, 42, 136), (160, 90, 230))
    if kind_ == 'tentacle':
        a = shade((30, 24, 37), w, h, 5, 1)
        for y in range(h): a[y] *= 0.86 + 0.14 * (1 + np.sin(y * 1.3)) / 2      # rings round it
        return veins(a, 0.003, (86, 42, 136), (156, 90, 226))
    if kind_ == 'bone':
        a = shade((64, 60, 69), w, h, 5, 1)
        if h > 1: a[0] += 14; a[-1] -= 14                                          # the light along its top, the shadow under
        a[rng.random((h, w)) > 0.92] -= 18
        return a
    if kind_ == 'teeth':
        return shade((176, 170, 162), w, h, 6, 0)
    if kind_ == 'block':   # the command block: amber casing, a steel plate set in it, a lit pip
        a = shade((190, 108, 50), w, h, 6, 0)
        for y in range(h):
            for x in range(w):
                if (x + y) % 3 == 0: a[y, x] -= 22
        if w >= 3 and h >= 3:
            x0, y0 = w // 4, h // 4; x1, y1 = max(x0 + 1, w - w // 4), max(y0 + 1, h - h // 4)
            a[y0:y1, x0:x1] = (128, 128, 138)
            if x1 - x0 > 2 and y1 - y0 > 2: a[y0 + 1:y1 - 1, x0 + 1:x1 - 1] = (58, 58, 68)
            a[(y0 + y1) // 2, (x0 + x1) // 2] = (120, 220, 140)
        return a
    if kind_ in ('jaw', 'upperJaw', 'lowerJaw'):
        a = shade((38, 33, 45), w, h, 5, 1)
        if (kind_ == 'upperJaw' and face == 'bottom') or (kind_ == 'lowerJaw' and face == 'top'):   # inside the mouth: a purple glow
            g = np.linspace(0.4, 1.0, h)[:, None]
            a = a * (1 - g[..., None] * 0.85) + np.array((140, 64, 210)) * g[..., None] * 0.85
        return veins(a, 0.004, (86, 42, 136), (160, 90, 230))
    if kind_ == 'head':
        a = shade((42, 39, 48), w, h, 5, 1)
        if h > 2: a[-1] -= 12; a[0] += 8                                           # the brow lit, the chin in shadow
        if face == 'front': face_of(a, w, h)
        return a
    return shade(BODY, w, h)


def face_of(a, w, h):
    """a skull's face, the same on every head, big or small: two deep sockets set in from the sides with the purple light
    in them (a gap between them as wide as an eye or wider, so a small head is never one-eyed), and a mouth glowing
    behind its teeth"""
    s = 2 if w >= 8 else 1                          # an eye's width
    m = max(1, (w - 2 * s) // 4)                    # in from each side
    eh = 2 if h >= 6 else 1                         # the socket, then the lit row under it
    ey = max(0, (h * 3) // 8 - (1 if h > 6 else 0))
    for ex in (m, w - m - s):
        a[ey:ey + eh, ex:ex + s] = (12, 8, 18)
        a[ey + eh - 1, ex:ex + s] = (186, 104, 255)
        a[ey + eh - 1, ex + (s - 1) // 2 + (1 if s > 1 and ex > w // 2 else 0)] = (236, 200, 255)   # the hot centre of each eye
    my = min(h - 1, ey + eh + max(1, h // 4))
    x0, x1 = m, w - m
    a[my, x0:x1] = (110, 54, 170)
    for x in range(x0, x1, 2): a[my, x] = (150, 144, 138)    # teeth across it


def rects(uv):
    u, v, w, h, d = uv
    return [(u + d, v, w, d), (u + d + w, v, w, d), (u, v + d, d, h), (u + d, v + d, w, h), (u + d + w, v + d, d, h), (u + 2 * d + w, v + d, w, h)]


def main(data):
    data = pathlib.Path(data)
    models = json.loads((data / 'models.json').read_text())
    img = np.zeros((W, W, 3))
    img[:] = paint('mass', 'front', W, W)              # every texel something: unread ones are the body too
    jobs = []
    for key in MODELS:
        m = models.get(key)
        if not m or m.get('uvTex') != 'textures/entity/wither_storm.png': continue
        for p in m['parts']:
            if p.get('uv'): jobs.append((PRI[kind(p['name'])], kind(p['name']), tuple(p['uv'])))
    for _, k, uv in sorted(set(jobs)):
        for fi, (x, y, w, h) in enumerate(rects(uv)):
            x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(W, y + h)
            if x1 <= x0 or y1 <= y0: continue
            pic = paint(k, FACES[fi], w, h)
            img[y0:y1, x0:x1] = pic[y0 - y:y1 - y, x0 - x:x1 - x]
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
    (data / 'textures/entity').mkdir(parents=True, exist_ok=True)
    out.save(data / 'textures/entity/wither_storm.png')
    # the voxel masses (the converter coloured them from its picture): the body's colour
    rgb = (BODY[0] << 16) | (BODY[1] << 8) | BODY[2]
    for key in MODELS:
        for p in models.get(key, {}).get('parts', []):
            vx = p.get('vox')
            if isinstance(vx, dict): vx['c'] = [c[:6] + [rgb] for c in vx.get('c', [])]
    for key in MODELS:   # the command block in the cage is our real command block, drawn whole (not 4 texels of this picture)
        for p in models.get(key, {}).get('parts', []):
            if p['name'].split('/')[-1] == 'block' and (p.get('uv') or p.get('item')):
                p.pop('uv', None); p.pop('mirror', None); p['vox'] = []; p['item'] = 'command_block'
    wither_like_storm(models)
    (data / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    print('wither_storm.png painted (%d faces)' % len(set(jobs)))


def wither_like_storm(models):
    """the Wither is the Storm's first form without what the Storm grows: no cage of ribs round a command block, no
    growing (the Storm's rig drives that); its own spine, three ribs, shoulders, tail and three heads, in the same picture.
    The heads take the names the Wither's rig turns (sim/wither.cpp animWither: head, headL, headR)."""
    p0 = models.get('wither_storm_p0')
    if not p0 or p0.get('uvTex') != 'textures/entity/wither_storm.png': return
    ren = {'center_head': 'head', 'left_head': 'headL', 'right_head': 'headR'}
    parts = []
    for p in p0['parts']:
        n = p['name']
        if n.startswith('ribcage/') or n.split('/')[-1] == 'block' or p.get('item'): continue   # the Storm's cage and its core
        q = json.loads(json.dumps(p))
        q['name'] = ren.get(n, n)
        if q.get('parent') in ren: q['parent'] = ren[q['parent']]
        parts.append(q)
    models['wither'] = {'rig': 'wither', 'scale': p0.get('scale', 2.0), 'uvTex': p0['uvTex'], 'uvTexW': p0.get('uvTexW', 160), 'parts': parts}


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
