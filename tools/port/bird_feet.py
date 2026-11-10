#!/usr/bin/env python3
"""tools/port/bird_feet.py — feet for the birds (idempotent; data_patches.py runs it): every model of the "bird" rig with
legs gets a foot at the bottom of each leg, a child of the leg so it steps with it. A foot is a voxel part (its own colour:
the leg picture's, the claws darker), built the way a bird's foot is: three toes forward, the outer two splayed out on a
slant, one hind toe, a knuckle where they meet, thin and tapering to the claws; a duck's, goose's or swan's webbed across
between the front toes. (They stood on bare sticks, then on flat plates of the leg picture: the "flat rectangles".)

  python3 tools/port/bird_feet.py [data]"""
import json, pathlib, sys

VOX = 32   # voxels per block (render/creature_models.cpp partGrid)


def leg_colour(d, tex):
    """the leg picture's mean colour (opaque texels): the foot's"""
    try:
        from PIL import Image
        if not isinstance(tex, str): return 0xE8A028
        if tex.startswith('@'):   # a look's own picture (tools/port/mob_variants.py: data/textures/entity/<name>.png)
            f = d / 'textures' / 'entity' / (tex[1:] + '.png')
            if not f.exists(): return 0xE8A028
        else:
            tiles = json.loads((d / 'tiles.json').read_text())
            i = tiles['names'].get(tex)
            if i is None: return 0xE8A028
            f = d / tiles['files'][i]
        im = Image.open(f).convert('RGBA')
        px = [p for p in (im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata()) if p[3] >= 128]
        if not px: return 0xE8A028
        r, g, b = (sum(p[k] for p in px) // len(px) for k in range(3))
        return (r << 16) | (g << 8) | b
    except Exception:
        return 0xE8A028


def shade(c, f):
    return (min(255, int(((c >> 16) & 255) * f)) << 16) | (min(255, int(((c >> 8) & 255) * f)) << 8) | min(255, int((c & 255) * f))


def foot(leg, webbed, col):
    """the foot under a leg: a voxel part, toes forward along -z (the models face -z)"""
    x0, y0, z0, x1, y1, z1 = leg['box']
    lw = max(1, round((x1 - x0) * VOX / 16))          # the leg's width in voxels
    tw = lw if lw <= 2 else max(2, round(lw * 0.55))   # a toe's width (a small bird's as wide as its leg, or they vanish)
    th = max(1, round(lw * 0.45))                       # its height at the knuckle (the tip one voxel)
    L = max(4, round(lw * 3.2))                         # the middle toe's length
    S = max(3, round(L * 0.8))                          # the outer toes', along their slant
    H = max(2, round(L * 0.4))                          # the hind toe's
    spread = max(2, round(L * 0.7))                     # how far out the outer toes' tips reach
    W = 2 * spread + lw + 2 * tw                        # the grid: across
    D = L + lw + H                                      # along (forward toes, the knuckle, the hind toe)
    cx = W // 2 - lw // 2                               # the knuckle's first column
    kz = L                                              # the knuckle's first row (the toes run to z = 0 in front of it)
    toe, claw, web = col, shade(col, 0.62), shade(col, 0.85)
    cells = {}

    def put(x, y, z, c):
        if 0 <= x < W and 0 <= z < D and y >= 0: cells[(x, y, z)] = c

    for x in range(cx, cx + lw):                        # the knuckle, the leg's width square
        for z in range(kz, kz + lw):
            for y in range(th): put(x, y, z, toe)
    mid = cx + lw // 2 - tw // 2

    def run(sx, dx, n, back=False):
        """a toe of n voxels from the knuckle, sliding dx across per step, tapering, its last voxel the claw"""
        for i in range(n):
            z = (kz + lw + i) if back else (kz - 1 - i)
            x = sx + round(dx * (i + 1))
            h = max(1, round(th * (1 - i / max(1, n))))
            for k in range(tw):
                for y in range(h): put(x + k, y, z, claw if i == n - 1 else toe)

    run(mid, 0, L)                                      # the middle toe, straight ahead
    run(cx - tw + 1, -spread / S, S)                    # the inner and outer ones, splayed
    run(cx + lw - 1, spread / S, S)
    run(mid, 0, H, back=True)                           # the hind toe
    if webbed:                                          # the web across the front toes, a voxel thick
        for i in range(S):
            z = kz - 1 - i
            xl = cx - tw + 1 + round(-spread / S * (i + 1)) + tw
            xr = cx + lw - 1 + round(spread / S * (i + 1))
            for x in range(xl, xr): put(x, 0, z, web) if (x, 0, z) not in cells else None
    boxes = [[x, y, z, 1, 1, 1, c] for (x, y, z), c in sorted(cells.items())]
    lo = [-(cx + lw / 2) / VOX, 0, -(kz + lw / 2) / VOX]     # the knuckle's middle under the leg
    hi = [lo[0] + W / VOX, max(1, th) / VOX, lo[2] + D / VOX]
    at = [0, y0, 0]
    return {'name': leg['name'].replace('leg', 'foot'), 'parent': leg['name'], 'at': at, 'box': [v * 16 for v in lo] + [v * 16 for v in hi],
            'vox': {'n': [W, max(1, th), D], 'c': boxes}, 'pivot': [a / 16 for a in at], 'min': lo, 'max': hi}


def main(d):
    d = pathlib.Path(d)
    models = json.loads((d / 'models.json').read_text())
    n = 0
    for key, m in models.items():
        if not isinstance(m, dict) or m.get('rig') != 'bird': continue
        m['parts'] = [p for p in m.get('parts', []) if p['name'] not in ('footR', 'footL')]   # (made afresh each run)
        for side in ('legR', 'legL'):
            leg = next((p for p in m['parts'] if p['name'] == side), None)
            if not leg: continue
            col = leg_colour(d, leg.get('tex', 'chicken_legs'))
            m['parts'].append(foot(leg, any(w in key for w in ('duck', 'mallard', 'goose', 'swan')), col)); n += 1
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    print('bird_feet: %d feet' % n)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
