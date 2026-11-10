#!/usr/bin/env python3
"""tools/port/armor_art.py — worn armour in the style of the user's pack, for every material the game has.

The active pack (DokuQuest Base) has no armour layers; the GeruDoku pack beside it in resourcepacks/ does (iron, gold,
diamond, netherite, chainmail, leather). Those are written into the local overlay pack "Mod Art (local)" as they are, and
every one of the game's own materials (copper, bronze, silver, steel, mithril, adamantite, emerald, and the Aether's and
Better End's zanite, gravitite, thallasium) gets the iron layers (the diamond's for gems) with their metal recoloured to
the material's own colour, read off its ingot or gem picture, so the shading and the edges are the pack's.
The overlay is the user's own and is never committed. assets/pack_armor.cpp reads it after the chosen pack.

  python3 tools/port/armor_art.py [data dir]
"""
import colorsys, io, json, pathlib, sys, zipfile
import numpy as np
from PIL import Image

HOME = pathlib.Path.home() / '.local/share/DokuQuest/resourcepacks'
OUT = HOME / 'Mod Art (local)/assets/minecraft/textures/models/armor'
VANILLA = ['iron', 'gold', 'diamond', 'netherite', 'chainmail', 'leather']
CUSTOM = {   # material: (picture to take its colour from, gem?, colour if there's no picture)
    'copper': ('copper_ingot', False, (214, 124, 82)), 'bronze': ('bronze_ingot', False, (196, 140, 70)),
    'silver': ('silver_ingot', False, (226, 230, 238)), 'steel': ('steel_ingot', False, (150, 160, 178)),
    'mithril': ('mithril_ingot', False, (150, 214, 240)), 'adamantite': ('adamantite_ingot', False, (90, 170, 110)),
    'emerald': ('emerald', True, (60, 200, 110)), 'zanite': ('zanite_gemstone', True, (170, 118, 236)),
    'gravitite': ('gravitite_plate', False, (232, 120, 214)), 'thallasium': ('thallasium_ingot', False, (92, 204, 194)),
}


def donor_pack():
    """the first pack in resourcepacks/ (zip or folder) with iron armour layers: {name: Image}"""
    for p in sorted(HOME.iterdir()):
        files = {}
        try:
            if p.suffix == '.zip':
                z = zipfile.ZipFile(p)
                for n in z.namelist():
                    if '/textures/models/armor/' in n and n.endswith('.png'): files[n.rsplit('/', 1)[1]] = lambda n=n, z=z: Image.open(io.BytesIO(z.read(n)))
            elif p.is_dir() and p.name != 'Mod Art (local)':
                for f in p.glob('assets/minecraft/textures/models/armor/*.png'): files[f.name] = lambda f=f: Image.open(f)
        except Exception:
            continue
        if 'iron_layer_1.png' in files: return p.name, files
    return None, {}


def colour_of(d, item, fallback):
    """the material's colour: the bright half of its picture (the overlay's, else the game's own), a little richer"""
    tj = json.loads((d / 'tiles.json').read_text())
    cands = [HOME / 'Mod Art (local)/assets/minecraft/textures/item' / (item + '.png'), d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/item' / (item + '.png')]
    if item in tj['names']: cands.append(d / tj['files'][tj['names'][item]])
    for c in cands:
        if c.exists():
            a = np.asarray(Image.open(c).convert('RGBA')).astype(np.float32)
            px = a[a[..., 3] > 128][:, :3]
            if len(px) < 8: continue
            lum = px @ np.array([0.299, 0.587, 0.114])
            px = px[lum >= np.median(lum)]
            r, g, b = px.mean(0) / 255
            h, l, s = colorsys.rgb_to_hls(r, g, b)
            return tuple(int(v * 255) for v in colorsys.hls_to_rgb(h, l, min(1, s * 1.15)))
    return fallback


def recolour(img, rgb):
    """the donor's light and shade on the material's colour ramp (every opaque pixel: armour is all metal)"""
    a = np.asarray(img.convert('RGBA')).astype(np.float32)
    m = a[..., 3] > 8
    if not m.any(): return img
    lum = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    lo, hi = np.percentile(lum[m], 2), np.percentile(lum[m], 98)
    t = np.clip((lum - lo) / max(1, hi - lo), 0, 1)
    h, l, s = colorsys.rgb_to_hls(*[c / 255 for c in rgb])
    stops = np.array([[c * 255 for c in colorsys.hls_to_rgb(h, L, min(1, s * k))] for L, k in ((0.14, 0.8), (0.3, 0.9), (0.48, 0.85), (0.66, 0.75), (0.85, 0.5))], np.float32)
    x = t * 4; i0 = np.clip(np.floor(x).astype(int), 0, 3); f = (x - i0)[..., None]
    a[..., :3] = np.where(m[..., None], stops[i0] * (1 - f) + stops[i0 + 1] * f, a[..., :3])
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')


def main(d):
    d = pathlib.Path(d)
    src, files = donor_pack()
    if not files: print('armor_art: no pack in resourcepacks/ has armour layers - skipped'); return
    OUT.mkdir(parents=True, exist_ok=True)
    n = 0
    for name in VANILLA:
        for layer in (1, 2):
            for suf in ('', '_overlay'):
                fn = '%s_layer_%d%s.png' % (name, layer, suf)
                if fn in files: files[fn]().save(OUT / fn); n += 1
    for mat, (item, gem, fb) in CUSTOM.items():
        rgb = colour_of(d, item, fb)
        base = 'diamond' if gem and 'diamond_layer_1.png' in files else 'iron'
        for layer in (1, 2):
            fn = '%s_layer_%d.png' % (base, layer)
            if fn in files: recolour(files[fn](), rgb).save(OUT / ('%s_layer_%d.png' % (mat, layer))); n += 1
    print('armor_art: %d armour layers into the overlay from %s' % (n, src))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
