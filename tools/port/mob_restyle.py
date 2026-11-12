#!/usr/bin/env python3
"""tools/port/mob_restyle.py — the art pass on creature skins (the user's list, 2026-11-01: "an art pass on mob skins and
textures that clash, making sure to only work on the ones not done by me from my pack").

Only the creatures whose skins are OURS are touched: the ones the DokuQuest Base pack has no skin for (its 19 creatures
keep the user's art, and nothing in the pack is read for writing). What clashed: ours were much brighter and sweeter
than the user's (mean light 139 against the pack's 104, saturation 0.38 against 0.22) and flat or speckled where the
pack's are painted. So each of our pictures keeps its own colours and markings (spots, stripes, bellies, faces) and is
brought halfway to the pack's light (only ever darker) and colourfulness, its speckle smoothed, with a soft mottle.
(MOB_RESTYLE_GRAIN=0.6 also borrows the fine grain of a pack skin of the same kind, but it stamped the donors' motifs.)

Re-runnable: the originals are kept in tools/port/mob_restyle_src/ the first time, and every run starts from them.
Hooked into data_patches.py before the creature looks (mob_variants.py makes the looks from these).
  python3 tools/port/mob_restyle.py data            # write
  python3 tools/port/mob_restyle.py data --sheet x  # also a before | after sheet for checking
"""
import json, os, re, shutil, sys
import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get('MOB_RESTYLE_SRC') or os.path.join(HERE, 'mob_restyle_src')
PACK_ENTITY = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/entity/'

# the creatures the pack itself skins (render/creature_models.cpp's pack map, where the pack has the picture): not ours
PACK_SKINNED = {'cave_spider', 'chicken', 'cinder_wisp', 'drowned', 'enderman', 'fuseling', 'ghast', 'ghoul', 'husk',
                'iron_golem', 'raider', 'sheep', 'skeleton', 'slime', 'spider', 'stone_mite', 'stray', 'witch', 'wolf'}

# what each kind of picture borrows its grain from: (pack picture, material)
DONORS = {
    'fur': 'wolf/wolf', 'feather': 'chicken', 'skin': 'zombie/zombie', 'dry': 'zombie/husk', 'cloth': 'illager/vindicator',
    'robe': 'witch', 'gel': 'slime/slime', 'bone': 'skeleton/skeleton', 'rock': 'iron_golem/iron_golem', 'chitin': 'spider/spider',
}
RULES = [   # (name pattern, material); first match wins
    (r'bone|skel|antler|horn|tooth|teeth|claw|spine|sting', 'bone'),
    (r'feather|wing|plume|fan|tail_fan|crest', 'feather'),
    (r'fur|mane|wool|pelt|hair|coat|tail', 'fur'),
    (r'robe|hood|trim|cloak|cape|herb', 'robe'),
    (r'cloth|rags|leather|pants|belt|shoe|wrap|saddle|shirt', 'cloth'),
    (r'gel|slime|core|swet|jelly|crust', 'gel'),
    (r'shell|scorp|mite|chitin|carapace|back', 'chitin'),
    (r'rock|golem|stone|iron', 'rock'),
    (r'husk|mummy|ghoul|zpig|dry|sand', 'dry'),
    (r'skin|hide|flesh|snout|face|scale|fin|bill|beak|ear|leg|hoof|wattle|tongue|body', 'skin'),
]
GRAIN = float(os.environ.get('MOB_RESTYLE_GRAIN', '0'))   # the donors' grain: off (it stamped their motifs on ours)
SKIP = re.compile(r'^(dscale|dwing|dbone|deye|dhorn)_|_eyes?$|^white$|^hb_|crystal|^cloud_puff$|^zephyr|wisp|warden_souls|_glow')


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def donor_patch(img):
    """the best patch of one material in a pack skin: all opaque, one colour family, an even, middling grain (32x32, or
    16x16 mirrored to 32 where the skin has no bigger one-piece area)"""
    a = np.asarray(img.convert('RGBA')).astype(np.float32)
    H, W = a.shape[:2]
    for n in (32, 16):
        best, bs = None, -1e9
        for y in range(0, H - n + 1, 2):
            for x in range(0, W - n + 1, 2):
                p = a[y:y + n, x:x + n]
                if p[..., 3].min() < 250: continue
                l = lum(p)
                if l.mean() < 30: continue
                chroma = p[..., :3] - l[..., None]
                li = Image.fromarray(np.clip(l, 0, 255).astype(np.uint8))
                b1 = np.asarray(li.filter(ImageFilter.GaussianBlur(1.2))).astype(np.float32)
                b3 = np.asarray(li.filter(ImageFilter.GaussianBlur(3))).astype(np.float32)
                hf, lf = (l - b1).std(), b3.std()   # fine grain wanted; shapes and motifs (low frequencies) not
                sc = -abs(hf - 11) - lf * 1.2 - np.abs(chroma - chroma.mean(axis=(0, 1))).mean() * 0.5 - abs(l.mean() - 120) * 0.03
                if sc > bs: bs, best = sc, p
        if best is not None:
            if n == 16:
                top = np.concatenate([best, best[:, ::-1]], axis=1)
                best = np.concatenate([top, top[::-1]], axis=0)
            return best
    return None


def detail_of(patch):
    """the grain: luminance over its own blur (1 = flat), as a 32x32 map"""
    l = lum(patch)
    im = Image.fromarray(np.clip(l, 0, 255).astype(np.uint8))
    bl = np.asarray(im.filter(ImageFilter.GaussianBlur(1.2))).astype(np.float32)
    return np.clip(l / np.maximum(bl, 8), 0.7, 1.3)


def desperckle(a):
    """smooth speckle (pixels that differ only a little from their neighbourhood's median); markings stay"""
    out = a.copy()
    H, W = a.shape[:2]
    pad = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode='wrap')
    stack = np.stack([pad[dy:dy + H, dx:dx + W] for dy in range(3) for dx in range(3)])
    med = np.median(stack[..., :3], axis=0)
    diff = np.abs(lum(a[..., :3]) - lum(med))
    m = (diff < 38) & (a[..., 3] > 0)
    k = np.clip(1 - diff / 38, 0, 1)[..., None] * 0.85
    out[..., :3] = np.where(m[..., None], a[..., :3] * (1 - k) + med * k, a[..., :3])
    return out


PACK_L, PACK_S = 104.0, 0.22   # the user's creature skins: mean light and saturation (ours were 139 and 0.38: too bright, too sweet)


def mottle(h, w, seed):
    """soft value noise (two octaves) for a painted, uneven look: about 1 +- 0.05"""
    rnd = np.random.RandomState(seed)
    out = np.zeros((h, w), np.float32)
    for cell, amp in ((8, 0.6), (4, 0.4)):
        g = rnd.rand(h // cell + 2, w // cell + 2).astype(np.float32)
        im = Image.fromarray((g * 255).astype(np.uint8)).resize(((w // cell + 2) * cell, (h // cell + 2) * cell), Image.BICUBIC)
        out += (np.asarray(im).astype(np.float32)[:h, :w] / 255 - 0.5) * amp
    return 1 + out * 0.1


def restyle(a, det, strength, deepen, seed=0):
    out = desperckle(a)
    f = 1 + (det - 1) * strength
    rgb = out[..., :3] * f[..., None] * deepen
    m = out[..., 3] > 0
    if m.sum() > 8:
        l = lum(rgb)
        ml = l[m].mean()
        # halfway to the pack's light, only ever darker (a black panther stays black)
        k = min(1.0, (ml * 0.55 + PACK_L * 0.45) / max(ml, 1))
        rgb = rgb * k
        mx, mn = rgb.max(-1), rgb.min(-1)
        sat = ((mx - mn) / np.maximum(mx, 1))[m].mean()
        ks = min(1.0, (sat * 0.5 + PACK_S * 0.5) / max(sat, 1e-3))   # and halfway to its colourfulness
        l = lum(rgb)[..., None]
        rgb = l + (rgb - l) * ks
        rgb = rgb * mottle(rgb.shape[0], rgb.shape[1], seed)[..., None]
    out[..., :3] = np.clip(rgb, 0, 255)
    return out


def main(d, sheet=None):
    tj = json.load(open(os.path.join(d, 'tiles.json')))
    names, files = tj['names'], tj['files']
    mobs = json.load(open(os.path.join(d, 'mobs.json')))
    models = json.load(open(os.path.join(d, 'models.json'))); M = models.get('models', models)
    # our creatures' pictures
    want = set()
    def texnames(t):
        if isinstance(t, str): return [t]
        if isinstance(t, dict): return [v for v in t.values() if isinstance(v, str)]
        return []
    for name, mb in mobs.items():
        mk = mb.get('model', name)
        if mk in PACK_SKINNED or name in PACK_SKINNED: continue
        m = M.get(mk)
        if not m or m.get('packBase') in PACK_SKINNED: continue
        for p in m['parts']:
            for t in texnames(p.get('tex')): want.add(t)
    donors = {}
    for mat, f in DONORS.items():
        p = os.path.join(d, PACK_ENTITY + f + '.png')
        if not os.path.exists(p): continue
        im = Image.open(p)
        patch = donor_patch(im)
        if patch is not None: donors[mat] = detail_of(patch)
    os.makedirs(SRC, exist_ok=True)
    done, pairs = [], []
    for t in sorted(want):
        i = names.get(t)
        if i is None or SKIP.search(t): continue
        rel = files[i]
        if not rel.startswith('textures/entity/'): continue   # block pictures (golems, fuselings) are the pack's own already
        path = os.path.join(d, rel)
        keep = os.path.join(SRC, os.path.basename(rel))
        if not os.path.exists(keep):
            if not os.path.exists(path): continue
            shutil.copyfile(path, keep)
        mat = next((m for rx, m in RULES if re.search(rx, t)), 'skin')
        det = donors.get(mat)
        if det is None: continue
        a = np.asarray(Image.open(keep).convert('RGBA')).astype(np.float32)
        h, w = a.shape[:2]
        dd = det if (h, w) == (32, 32) else np.asarray(Image.fromarray(((det - 0.7) / 0.6 * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR)).astype(np.float32) / 255 * 0.6 + 0.7
        face = bool(re.search(r'face|eye|snout|bill|beak', t))
        out = restyle(a, dd, GRAIN * (0.5 if face else 1.0), 1.0, seed=sum(map(ord, t)))
        Image.fromarray(out.astype(np.uint8)).save(path)
        done.append((t, mat)); pairs.append((t, keep, path))
    print('mob restyle: %d of our creature pictures brought into the base pack\'s light and colour' % len(done))
    if sheet:
        S, cols = 66, 6
        rows = (len(pairs) + cols - 1) // cols
        sh = Image.new('RGB', (cols * S * 2 + cols * 4, rows * S), (40, 40, 40))
        for k, (t, a, b) in enumerate(pairs):
            x, y = (k % cols) * (S * 2 + 4), (k // cols) * S
            for j, p in enumerate((a, b)):
                im = Image.open(p).convert('RGBA').resize((64, 64), Image.NEAREST)
                sh.paste(im, (x + j * S, y), im)
        sh.save(sheet)


if __name__ == '__main__':
    args = sys.argv[1:]
    sheet = None
    if '--sheet' in args: sheet = args[args.index('--sheet') + 1]; args = [a for a in args if a not in ('--sheet', sheet)]
    main(args[0] if args else 'data', sheet)
