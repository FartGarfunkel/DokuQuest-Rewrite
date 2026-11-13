#!/usr/bin/env python3
"""tools/port/soulrend_worldgen/ns_blocks.py — Nature's Spirit's plants and ground, as blocks of ours (idempotent).

Soulrend's biomes grow Nature's Spirit's flowers, grasses, succulents and water plants, and its surface rules lay its
ground (chert, the kaolins, travertine, white chalk, sandy soil, pink sand and sandstone, red moss). Each becomes a block
of the game's own, cloned from the nearest block we have (a flower, a tall grass, a lily pad, sand...):

  - its built-in picture (data/textures/block, the game's own art) is that block's picture recoloured to the mod's
    colours, so no mod art is committed;
  - the mod's own picture goes into the git-excluded DokuQuest Base pack (padded to a square where the mod's is a tall
    one), recorded in its soulrend_import.json like soulrend_import.py's, and named in data/packmap.json.

  python3 tools/port/soulrend_worldgen/ns_blocks.py [data] [unpacked mods dir]
"""
import colorsys, io, json, os, pathlib, sys
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[3]
DATA = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'data'
MODS = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path.home() / 'soulrend_extract/mods'
PACK = DATA / 'resourcepacks/DokuQuest Base'

# key: (kind, its pictures in the mod (lower[, upper] / top, side), tinted by the grass?)
#   kinds: flower, grass (single cross); flower2, grass2 (two blocks tall); pad (on the water); cube, column, sand,
#   sandstone, soil, moss; stem
PLANTS = {
    'anemone': ('flower', ['anemone']), 'black_iris': ('flower', ['black_iris']), 'blue_iris': ('flower', ['blue_iris']),
    'bluebell': ('flower', ['bluebell']), 'dwarf_blossoms': ('flower', ['dwarf_blossoms']), 'hibiscus': ('flower', ['hibiscus']),
    'marigold': ('flower', ['marigold']), 'protea': ('flower', ['protea_flower']), 'ruby_blossoms': ('flower', ['ruby_blossoms']),
    'tiger_lily': ('flower', ['tiger_lily']), 'purple_wildflower': ('flower', ['purple_wildflower']), 'yellow_wildflower': ('flower', ['yellow_wildflower']),
    'white_heather': ('flower', ['white_heather']), 'red_heather': ('flower', ['red_heather']), 'purple_heather': ('flower', ['purple_heather']),
    'red_bearberries': ('flower', ['red_bearberries']), 'green_bearberries': ('flower', ['green_bearberries']), 'purple_bearberries': ('flower', ['purple_bearberries']),
    'red_bitter_sprouts': ('flower', ['red_bitter_sprouts']), 'green_bitter_sprouts': ('flower', ['green_bitter_sprouts']), 'purple_bitter_sprouts': ('flower', ['purple_bitter_sprouts']),
    'aureate_succulent': ('flower', ['aureate_succulent']), 'drowsy_succulent': ('flower', ['drowsy_succulent']), 'foamy_succulent': ('flower', ['foamy_succulent']),
    'imperial_succulent': ('flower', ['imperial_succulent']), 'ornate_succulent': ('flower', ['ornate_succulent']), 'regal_succulent': ('flower', ['regal_succulent']),
    'sage_succulent': ('flower', ['sage_succulent']),
    'beach_grass': ('grass', ['beach_grass']), 'frigid_grass': ('grass', ['frigid_grass']), 'oat_grass': ('grass', ['oat_grass']),
    'scorched_grass': ('grass', ['scorched_grass']), 'sedge_grass': ('grass', ['sedge_grass']), 'flaxen_fern': ('grass', ['flaxen_fern']),
    'melic_grass': ('grass', ['melic_grass'], True), 'lush_fern': ('grass', ['lush_fern'], True),
    'blue_bulbs': ('flower2', ['blue_bulbs_bottom', 'blue_bulbs_top']), 'carnation': ('flower2', ['carnation_bottom', 'carnation_top']),
    'foxglove': ('flower2', ['foxglove_bottom', 'foxglove_top']), 'gardenia': ('flower2', ['gardenia_bottom', 'gardenia_top']),
    'snapdragon': ('flower2', ['snapdragon_bottom', 'snapdragon_top']), 'silverbush': ('flower2', ['silverbush_bottom', 'silverbush_top']),
    'tall_beach_grass': ('grass2', ['tall_beach_grass_bottom', 'tall_beach_grass_top']), 'tall_frigid_grass': ('grass2', ['tall_frigid_grass_bottom', 'tall_frigid_grass_top']),
    'tall_oat_grass': ('grass2', ['tall_oat_grass_bottom', 'tall_oat_grass_top']), 'tall_scorched_grass': ('grass2', ['tall_scorched_grass_bottom', 'tall_scorched_grass_top']),
    'tall_sedge_grass': ('grass2', ['tall_sedge_grass_bottom', 'tall_sedge_grass_top']), 'large_flaxen_fern': ('grass2', ['large_flaxen_fern_bottom', 'large_flaxen_fern_top']),
    'tall_melic_grass': ('grass2', ['tall_melic_grass_bottom', 'tall_melic_grass_top'], True), 'large_lush_fern': ('grass2', ['large_lush_fern_bottom', 'large_lush_fern_top'], True),
    'azolla': ('pad', ['azolla']), 'helvola': ('pad', ['helvola_pad']),
    'desert_turnip_stem': ('stem', ['desert_turnip_stem_stage7']),
}
GROUND = {
    'chert': ('column', ['chert_top', 'chert'], 'stone'), 'travertine': ('cube', ['travertine'], 'calcite'), 'white_chalk': ('cube', ['white_chalk'], 'calcite'),
    'kaolin': ('cube', ['kaolin'], 'terracotta'), 'white_kaolin': ('cube', ['white_kaolin'], 'terracotta'), 'light_gray_kaolin': ('cube', ['light_gray_kaolin'], 'terracotta'),
    'brown_kaolin': ('cube', ['brown_kaolin'], 'terracotta'), 'yellow_kaolin': ('cube', ['yellow_kaolin'], 'terracotta'),
    'sandy_soil': ('cube', ['sandy_soil'], 'dirt'), 'pink_sand': ('cube', ['pink_sand'], 'sand'), 'red_moss_block': ('cube', ['red_moss_block'], 'moss_block'),
    'pink_sandstone': ('sandstone', ['pink_sandstone_top', 'pink_sandstone', 'pink_sandstone_bottom'], 'sandstone'),
    'desert_turnip_block': ('sandstone', ['desert_turnip_block_top', 'desert_turnip_block_side', 'desert_turnip_block_bottom'], 'pumpkin'),
    'desert_turnip_root_block': ('column', ['desert_turnip_root_block_top', 'desert_turnip_root_block'], 'rooted_dirt'),
}
# the block of ours each kind is cloned from, and its pictures (lower, upper)
BASE = {'flower': ('oxeye_daisy', ['oxeye_daisy']), 'grass': ('tall_grass', ['tall_grass']), 'flower2': ('sunflower', ['sunflower_lower', 'sunflower_upper']),
        'grass2': ('tall_grass_double', ['tall_grass_double_lower', 'tall_grass_double_upper']), 'pad': ('lily_pad', ['lily_pad']),
        'stem': ('pumpkin_stem', ['pumpkin_stem_stage0'])}


def modTex(name):
    for p in MODS.glob('natures_spirit*/assets/natures_spirit/textures/block/%s.png' % name):
        return Image.open(p).convert('RGBA')
    return None


def modelOf(block):
    """the block's model template in the mod (tall_cross, tall_large_cross, large_cross...; following its parents)"""
    root = next(iter(MODS.glob('natures_spirit*/assets/natures_spirit')), None)
    if root is None: return None
    try:
        bs = json.loads((root / 'blockstates' / (block + '.json')).read_text())
        v = next(iter(bs['variants'].values())); v = v[0] if isinstance(v, list) else v
        m = v['model']
        for _ in range(4):
            name = m.split(':')[-1].split('/')[-1]
            if name in ('tall_cross', 'tall_large_cross', 'tinted_tall_large_cross', 'large_cross', 'tinted_large_cross'): return name
            mj = json.loads((root / 'models' / 'block' / (name + '.json')).read_text())
            if not mj.get('parent'): return None
            m = mj['parent']
    except Exception: return None
    return None


def square(im, model=None):
    """the part of the picture the mod's model draws (its tall crosses: the lower three quarters, 1.5 blocks high; its large
    ones: the lower half of each), filling our one-block square; any other tall picture padded to a square"""
    w, h = im.size
    if model in ('tall_cross', 'tall_large_cross', 'tinted_tall_large_cross'): im = im.crop((0, h // 4, w, h))
    elif model in ('large_cross', 'tinted_large_cross'): im = im.crop((0, h // 2, w, h))
    elif w == h: return im
    else:
        n = max(w, h); out = Image.new('RGBA', (n, n)); out.paste(im, ((n - w) // 2, n - h)); return out
    n = max(im.size); return im.resize((n, n), Image.NEAREST)


def colours(im):
    """the picture's mean colour, and that of its colour away from the greens (a flower's petals), each or None"""
    px = [p for p in im.getdata() if p[3] > 100]
    if not px: return None, None
    mean = tuple(sum(p[i] for p in px) // len(px) for i in range(3))
    bloom = [p for p in px if not (0.17 < colorsys.rgb_to_hsv(p[0] / 255, p[1] / 255, p[2] / 255)[0] < 0.47) or max(p[:3]) - min(p[:3]) < 30]
    return mean, (tuple(sum(p[i] for p in bloom) // len(bloom) for i in range(3)) if len(bloom) > 4 else None)


def lum(c): return (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2])


def recolour(src, target, keep_green, untint, scale=None):
    """our picture with its colours moved to target by brightness (keep_green: its leaves and stems as they were)"""
    im = src.copy(); o = im.load(); W, H = im.size
    pts = [o[x, y] for y in range(H) for x in range(W) if o[x, y][3]]
    ref = scale or (sum(lum(p) for p in pts) / max(1, len(pts)))
    for y in range(H):
        for x in range(W):
            r, g, b, a = o[x, y]
            if not a: continue
            if untint and a == 253: a = 255   # (the game's tint marker: this one isn't tinted)
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if keep_green and 0.17 < h < 0.47 and s > 0.15: o[x, y] = (r, g, b, a); continue
            k = lum((r, g, b)) / max(1, ref)
            o[x, y] = (min(255, int(target[0] * k)), min(255, int(target[1] * k)), min(255, int(target[2] * k)), a)
    return im


def main():
    bj = json.loads((DATA / 'blocks.json').read_text()); keys = {b['key']: b for b in bj['blocks']}
    tj = json.loads((DATA / 'tiles.json').read_text()); tiles, files = tj['names'], tj['files']
    items = json.loads((DATA / 'items.json').read_text())
    pm = json.loads((DATA / 'packmap.json').read_text()); pmk = {e[0] for e in pm}
    recp = PACK / 'soulrend_import.json'
    rec = json.loads(recp.read_text()) if recp.exists() else []
    recTo = {r['to'] for r in rec}
    made = []

    def tile(name, img, res=None):
        """our built-in picture for the tile (made once), its pack picture from the mod (each run)"""
        if name not in tiles:
            rel = 'textures/block/%s.png' % name
            img.save(DATA / rel)
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(img.size[0]); tj['count'] += 1
        if name not in pmk: pm.append([name, [name], [name]]); pmk.add(name)
        return tiles[name]

    def packPic(name, modName, block=None):
        im = modTex(modName)
        if im is None or not PACK.exists(): return
        to = 'assets/minecraft/textures/block/%s.png' % name
        (PACK / to).parent.mkdir(parents=True, exist_ok=True)
        square(im, modelOf(block) if block else None).save(PACK / to)
        if to not in recTo:
            rec.append({'to': to, 'from': 'mod:natures_spirit:assets/natures_spirit/textures/block/%s.png' % modName, 'tile': name, 'mcmeta': False}); recTo.add(to)

    def ourPic(t): return Image.open(DATA / files[tiles[t]]).convert('RGBA')

    def add(key, base, upd, faces):
        if key in keys: keys[key].update(upd); return
        b = dict(keys[base]); b.update(upd); b['key'] = key; b['id'] = len(bj['blocks']); b['drop'] = key
        b.pop('harvest', None); b.pop('refill', None)
        bj['blocks'].append(b); bj['faces'].append(faces); keys[key] = b; made.append(key)
        if key not in items: items[key] = {'key': key, 'name': b['name'], 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    title = lambda k: k.replace('_', ' ').title()
    for key, spec in PLANTS.items():
        kind, pics = spec[0], spec[1]; tinted = len(spec) > 2 and spec[2]
        mods = [modTex(p) for p in pics]
        if any(m is None for m in mods): print('  (no picture for', key, ')'); continue
        base, bpics = BASE[kind]
        names = [key] if len(pics) == 1 else [key + '_lower', key + '_upper']
        ids = []
        for i, (nm, m) in enumerate(zip(names, mods)):
            m = square(m, modelOf(key)) if modelOf(key) else m
            mean, bloom = colours(m)
            src = ourPic(bpics[min(i, len(bpics) - 1)])
            if kind in ('flower', 'flower2'): img = recolour(src, bloom or mean, True, False)
            elif tinted: img = src.copy()
            else: img = recolour(src, mean, False, True)
            ids.append(tile(nm, img)); packPic(nm, pics[i], key)
        upd = {'name': title(key), 'tex': names[0], 'tint': 'grass' if tinted else None}
        if kind in ('flower', 'flower2'): upd['flower'] = True
        if kind in ('grass', 'grass2'): upd['replaceable'] = True; upd['drop'] = None
        if len(names) > 1: upd['texUpper'] = names[1]; upd['upperTile'] = ids[1]
        if kind == 'pad': upd['tint'] = None
        add(key, base, upd, [ids[0]] * 6)
        if kind in ('grass', 'grass2'): keys[key]['drop'] = None
    for key, (kind, pics, base) in GROUND.items():
        mods = [modTex(p) for p in pics]
        if any(m is None for m in mods): print('  (no picture for', key, ')'); continue
        bt = keys[base]['tex']; bt = bt if isinstance(bt, dict) else {'top': bt, 'side': bt, 'bottom': bt}
        roles = ['top', 'side'] if kind == 'column' else ['top', 'side', 'bottom'] if kind == 'sandstone' else ['all']
        ids = {}
        for role, m in zip(roles, mods):
            nm = key if role in ('all', 'side') and kind != 'sandstone' else '%s_%s' % (key, role)
            src = ourPic(bt['side' if role == 'all' else role])
            ids[role] = tile(nm, recolour(src, colours(m)[0], False, True)); packPic(nm, pics[roles.index(role)])
        if kind == 'cube': tex, faces = key, [ids['all']] * 6
        elif kind == 'column': tex, faces = {'top': key + '_top', 'bottom': key + '_top', 'side': key}, [ids['side'], ids['side'], ids['top'], ids['top'], ids['side'], ids['side']]
        else: tex, faces = {'top': key + '_top', 'bottom': key + '_bottom', 'side': key + '_side'}, [ids['side'], ids['side'], ids['top'], ids['bottom'], ids['side'], ids['side']]
        add(key, base, {'name': title(key), 'tex': tex}, faces)
    (DATA / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (DATA / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (DATA / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (DATA / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), ensure_ascii=False))
    if PACK.exists(): recp.write_text(json.dumps(rec, indent=0))
    print('ns_blocks: %d new blocks (%s), %d tiles now' % (len(made), ', '.join(made), tj['count']))


if __name__ == '__main__':
    main()
