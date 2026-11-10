#!/usr/bin/env python3
"""tools/port/dragon_shape.py — every model on the dragon rig (the eight dragons, their drakes, the hatchlings) a better
shape, and their scales, bellies and wings repainted.

Shape (parts added as children of the rig's parts, so the animation in sim/dragons.cpp moves them with the rest): a deep
chest and haunches either side of a slimmer middle and a belly under it; thick thighs; feet with three claws; a fourth,
finer tail bone before the spade; frills back from the jaw, nostrils on the snout and teeth along its edge. Each piece
is sized from the model's own boxes, so a drake gets the same look at its size.

Pictures (data/textures/entity, d<part>_<kind>.png, 32 x 32; each kind keeps its own colours, read from its scales and
wings): overlapping scales with a lit rim, belly plates, and wing membrane with veins fanning from the bone in place of
the old grid.

  python3 tools/port/dragon_shape.py data
"""
import json, pathlib, sys
import numpy as np
from PIL import Image

KINDS = ['ender', 'verdant', 'sand', 'frost', 'bog', 'ember', 'storm', 'tide']


def shape(m):
    parts = m['parts']
    P = {p['name']: p for p in parts}
    if 'footFR' in P or 'body' not in P: return False
    tex = P['body'].get('tex', {})
    scale = tex.get('all') if isinstance(tex, dict) else tex
    kind = scale.split('_', 1)[1] if scale and '_' in scale else 'ender'
    T = lambda part: {'all': 'd%s_%s' % (part, kind)}

    def add(name, parent, at, box, t, rest=None):
        if name in P or parent not in P: return
        p = {'name': name, 'parent': parent, 'at': [round(v, 3) for v in at], 'box': [round(v, 3) for v in box], 'tex': t,
             'pivot': [round(v / 16, 5) for v in at], 'min': [round(v / 16, 5) for v in box[:3]], 'max': [round(v / 16, 5) for v in box[3:]]}
        if rest: p['rest'] = rest
        parts.append(p); P[name] = p

    # the body: a slimmer middle between a deep chest and the haunches, a belly under it
    b = P['body']['box']; x0, y0, z0, x1, y1, z1 = b
    W, H, L = x1 - x0, y1 - y0, z1 - z0
    P['body']['box'] = [round(v, 3) for v in (x0 + W * 0.07, y0 + H * 0.05, z0 + L * 0.05, x1 - W * 0.07, y1 - H * 0.04, z1 - L * 0.04)]
    P['body']['min'] = [round(v / 16, 5) for v in P['body']['box'][:3]]; P['body']['max'] = [round(v / 16, 5) for v in P['body']['box'][3:]]
    add('chest', 'body', [0, 0, 0], [x0 - W * 0.04, y0 - H * 0.04, z0, x1 + W * 0.04, y1 + H * 0.02, z0 + L * 0.40], {'all': scale, 'bottom': 'dbelly_' + kind})
    add('haunch', 'body', [0, 0, 0], [x0 + W * 0.01, y0 + H * 0.02, z1 - L * 0.34, x1 - W * 0.01, y1 - H * 0.05, z1 - L * 0.01], {'all': scale, 'bottom': 'dbelly_' + kind})
    add('belly', 'body', [0, 0, 0], [x0 + W * 0.16, y0 - H * 0.07, z0 + L * 0.28, x1 - W * 0.16, y0 + H * 0.12, z1 - L * 0.28], T('belly'))
    # legs: a thick thigh at the top, a foot with three claws at the bottom
    for s in ('FR', 'FL', 'BR', 'BL'):
        if 'leg' + s not in P or 'shin' + s not in P: continue
        lx0, ly0, lz0, lx1, ly1, lz1 = P['leg' + s]['box']
        add('thigh' + s, 'leg' + s, [0, 0, 0], [lx0 - 0.7, ly0 * 0.62, lz0 - 0.6, lx1 + 0.7, ly1 + 0.9, lz1 + 0.7], {'all': scale})
        sx0, sy0, sz0, sx1, sy1, sz1 = P['shin' + s]['box']
        fw = (sx1 - sx0) / 2 + 0.5
        add('foot' + s, 'shin' + s, [0, sy0, 0], [-fw, -1.1, sz0 - 2.4, fw, 0.5, sz1 + 0.3], {'all': scale, 'bottom': 'dbelly_' + kind})
        for i, cx in enumerate((-fw * 0.62, 0.0, fw * 0.62)):
            add('claw%s%d' % (s, i), 'foot' + s, [cx, -0.5, sz0 - 2.4], [-0.38, -0.6, -1.7, 0.38, 0.5, 0.0], T('horn'), [0.25, 0, 0])
    # the tail: a finer fourth bone before the spade
    if 'tail3' in P and 'spade' in P and P['spade'].get('parent') == 'tail3':
        tx0, ty0, tz0, tx1, ty1, tz1 = P['tail3']['box']
        tl = (tz1 - tz0) * 0.7
        add('tail4', 'tail3', [0, 0, tz1], [tx0 * 0.62, ty0 * 0.62, 0, tx1 * 0.62, ty1 * 0.62, tl], {'all': scale})
        P['spade']['at'][2] = round(P['spade']['at'][2] + tl, 3); P['spade']['pivot'] = [round(v / 16, 5) for v in P['spade']['at']]
    # the head: frills back from the jaw, nostrils and teeth on the snout
    if 'head' in P:
        hx0, hy0, hz0, hx1, hy1, hz1 = P['head']['box']
        for side, sx in (('L', hx1), ('R', hx0)):
            add('frill' + side, 'head', [sx, hy0 * 0.25, hz0 * 0.12], [-0.25, -(hy1 - hy0) * 0.28, 0, 0.25, (hy1 - hy0) * 0.28, (hz1 - hz0) * 0.5], T('horn'), [0.25, 0.5 if side == 'L' else -0.5, 0])
    if 'snout' in P:
        nx0, ny0, nz0, nx1, ny1, nz1 = P['snout']['box']
        for side, sx in (('L', nx1 * 0.55), ('R', nx0 * 0.55)):
            add('nostril' + side, 'snout', [sx, ny1, nz0 + 1.2], [-0.55, 0, -0.9, 0.55, 0.55, 0.9], {'all': scale})
            for i in range(3):
                add('fang%s%d' % (side, i), 'snout', [nx1 * 0.8 if side == 'L' else nx0 * 0.8, ny0, nz0 + 1.0 + i * (nz1 - nz0) * 0.14], [-0.28, -0.85, -0.28, 0.28, 0, 0.28], T('belly'))
    return True


def palette(img):
    a = np.asarray(img.convert('RGBA')).astype(np.float32)
    px = a[a[..., 3] > 128][:, :3]
    lum = px @ np.array([0.299, 0.587, 0.114])
    o = np.argsort(lum)
    pick = lambda q: px[o[min(len(o) - 1, int(q * (len(o) - 1)))]]
    return pick(0.08), pick(0.45), pick(0.9)


def save(arr, path): Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGBA').save(path)


def paint_scales(dark, mid, light):
    """overlapping round scales, rows offset by half a scale: a dark under-edge, a lit rim at the top"""
    out = np.zeros((32, 32, 4), np.float32); out[..., 3] = 255
    rng = np.random.default_rng(7)
    for y in range(32):
        for x in range(32):
            row = y // 6; ox = (x + (row % 2) * 4) % 8
            dy = y % 6
            # inside one scale: an arch (round bottom), lit near its top, dark along the bottom curve
            r = ((ox - 3.5) / 4.0) ** 2 + ((dy - 1.0) / 5.2) ** 2
            c = mid * (0.92 + 0.10 * rng.random())
            if r > 0.78: c = dark
            elif dy <= 1 and 1 <= ox <= 6: c = mid + (light - mid) * 0.75
            elif r < 0.25: c = mid + (light - mid) * 0.25
            out[y, x, :3] = c
    return out


def paint_belly(dark, mid, light):
    """belly plates: bands four pixels tall, a lit line at the top of each, a dark seam under it"""
    out = np.zeros((32, 32, 4), np.float32); out[..., 3] = 255
    for y in range(32):
        k = y % 4
        c = light * 0.9 + mid * 0.1 if k == 0 else dark if k == 3 else mid + (light - mid) * (0.45 - 0.15 * k)
        out[y, :, :3] = c
    out[:, ::8, :3] *= 0.9   # faint ridges down the plates
    return out


def paint_wing(dark, mid, light, vein):
    """membrane: dark at the edges, a little lighter in the middle, veins fanning from the bone's corner"""
    out = np.zeros((32, 32, 4), np.float32); out[..., 3] = 255
    yy, xx = np.mgrid[0:32, 0:32].astype(np.float32)
    d = np.hypot(xx - 15.5, yy - 15.5) / 22.0
    base = mid[None, None, :] * (1.0 - d[..., None] * 0.35) + dark[None, None, :] * (d[..., None] * 0.35)
    out[..., :3] = base
    for ang in (0.12, 0.42, 0.75, 1.1, 1.42):   # veins from the corner at (0, 0)
        for t in np.linspace(0, 46, 140):
            x, y = int(t * np.cos(ang)), int(t * np.sin(ang))
            if 0 <= x < 32 and 0 <= y < 32:
                out[y, x, :3] = vein
                if y + 1 < 32: out[y + 1, x, :3] = vein * 0.8 + base[y + 1, x] * 0.2
    out[0, :, :3] = vein * 0.9; out[:, 0, :3] = vein * 0.9   # where it meets the bone
    return out


def repaint(d):
    ent = d / 'textures/entity'
    n = 0
    for k in KINDS:
        sp, wp, bp = ent / ('dscale_%s.png' % k), ent / ('dwing_%s.png' % k), ent / ('dbelly_%s.png' % k)
        if not (sp.exists() and wp.exists() and bp.exists()): continue
        sd, sm, sl = palette(Image.open(sp))
        wd, wm, wl = palette(Image.open(wp))
        bd, bm, bl = palette(Image.open(bp))
        if k == 'ender':   # the ender wing's grid was magenta: a dark membrane, magenta only in its veins
            wd, wm, wl = sd * 0.9, sd * 0.6 + wm * 0.18, wl
        save(paint_scales(sd, sm, sl), sp)
        save(paint_belly(bd, bm, bl), bp)
        save(paint_wing(wd, wm, wl, wd * 0.5 + wl * 0.5 if k != 'ender' else wl * 0.75), wp)
        n += 1
    return n


def main(d):
    d = pathlib.Path(d)
    mj = json.loads((d / 'models.json').read_text())
    shaped = sum(1 for m in mj.values() if isinstance(m, dict) and m.get('rig') == 'dragon' and shape(m))
    if shaped:
        (d / 'models.json').write_text(json.dumps(mj, separators=(',', ':'), ensure_ascii=False))
        print('dragon_shape: %d dragon models reshaped, %d kinds repainted' % (shaped, repaint(d)))
    else:
        print('dragon_shape: dragon models already reshaped')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
