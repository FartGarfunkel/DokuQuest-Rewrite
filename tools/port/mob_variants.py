#!/usr/bin/env python3
# tools/port/mob_variants.py — every creature comes in looks of its own, each with a story (the user's ask, 2026-11-01:
# "physically different, like the golems and fuselings... a skeleton shot full of arrows, a miner with a pickaxe and a
# lamp helmet... stuff that fits the world and has meaning"). Each look is a model of its own: the creature's model with
# things of the world built onto it (helmets, arrows, moss, coral, ice, bells, packs, horns...) and/or its coat
# repainted (our own painting: recoloured from the creature's own pictures, with patches, spots and belts). Where it
# turns up (lands, underground, by a village, in the water), how often, what it holds and what its story leaves when it
# dies go into mobs.json "variantDefs" (sim/creature.cpp). With a resource pack, a look wears its base's pack skin
# (tinted to match, or the pack's own picture of it: render/creature_models.cpp). Hooked into data_patches.py.
#   python3 tools/port/mob_variants.py data
import colorsys, copy, json, math, pathlib, random, sys
from PIL import Image

PI = math.pi
D = None            # data dir
TJ = None           # tiles.json
MODELS = None
MOBS = None


# ------------------------------------------------------------------ tiles: our own paintings
def tile_path(name):
    if name.startswith('@'):
        q = D / 'textures' / 'entity' / (name[1:] + '.png')
        return q if q.exists() else None
    i = TJ['names'].get(name)
    if i is None: return None
    p = D / TJ['files'][i]
    return p if p.exists() else None


def tile_img(name):
    p = tile_path(name)
    return Image.open(p).convert('RGBA') if p else None


def add_tile(name, im):
    """A look's own picture: saved as data/textures/entity/<name>.png and used by its file ('@name', read on the CPU by
    render/creature_models.cpp), never put in tiles.json: the GPU's tile arrays hold 4096 at most."""
    im.save(D / 'textures' / 'entity' / (name + '.png'))
    return '@' + name


def has_tile(name): return tile_path(name) is not None


def tint_px(r, g, b, t):
    """The same colour change as render/creature_models.cpp tintImg (so our paintings and a pack's skin agree)."""
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    s0 = s
    h = (h + t.get('hue', 0) / 360.0) % 1.0
    s = min(1.0, max(0.0, s * t.get('sat', 1)))
    v = min(1.0, max(0.0, v * t.get('val', 1) * (1 + (t.get('colVal', 1) - 1) * min(1.0, s0 * 1.6))))
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    if 'mix' in t:
        mr, mg, mb, ma = t['mix']; r += (mr - r) * ma; g += (mg - g) * ma; b += (mb - b) * ma
    return int(r * 255 + .5), int(g * 255 + .5), int(b * 255 + .5)


def lum(c): return (c[0] * .299 + c[1] * .587 + c[2] * .114)


def noise2(seed, w, h, cell):
    rnd = random.Random(seed)
    gw, gh = w // cell + 2, h // cell + 2
    g = [[rnd.random() for _ in range(gw)] for _ in range(gh)]
    out = [[0.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            fx, fy = x / cell, y / cell; ix, iy = int(fx), int(fy); tx, ty = fx - ix, fy - iy
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            a = g[iy][ix] + (g[iy][ix + 1] - g[iy][ix]) * tx; b = g[iy + 1][ix] + (g[iy + 1][ix + 1] - g[iy + 1][ix]) * tx
            out[y][x] = a + (b - a) * ty
    return out


def paint(im, col, mask):
    """Lay a colour where mask (0..1) says, keeping the picture's light and dark."""
    px = im.load(); w, h = im.size
    tot = [lum(px[x, y]) for y in range(h) for x in range(w) if px[x, y][3] > 0]
    avg = (sum(tot) / len(tot)) if tot else 128
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            k = mask(x, y)
            if a == 0 or k <= 0: continue
            sh = max(0.55, min(1.35, lum((r, g, b)) / max(1, avg)))
            c = [min(255, int(col[i] * sh)) for i in range(3)]
            px[x, y] = (int(r + (c[0] - r) * k), int(g + (c[1] - g) * k), int(b + (c[2] - b) * k), a)
    return im


def pat_patches(col, cover=0.45, cell=7):
    def f(im, seed):
        n = noise2(seed, im.size[0], im.size[1], max(2, cell * im.size[0] // 32))
        return paint(im, col, lambda x, y: 1.0 if n[y][x] < cover else 0.0)
    return f


def pat_spots(col, n=6, r=2.6):
    def f(im, seed):
        rnd = random.Random(seed); w, h = im.size; k = w / 32
        S = [(rnd.random() * w, rnd.random() * h, r * k * (0.6 + rnd.random() * 0.7)) for _ in range(n)]
        return paint(im, col, lambda x, y: 1.0 if any((x - a) ** 2 + (y - b) ** 2 < rr * rr for a, b, rr in S) else 0.0)
    return f


def pat_speckle(col, dens=0.12):
    def f(im, seed):
        rnd = random.Random(seed); w, h = im.size
        M = {(x, y) for y in range(h) for x in range(w) if rnd.random() < dens}
        return paint(im, col, lambda x, y: 1.0 if (x, y) in M else 0.0)
    return f


def pat_stripes(col, period=6, width=2, vertical=False):
    def f(im, seed):
        k = im.size[0] / 32
        return paint(im, col, lambda x, y: 1.0 if int((x if vertical else y) / k) % period < width else 0.0)
    return f


def pat_low(col, frac=0.35):
    """The bottom of the picture (socks, a pale belly)."""
    def f(im, seed):
        h = im.size[1]
        return paint(im, col, lambda x, y: 1.0 if y >= h * (1 - frac) else 0.0)
    return f


# ------------------------------------------------------------------ building
def part(name, parent, at, box, tex, glow=False, rest=None):
    p = {'name': name, 'parent': parent, 'at': list(at), 'box': list(box), 'tex': tex,
         'pivot': [at[0] / 16, at[1] / 16, at[2] / 16], 'min': [box[0] / 16, box[1] / 16, box[2] / 16], 'max': [box[3] / 16, box[4] / 16, box[5] / 16]}
    if glow: p['glow'] = True
    if rest: p['rest'] = list(rest)
    return p


def P(m, name):
    for p in m['parts']:
        if p['name'] == name: return p
    return None


def has(m, name): return P(m, name) is not None


def resize(m, name, sx=1.0, sy=1.0, sz=1.0):
    p = P(m, name)
    if not p: return
    b = p['box']; p['box'] = [b[0] * sx, b[1] * sy, b[2] * sz, b[3] * sx, b[4] * sy, b[5] * sz]
    p['min'] = [x / 16 for x in p['box'][:3]]; p['max'] = [x / 16 for x in p['box'][3:]]


FACE_REST = {'front': (0, 0), 'back': (0, PI), 'right': (0, -PI / 2), 'left': (0, PI / 2), 'top': (PI / 2, 0), 'bottom': (-PI / 2, 0)}


def surface(m, pname, face, u, v):
    """A point on a part's face (u, v in 0..1), in the part's own space."""
    x0, y0, z0, x1, y1, z1 = P(m, pname)['box']
    X = x0 + u * (x1 - x0); Y = y0 + v * (y1 - y0); Z = z0 + u * (z1 - z0)
    if face == 'front': return (X, Y, z0)
    if face == 'back': return (X, Y, z1)
    if face == 'right': return (x1, Y, Z)
    if face == 'left': return (x0, Y, Z)
    if face == 'top': return (X, y1, z0 + v * (z1 - z0))
    return (X, y0, z0 + v * (z1 - z0))


class Kit:
    """Things built onto a creature: each points out of the face it grows from (boxes along -z, turned by FACE_REST)."""
    def __init__(self, m, seed):
        self.m = m; self.rnd = random.Random(seed); self.n = 0; self.out = []

    def nm(self, s): self.n += 1; return 'v_%s%d' % (s, self.n)

    def stick(self, pname, face, u, v, box, tex, tilt=0.35, glow=False, sink=0.4):
        if not has(self.m, pname): return None
        x, y, z = surface(self.m, pname, face, u, v)
        rx, ry = FACE_REST[face]
        r = self.rnd
        rest = [rx + (r.random() - .5) * 2 * tilt, ry + (r.random() - .5) * 2 * tilt, (r.random() - .5) * tilt]
        # sink a little into the face so it reads as stuck in, not floating
        bx = list(box); bx[5] += sink
        p = part(self.nm('g'), pname, (x, y, z), bx, tex, glow, rest); self.out.append(p); return p

    def arrows(self, spots, shaft='birch_planks', fletch='wool', length=7.5):
        for pname, face, u, v in spots:
            p = self.stick(pname, face, u, v, (-0.5, -0.5, -length, 0.5, 0.5, 0), shaft, tilt=0.5, sink=1.5)
            if not p: continue
            self.out.append(part(self.nm('f'), p['name'], (0, 0, -length), (-1.3, -0.2, -2.6, 1.3, 0.2, 0.4), fletch))
            self.out.append(part(self.nm('f'), p['name'], (0, 0, -length), (-0.2, -1.3, -2.6, 0.2, 1.3, 0.4), fletch))

    def lumps(self, spots, tex, size=(1.6, 1.2), thick=0.9, glow=False):
        """Flat clumps on a face: moss, snow, barnacles, mud, ash."""
        for pname, face, u, v in spots:
            a = size[0] * (0.7 + self.rnd.random() * 0.6); b = size[1] * (0.7 + self.rnd.random() * 0.6)
            self.stick(pname, face, u, v, (-a, -b, -thick, a, b, 0), tex, tilt=0.12, glow=glow, sink=0.3)

    def spikes(self, spots, tex, w=0.6, h=(2.5, 4.5), tilt=0.4, glow=False):
        """Upright points: ice, crystals, coral, dripstone."""
        for pname, face, u, v in spots:
            hh = h[0] + self.rnd.random() * (h[1] - h[0]); ww = w * (0.7 + self.rnd.random() * 0.5)
            self.stick(pname, face, u, v, (-ww, -ww, -hh, ww, ww, 0), tex, tilt=tilt, glow=glow)

    def fans(self, spots, tex, w=1.8, h=3.0):
        """Thin planes: coral fans, leaves, fins, feathers."""
        for pname, face, u, v in spots:
            self.stick(pname, face, u, v, (-w, -0.12, -h, w, 0.12, 0), tex, tilt=0.5)

    def shrooms(self, spots, cap='red_mushroom_block', stem='mushroom_stem', s=1.0):
        for pname, face, u, v in spots:
            st = self.stick(pname, face, u, v, (-0.45 * s, -0.45 * s, -1.6 * s, 0.45 * s, 0.45 * s, 0), stem, tilt=0.25)
            if st: self.out.append(part(self.nm('c'), st['name'], (0, 0, -1.6 * s), (-1.5 * s, -1.5 * s, -1.0 * s, 1.5 * s, 1.5 * s, 0), cap))

    def add(self, name, parent, at, box, tex, glow=False, rest=None):
        if parent is not None and not has(self.m, parent) and not any(p['name'] == parent for p in self.out): return None
        p = part(self.nm(name), parent, at, box, tex, glow, rest); self.out.append(p); return p

    # ---- worn things
    def hat(self, tex, crown=2.4, brim=1.6, pname='head', pad=0.45, brim_h=0.5, band=None):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2; w, d = (x1 - x0) / 2 + pad, (z1 - z0) / 2 + pad
        self.add('hat', pname, (cx, y1 - 1.2, cz), (-w, 0, -d, w, crown + 1.2, d), tex)
        if brim: self.add('brim', pname, (cx, y1 - 1.0, cz), (-w - brim, 0, -d - brim, w + brim, brim_h, d + brim), tex)
        if band: self.add('band', pname, (cx, y1 - 0.6, cz), (-w - 0.1, 0, -d - 0.1, w + 0.1, 0.7, d + 0.1), band)

    def helmet(self, tex, pname='head', pad=0.45, depth=3.2, visor=None):
        """A cap over the top and sides of the head (a miner's hard hat, a rusted helm)."""
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2; w, d = (x1 - x0) / 2 + pad, (z1 - z0) / 2 + pad
        self.add('helm', pname, (cx, y1 - depth, cz), (-w, 0, -d, w, depth + 0.9, d), tex)
        if visor: self.add('visor', pname, (cx, y1 - depth + 0.3, z0 - pad), (-w + 0.4, 0, -1.4, w - 0.4, 0.5, 0), visor)

    def lamp(self, pname='head', tex='glowstone'):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        self.add('lamp', pname, ((x0 + x1) / 2, y1 - 1.4, z0 - 0.45), (-1.1, -0.9, -1.1, 1.1, 0.9, 0), tex, glow=True)

    def band(self, tex, pname, y_from, y_to, pad=0.35):
        """A ring round a part (a collar, a belt, a scarf, shackles): y as fractions of the part's height."""
        if not has(self.m, pname): return None
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        ya, yb = y0 + (y1 - y0) * y_from, y0 + (y1 - y0) * y_to
        return self.add('band', pname, (0, 0, 0), (x0 - pad, ya, z0 - pad, x1 + pad, yb, z1 + pad), tex)

    def ring_z(self, tex, pname, z_from, z_to, pad=0.35):
        """A ring round a lying part (a quad's body: a belt round the middle; a head: a collar at its back)."""
        if not has(self.m, pname): return None
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        za, zb = z0 + (z1 - z0) * z_from, z0 + (z1 - z0) * z_to
        return self.add('ring', pname, (0, 0, 0), (x0 - pad, y0 - pad, za, x1 + pad, y1 + pad, zb), tex)

    def coat(self, tex, pname='body', pad=0.7, hang=2.5):
        """A thick hanging coat over a part (shaggy hair, a fleece, a cloak)."""
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        self.add('coat', pname, (0, 0, 0), (x0 - pad, y0 - hang, z0 - pad, x1 + pad, y1 + pad, z1 + pad), tex)

    def back_pack(self, tex, roll=None, pname='body', w=3.0, h=0.65, d=2.6):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        top = y0 + (y1 - y0) * 0.92; bot = y0 + (y1 - y0) * (1 - h)
        self.add('pack', pname, (0, 0, z1), (-w, bot, -0.2, w, top, d), tex)
        if roll: self.add('roll', pname, (0, top, z1 + d / 2), (-w - 0.6, 0, -1.1, w + 0.6, 2.0, 1.1), roll)

    def saddlebags(self, tex, pname='body'):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        zm = (z0 + z1) / 2; L = (z1 - z0) * 0.22
        for x, s in ((x1, 1), (x0, -1)):
            self.add('bag', pname, (x, y0 + (y1 - y0) * 0.55, zm), (min(0, s * 2.2), -3.2, -L, max(0, s * 2.2), 0.6, L), tex)
        self.add('blanket', pname, (0, y1, zm), (x0 - 0.3, -0.2, -L * 1.6, x1 + 0.3, 0.5, L * 1.6), 'red_wool')

    def tool_on_back(self, head='iron_block', handle='spruce_planks', pname='body', kind='pick'):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        h = self.add('haft', pname, (0, (y0 + y1) / 2, z1 + 0.7), (-0.45, -6.5, -0.45, 0.45, 6.5, 0.45), handle, rest=(0, 0, 0.75))
        if not h: return
        if kind == 'pick':
            self.add('pick', h['name'], (0, 6.0, 0), (-4.2, -0.75, -0.5, 4.2, 0.75, 0.5), head)
            for s in (-1, 1): self.add('tip', h['name'], (s * 4.0, 5.6, 0), (-0.6, -1.4, -0.45, 0.6, 0.2, 0.45), head)
        elif kind == 'axe':
            self.add('blade', h['name'], (1.6, 5.0, 0), (-1.6, -2.2, -0.35, 1.6, 1.6, 0.35), head)
        elif kind == 'shovel':
            self.add('spade', h['name'], (0, 7.6, 0), (-1.6, -0.2, -0.3, 1.6, 3.2, 0.3), head)

    def quiver(self, pname='body', tex='brown_wool'):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        q = self.add('quiver', pname, (1.0, (y0 + y1) / 2 + 1, z1 + 1.1), (-1.4, -5, -1.1, 1.4, 4.5, 1.1), tex, rest=(0, 0, -0.45))
        if q:
            for dx in (-0.7, 0.0, 0.7):
                s = self.add('qarrow', q['name'], (dx, 4.5, 0), (-0.3, 0, -0.3, 0.3, 2.0, 0.3), 'birch_planks')
                if s: self.add('qflet', s['name'], (0, 2.0, 0), (-0.8, 0, -0.12, 0.8, 1.6, 0.12), 'wool')

    def horns_curl(self, tex='bone_block_side', pname='head', size=1.0):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        for x, s in ((x1, 1), (x0, -1)):
            a = self.add('horn', pname, (x - s * 0.4, y1 - 1.0, z1 - 1.5), (min(0, s * 2.2 * size), -0.8, -0.9, max(0, s * 2.2 * size), 1.4, 2.5 * size), tex)
            if a:
                b = self.add('horn', a['name'], (s * 2.0 * size, 0, 2.2 * size), (min(0, s * 1.6), -3.2 * size, -0.8, max(0, s * 1.6), 0.6, 0.9), tex)
                if b: self.add('horn', b['name'], (s * 0.4, -3.0 * size, 0), (min(0, s * 1.4), -0.4, -2.6 * size, max(0, s * 1.4), 1.2, 0.4), tex)

    def horns_long(self, tex='bone_block_side', pname='head', reach=4.5, up=1.5):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        for x, s in ((x1, 1), (x0, -1)):
            a = self.add('lhorn', pname, (x, y1 - 1.2, (z0 + z1) / 2 + 1), (min(0, s * reach), -0.6, -0.6, max(0, s * reach), 0.6, 0.6), tex)
            if a: self.add('lhornt', a['name'], (s * reach, 0, 0), (-0.5, 0, -0.5, 0.5, up + 1.2, 0.5), tex, rest=(0, 0, -s * 0.35))

    def antlers(self, tex='spruce_log_side', pname='head', h=4.0):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        for x, s in ((x1 - 1, 1), (x0 + 1, -1)):
            a = self.add('ant', pname, (x, y1, (z0 + z1) / 2 + 1), (-0.4, 0, -0.4, 0.4, h, 0.4), tex, rest=(0, 0, -s * 0.4))
            if a:
                self.add('antt', a['name'], (0, h * 0.55, 0), (-0.35, 0, -0.35, 0.35, h * 0.5, 0.35), tex, rest=(-0.6, 0, s * 0.5))
                self.add('antt', a['name'], (0, h, 0), (-0.3, 0, -0.3, 0.3, h * 0.35, 0.3), tex, rest=(0.5, 0, s * 0.3))

    def crest(self, tex, pname='head', h=2.6, n=3):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        for i in range(n):
            z = z0 + (z1 - z0) * (0.25 + 0.5 * i / max(1, n - 1))
            self.add('crest', pname, (0, y1, z), (-0.25, 0, -0.7, 0.25, h * (1 - abs(i - (n - 1) / 2) * 0.25), 0.7), tex, rest=(-0.3, 0, 0))

    def mane(self, tex, pname='body', pad=1.2, frac=0.4):
        """A thick ruff round the front of a lying body (a lion's dark mane, a pack leader's)."""
        return self.ring_z(tex, pname, 0.0, frac, pad=pad)

    def eyepatch(self, pname='head', side=1):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        ex = (x1 if side > 0 else x0) * 0.45; ey = y0 + (y1 - y0) * 0.62
        self.add('patch', pname, (ex, ey, z0), (-1.3, -1.1, -0.35, 1.3, 1.1, 0.05), 'black_wool')
        self.add('strap', pname, (0, ey + 0.6, 0), (x0 - 0.2, -0.3, z0 - 0.25, x1 + 0.2, 0.3, z1 + 0.2), 'black_wool', rest=(0, 0, side * 0.25))

    def beard(self, tex, pname='head', h=3.0):
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        self.add('beard', pname, (0, y0 + 1.2, z0), (-(x1 - x0) * 0.35, -h, -0.6, (x1 - x0) * 0.35, 0, 0.3), tex)

    def drape(self, tex, pname, face='front', w=None, h=6.0, u=0.5):
        """Something hanging down a face (vines, kelp, rags, a cape)."""
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        ww = (w if w else (x1 - x0) * 0.4)
        if face == 'front': self.add('drape', pname, (x0 + (x1 - x0) * u, y1, z0 - 0.15), (-ww, -h, -0.2, ww, 0, 0.05), tex)
        elif face == 'back': self.add('drape', pname, (x0 + (x1 - x0) * u, y1, z1 + 0.15), (-ww, -h, -0.05, ww, 0, 0.2), tex)
        elif face == 'right': self.add('drape', pname, (x1 + 0.15, y1, z0 + (z1 - z0) * u), (-0.05, -h, -ww, 0.2, 0, ww), tex)
        else: self.add('drape', pname, (x0 - 0.15, y1, z0 + (z1 - z0) * u), (-0.2, -h, -ww, 0.05, 0, ww), tex)

    def inside(self, tex, pname, frac=0.45, glow=False):
        """Something seen inside a see-through body (a slime's swallowed skull, a nugget)."""
        if not has(self.m, pname): return
        x0, y0, z0, x1, y1, z1 = P(self.m, pname)['box']
        s = min(x1 - x0, y1 - y0, z1 - z0) * frac / 2
        self.add('inner', pname, ((x0 + x1) / 2, (y0 + y1) / 2 - s * 0.3, (z0 + z1) / 2), (-s, -s, -s, s, s, s), tex, glow=glow)


def spots_on(m, plan, rnd):
    """plan: [(part, face, n)] -> [(part, face, u, v)] scattered (kept off the very edges)."""
    out = []
    for pname, face, n in plan:
        if not has(m, pname): continue
        for _ in range(n): out.append((pname, face, 0.18 + rnd.random() * 0.64, 0.2 + rnd.random() * 0.6))
    return out


# ------------------------------------------------------------------ one look
def entity_tiles(m):
    """The creature's own pictures (its coat, face, ...), not blocks: the ones a recolour repaints."""
    out = set()
    def walk(t):
        if isinstance(t, str):
            p = tile_path(t)
            if p and not t.startswith('@') and '/entity/' in str(p).replace('\\', '/'): out.add(t)
        elif isinstance(t, dict):
            for v in t.values(): walk(v)
        elif isinstance(t, list):
            for v in t: walk(v)
    for p in m['parts']: walk(p['tex'])
    return out


def base_model_key(mob):
    v = MOBS[mob]
    return v.get('model') or (v.get('variants') or [mob])[0]


DEFS = {}   # mob -> [variantDef]


def look(mob, suffix, label, w=1.0, *, tint=None, only=None, skip=(), pattern=None, pattern_on=None, build=None, edit=None,
         lands=None, cave=False, surface=False, village=False, water=False, dim=None, held=None, drops=None, packTex=None, scale=None,
         ptint=None, pack=True):
    if mob not in MOBS: return
    bk = base_model_key(mob)
    if bk not in MODELS: return
    key = '%s_%s' % (mob, suffix)
    m = copy.deepcopy(MODELS[bk])
    m.pop('packTint', None); m.pop('packTex', None)
    if pack: m['packBase'] = bk          # (a golem of another block wears no iron golem's skin)
    else: m.pop('packBase', None)
    pt = ptint if isinstance(ptint, dict) else tint if (ptint is True or (ptint is None and only is None)) else None
    if pt and pack: m['packTint'] = pt   # (a recoloured coat only: the pack's whole skin takes the colour; clothes only: not)
    if packTex: m['packTex'] = packTex
    if scale: m['scale'] = m.get('scale', 1) * scale
    seed = sum(ord(c) * (i + 7) for i, c in enumerate(key))
    # repaint its own pictures
    if tint or pattern:
        own = entity_tiles(m)
        mp = {}
        for t in sorted(own):
            if only is not None and t not in only: continue
            if any(s in t for s in skip): continue
            im = tile_img(t)
            if im is None: continue
            if tint:
                px = im.load()
                for y in range(im.size[1]):
                    for x in range(im.size[0]):
                        r, g, b, a = px[x, y]
                        if a: px[x, y] = tint_px(r, g, b, tint) + (a,)
            if pattern and (pattern_on is None or t in pattern_on):
                for i, f in enumerate(pattern if isinstance(pattern, list) else [pattern]): im = f(im, seed + i * 31)
            mp[t] = add_tile('%s_%s' % (t, suffix), im)
        def re(t):
            if isinstance(t, str): return mp.get(t, t)
            if isinstance(t, dict): return {k: re(v) for k, v in t.items()}
            if isinstance(t, list): return [re(v) for v in t]
            return t
        for p in m['parts']: p['tex'] = re(p['tex'])
    if edit: edit(m)
    if build:
        k = Kit(m, seed)
        build(k, m, random.Random(seed))
        m['parts'] += k.out
    MODELS[key] = m
    d = {'model': key, 'w': w}
    if label: d['label'] = label
    if lands: d['lands'] = lands
    if cave: d['cave'] = True
    if surface: d['surface'] = True
    if village: d['village'] = True
    if water: d['water'] = True
    if dim is not None: d['dim'] = dim
    if held: d['held'] = held
    if drops: d['drops'] = drops
    DEFS.setdefault(mob, []).append(d)


SNOW = ['snowy_plains', 'snowy_taiga', 'snowy_peaks', 'snowy_beach']
COLD = SNOW + ['taiga', 'mountains']
HOT = ['desert', 'savanna', 'redlands']
WET = ['swamp', 'jungle']
WOODS = ['forest', 'birch_forest', 'taiga', 'jungle']
COAST = ['beach', 'snowy_beach', 'ocean']
FIELDS = ['plains', 'savanna', 'birch_forest', 'forest']


# ------------------------------------------------------------------ the catalogue
def catalogue():
    biped_spots = lambda k, rnd, n=9: spots_on(k.m, [('body', 'front', 3), ('body', 'back', 2), ('head', 'left', 1), ('armR', 'right', 1), ('armL', 'front', 1), ('legL', 'front', 1)], rnd)[:n]

    # ===== the restless dead =====
    def miner(k, m, rnd):
        k.helmet('yellow_wool'); k.lamp(); k.tool_on_back()
        k.lumps(spots_on(m, [('body', 'front', 2), ('legR', 'front', 1)], rnd), 'coal_block', size=(0.9, 0.7), thick=0.4)
    def farmer(k, m, rnd):
        k.hat('hay_side', crown=2.0, brim=2.4, band='red_wool')
        k.add('straw', 'head', (1.5, 3.0, P(m, 'head')['box'][2]), (-0.12, -0.12, -2.6, 0.12, 0.12, 0), 'hay_side', rest=(0.3, 0.4, 0))
    def frozen(k, m, rnd):
        k.spikes(spots_on(m, [('armR', 'top', 2), ('armL', 'top', 2), ('head', 'top', 2), ('body', 'back', 2)], rnd), 'ice', w=0.7, h=(1.5, 3.2))
        k.lumps(spots_on(m, [('head', 'top', 1), ('armR', 'top', 1), ('armL', 'top', 1)], rnd), 'snow', size=(1.8, 1.8), thick=0.7)
    def overgrown(k, m, rnd):
        k.lumps(spots_on(m, [('head', 'top', 2), ('armR', 'top', 1), ('armL', 'top', 1), ('body', 'back', 2)], rnd), 'moss_block', size=(1.8, 1.4))
        k.drape('vine', 'body', 'front', h=7, u=0.35); k.drape('vine', 'armL', 'left', h=5)
        k.fans(spots_on(m, [('head', 'top', 2)], rnd), 'jungle_leaves', w=1.4, h=2.2)
    def traveller(k, m, rnd):
        k.back_pack('barrel_side', roll='red_wool'); k.hat('brown_wool', crown=1.8, brim=1.2)
    def soldier(k, m, rnd):
        k.helmet('exposed_copper', visor='weathered_copper')
        k.band('weathered_copper', 'armR', 0.6, 1.0, pad=0.6); k.band('weathered_copper', 'armL', 0.6, 1.0, pad=0.6)
    def shot(k, m, rnd): k.arrows(biped_spots(k, rnd, 8))
    def mossy(k, m, rnd):
        k.lumps(spots_on(m, [('head', 'top', 2), ('head', 'front', 1), ('armR', 'top', 1), ('armL', 'top', 1), ('body', 'front', 2)], rnd), 'moss_block', size=(1.5, 1.2))
        k.fans(spots_on(m, [('head', 'top', 1)], rnd), 'fern', w=1.3, h=2.6)
        k.drape('vine', 'body', 'back', h=8, u=0.6)
    def sunken(k, m, rnd):
        k.spikes(spots_on(m, [('head', 'top', 1)], rnd), 'tube_coral_block', w=0.8, h=(2.5, 3.5))
        k.fans(spots_on(m, [('armR', 'top', 1), ('head', 'top', 1)], rnd), 'fire_coral', w=1.6, h=2.6)
        k.spikes(spots_on(m, [('armL', 'top', 1), ('body', 'back', 1)], rnd), 'horn_coral_block', w=0.7, h=(1.5, 2.5))
        k.lumps(spots_on(m, [('body', 'front', 2), ('legR', 'front', 1)], rnd), 'prismarine', size=(0.9, 0.9), thick=0.6)
        k.drape('kelp', 'body', 'front', h=6, u=0.65, w=0.8)
    def webbed(k, m, rnd):
        k.drape('cobweb', 'head', 'front', h=5, w=2.5, u=0.4); k.drape('cobweb', 'armR', 'right', h=6, w=1.6)
        k.lumps(spots_on(m, [('body', 'front', 2), ('body', 'back', 1)], rnd), 'wool', size=(1.6, 1.3), thick=0.5)
    def knight(k, m, rnd):
        k.helmet('iron_block', visor='iron_block', depth=4.0)
        k.band('iron_block', 'body', 0.5, 1.0, pad=0.5)
        k.add('plume', 'head', (0, P(m, 'head')['box'][4] + 0.9, 0), (-0.3, 0, -2.5, 0.3, 2.4, 2.5), 'red_wool')

    look('ghoul', 'miner', 'a miner ghoul', 1.2, cave=True, build=miner, held='stone_pickaxe', tint={'val': 0.8, 'sat': 0.8}, only={'ghoul_cloth'},
         drops=[['coal', 0, 2, 0.6], ['torch', 1, 2, 0.4], ['iron_nugget', 1, 3, 0.25]])
    look('ghoul', 'farmer', 'a farmhand ghoul', 1.0, village=True, surface=True, build=farmer, held='iron_hoe', tint={'hue': 190, 'sat': 0.9}, only={'ghoul_cloth'},
         drops=[['wheat', 1, 3, 0.6], ['potato', 0, 2, 0.35], ['carrot', 0, 2, 0.35]])
    look('ghoul', 'frozen', 'a frozen ghoul', 1.5, lands=COLD, build=frozen, tint={'hue': 150, 'sat': 0.5, 'val': 1.15}, only={'ghoul_skin', 'ghoul_face'},
         drops=[['ice', 0, 1, 0.4], ['snowball', 1, 3, 0.5]])
    look('ghoul', 'overgrown', 'an overgrown ghoul', 1.5, lands=WET + ['forest'], build=overgrown, tint={'hue': -15, 'sat': 1.2}, only={'ghoul_skin', 'ghoul_face'},
         drops=[['vine', 1, 2, 0.5], ['moss_block', 0, 1, 0.3]])
    look('ghoul', 'traveller', 'a lost traveller', 0.6, surface=True, lands=FIELDS + ['mountains', 'taiga'], build=traveller,
         drops=[['bread', 1, 2, 0.5], ['emerald', 1, 2, 0.15], ['compass', 1, 1, 0.04]])
    look('ghoul', 'soldier', 'a fallen soldier', 0.5, build=soldier, held='iron_sword', drops=[['iron_nugget', 2, 5, 0.6]])
    look('husk', 'prospector', 'a prospector husk', 1.2, build=lambda k, m, r: (k.hat('brown_wool', crown=2.2, brim=2.6, band='black_wool'), k.tool_on_back(head='stone')),
         held='iron_shovel', drops=[['gold_nugget', 1, 4, 0.6], ['gold_ingot', 1, 1, 0.05]])
    look('husk', 'mummy', 'an embalmed husk', 1.0, tint={'sat': 0.25, 'val': 1.25}, only={'husk_wrap', 'husk_skin'},
         build=lambda k, m, r: (k.drape('wool', 'armR', 'right', h=5, w=0.7), k.drape('wool', 'armL', 'left', h=4, w=0.7), k.drape('wool', 'body', 'back', h=9, w=1.2),
                                k.band('wool', 'head', 0.45, 0.65, pad=0.3)),
         drops=[['string', 1, 3, 0.5], ['paper', 1, 2, 0.3]])
    look('husk', 'pinned', 'a cactus-stuck husk', 0.8, lands=['desert', 'redlands'],
         build=lambda k, m, r: (k.spikes(spots_on(m, [('body', 'front', 3), ('armR', 'right', 2), ('legL', 'front', 2), ('head', 'left', 1)], r), 'cactus_side', w=0.2, h=(1.0, 1.8), tilt=0.5),
                                k.lumps(spots_on(m, [('head', 'top', 1), ('armL', 'top', 1)], r), 'sand', size=(1.6, 1.4))),
         drops=[['cactus', 1, 1, 0.3], ['sand', 1, 3, 0.5]])
    look('drowned', 'barnacled', 'a barnacled drowned', 1.5,
         build=lambda k, m, r: (k.lumps(spots_on(m, [('head', 'top', 2), ('head', 'left', 1), ('armR', 'top', 2), ('body', 'front', 3), ('legL', 'front', 1)], r), 'prismarine', size=(0.8, 0.8), thick=0.7),
                                k.drape('kelp', 'armL', 'left', h=5, w=0.8)), drops=[['kelp', 1, 3, 0.6]])
    look('drowned', 'reef', 'a reef-grown drowned', 1.2, build=sunken, drops=[['tube_coral', 1, 1, 0.3], ['fire_coral', 1, 1, 0.2], ['horn_coral', 1, 1, 0.2]])
    look('drowned', 'sailor', 'a drowned sailor', 0.8,
         build=lambda k, m, r: (k.band('red_wool', 'head', 0.7, 0.85, pad=0.3), k.add('knot', 'head', (0, P(m, 'head')['box'][4] - 2, P(m, 'head')['box'][5] + 0.2), (-0.7, -1.8, 0, 0.7, 0.4, 0.8), 'red_wool'),
                                k.band('black_wool', 'body', 0.0, 0.18, pad=0.3), k.eyepatch()), tint={'hue': 195, 'sat': 0.8}, only={'drowned_rags'},
         held='fishing_rod', drops=[['gold_nugget', 1, 3, 0.4], ['string', 1, 2, 0.4]])
    look('drowned', 'anchored', 'an anchored drowned', 0.6,
         build=lambda k, m, r: (k.band('iron_block', 'shinR', 0.0, 0.25, pad=0.45), k.add('chain', 'shinR', (0, 0.3, 0), (-0.3, -3.5, -0.3, 0.3, 0, 0.3), 'iron_block', rest=(0.6, 0, 0)),
                                k.band('iron_block', 'foreL', 0.0, 0.25, pad=0.45)), drops=[['iron_nugget', 2, 4, 0.6]])
    look('charred_ghoul', 'shackled', 'a shackled prisoner', 1.2,
         build=lambda k, m, r: (k.band('iron_block', 'foreR', 0.0, 0.3, pad=0.45), k.band('iron_block', 'foreL', 0.0, 0.3, pad=0.45),
                                k.add('chain', 'foreR', (0, 0.4, 0), (-0.3, -4, -0.3, 0.3, 0, 0.3), 'iron_block', rest=(0.3, 0, 0.2)),
                                k.band('iron_block', 'body', 0.85, 1.0, pad=0.4)), drops=[['iron_nugget', 2, 5, 0.6]])
    look('charred_ghoul', 'magma', 'a magma-cracked ghoul', 1.0,
         build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'front', 3), ('head', 'top', 1), ('armR', 'top', 1), ('legL', 'front', 1)], r), 'magma_block', size=(0.9, 0.8), thick=0.4, glow=True),
         drops=[['magma_cream', 1, 1, 0.25]])
    for z in ('rift_ghoul',):
        pass   # (the rift's own are the story's; they keep their look)

    look('skeleton', 'pincushion', 'a skeleton shot full of arrows', 1.3, build=shot, drops=[['arrow', 2, 5, 0.9]])
    look('skeleton', 'miner', 'a miner skeleton', 1.4, cave=True, build=miner, held='stone_pickaxe', drops=[['coal', 0, 2, 0.5], ['torch', 1, 2, 0.4], ['iron_nugget', 1, 3, 0.2]])
    look('skeleton', 'mossy', 'a mossy skeleton', 1.4, lands=WOODS + ['swamp'], build=mossy, tint={'hue': 40, 'sat': 1.4, 'val': 0.9}, drops=[['moss_block', 0, 1, 0.3], ['vine', 1, 2, 0.4]])
    look('skeleton', 'sunken', 'a sunken skeleton', 1.6, lands=COAST, build=sunken, tint={'hue': 120, 'sat': 1.3, 'val': 0.85}, drops=[['tube_coral', 1, 1, 0.3], ['kelp', 1, 2, 0.4]])
    look('skeleton', 'webbed', 'a webbed skeleton', 0.8, cave=True, build=webbed, drops=[['string', 1, 3, 0.6], ['cobweb', 1, 1, 0.15]])
    look('skeleton', 'knight', 'a fallen knight', 0.4, build=knight, held='iron_sword', drops=[['iron_nugget', 2, 6, 0.7]])
    look('skeleton', 'ranger', 'a ranger skeleton', 0.8, surface=True, lands=WOODS + FIELDS, build=lambda k, m, r: (k.quiver(), k.hat('green_wool' if 'green_wool' in TJ['names'] else 'moss_block', crown=2.2, brim=0.8)),
         drops=[['arrow', 1, 4, 0.8], ['feather', 1, 2, 0.4]])
    look('stray', 'icicle', 'an icicle stray', 1.4, build=lambda k, m, r: k.spikes(spots_on(m, [('head', 'top', 3), ('armR', 'top', 2), ('armL', 'top', 2), ('body', 'back', 2)], r), 'blue_ice', w=0.6, h=(2.0, 4.0)),
         drops=[['ice', 1, 1, 0.4], ['packed_ice', 1, 1, 0.1]])
    look('stray', 'snowcapped', 'a snowbound stray', 1.2, build=lambda k, m, r: (k.lumps(spots_on(m, [('head', 'top', 2), ('armR', 'top', 1), ('armL', 'top', 1)], r), 'snow', size=(2.2, 2.0), thick=1.0), k.band('wool', 'body', 0.82, 1.0, pad=0.7)),
         drops=[['snowball', 2, 4, 0.6], ['wool', 1, 1, 0.3]])
    look('stray', 'lantern', 'a lantern-bearer stray', 0.6, held='lantern', build=lambda k, m, r: k.hat('gray_wool', crown=3.0, brim=0.6), drops=[['lantern', 1, 1, 0.3], ['candle', 1, 1, 0.3]])
    look('scorched_skeleton', 'obsidian', 'an obsidian-shard skeleton', 1.0, build=lambda k, m, r: k.spikes(spots_on(m, [('head', 'top', 2), ('armR', 'top', 1), ('body', 'back', 2)], r), 'obsidian', w=0.6, h=(1.5, 3.0)),
         drops=[['obsidian', 1, 1, 0.08], ['flint', 1, 2, 0.4]])
    look('scorched_skeleton', 'chained', 'a chained skeleton', 1.0, build=lambda k, m, r: (k.band('iron_block', 'foreR', 0, 0.3, pad=0.4), k.band('iron_block', 'foreL', 0, 0.3, pad=0.4), k.band('iron_block', 'shinL', 0, 0.25, pad=0.4)),
         drops=[['iron_nugget', 2, 4, 0.6]])
    look('wither_skeleton', 'crowned', 'a crowned wither skeleton', 0.5, build=lambda k, m, r: (k.band('gold_block', 'head', 0.92, 1.12, pad=0.4), k.spikes(spots_on(m, [('head', 'top', 4)], r), 'gold_block', w=0.4, h=(1.0, 1.8), tilt=0.1)),
         drops=[['gold_ingot', 1, 1, 0.3], ['gold_nugget', 2, 5, 0.6]])
    look('wither_skeleton', 'cracked', 'a cracked wither skeleton', 1.0, build=lambda k, m, r: k.spikes(spots_on(m, [('head', 'top', 2), ('armL', 'top', 2), ('body', 'back', 2)], r), 'crying_obsidian', w=0.6, h=(1.5, 2.8)),
         drops=[['crying_obsidian', 1, 1, 0.1]])
    look('zombie_pigman', 'gilded', 'a gilded pigman', 0.8, build=lambda k, m, r: (k.band('gold_block', 'armR', 0.6, 1.0, pad=0.6), k.band('gold_block', 'armL', 0.6, 1.0, pad=0.6), k.helmet('gold_block')),
         drops=[['gold_nugget', 3, 6, 0.8]])
    look('zombie_pigman', 'bandaged', 'a bandaged pigman', 1.0, build=lambda k, m, r: (k.band('wool', 'head', 0.55, 0.75, pad=0.3), k.band('wool', 'foreL', 0.3, 0.7, pad=0.3), k.band('wool', 'shinR', 0.4, 0.8, pad=0.3)),
         drops=[['string', 1, 2, 0.4]])
    look('enderman', 'crystal', 'a crystal-grown enderman', 1.0, build=lambda k, m, r: k.spikes(spots_on(m, [('head', 'top', 3), ('armR', 'top', 1), ('body', 'back', 2)], r), 'purpur_block', w=0.6, h=(1.5, 3.0)),
         drops=[['chorus_fruit', 1, 1, 0.3]])
    look('enderman', 'endstone', 'an end-stone enderman', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 1), ('body', 'front', 2), ('armL', 'top', 1), ('legR', 'front', 2)], r), 'end_stone', size=(1.2, 1.0)),
         drops=[['ender_pearl', 1, 1, 0.15]])
    look('witch', 'herbalist', 'a herbalist witch', 1.0, build=lambda k, m, r: (k.shrooms(spots_on(m, [('head', 'top', 2)], r), s=0.8), k.fans(spots_on(m, [('body', 'back', 2)], r), 'fern', w=1.0, h=2.0)),
         drops=[['red_mushroom', 1, 2, 0.5], ['brown_mushroom', 1, 2, 0.5]])
    look('witch', 'bog', 'a bog witch', 1.0, lands=['swamp'], build=lambda k, m, r: (k.lumps(spots_on(m, [('head', 'top', 2), ('body', 'front', 2)], r), 'mud', size=(1.3, 1.2)), k.drape('vine', 'body', 'back', h=8)),
         tint={'hue': 60, 'sat': 0.8}, drops=[['mud', 1, 2, 0.4], ['slime_ball', 1, 1, 0.3]])
    look('witch', 'candle', 'a candle witch', 0.7, build=lambda k, m, r: k.spikes(spots_on(m, [('head', 'top', 3)], r), 'wool', w=0.4, h=(1.2, 2.2), tilt=0.15),
         drops=[['candle', 1, 3, 0.7]])
    look('raider', 'bannerman', 'a raider bannerman', 0.8, build=lambda k, m, r: (k.add('pole', 'body', (0, 4, P(m, 'body')['box'][5] + 0.6), (-0.35, 0, -0.35, 0.35, 16, 0.35), 'dark_oak_planks'),
                                                                                    k.add('flag', 'body', (0, 13, P(m, 'body')['box'][5] + 0.6), (-0.1, -6, 0, 0.1, 3, 6), 'black_wool')),
         drops=[['emerald', 1, 1, 0.3]])
    look('raider', 'scout', 'a raider scout', 1.0, held='spyglass', build=lambda k, m, r: (k.quiver(), k.hat('brown_wool', crown=1.6, brim=0.8)), drops=[['arrow', 1, 3, 0.6]])
    look('raider', 'axeman', 'a raider axeman', 1.0, held='iron_axe', build=lambda k, m, r: (k.helmet('iron_block'), k.band('iron_block', 'armR', 0.6, 1.0, pad=0.6)), drops=[['iron_nugget', 1, 3, 0.5]])
    look('warlock', 'candlelit', 'a candlelit warlock', 1.0, build=lambda k, m, r: k.spikes(spots_on(m, [('armR', 'top', 1), ('armL', 'top', 1)], r), 'wool', w=0.45, h=(1.2, 1.8), tilt=0.1),
         drops=[['candle', 1, 2, 0.6]])
    look('warlock', 'bookbound', 'a bookbound warlock', 0.8, build=lambda k, m, r: k.back_pack('bookshelf', pname='body', w=2.6, h=0.55, d=1.8), drops=[['book', 1, 1, 0.4], ['paper', 1, 3, 0.5]])
    look('werewolf', 'grey', 'a grey werewolf', 1.0, tint={'sat': 0.2, 'val': 1.1})
    look('werewolf', 'black', 'a black werewolf', 1.0, tint={'sat': 0.4, 'val': 0.45})
    look('werewolf', 'scarred', 'a scarred werewolf', 0.6, build=lambda k, m, r: (k.eyepatch(), k.arrows(spots_on(m, [('body', 'back', 2), ('armL', 'left', 1)], r))),
         drops=[['arrow', 1, 2, 0.6]])
    look('wraith', 'lantern', 'a lantern wraith', 1.0, held='lantern', drops=[['lantern', 1, 1, 0.2]])
    look('wraith', 'tattered', 'a tattered wraith', 1.0, build=lambda k, m, r: (k.drape('gray_wool', 'body', 'back', h=10, w=2.5), k.drape('gray_wool', 'armR', 'right', h=5, w=0.9)), tint={'val': 0.7})
    look('ogre', 'chieftain', 'an ogre chieftain', 0.6, scale=1.08, build=lambda k, m, r: (k.band('bone_block_side', 'body', 0.82, 0.95, pad=0.5), k.horns_long('bone_block_side', reach=3.0, up=2.0)),
         drops=[['bone', 2, 4, 0.8], ['gold_ingot', 1, 1, 0.2]])
    look('ogre', 'mossy', 'a mossback ogre', 1.0, lands=WOODS + ['swamp'], build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 2), ('body', 'back', 3), ('armR', 'top', 1), ('armL', 'top', 1)], r), 'moss_block', size=(2.2, 1.8)),
         drops=[['moss_block', 1, 2, 0.5]])
    look('pirate', 'peg', 'a one-eyed pirate', 1.0, build=lambda k, m, r: (k.eyepatch(), k.band('red_wool', 'head', 0.72, 0.9, pad=0.3)), drops=[['gold_nugget', 1, 3, 0.5]])
    look('pirate', 'parrot', 'a pirate with a parrot', 0.6, build=lambda k, m, r: (k.add('parrot', 'armL', (0, 2.6, 0), (-1.0, 0, -1.4, 1.0, 2.4, 1.2), 'red_wool'),
                                                                                    k.add('parrotHead', 'armL', (0, 5.0, -0.6), (-0.8, 0, -1.0, 0.8, 1.4, 0.6), 'red_wool'),
                                                                                    k.add('parrotBeak', 'armL', (0, 5.4, -1.6), (-0.3, -0.4, -0.6, 0.3, 0.4, 0), 'yellow_wool'),
                                                                                    k.add('parrotWing', 'armL', (-1.0, 3.6, 0), (-0.2, -1.6, -1.2, 0, 0.6, 1.0), 'blue_wool')),
         drops=[['feather', 1, 2, 0.6]])
    look('pirate', 'deckhand', 'a pirate deckhand', 1.0, build=lambda k, m, r: (k.band('black_wool', 'body', 0.0, 0.2, pad=0.3), k.band('wool', 'body', 0.45, 0.6, pad=0.25)), held='iron_shovel')
    look('village_guard', 'veteran', 'a veteran guard', 0.6, build=lambda k, m, r: (k.helmet('iron_block', visor='iron_block'), k.eyepatch(side=-1)))
    look('village_guard', 'archer', 'a guard archer', 1.0, build=lambda k, m, r: k.quiver(), held='bow')
    look('village_guard', 'lamplighter', 'a lamplighter guard', 0.6, held='lantern', build=lambda k, m, r: k.hat('black_wool', crown=3.0, brim=0.8))
    look('minotaur', 'mossy', None, 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 2), ('body', 'back', 3)], r), 'moss_block', size=(2.0, 1.6)))
    look('gaoler', 'keys', None, 1.0, build=lambda k, m, r: k.band('iron_block', 'body', 0.05, 0.15, pad=0.4))

    # ===== golems: built of whatever their makers had =====
    def golem_of(block, chest=None, limbs=None, head=None):
        def e(m):
            for p in m['parts']:
                n = p['name']
                if n == 'body': p['tex'] = {'all': block, 'front': chest or block}
                elif n == 'head': p['tex'] = head or block
                elif n in ('nose', 'brow'): p['tex'] = chest or block
                elif n.startswith('arm') or n.startswith('fore') or n.startswith('leg') or n.startswith('shin'): p['tex'] = limbs or block
                else: p['tex'] = block
            m['parts'] += [part('eyeL', 'head', (-2.2, 6.5, -4), (-1, -0.6, -0.15, 1, 0.6, 0.05), 'coal_block'), part('eyeR', 'head', (2.2, 6.5, -4), (-1, -0.6, -0.15, 1, 0.6, 0.05), 'coal_block')]
        return e
    look('iron_golem', 'copper', 'a copper golem', 1.0, pack=False, edit=golem_of('copper_block', 'exposed_copper', 'weathered_copper'),
         build=lambda k, m, r: (k.add('rod', 'head', (0, 10, 0), (-0.5, 0, -0.5, 0.5, 5, 0.5), 'copper_block'), k.add('rodTip', 'head', (0, 15, 0), (-0.9, 0, -0.9, 0.9, 1.2, 0.9), 'copper_block'),
                                k.lumps(spots_on(m, [('body', 'front', 3), ('armL', 'top', 1)], r), 'oxidized_copper', size=(1.6, 1.4), thick=0.3)),
         drops=[['copper_ingot', 2, 5, 1.0], ['lightning_rod', 1, 1, 0.3]])
    look('iron_golem', 'amethyst', 'an amethyst golem', 0.4, pack=False, lands=['mountains', 'snowy_peaks'], edit=golem_of('amethyst_block', 'amethyst_block', 'calcite', 'amethyst_block'),
         build=lambda k, m, r: k.spikes(spots_on(m, [('head', 'top', 3), ('armR', 'top', 2), ('armL', 'top', 2), ('body', 'back', 3)], r), 'amethyst_block', w=0.9, h=(2.0, 4.5)),
         drops=[['amethyst_shard', 3, 7, 1.0]])
    look('iron_golem', 'mushroom', 'a mushroom golem', 0.4, pack=False, lands=['swamp', 'forest'], edit=golem_of('mushroom_stem', 'mushroom_stem', 'mushroom_stem', 'mushroom_stem'),
         build=lambda k, m, r: (k.add('cap', 'head', (0, 9, 0), (-8, 0, -8, 8, 4, 8), 'red_mushroom_block'), k.add('gills', 'head', (0, 8.6, 0), (-7.6, 0, -7.6, 7.6, 0.4, 7.6), 'mushroom_stem'),
                                k.shrooms(spots_on(m, [('armR', 'top', 1), ('body', 'back', 2)], r), cap='brown_mushroom_block' if 'brown_mushroom_block' in TJ['names'] else 'red_mushroom_block')),
         drops=[['red_mushroom', 2, 4, 1.0], ['brown_mushroom', 1, 3, 0.6]])
    look('iron_golem', 'cactus', 'a cactus golem', 0.5, pack=False, lands=['desert', 'redlands'], edit=golem_of('cactus_side', 'cactus_side', 'cactus_side', 'cactus_side'),
         build=lambda k, m, r: (k.spikes(spots_on(m, [('armR', 'right', 3), ('armL', 'left', 3), ('body', 'front', 3), ('head', 'left', 1), ('head', 'right', 1)], r), 'cactus_side', w=0.18, h=(0.8, 1.4), tilt=0.3),
                                k.add('bloom', 'head', (1.5, 10, 0), (-0.9, 0, -0.9, 0.9, 1.6, 0.9), 'flower_red')),
         drops=[['cactus', 2, 4, 1.0]])
    look('iron_golem', 'prismarine', 'a prismarine golem', 0.5, pack=False, lands=COAST, edit=golem_of('prismarine', 'prismarine_bricks', 'dark_prismarine', 'prismarine_bricks'),
         build=lambda k, m, r: (k.fans(spots_on(m, [('head', 'top', 2), ('armR', 'top', 1)], r), 'tube_coral_fan', w=1.8, h=3.0), k.drape('kelp', 'body', 'back', h=9, w=1.2), k.lamp('head', 'sea_lantern')),
         drops=[['kelp', 2, 4, 0.8], ['sea_pickle', 1, 2, 0.4]])
    look('iron_golem', 'honeycomb', 'a honeycomb golem', 0.4, pack=False, lands=['plains', 'birch_forest'], edit=golem_of('honeycomb_block', 'bee_nest_side', 'honeycomb_block', 'honeycomb_block'),
         build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 2), ('armL', 'top', 1)], r), 'honeycomb_block', size=(1.4, 1.0)),
         drops=[['honeycomb', 3, 6, 1.0], ['honey_bottle', 1, 1, 0.2]])
    look('iron_golem', 'emerald', 'an emerald golem', 0.08, pack=False, village=True, edit=golem_of('emerald_block', 'emerald_block', 'stone_bricks', 'emerald_block'), drops=[['emerald', 4, 9, 1.0]])
    look('iron_golem', 'diamond', 'a diamond golem', 0.04, pack=False, edit=golem_of('diamond_block', 'diamond_block', 'stone_bricks', 'diamond_block'), drops=[['diamond', 1, 3, 1.0]])
    look('iron_golem', 'gold', 'a golden golem', 0.08, pack=False, lands=['redlands', 'desert', 'savanna'], edit=golem_of('gold_block', 'gold_block', 'cut_sandstone', 'gold_block'), drops=[['gold_ingot', 2, 5, 1.0]])
    look('iron_golem', 'quartz', 'a quartz golem', 0.3, pack=False, lands=SNOW, edit=golem_of('quartz_block', 'chiseled_quartz_block', 'smooth_quartz', 'quartz_block'),
         build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 1), ('armR', 'top', 1), ('armL', 'top', 1)], r), 'snow', size=(2.6, 2.4), thick=1.0), drops=[['quartz', 3, 6, 1.0]])
    look('iron_golem', 'ancient', 'an ancient golem', 0.5, pack=False, lands=WOODS, edit=golem_of('mossy_stone_bricks', 'cracked_stone_bricks', 'mossy_cobblestone', 'cracked_stone_bricks'),
         build=lambda k, m, r: (k.lumps(spots_on(m, [('head', 'top', 2), ('body', 'front', 2), ('armR', 'top', 1)], r), 'moss_block', size=(2.0, 1.6)), k.drape('vine', 'body', 'front', h=10, u=0.3),
                                k.fans(spots_on(m, [('head', 'top', 2)], r), 'flower_red', w=0.9, h=1.4)),
         drops=[['iron_ingot', 1, 3, 0.8], ['moss_block', 1, 2, 0.5]])
    look('iron_golem', 'rusted', 'a rusted golem', 0.6, tint={'hue': -20, 'sat': 2.2, 'val': 0.75}, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'front', 3), ('armL', 'top', 2), ('legR', 'front', 1)], r), 'exposed_copper', size=(1.5, 1.2), thick=0.3),
         drops=[['iron_nugget', 4, 9, 1.0]])
    look('iron_golem', 'flowered', 'a village golem with a flower', 1.2, village=True, build=lambda k, m, r: k.add('flower', 'foreR', (0, -13.5, -3.4), (-0.6, -3, -0.6, 0.6, 0, 0.6), 'flower_red'))
    look('frost_golem', 'lantern', 'a frost golem with a lantern', 1.0, held='lantern')
    look('frost_golem', 'snowcap', 'a snow-capped frost golem', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 1), ('armR', 'top', 1), ('armL', 'top', 1), ('body', 'back', 1)], r), 'snow', size=(3, 2.6), thick=1.2))
    look('sandstone_golem', 'wrapped', 'a wrapped sandstone golem', 1.0, build=lambda k, m, r: (k.band('red_wool', 'body', 0.75, 0.95, pad=0.4), k.drape('red_wool', 'body', 'back', h=8, w=2.0)))
    look('moss_golem', 'blooming', 'a blooming mossy golem', 1.0, build=lambda k, m, r: k.fans(spots_on(m, [('head', 'top', 2), ('armR', 'top', 2), ('body', 'front', 2)], r), 'flower_yellow', w=0.8, h=1.4))
    look('savanna_golem', 'painted', 'a painted savanna golem', 1.0, build=lambda k, m, r: (k.band('white_terracotta', 'body', 0.35, 0.45, pad=0.25), k.band('black_terracotta', 'body', 0.55, 0.62, pad=0.25)))
    look('cinder_golem', 'chained', None, 1.0, build=lambda k, m, r: k.band('iron_block', 'body', 0.4, 0.55, pad=0.5))

    # ===== the farm and the fields (the user's sheets, our own painting, and builds of their own) =====
    WHITE, BLACK, CREAM, GINGER, SLATE = (238, 236, 228), (34, 32, 36), (232, 214, 170), (196, 104, 40), (70, 76, 92)
    look('cow', 'dairy', 'a dairy cow', 1.2, ptint=True, tint={'sat': 0.0, 'colVal': 0.3}, only={'cow_hide', 'cow_face'}, village=True)
    look('cow', 'pinto', 'a pinto cow', 1.0, ptint=True, tint={'hue': 8, 'sat': 1.5, 'val': 1.15}, only={'cow_hide', 'cow_face'}, lands=FIELDS)
    look('cow', 'ashen', 'an ashen cow', 0.8, ptint=True, tint={'hue': 190, 'sat': 0.35, 'val': 1.0}, only={'cow_hide', 'cow_face'}, lands=COLD)
    look('cow', 'umbra', 'an umbra cow', 0.5, ptint=True, tint={'hue': 250, 'sat': 0.5, 'val': 0.45}, only={'cow_hide', 'cow_face'}, lands=['forest', 'taiga', 'swamp'])
    look('cow', 'sunset', 'a sunset cow', 0.8, ptint=True, tint={'hue': 10, 'sat': 1.8, 'val': 1.2}, only={'cow_hide'}, lands=HOT,
         build=lambda k, m, r: k.add('blaze', 'head', (0, 0.5, P(m, 'head')['box'][2]), (-1.0, -3.5, -0.25, 1.0, 3.5, 0.05), 'orange_terracotta'))
    look('cow', 'cookie', 'a belted cow', 0.7, ptint=True, tint={'hue': 200, 'sat': 0.25, 'val': 0.5}, only={'cow_hide', 'cow_face'}, lands=['mountains', 'taiga', 'plains'],
         build=lambda k, m, r: k.ring_z('wool', 'body', 0.35, 0.65, pad=0.2))
    def shaggy(k, m, r):
        k.coat('@cow_hide_wooly' if has_tile('@cow_hide_wooly') else 'orange_wool', 'body', pad=0.9, hang=3.0)
        x0, y0, z0, x1, y1, z1 = P(m, 'head')['box']
        k.add('fringe', 'head', (0, y1, z0), (x0 - 0.5, -3.2, -0.6, x1 + 0.5, 0.8, 3.5), '@cow_hide_wooly' if has_tile('@cow_hide_wooly') else 'orange_wool')
        k.horns_long('cow_horn', reach=4.0, up=1.8)
        for s in ('FR', 'FL', 'BR', 'BL'): k.band('@cow_hide_wooly' if has_tile('@cow_hide_wooly') else 'orange_wool', 'leg' + s, 0.2, 1.0, pad=0.6)
    look('cow', 'wooly', 'a shaggy highland cow', 1.0, ptint=True, tint={'hue': 12, 'sat': 1.6, 'val': 1.25}, pattern=pat_stripes((150, 70, 26), period=3, width=1, vertical=True),
         only={'cow_hide', 'cow_face'}, lands=COLD + ['mountains'], build=shaggy, drops=[['wool', 1, 2, 0.5]])
    look('cow', 'belled', 'a belled cow', 1.0, village=True,
         build=lambda k, m, r: (k.ring_z('red_wool', 'head', 0.82, 1.0, pad=0.35), k.add('bell', 'head', (0, P(m, 'head')['box'][1] - 0.3, -1.0), (-1.1, -2.2, -1.1, 1.1, 0, 1.1), 'gold_block')),
         drops=[['bell', 1, 1, 0.05]])
    look('cow', 'longhorn', 'a longhorn cow', 0.8, ptint=True, lands=['savanna', 'redlands', 'desert'], tint={'hue': 15, 'sat': 1.3, 'val': 1.1}, only={'cow_hide', 'cow_face'},
         build=lambda k, m, r: k.horns_long('bone_block_side', reach=6.0, up=2.2))

    look('pig', 'piebald', 'a piebald pig', 1.0, pattern=pat_patches((214, 120, 70), cover=0.5), only={'pig_skin'}, lands=FIELDS)
    look('pig', 'pinkfooted', 'a pink-footed pig', 0.8, tint={'hue': -15, 'sat': 0.5, 'val': 0.42}, only={'pig_skin', 'pig_face'}, lands=WOODS,
         ptint=True, build=lambda k, m, r: [k.band('pig_skin', 'leg' + s, 0.0, 0.3, pad=0.15) for s in ('FR', 'FL', 'BR', 'BL')])
    look('pig', 'sooty', 'a sooty pig', 0.8, ptint=True, tint={'sat': 0.2, 'val': 0.32}, only={'pig_skin', 'pig_face'}, lands=['mountains', 'taiga'],
         build=lambda k, m, r: (k.ring_z('pink_wool' if 'pink_wool' in TJ['names'] else 'pig_skin', 'body', 0.3, 0.5, pad=0.15)))
    look('pig', 'spotted', 'a spotted pig', 1.0, pattern=pat_spots(BLACK, n=7, r=2.8), only={'pig_skin'})
    look('pig', 'mottled', 'a mottled pig', 0.8, ptint=True, tint={'hue': 15, 'sat': 0.9, 'val': 0.6}, pattern=pat_patches((120, 70, 40), cover=0.4, cell=4), only={'pig_skin', 'pig_face'}, lands=['forest', 'savanna'])
    look('pig', 'muddy', 'a mud-caked pig', 1.0, lands=['swamp', 'plains'], build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'top', 3), ('body', 'left', 3), ('body', 'right', 3), ('head', 'top', 1), ('head', 'front', 1)], r), 'packed_mud', size=(2.6, 2.0), thick=0.7),
         drops=[['mud', 1, 1, 0.3]])
    look('pig', 'truffle', 'a truffle-hunting pig', 0.6, ptint=True, lands=['forest', 'birch_forest', 'taiga'], tint={'hue': 10, 'sat': 0.7, 'val': 0.7}, only={'pig_skin', 'pig_face'},
         build=lambda k, m, r: k.shrooms([('head', 'top', 0.3, 0.6)], cap='brown_mushroom_block' if 'brown_mushroom_block' in TJ['names'] else 'red_mushroom_block', s=0.8),
         drops=[['brown_mushroom', 1, 2, 0.7]])

    def wool_edit(col):
        def e(m):
            for p in m['parts']:
                if p['tex'] == 'sheep_wool': p['tex'] = col
        return e
    look('sheep', 'patched', 'a patched sheep', 1.0, ptint=True, tint={'sat': 0.4, 'val': 0.35}, only={'sheep_face', 'sheep_skin'}, pattern=pat_speckle((220, 220, 214), 0.18), pattern_on={'sheep_skin'})
    look('sheep', 'rocky', 'a rocky sheep', 0.8, lands=['mountains', 'snowy_peaks'], pattern=pat_speckle(BLACK, 0.22), only={'sheep_skin'},
         build=lambda k, m, r: k.horns_curl('stone', size=0.9))
    look('sheep', 'fuzzy', 'a fuzzy sheep', 0.8, tint={'hue': 10, 'sat': 0.8, 'val': 0.45}, only={'sheep_face', 'sheep_skin'},
         ptint=True, edit=lambda m: [resize(m, n, 1.14, 1.14, 1.08) for n in ('wool', 'cap')])
    look('sheep', 'ram', 'a ram', 0.8, build=lambda k, m, r: k.horns_curl('bone_block_side', size=1.1))
    look('sheep', 'bellwether', 'a bellwether', 0.5, village=True, build=lambda k, m, r: (k.ring_z('leather' if False else 'brown_wool', 'head', 0.82, 1.0, pad=0.4), k.add('bell', 'head', (0, P(m, 'head')['box'][1] - 0.3, -1.0), (-1.0, -2.0, -1.0, 1.0, 0, 1.0), 'gold_block')),
         drops=[['bell', 1, 1, 0.05]])

    look('chicken', 'stormy', 'a stormy chicken', 1.0, ptint=True, tint={'sat': 0.15, 'val': 0.85}, only={'chicken_feather'}, lands=COLD + ['mountains'],
         build=lambda k, m, r: k.add('hood', 'head', (0, 3.2, -1.5), (-2.25, 0, -1.75, 2.25, 3.0, 1.75), 'purple_wool' if 'purple_wool' in TJ['names'] else 'gray_wool'))
    look('chicken', 'amber', 'an amber chicken', 1.0, ptint=True, tint={'hue': 30, 'sat': 3.0, 'val': 0.95, 'mix': [0.85, 0.45, 0.1, 0.55]}, only={'chicken_feather', 'chicken_face'}, lands=HOT)
    look('chicken', 'bronzed', 'a bronzed chicken', 0.8, ptint=True, tint={'mix': [0.25, 0.42, 0.4, 0.7]}, only={'chicken_feather'}, lands=WOODS,
         build=lambda k, m, r: k.fans([('body', 'back', 0.5, 0.75)], 'dark_prismarine', w=1.4, h=3.2))
    look('chicken', 'goldcrest', 'a gold-crested chicken', 0.8, pattern=pat_low((214, 140, 40), 0.5), only={'chicken_feather'},
         build=lambda k, m, r: k.crest('gold_block', h=2.4, n=3))
    look('chicken', 'skewbald', 'a skewbald chicken', 1.0, pattern=pat_patches((50, 44, 40), cover=0.38, cell=5), only={'chicken_feather'})
    look('chicken', 'midnight', 'a midnight chicken', 0.6, ptint=True, tint={'mix': [0.12, 0.14, 0.32, 0.8]}, only={'chicken_feather', 'chicken_face'}, cave=False, lands=['taiga', 'forest', 'swamp'])
    look('chicken', 'silkie', 'a silkie chicken', 0.8, village=True, build=lambda k, m, r: (k.coat('wool', 'body', pad=0.9, hang=0.6), k.add('puff', 'head', (0, 6, -1.5), (-2.4, -0.4, -2.0, 2.4, 2.2, 2.0), 'wool')),
         drops=[['feather', 1, 3, 0.8]])
    look('chicken', 'scrawny', 'a scrawny chicken', 0.4, ptint=True, tint={'sat': 0.3, 'val': 1.1}, only={'chicken_feather'},
         edit=lambda m: resize(m, 'body', 0.8, 0.9, 0.9))

    # (the wolf's own coat is grey: a colour is laid over it, keeping its light and dark)
    for wk, wl, wt, lands, ptex in (('brown', 'a brown wolf', {'mix': [0.58, 0.4, 0.24, 0.62], 'val': 1.15}, WOODS, 'entity/wolf/wolf_woods'),
                                    ('husky', 'a husky', {'sat': 0.0, 'val': 1.05, 'mix': [0.42, 0.46, 0.55, 0.35]}, SNOW, 'entity/wolf/wolf_ashen'),
                                    ('golden', 'a golden wolf', {'mix': [0.85, 0.6, 0.28, 0.62], 'val': 1.1}, ['savanna', 'plains'], 'entity/wolf/wolf_rusty'),
                                    ('black', 'a black wolf', {'sat': 0.3, 'val': 0.3}, ['taiga', 'snowy_taiga'], 'entity/wolf/wolf_black'),
                                    ('chestnut', 'a chestnut wolf', {'mix': [0.7, 0.38, 0.2, 0.62], 'val': 1.1}, ['taiga', 'forest'], 'entity/wolf/wolf_chestnut'),
                                    ('spotted', 'a spotted wolf', {'mix': [0.84, 0.7, 0.48, 0.6], 'val': 1.15}, ['savanna'], 'entity/wolf/wolf_spotted'),
                                    ('striped', 'a striped wolf', {'mix': [0.74, 0.54, 0.32, 0.6], 'val': 1.1}, ['redlands', 'desert'], 'entity/wolf/wolf_striped'),
                                    ('snowy', 'a snowy wolf', {'mix': [0.96, 0.96, 0.94, 0.75], 'val': 1.2}, SNOW + ['mountains'], 'entity/wolf/wolf_snowy')):
        pat = None
        if wk == 'husky': pat = pat_low(WHITE, 0.45)
        if wk == 'spotted': pat = pat_spots((60, 40, 30), n=6, r=2.2)
        if wk == 'striped': pat = pat_stripes((70, 50, 36), period=5, width=1, vertical=True)
        look('wolf', wk, wl, 1.0, tint=wt, pattern=pat, pattern_on={'wolf_fur', 'wolf_back'}, lands=lands, packTex=[ptex])
    look('wolf', 'shepherd', 'a shepherd wolf', 0.8, tint={'mix': [0.78, 0.52, 0.28, 0.62], 'val': 1.1}, lands=['plains', 'forest'], village=True,
         build=lambda k, m, r: (k.add('saddle', 'body', (0, P(m, 'body')['box'][4], 0), (-3.4, -1.6, -4.5, 3.4, 0.4, 4.5), 'black_wool'),
                                k.add('saddleB', 'head', (0, P(m, 'head')['box'][4], -1), (-2.6, -0.6, -2.2, 2.6, 0.3, 1.0), 'black_wool')))
    def pug(m):
        for p in m['parts']:
            if p['name'] in ('legFR', 'legFL', 'legBR', 'legBL', 'shinFR', 'shinFL', 'shinBR', 'shinBL'):
                b = p['box']; p['box'] = [b[0] * 1.3, b[1] * 0.55, b[2] * 1.3, b[3] * 1.3, b[4] * 0.55, b[5] * 1.3]
                p['min'] = [x / 16 for x in p['box'][:3]]; p['max'] = [x / 16 for x in p['box'][3:]]
            if p['name'] in ('shinFR', 'shinFL', 'shinBR', 'shinBL'):
                p['at'] = [p['at'][0], p['at'][1] * 0.55, p['at'][2]]; p['pivot'] = [x / 16 for x in p['at']]
            if p['name'] in ('body', 'head', 'tail', 'legFR', 'legFL', 'legBR', 'legBL') and p.get('parent') is None:
                p['at'] = [p['at'][0], p['at'][1] - 3.6, p['at'][2]]; p['pivot'] = [x / 16 for x in p['at']]
            if p['name'] == 'snout':
                b = p['box']; p['box'] = [b[0] * 1.2, b[1], b[2] * 0.35, b[3] * 1.2, b[4], b[5]]
                p['min'] = [x / 16 for x in p['box'][:3]]; p['max'] = [x / 16 for x in p['box'][3:]]
            if p['name'] == 'body':
                b = p['box']; p['box'] = [b[0] * 1.2, b[1] * 1.15, b[2] * 0.85, b[3] * 1.2, b[4] * 1.15, b[5] * 0.85]
                p['min'] = [x / 16 for x in p['box'][:3]]; p['max'] = [x / 16 for x in p['box'][3:]]
        m['height'] = m.get('height', 20) * 0.75
    look('wolf', 'pug', 'a pug', 0.4, village=True, tint={'mix': [0.86, 0.68, 0.45, 0.65]}, edit=pug,
         build=lambda k, m, r: k.add('mask', 'head', (0, 0, P(m, 'head')['box'][2]), (-2.6, -2.6, -0.3, 2.6, 1.6, 0.05), 'black_wool'))
    look('wolf', 'leader', 'a pack leader', 0.25, lands=COLD + WOODS, scale=1.15, tint={'sat': 0.6, 'val': 0.75},
         build=lambda k, m, r: (k.eyepatch(), k.add('ruff', 'body', (0, 0.5, -3), (-4.6, -4.2, -4.0, 4.6, 4.2, 4.0), 'gray_wool')))

    # ===== the wild =====
    def feathered(k, m, r):
        legs = [s for s in ('shinFR', 'shinFL', 'shinBR', 'shinBL') if has(m, s)] or [s for s in ('legFR', 'legFL', 'legBR', 'legBL') if has(m, s)]
        for s in legs: k.band('wool', s, 0.0, 0.45, pad=0.6)
    look('horse', 'draft', 'a draft horse', 0.8, village=True, scale=1.1, build=feathered)
    look('horse', 'painted', 'a painted horse', 1.0, lands=['savanna', 'plains'], pattern=pat_patches(WHITE, cover=0.42), skip=('face',))
    look('horse', 'warhorse', 'a lost warhorse', 0.25, build=lambda k, m, r: (k.ring_z('iron_block', 'body', 0.0, 0.3, pad=0.4), k.helmet('iron_block', pad=0.3, depth=2.0)), drops=[['iron_nugget', 2, 5, 0.8]])
    look('donkey', 'packed', 'a runaway pack donkey', 0.6, build=lambda k, m, r: k.saddlebags('barrel_side'), drops=[['bread', 1, 2, 0.5], ['emerald', 1, 1, 0.2], ['string', 1, 2, 0.4]])
    look('donkey', 'grey', 'a grey donkey', 1.0, tint={'sat': 0.15, 'val': 1.05})
    look('llama', 'caravan', 'a caravan llama', 0.6, build=lambda k, m, r: k.saddlebags('barrel_side'), drops=[['emerald', 1, 1, 0.2]])
    look('llama', 'tasseled', 'a tasseled llama', 0.8, village=True, build=lambda k, m, r: (k.add('rug', 'body', (0, P(m, 'body')['box'][4], 0), (-5.6, -2.5, -6, 5.6, 0.4, 6), 'red_wool'),
                                                                                         k.drape('yellow_wool', 'body', 'left', h=3, w=1.0, u=0.3), k.drape('yellow_wool', 'body', 'right', h=3, w=1.0, u=0.7)))
    look('camel', 'trader', 'a trader camel', 0.8, build=lambda k, m, r: k.saddlebags('barrel_side'), drops=[['emerald', 1, 2, 0.25]])
    look('camel', 'pale', 'a pale camel', 1.0, tint={'sat': 0.5, 'val': 1.2})
    look('goat', 'longhorn', 'a longhorn goat', 1.0, build=lambda k, m, r: k.horns_curl('bone_block_side', size=1.3), drops=[['goat_horn', 1, 1, 0.1]])
    look('goat', 'frostbeard', 'a frost-bearded goat', 1.0, lands=SNOW, build=lambda k, m, r: (k.beard('wool', h=3.5), k.lumps(spots_on(m, [('body', 'top', 2)], r), 'snow', size=(2.0, 1.6), thick=0.6)))
    look('goat', 'black', 'a black goat', 0.6, tint={'sat': 0.3, 'val': 0.35})
    look('deer', 'snowy', 'a snowy deer', 1.0, lands=SNOW, tint={'sat': 0.25, 'val': 1.3})
    look('deer', 'fawnspot', 'a dappled deer', 1.0, pattern=pat_spots(WHITE, n=9, r=1.4), lands=['forest', 'birch_forest'])
    look('deer', 'mossantler', 'a moss-antlered deer', 0.5, lands=WOODS, build=lambda k, m, r: (k.antlers('spruce_log_side'), k.lumps(spots_on(m, [('head', 'top', 1)], r), 'moss_block', size=(1.4, 1.2))))
    look('boar', 'tusker', 'an old tusker', 0.8, scale=1.1, build=lambda k, m, r: (k.horns_long('bone_block_side', reach=2.2, up=2.0), k.crest('brown_wool', pname='body', h=2.2, n=5)))
    look('boar', 'scarred', 'a scarred boar', 1.0, build=lambda k, m, r: (k.eyepatch(), k.arrows(spots_on(m, [('body', 'left', 1), ('body', 'top', 1)], r))), drops=[['arrow', 1, 2, 0.6]])
    look('brown_bear', 'honeypaw', 'a honey-pawed bear', 0.8, lands=['forest', 'plains', 'birch_forest'], build=lambda k, m, r: (k.lumps(spots_on(m, [('legFR', 'front', 1), ('head', 'front', 1)], r), 'honeycomb_block', size=(1.4, 1.2), thick=0.5),
                                                                                                                         k.add('bee', 'head', (2, P(m, 'head')['box'][4] + 2, 0), (-0.5, -0.4, -0.6, 0.5, 0.4, 0.6), 'yellow_wool')),
         drops=[['honeycomb', 1, 2, 0.6]])
    look('brown_bear', 'black', 'a black bear', 1.0, tint={'sat': 0.3, 'val': 0.4})
    look('brown_bear', 'cinnamon', 'a cinnamon bear', 0.8, tint={'hue': 8, 'sat': 1.5, 'val': 1.25})
    look('polar_bear', 'scarred', 'a scarred polar bear', 0.8, build=lambda k, m, r: k.eyepatch())
    look('polar_bear', 'snowcap', 'a snow-dusted polar bear', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'top', 3), ('head', 'top', 1)], r), 'snow', size=(2.6, 2.0), thick=0.7))
    look('panda', 'brown', 'a brown panda', 0.6, tint={'hue': 25, 'sat': 2.5, 'val': 1.4, 'mix': [0.45, 0.3, 0.2, 0.25]})
    look('panda', 'bamboo', 'a bamboo-chewing panda', 1.0, held='bamboo', drops=[['bamboo', 1, 2, 0.6]])
    for c, lbl, t, pat in (('calico', 'a calico cat', None, [pat_patches(GINGER, 0.35), pat_patches(BLACK, 0.25, 5)]), ('ginger', 'a ginger cat', {'hue': 15, 'sat': 2.5, 'val': 1.2}, None),
                           ('siamese', 'a siamese cat', {'sat': 0.25, 'val': 1.35}, None), ('tuxedo', 'a tuxedo cat', {'sat': 0, 'val': 0.25}, pat_low(WHITE, 0.4))):
        look('cat', c, lbl, 1.0, tint=t, pattern=pat, village=True)
    look('cat', 'shipcat', "a ship's cat", 0.5, lands=COAST, build=lambda k, m, r: (k.ring_z('red_wool', 'head', 0.8, 1.0, pad=0.3), k.eyepatch()))
    look('ocelot', 'jungle', 'a dark ocelot', 1.0, tint={'val': 0.7, 'sat': 1.2})
    look('lion', 'darkmane', 'a dark-maned lion', 0.8, build=lambda k, m, r: k.mane('brown_wool', pad=1.6, frac=0.38))
    look('lion', 'white', 'a white lion', 0.2, tint={'sat': 0.15, 'val': 1.3})
    look('lioness', 'scarred', 'a scarred lioness', 1.0, build=lambda k, m, r: k.eyepatch())
    look('tiger', 'white', 'a white tiger', 0.25, tint={'sat': 0.05, 'val': 1.35})
    look('tiger', 'golden', 'a golden tiger', 0.3, tint={'hue': 15, 'sat': 0.6, 'val': 1.3})
    look('panther', 'jaguar', 'a jaguar', 1.0, tint={'hue': 30, 'sat': 4.0, 'val': 3.0, 'mix': [0.8, 0.55, 0.25, 0.4]}, pattern=pat_spots(BLACK, n=10, r=1.6))
    look('snow_leopard', 'dark', 'a dusky snow leopard', 0.6, tint={'val': 0.7})
    look('fox', 'silver', 'a silver fox', 0.6, tint={'sat': 0.1, 'val': 0.5})
    look('fox', 'cross', 'a cross fox', 0.8, tint={'hue': -5, 'sat': 0.9, 'val': 0.75}, pattern=pat_stripes((40, 30, 26), period=8, width=2))
    look('snow_fox', 'blue', 'a blue arctic fox', 0.6, tint={'hue': 200, 'sat': 0.4, 'val': 0.75})
    for c, lbl, t, pat in (('white', 'a white rabbit', {'sat': 0.05, 'val': 1.4}, None), ('black', 'a black rabbit', {'sat': 0.3, 'val': 0.35}, None),
                           ('gold', 'a golden rabbit', {'hue': 15, 'sat': 2.0, 'val': 1.3}, None), ('saltpepper', 'a salt-and-pepper rabbit', {'sat': 0.3}, pat_speckle(BLACK, 0.25)),
                           ('dutch', 'a dutch rabbit', {'sat': 0.05, 'val': 1.4}, pat_patches(BLACK, 0.45))):
        look('rabbit', c, lbl, 1.0, tint=t, pattern=pat, lands=(SNOW if c == 'white' else HOT if c == 'gold' else None))
    look('rabbit', 'jackalope', 'a jackalope', 0.06, lands=['desert', 'savanna', 'redlands'], build=lambda k, m, r: k.antlers('spruce_log_side', h=3.0), drops=[['rabbit_foot', 1, 1, 1.0]])
    look('raccoon', 'bandit', 'a bandit raccoon', 0.8, build=lambda k, m, r: k.back_pack('brown_wool', pname='body', w=1.6, h=0.5, d=1.6), drops=[['gold_nugget', 1, 2, 0.4], ['apple', 1, 1, 0.4]])
    look('raccoon', 'pale', 'a pale raccoon', 0.6, tint={'sat': 0.3, 'val': 1.25})
    look('mouse', 'white', 'a white mouse', 1.0, tint={'sat': 0.05, 'val': 1.45})
    look('mouse', 'harvest', 'a harvest mouse', 1.0, lands=FIELDS, tint={'hue': 15, 'sat': 1.8, 'val': 1.25}, held='wheat', drops=[['wheat_seeds', 1, 2, 0.6]])
    look('armadillo', 'rusty', 'a rusty armadillo', 1.0, tint={'hue': -5, 'sat': 1.5, 'val': 0.9})
    look('armadillo', 'pale', 'a pale armadillo', 0.8, tint={'sat': 0.4, 'val': 1.3})
    look('crocodile', 'albino', 'an albino crocodile', 0.15, tint={'sat': 0.1, 'val': 1.6})
    look('crocodile', 'mossback', 'a mossback crocodile', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'top', 4), ('head', 'top', 1)], r), 'moss_block', size=(2.0, 1.6), thick=0.6))
    look('turtle', 'mossy', 'a mossy-shelled turtle', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'top', 3)], r), 'moss_block', size=(2.2, 1.8), thick=0.5))
    look('turtle', 'barnacled', 'a barnacled turtle', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'top', 4)], r), 'prismarine', size=(0.9, 0.9), thick=0.6))
    look('frog', 'warm', 'a warm frog', 1.0, lands=HOT + ['jungle'], tint={'hue': 30, 'sat': 1.3, 'val': 1.2}, packTex=['entity/frog/warm_frog'])
    look('frog', 'cold', 'a cold frog', 1.0, lands=COLD, tint={'hue': -40, 'sat': 0.9}, packTex=['entity/frog/cold_frog'])
    look('frog', 'lilypad', 'a lily-hatted frog', 0.4, build=lambda k, m, r: k.add('lily', 'head', (0, P(m, 'head')['box'][4], 0), (-2.6, 0, -2.6, 2.6, 0.3, 2.6), 'lily_pad' if 'lily_pad' in TJ['names'] else 'jungle_leaves'))
    look('bat', 'fruit', 'a fruit bat', 1.0, tint={'hue': 15, 'sat': 1.5, 'val': 1.2})
    look('bat', 'pale', 'a pale cave bat', 1.0, cave=True, tint={'sat': 0.2, 'val': 1.5})
    look('axolotl', 'leucistic', 'a leucistic axolotl', 0.6, tint={'sat': 0.2, 'val': 1.3})
    look('parrot', 'macaw', 'a scarlet macaw', 1.0, pattern=[pat_low((40, 90, 200), 0.3)])
    look('duck', 'teal', 'a teal duck', 1.0, tint={'hue': 160, 'sat': 1.2})
    look('turkey', 'white', 'a white turkey', 1.0, tint={'sat': 0.1, 'val': 1.5})
    look('turkey', 'bronze', 'a bronze turkey', 1.0, tint={'mix': [0.4, 0.32, 0.2, 0.5]})
    look('adder', 'banded', 'a banded adder', 1.0, pattern=pat_stripes(BLACK, period=6, width=2))
    look('green_snake', 'emerald', 'an emerald snake', 1.0, tint={'hue': 10, 'sat': 1.4, 'val': 1.2})
    look('rattlesnake', 'desert', 'a pale rattlesnake', 1.0, tint={'sat': 0.5, 'val': 1.25})
    look('stone_mite', 'mossy', 'a mossy stone mite', 1.0, tint={'hue': 70, 'sat': 1.6})
    look('stone_mite', 'ore', 'an ore-flecked stone mite', 0.6, pattern=pat_speckle((210, 170, 120), 0.12), drops=[['iron_nugget', 1, 1, 0.3]])

    # ===== crawlers =====
    look('spider', 'broody', 'a spider with an egg sac', 0.8, build=lambda k, m, r: k.add('sac', 'abdomen', (0, 4, 7), (-3, 0, -3.5, 3, 4, 3.5), 'wool'), drops=[['string', 2, 4, 0.8]])
    look('spider', 'mossback', 'a mossback spider', 1.0, lands=WET + ['forest'], build=lambda k, m, r: (k.lumps(spots_on(m, [('abdomen', 'top', 4)], r), 'moss_block', size=(2.4, 2.0), thick=0.7), k.fans(spots_on(m, [('abdomen', 'top', 2)], r), 'fern', w=1.3, h=2.4)))
    look('spider', 'frost', 'a frost spider', 1.0, lands=SNOW, tint={'hue': 180, 'sat': 0.4, 'val': 1.5}, build=lambda k, m, r: k.spikes(spots_on(m, [('abdomen', 'top', 3)], r), 'ice', w=0.6, h=(1.5, 3.0)),
         drops=[['ice', 1, 1, 0.3]])
    look('spider', 'desert', 'a sand spider', 1.0, lands=['desert', 'redlands', 'savanna'], tint={'hue': 25, 'sat': 2.0, 'val': 2.2})
    look('spider', 'webbed', 'a web-weaver', 0.6, cave=True, build=lambda k, m, r: k.lumps(spots_on(m, [('abdomen', 'top', 3), ('head', 'top', 1)], r), 'cobweb', size=(2.0, 1.8), thick=0.3), drops=[['cobweb', 1, 1, 0.2]])
    look('cave_spider', 'crystal', 'a crystal cave spider', 0.7, cave=True, build=lambda k, m, r: k.spikes(spots_on(m, [('abdomen', 'top', 3)], r), 'amethyst_block', w=0.6, h=(1.2, 2.6)), drops=[['amethyst_shard', 1, 1, 0.3]])
    look('cave_spider', 'pale', 'a pale cave spider', 1.0, tint={'sat': 0.3, 'val': 1.5})
    look('scorpion', 'black', 'a black scorpion', 1.0, tint={'sat': 0.3, 'val': 0.35})
    look('scorpion', 'emperor', 'an emperor scorpion', 0.4, scale=1.2, tint={'hue': 200, 'sat': 0.6, 'val': 0.5})

    # ===== the water =====
    look('cod', 'dark', 'a dark cod', 1.0, tint={'val': 0.7})
    look('salmon', 'sockeye', 'a sockeye salmon', 1.0, tint={'hue': -15, 'sat': 2.0, 'val': 1.1})
    look('pufferfish', 'spiny', 'a spiny pufferfish', 1.0, tint={'hue': 30, 'sat': 0.8})
    look('dolphin', 'spotted', 'a spotted dolphin', 1.0, pattern=pat_speckle(WHITE, 0.08))
    look('shark', 'scarred', 'a scarred old shark', 0.6, scale=1.15, build=lambda k, m, r: k.arrows(spots_on(m, [('body', 'top', 1)], r), shaft='iron_block'), drops=[['iron_nugget', 1, 2, 0.5]])
    look('shark', 'pale', 'a great pale shark', 0.4, scale=1.25, tint={'sat': 0.2, 'val': 1.3})
    look('squid', 'red', 'a red squid', 0.6, tint={'hue': -20, 'sat': 2.0, 'val': 1.2})
    look('glow_squid', 'deep', 'a deep glow squid', 1.0, tint={'hue': 40})
    look('tropical_fish', 'yellow', 'a yellow tang', 1.0, tint={'hue': 50, 'sat': 1.3})

    # ===== slimes, wisps, the odd ones =====
    look('slime', 'skull', 'a slime with a swallowed skull', 0.5, build=lambda k, m, r: k.inside('bone_block_side', 'core' if has(m, 'core') else 'cube', frac=0.7), drops=[['bone', 1, 2, 0.8]])
    look('slime', 'nugget', 'a slime with a swallowed nugget', 0.3, build=lambda k, m, r: k.inside('gold_block', 'core' if has(m, 'core') else 'cube', frac=0.4), drops=[['gold_nugget', 1, 3, 0.9]])
    look('slime', 'mossy', 'a mossy slime', 1.0, lands=WET, build=lambda k, m, r: k.lumps(spots_on(m, [('cube', 'top', 2)], r), 'moss_block', size=(2.0, 1.6), thick=0.4))
    look('magma_slime', 'obsidian', 'an obsidian-crusted magma slime', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('cube', 'top', 2), ('cube', 'front', 1)], r), 'obsidian', size=(1.6, 1.2), thick=0.4))
    look('sulfur_slime', 'crystal', 'a crystalline sulfur slime', 1.0, build=lambda k, m, r: k.spikes(spots_on(m, [('cube', 'top', 3)], r), 'yellow_stained_glass' if 'yellow_stained_glass' in TJ['names'] else 'gold_block', w=0.6, h=(1.5, 3.0)))
    look('mimic', 'spruce', 'a spruce mimic', 1.0, tint={'hue': -10, 'val': 0.7})
    look('mimic', 'gilded', 'a gilded mimic', 0.3, tint={'hue': 20, 'sat': 2.0, 'val': 1.3}, drops=[['gold_ingot', 1, 2, 0.6]])
    look('ghast', 'weeping', 'a weeping ghast', 1.0, build=lambda k, m, r: [k.drape('light_blue_wool', 'body', 'front', h=4 + i, w=0.35, u=u) for i, u in enumerate((0.3, 0.7))])
    look('ghast', 'soul', 'a soul ghast', 0.8, tint={'hue': 180, 'sat': 0.5, 'val': 0.9})
    look('cinder_wisp', 'blue', 'a blue-flame wisp', 0.6, tint={'hue': 180})
    look('crystal_wisp', 'rose', 'a rose crystal wisp', 0.8, tint={'hue': 120})
    look('shulker', 'violet', 'a violet shulker', 1.0, tint={'hue': 30, 'sat': 1.3})
    look('shulker', 'pale', 'a pale shulker', 0.6, tint={'sat': 0.2, 'val': 1.3})
    look('zephyr', 'storm', 'a storm zephyr', 1.0, tint={'sat': 0.3, 'val': 0.6})
    look('aerwhale', 'humpback', 'a barnacled aerwhale', 1.0, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'top', 4), ('head', 'top', 2)], r), 'calcite', size=(1.4, 1.2), thick=0.6))
    look('aechor_plant', 'violet', 'a violet aechor plant', 1.0, tint={'hue': 60})
    look('aerbunny', 'cloudy', 'a cloud-fluffed aerbunny', 1.0, build=lambda k, m, r: k.coat('wool', 'body', pad=0.8, hang=0.6))
    look('flying_cow', 'golden', 'a golden-winged cow', 0.3, tint={'hue': 15, 'sat': 1.5, 'val': 1.2})
    look('phyg', 'storm', 'a storm phyg', 0.8, tint={'sat': 0.3, 'val': 0.7})
    look('sheepuff', 'golden', 'a golden-fleeced sheepuff', 0.2, edit=wool_edit('yellow_wool'), drops=[['gold_nugget', 1, 2, 0.6]])
    look('moa', 'blue', 'a blue moa', 1.0, tint={'hue': 200, 'sat': 1.2})
    look('moa', 'black', 'a black moa', 0.4, tint={'sat': 0.3, 'val': 0.35})
    look('swet', 'blue', 'a blue swet', 1.0, tint={'hue': 180})
    look('golden_swet', 'glitter', 'a glittering golden swet', 1.0, tint={'sat': 1.3, 'val': 1.15}, drops=[['gold_nugget', 1, 2, 0.5]])
    look('cockatrice', 'stone', 'a stone-crested cockatrice', 1.0, build=lambda k, m, r: k.crest('stone', h=2.6, n=4))
    look('spirit_wolf', 'pale', 'a pale spirit wolf', 1.0, tint={'sat': 0.3, 'val': 1.2})
    look('bee', 'pollen', 'a pollen-dusted bee', 1.0, tint={'hue': 10, 'sat': 0.8, 'val': 1.2})
    look('fuseling', 'ore', 'an ore-crusted fuseling', 1.0, cave=True, build=lambda k, m, r: k.lumps(spots_on(m, [('body', 'front', 3), ('head', 'top', 2), ('body', 'back', 2)], r), 'coal_ore', size=(1.4, 1.2), thick=0.5),
         drops=[['coal', 1, 3, 0.7]])
    look('fuseling', 'sprouting', 'a sprouting fuseling', 1.0, surface=True, lands=WOODS + FIELDS, build=lambda k, m, r: k.fans(spots_on(m, [('head', 'top', 3)], r), 'leaves', w=1.4, h=2.6))
    for f in ('beach', 'bog', 'dune', 'frost', 'gravel', 'jungle', 'meadow', 'moss', 'mushroom', 'redlands', 'savanna', 'swamp', 'ember', 'sky'):
        fk = f + '_fuseling'
        if fk not in MOBS: continue
        look(fk, 'elder', 'an elder ' + f + ' fuseling', 0.4, scale=1.2, build=lambda k, m, r: k.lumps(spots_on(m, [('head', 'top', 2), ('body', 'back', 2)], r), 'mossy_cobblestone', size=(1.4, 1.2), thick=0.5))
    # ===== two more looks for every land's fuseling: what its land has grown on it, or burnt, or bleached into it =====
    sp = lambda m, r, plan: spots_on(m, plan, r)
    TOP2 = [('head', 'top', 2), ('body', 'back', 2)]
    LAND = {
        'beach': [('barnacled', 'a barnacled beach fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'back', 3), ('head', 'top', 2), ('body', 'front', 2)]), 'bone_block_side', size=(1.3, 1.12), thick=0.85), None, [['bone_meal', 1, 2, 0.5]]),
                  ('kelp', 'a kelp-draped beach fuseling', lambda k, m, r: (k.drape('kelp', 'body', 'front', h=6, u=0.5, w=0.8), k.fans(sp(m, r, [('head', 'top', 2)]), 'seagrass', w=1.2, h=2.4)), None, [['kelp', 1, 2, 0.6]])],
        'bog': [('reedy', 'a reed-grown bog fuseling', lambda k, m, r: k.fans(sp(m, r, [('head', 'top', 3), ('body', 'back', 2)]), 'sugar_cane', w=1.0, h=3.4), None, [['sugar_cane', 1, 2, 0.5]]),
                ('mudcaked', 'a mud-caked bog fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 3), ('body', 'back', 2), ('head', 'top', 1)]), 'mud', size=(2.32, 1.82), thick=1.19), {'val': 0.85}, None)],
        'dune': [('spined', 'a cactus-spined dune fuseling', lambda k, m, r: k.spikes(sp(m, r, [('head', 'top', 3), ('body', 'back', 3)]), 'cactus_side', w=0.6, h=(2.7, 4.8)), None, [['cactus', 1, 1, 0.4]]),
                 ('bleached', 'a sun-bleached dune fuseling', lambda k, m, r: k.lumps(sp(m, r, TOP2), 'sand', size=(2.03, 1.4), thick=0.68), {'sat': 0.45, 'val': 1.18}, None)],
        'frost': [('icicled', 'an icicled frost fuseling', lambda k, m, r: k.spikes(sp(m, r, [('head', 'top', 2), ('body', 'back', 3), ('body', 'left', 1), ('body', 'right', 1)]), 'ice', w=0.6, h=(2.25, 4.5)), None, [['ice', 1, 1, 0.3]]),
                  ('snowcapped', 'a snow-capped frost fuseling', lambda k, m, r: k.lumps(sp(m, r, [('head', 'top', 3), ('body', 'back', 2)]), 'snow', size=(2.9, 2.52), thick=1.36), None, [['snowball', 1, 3, 0.7]])],
        'gravel': [('flinty', 'a flint-studded gravel fuseling', lambda k, m, r: k.spikes(sp(m, r, [('body', 'front', 2), ('body', 'back', 2), ('head', 'top', 2)]), 'flint', w=0.7, h=(1.8, 3.3)), None, [['flint', 1, 2, 0.6]]),
                   ('cobbled', 'a cobbled gravel fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 3), ('head', 'top', 2), ('body', 'back', 2)]), 'cobblestone', size=(1.89, 1.54), thick=1.02), None, None)],
        'jungle': [('vined', 'a vine-hung jungle fuseling', lambda k, m, r: (k.drape('vine', 'body', 'front', h=7, u=0.3), k.drape('vine', 'body', 'back', h=6, u=0.7)), None, [['vine', 1, 1, 0.4]]),
                   ('blooming', 'a blooming jungle fuseling', lambda k, m, r: k.fans(sp(m, r, [('head', 'top', 3)]), 'blue_orchid', w=1.3, h=2.2), None, [['blue_orchid', 1, 1, 0.5]])],
        'meadow': [('flowering', 'a flowering meadow fuseling', lambda k, m, r: (k.fans(sp(m, r, [('head', 'top', 2)]), 'cornflower', w=1.2, h=2.0), k.fans(sp(m, r, [('body', 'back', 2)]), 'red_tulip', w=1.2, h=2.0)), None, [['cornflower', 1, 1, 0.4]]),
                   ('hayback', 'a hay-backed meadow fuseling', lambda k, m, r: k.lumps(sp(m, r, TOP2), 'hay_side', size=(2.61, 1.96), thick=1.02), None, [['wheat', 1, 2, 0.5]])],
        'moss': [('shroomed', 'a toadstool-sprouting moss fuseling', lambda k, m, r: k.shrooms(sp(m, r, [('head', 'top', 2), ('body', 'back', 2)]), s=0.8), None, [['red_mushroom', 1, 1, 0.5]]),
                 ('ferny', 'a fern-tufted moss fuseling', lambda k, m, r: k.fans(sp(m, r, [('head', 'top', 3), ('body', 'back', 2)]), 'fern', w=1.4, h=2.6), None, None)],
        'mushroom': [('toadstool', 'a great-capped mushroom fuseling', lambda k, m, r: k.shrooms(sp(m, r, [('head', 'top', 1)]), s=1.6), None, [['red_mushroom', 1, 2, 0.6]]),
                     ('brownspore', 'a brown-spored mushroom fuseling', lambda k, m, r: k.shrooms(sp(m, r, [('head', 'top', 2), ('body', 'back', 2)]), cap='brown_mushroom_block', s=0.9), {'hue': -12, 'val': 0.9}, [['brown_mushroom', 1, 2, 0.6]])],
        'redlands': [('goldseamed', 'a gold-seamed redlands fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 3), ('body', 'back', 2), ('head', 'top', 1)]), 'gold_ore', size=(1.74, 1.4), thick=0.85), None, [['gold_nugget', 1, 3, 0.7]]),
                     ('terraced', 'a terraced redlands fuseling', lambda k, m, r: k.lumps(sp(m, r, TOP2), 'terracotta', size=(2.9, 1.26), thick=1.02), {'hue': 8, 'sat': 1.15}, None)],
        'savanna': [('thorned', 'a thorn-backed savanna fuseling', lambda k, m, r: k.spikes(sp(m, r, [('body', 'back', 3), ('head', 'top', 2)]), 'acacia_log_side', w=0.5, h=(2.4, 4.2)), None, [['stick', 1, 2, 0.5]]),
                    ('scrubby', 'a scrub-tufted savanna fuseling', lambda k, m, r: k.fans(sp(m, r, [('head', 'top', 2), ('body', 'back', 2)]), 'dead_bush', w=1.4, h=2.2), {'hue': 6, 'val': 0.92}, None)],
        'swamp': [('lilybacked', 'a lily-backed swamp fuseling', lambda k, m, r: k.lumps(sp(m, r, [('head', 'top', 1), ('body', 'back', 2)]), 'lily_pad', size=(3.19, 3.08), thick=0.15), None, [['lily_pad', 1, 1, 0.4]]),
                  ('slimed', 'a slime-streaked swamp fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 2), ('body', 'back', 2), ('head', 'top', 1)]), 'slime_block', size=(1.89, 1.54), thick=0.85), {'hue': 10, 'sat': 1.2}, [['slime_ball', 1, 2, 0.6]])],
        'ember': [('cindercracked', 'a cinder-cracked ember fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 3), ('body', 'back', 2), ('head', 'top', 2)]), 'magma_block', size=(1.74, 1.4), thick=0.68, glow=True), None, [['magma_cream', 1, 1, 0.4]]),
                  ('obsidian', 'an obsidian-plated ember fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 2), ('body', 'back', 3), ('head', 'top', 1)]), 'obsidian', size=(2.61, 1.96), thick=0.85), {'val': 0.8}, [['obsidian', 1, 1, 0.25]])],
        'sky': [('cloudwrapped', 'a cloud-wrapped sky fuseling', lambda k, m, r: k.lumps(sp(m, r, [('body', 'front', 2), ('body', 'back', 3), ('head', 'top', 2)]), 'cloud_puff_block', size=(2.9, 2.24), thick=1.19), None, None),
                ('stormcrest', 'a storm-crested sky fuseling', lambda k, m, r: k.crest('cloud_side', h=2.8, n=4), {'hue': 200, 'sat': 0.7}, [['feather', 1, 2, 0.6]])],
    }
    for f_, looks in LAND.items():
        fk = f_ + '_fuseling'
        if fk not in MOBS: continue
        for suf, label, build, tint, drops in looks:
            look(fk, suf, label, 0.5, tint=tint, build=build, drops=drops)
    for dk in ('bog_drake', 'ember_drake', 'ender_drake', 'frost_drake', 'sand_drake', 'storm_drake', 'tide_drake', 'verdant_drake'):
        look(dk, 'scarred', 'a scarred ' + dk.replace('_', ' '), 0.6, build=lambda k, m, r: k.arrows(spots_on(m, [('body', 'top', 2), ('body', 'left', 1)], r)), drops=[['arrow', 1, 3, 0.6]])


def main(d):
    global D, TJ, MODELS, MOBS
    D = pathlib.Path(d)
    TJ = json.loads((D / 'tiles.json').read_text())
    TJ['res'] += [32] * max(0, TJ['count'] - len(TJ['res']))
    models = json.loads((D / 'models.json').read_text()); MODELS = models.get('models', models)
    MOBS = json.loads((D / 'mobs.json').read_text())
    (D / 'textures' / 'entity').mkdir(parents=True, exist_ok=True)
    DEFS.clear()
    catalogue()
    n = 0
    for mob, defs in DEFS.items():
        v = MOBS[mob]
        bk = base_model_key(mob)
        base = [{'model': k, 'w': 1.0} for k in (v.get('variants') or [bk])]   # its own coats, as before
        tot = sum(x['w'] for x in defs)
        bw = max(2.0, tot * 1.2) / len(base)   # the plain look stays the most common
        for b in base: b['w'] = round(bw, 3)
        v['variantDefs'] = base + defs
        n += len(defs)
    (D / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (D / 'mobs.json').write_text(json.dumps(MOBS, separators=(',', ':'), ensure_ascii=False))
    print('mob variants ok: %d looks for %d creatures' % (n, len(DEFS)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
