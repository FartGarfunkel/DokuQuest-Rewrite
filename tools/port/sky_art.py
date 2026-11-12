#!/usr/bin/env python3
# tools/port/sky_art.py — pictures for the Skylands, the cloud banks and the new flowers, made from the DokuQuest Base
# pack's OWN pictures (the user's art) so they sit in the same style: each one takes a pack picture of the nearest
# thing (stone for skystone, birch for sunwood, powder snow for clouds, lily of the valley for bluebells...) and
# recolours it, keeping every pixel's light and shade and the picture's shape. Written into the pack folder; the
# list goes to tools/port/sky_art_manifest.json. Run: python3 tools/port/sky_art.py data
import colorsys, json, pathlib, sys
from PIL import Image

PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'


def load(d, name):
    im = Image.open(d / PACK / (name + '.png')).convert('RGBA')
    return im.crop((0, 0, im.width, im.width)).resize((32, 32), Image.NEAREST) if im.width != 32 or im.height != 32 else im


def lum(r, g, b): return (r * 299 + g * 587 + b * 114) / 1000


def tint(im, rgb, mask=None, keep=0.0, gamma=1.0):
    """recolour to rgb by each pixel's brightness (relative to the picture's mean), mask(r,g,b,a) picks the pixels"""
    im = im.copy(); px = im.load()
    ls = [lum(*px[x, y][:3]) for y in range(im.height) for x in range(im.width) if px[x, y][3] > 8 and (mask is None or mask(*px[x, y]))]
    mean = (sum(ls) / len(ls)) if ls else 128
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a <= 8 or (mask and not mask(r, g, b, a)): continue
            k = (lum(r, g, b) / (mean or 1)) ** gamma
            nr, ng, nb = (min(255, int(c * k)) for c in rgb)
            px[x, y] = (int(nr * (1 - keep) + r * keep), int(ng * (1 - keep) + g * keep), int(nb * (1 - keep) + b * keep), a)
    return im


def greenish(r, g, b, a):
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return 0.15 < h < 0.48 and s > 0.18


def whiteish(r, g, b, a): return min(r, g, b) > 150 and max(r, g, b) - min(r, g, b) < 60


def petals(r, g, b, a):   # the bright, unsaturated or yellow-white petal pixels (not the green stem)
    return not greenish(r, g, b, a)


def main(d):
    d = pathlib.Path(d); out = {}

    def save(name, im, donor):
        im.save(d / PACK / (name + '.png')); out[name] = donor

    # the Skylands
    save('sky_grass_top', tint(load(d, 'grass_block_top'), (150, 226, 196)), 'grass_block_top')
    save('sky_grass_side', tint(load(d, 'grass_block_side'), (140, 214, 186), mask=greenish), 'grass_block_side')
    sky = (196, 206, 222)   # skystone: a pale, cool blue-grey (holystone-like)
    save('skystone', tint(load(d, 'stone'), sky, gamma=0.9), 'stone')
    save('skystone_bricks', tint(load(d, 'stone_bricks'), sky, gamma=0.9), 'stone_bricks')
    save('sunwood_log', tint(load(d, 'birch_log'), (238, 214, 150), keep=0.15), 'birch_log')
    save('sunwood_leaves', tint(load(d, 'oak_leaves'), (236, 196, 72)), 'oak_leaves')
    save('cloud_leaves', tint(load(d, 'oak_leaves'), (232, 240, 250)), 'oak_leaves')
    save('glimmersand', glimmer(tint(load(d, 'sand'), (240, 232, 206))), 'sand')
    save('sky_bloom', tint(load(d, 'allium'), (150, 200, 255), mask=petals), 'allium')
    ore = load(d, 'diamond_ore'); op = ore.load()
    gem = lambda r, g, b, a: colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)[1] > 0.25
    ore = tint(ore, sky, mask=lambda r, g, b, a: not gem(r, g, b, a), gamma=0.9)
    save('aether_crystal_ore', tint(ore, (170, 236, 255), mask=gem), 'diamond_ore')
    # the cloud banks: powder snow's soft speckle, white, sky blue (bounce) and gold (float down)
    save('cloud_puff_block', tint(load(d, 'powder_snow'), (246, 249, 255), gamma=0.6), 'powder_snow')
    save('bounce_cloud', tint(load(d, 'powder_snow'), (150, 200, 250), gamma=0.7), 'powder_snow')
    save('sun_cloud', tint(load(d, 'powder_snow'), (252, 222, 130), gamma=0.7), 'powder_snow')
    # the new flowers and plants
    save('bluebells', tint(load(d, 'lily_of_the_valley'), (96, 120, 236), mask=petals), 'lily_of_the_valley')
    save('marigold', tint(load(d, 'oxeye_daisy'), (240, 140, 30), mask=lambda r, g, b, a: whiteish(r, g, b, a)), 'oxeye_daisy')
    save('spinifex', tint(load(d, 'short_grass'), (198, 170, 92)), 'short_grass')
    # the pack map must list each of them (only listed tiles take pack pictures); skystone looks for its own first
    pm = json.loads((d / 'packmap.json').read_text())
    have = {e[0]: e for e in pm}
    for n in out:
        if n in have:
            e = have[n]
            if e[1][0] != n: e[1] = [n] + [x for x in e[1] if x != n]
        else: pm.append([n, [n], [n]])
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':')))
    (d.parent / 'tools' / 'port' / 'sky_art_manifest.json').write_text(json.dumps(out, indent=1)) if (d.parent / 'tools').exists() else None
    print('sky art:', len(out), 'pictures')


def glimmer(im):   # a few bright glints in the sand
    px = im.load()
    for i, (x, y) in enumerate(((3, 5), (11, 2), (20, 9), (27, 4), (6, 18), (15, 14), (24, 22), (9, 27), (29, 28), (18, 25))):
        px[x, y] = (255, 255, 236, 255)
        if i % 2 == 0 and x + 1 < 32: px[x + 1, y] = (250, 244, 214, 255)
    return im


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
