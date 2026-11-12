#!/usr/bin/env python3
"""tools/port/restyle.py — the art pass (MASTER_PLAN Phase 9): every block and item picture the DokuQuest Base pack
doesn't have (the original's code-painted tiles and the ones our generators made) gets one in the pack's own style.

How: each missing picture takes a *donor* from the pack, the pack picture of the nearest kind of thing (an ore takes
the pack's iron ore, an ingot its iron ingot, bricks its stone bricks...; with no rule, the pack picture whose light and
shade is most alike). The donor's light and shade are kept, pixel for pixel, and its colours are swapped for the
missing picture's own, darkest to lightest (a colour ramp). Items keep their own outline where the donor's shape is a
different thing. The results go into the pack (textures/block, textures/item) with a packmap entry where needed, and
are listed in tools/port/restyle_manifest.json (code_tiles.txt lists the pictures that are still the original's code
paintings: added by the port, 4a17ee0, and never edited since) so they can be told apart from the hand-made art (and redone).

  python3 tools/port/restyle.py data            # write
  python3 tools/port/restyle.py data --sheet x  # also a contact sheet (code | pack-styled) for checking
"""
import json, os, re, sys
import numpy as np
from PIL import Image

N = 32


def load32(path):
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    if h > w: im = im.crop((0, 0, w, w))   # an animation strip: its first frame
    if im.size[0] != N: im = im.resize((N, N), Image.NEAREST if im.size[0] < N else Image.BOX)
    return np.asarray(im).astype(np.float32)


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def ramp_recolour(donor, target):
    """donor's light and shade, target's colours: rank each opaque donor pixel by brightness and give it the colour of
    the same rank among the target's opaque pixels."""
    out = donor.copy()
    dm, tm = donor[..., 3] >= 128, target[..., 3] >= 128
    if dm.sum() == 0 or tm.sum() == 0: return out
    tcol = target[tm][:, :3]
    tl = lum(target)[tm]
    order = np.argsort(tl)
    tcol = tcol[order]
    dl = lum(donor)[dm]
    ranks = np.argsort(np.argsort(dl)) / max(1, dl.size - 1)
    idx = np.clip((ranks * (tcol.shape[0] - 1)).round().astype(int), 0, tcol.shape[0] - 1)
    cols = tcol[idx]
    # keep the donor's own contrast a little (the pack's crisp highlights and outlines), the target's hue
    dmean, tmean = dl.mean(), tl.mean()
    k = (dl - dmean)[:, None] * 0.25
    out_rgb = np.clip(cols + k, 0, 255)
    o = out[..., :3]; o[dm] = out_rgb; out[..., :3] = o
    out[..., 3] = np.where(dm, 255, 0)
    return out


def sat(a):
    mx, mn = a[..., :3].max(-1), a[..., :3].min(-1)
    return (mx - mn) / np.maximum(mx, 1)


def tool_recolour(donor, target):
    """a tool or weapon: the pack's handle stays as it is, the head takes the target's head colours"""
    dm = donor[..., 3] >= 128
    head = dm & ((sat(donor) < 0.22) | (lum(donor) < 40))           # the pack's iron head and its dark outline
    handle = dm & ~head
    hc = donor[handle][:, :3]
    tm = target[..., 3] >= 128
    tcol = target[tm][:, :3]
    if hc.size:
        dist = np.sqrt(((tcol[:, None, :] - hc[None, :, :]) ** 2).sum(-1)).min(1)
        th = tcol[dist > 34]
        if th.shape[0] >= 8: tcol = th
    fake_t = np.zeros_like(target); fake_t[..., 3] = 0
    flat = fake_t.reshape(-1, 4); flat[:tcol.shape[0], :3] = tcol; flat[:tcol.shape[0], 3] = 255
    fake_d = donor.copy(); fake_d[..., 3] = np.where(head, 255, 0)
    out = ramp_recolour(fake_d, fake_t)
    out[handle] = donor[handle]
    out[..., 3] = np.where(dm, 255, 0)
    return out


def blur(a):
    k = np.zeros_like(a)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): k += np.roll(np.roll(a, dy, 0), dx, 1)
    return k / 9


def grain(donor, target):
    """the target's own picture (its shapes stay: a chest's lock, a pattern), with the pack's finer grain and contrast laid
    over it from a pack picture of the same sort of material"""
    out = target.copy()
    dl = lum(donor); hf = dl - blur(dl)
    hf = hf / (np.abs(hf).mean() + 1e-3) * 7.0
    tl = lum(target); tb = blur(tl)
    m = target[..., 3] >= 128
    o = out[..., :3]
    o += (hf[..., None] + (tl - tb)[..., None] * 0.35)
    o[:] = np.clip(o, 0, 255)
    out[..., :3] = np.where(m[..., None], o, target[..., :3])
    return out


def outline_style(target, outline):
    """an item that keeps its own shape: the pack's dark outline round it, and the pack's soft top-left light"""
    out = target.copy()
    a = target[..., 3] >= 128
    edge = np.zeros_like(a)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        sh = np.roll(np.roll(a, dy, 0), dx, 1)
        edge |= a & ~sh
    o = out[..., :3]
    o[edge] = o[edge] * 0.35 + np.array(outline) * 0.65
    ys, xs = np.mgrid[0:N, 0:N]
    light = ((N - xs) + (N - ys)) / (2 * N) - 0.5   # brighter toward the top left
    o[a & ~edge] = np.clip(o[a & ~edge] * (1 + light[a & ~edge, None] * 0.18), 0, 255)
    out[..., :3] = o
    return out


BLOCK_RULES = [
    (r'deepslate_.*_ore$', 'deepslate_iron_ore'), (r'_ore$', 'iron_ore'),
    (r'(^|_)(tin|bronze|steel|silver|mithril|adamantite|mese|ruby|sapphire|cinderite|aether_crystal)_block$', 'iron_block'),
    (r'_bricks?$', 'stone_bricks'), (r'roof_tiles$', 'bricks'), (r'pillar_top$', 'quartz_pillar_top'), (r'pillar$', 'quartz_pillar'),
    (r'_planks$', 'oak_planks'), (r'_log_top$', 'oak_log_top'), (r'_log(_side)?$', 'oak_log'), (r'_leaves$', 'oak_leaves'),
    (r'_sapling$', 'oak_sapling'), (r'_door_lower$', 'oak_door_bottom'), (r'_door_upper$', 'oak_door_top'), (r'_trapdoor$', 'oak_trapdoor'),
    (r'grass_top$', 'grass_block_top'), (r'grass_side$', 'grass_block_side'),
    (r'(^|_)(sand|glimmersand)$', 'sand'), (r'(dirt|mud|soil|peat)$', 'dirt'),
    (r'polished_', 'polished_andesite'), (r'(stone|rock|marble|limestone|basalt|slate|schist|shale|skystone)$', 'stone'),
    (r'glass$', 'glass'), (r'_lamp(_on)?$', 'redstone_lamp_on'), (r'_lamp_off$', 'redstone_lamp'),
    (r'(^|_)ice$', 'ice'), (r'snow', 'snow'), (r'wool$', 'white_wool'), (r'terracotta$', 'terracotta'),
    (r'(plaster|cob|thatch)$', 'smooth_sandstone'),
]
ITEM_RULES = [
    (r'_ingot$', 'iron_ingot'), (r'_nugget$', 'iron_nugget'), (r'_(pickaxe|axe|shovel|hoe|sword)$', r'iron_\1'),
    (r'_(helmet|chestplate|leggings|boots)$', r'iron_\1'), (r'_door_item$', 'oak_door'), (r'_sign_item$', 'oak_sign'),
    (r'_boat$', 'oak_boat'), (r'_seeds$', 'wheat_seeds'), (r'_(dust|blend)$', 'redstone'), (r'(ruby|sapphire|gem)$', 'diamond'),
    (r'(crystal|shard)$', 'amethyst_shard'), (r'_bucket$', 'water_bucket'), (r'(flask|potion|tonic|elixir)$', 'potion'),
    (r'_(apple|fruit|berry|berries)$', 'apple'), (r'_(stew|soup)$', 'mushroom_stew'), (r'_(fish|cod|salmon)$', 'cod'),
]


def main(d, sheet=None):
    d = d.rstrip('/')
    tj = json.load(open(f'{d}/tiles.json')); names, files = tj['names'], tj['files']
    pm = json.load(open(f'{d}/packmap.json'))
    items = set(json.load(open(f'{d}/itemshapes.json'))['itemTiles'])
    root = f'{d}/resourcepacks/DokuQuest Base/assets/minecraft/textures/'
    def pack_path(base, item):
        for dd in (['item/', 'items/'] if item else ['block/', 'blocks/']):
            p = root + dd + base + '.png'
            if os.path.exists(p): return p
        return None
    pmd = {e[0]: e for e in pm}
    covered = set()
    for e in pm:
        isItem = e[0] in items
        if any(pack_path(c, isItem) for c in (e[2] if isItem else e[1])): covered.add(e[0])
    # the pack's pictures, for the structural fallback (blocks only)
    pack_blocks = {}
    for fn in os.listdir(root + 'block'):
        if fn.endswith('.png') and not fn.endswith('_overlay.png'):
            try: pack_blocks[fn[:-4]] = load32(root + 'block/' + fn)
            except Exception: pass
    material = re.compile(r'(stone|cobble|brick|plank|dirt|sand|gravel|clay|terracotta|concrete|wool|_log$|_wood$|stem$|deepslate|andesite|'
                          r'diorite|granite|tuff|calcite|basalt|blackstone|netherrack|obsidian|prismarine|quartz_block|purpur_block|moss_block|'
                          r'^mud|ice$|snow|sandstone|copper|_block$|leaves|hyphae|nylium|soul_soil|magma|wart_block|sponge|dripstone|'
                          r'sapling|tulip|orchid|allium|bluet|daisy|poppy|dandelion|cornflower|lily|fern|grass$|mushroom$|roots$|sprouts$|bush$)')
    skipname = re.compile(r'(ore|door|chest|rail|lamp|command|structure|jigsaw|spawner|observer|piston|furnace|dispenser|dropper|'
                          r'comparator|repeater|daylight|tnt|bookshelf|crafting|loom|smithing|barrel|beehive|target|note|jukebox|front|'
                          r'stage|_\d+$|cracked|glazed|shulker|bed_|banner|candle|frame|anvil|cauldron|hopper|bell|lantern|campfire)')
    pack_blocks = {k: v for k, v in pack_blocks.items() if material.search(k) and not skipname.search(k)}
    pk_names = list(pack_blocks)
    pk_opaque = np.array([(pack_blocks[n][..., 3] >= 128).all() for n in pk_names])
    pk_alpha = np.stack([(pack_blocks[n][..., 3] >= 128).reshape(-1) for n in pk_names])
    pk_l = np.stack([lum(pack_blocks[n]) for n in pk_names]).reshape(len(pk_names), -1)
    pk_l = (pk_l - pk_l.mean(1, keepdims=True)) / (pk_l.std(1, keepdims=True) + 1e-3)
    manifest_path = os.path.join(os.path.dirname(__file__), 'restyle_manifest.json')
    manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {}
    # only the original game's code-painted pictures, as the port first wrote them, are restyled: never the user's own
    # edited pictures, nor the ones our generators already made from the pack (forged tools, spawn eggs, BTA blocks...)
    code = set(open(os.path.join(os.path.dirname(__file__), 'code_tiles.txt')).read().split())
    made, rows = 0, []
    skip = re.compile(r'^(crack_|_tile_)|_f\d+$|spawn_egg|_pic$|_head$|^head_|bed_|_bed$|chest|_front|machine|panel|screen|'
                      r'console|furnace|crafter|press|grinder|generator|pipe|cable|_on$|_off$')
    for name, i in sorted(names.items()):
        if name in covered and name not in manifest: continue
        f = files[i] if i < len(files) else ''
        kind = f.split('/')[1] if '/' in f else ''
        if kind not in ('block', 'item') or skip.search(name) or f not in code: continue
        src = f'{d}/{f}'
        if not os.path.exists(src): continue
        target = load32(src)
        if (target[..., 3] == 253).any(): continue   # biome-tinted (grass, leaves): the pack's grey ones work already
        isItem = name in items          # as the game decides (assets/packs.cpp): an item picture is looked for in item/
        donor_name = None
        for pat, rep in (ITEM_RULES if isItem else BLOCK_RULES):
            if re.search(pat, name):
                donor_name = re.sub(r'^.*$', rep, name) if '\\1' not in rep else re.sub(r'.*' + pat, rep, name)
                break
        donor = None
        if donor_name:
            p = pack_path(donor_name, isItem) or pack_path(donor_name, not isItem)
            if p: donor = load32(p)
        if donor is None and not isItem:
            tl = lum(target).reshape(-1); tl = (tl - tl.mean()) / (tl.std() + 1e-3)
            ta = (target[..., 3] >= 128).reshape(-1)
            score = pk_l @ tl
            if ta.all(): score = np.where(pk_opaque, score, -1e9)
            else:
                iou = (pk_alpha & ta).sum(1) / np.maximum(1, (pk_alpha | ta).sum(1))
                score = np.where(~pk_opaque & (iou > 0.5), score, -1e9)
            k = int(np.argmax(score))
            if score[k] < -1e8: continue        # nothing in the pack is that kind of thing: it keeps its own picture
            donor = pack_blocks[pk_names[k]]; donor_name = pk_names[k] + ' (alike)'
            if score[k] / tl.size < 0.5 or not (set(name.split('_')) & set(pk_names[k].split('_')) - {'block', 'side', 'top'}):
                # not laid out alike: keep the target's own shapes and take the grain of the nearest-coloured material
                tc = target[..., :3][target[..., 3] >= 128].mean(0)
                cd = [np.abs(pack_blocks[n][..., :3][pack_blocks[n][..., 3] >= 128].mean(0) - tc).sum() if pk_opaque[j] == ta.all() else 1e9
                      for j, n in enumerate(pk_names)]
                j = int(np.argmin(cd)); donor = pack_blocks[pk_names[j]]; donor_name = pk_names[j] + ' (grain)'
        if isItem:
            # an item keeps its own shape unless the donor is the same kind of thing (same outline, give or take)
            ta, da = target[..., 3] >= 128, (donor[..., 3] >= 128) if donor is not None else None
            same = donor is not None and (ta & da).sum() / max(1, (ta | da).sum()) > 0.55
            tool = same and re.match(r'iron_(pickaxe|axe|shovel|hoe|sword)$', donor_name or '')
            out = (tool_recolour if tool else ramp_recolour)(donor, target) if same else outline_style(target, (34, 22, 18))
            if not same: donor_name = 'own shape'
        elif donor_name.endswith('(grain)'):
            out = grain(donor, target)
        else:
            out = ramp_recolour(donor, target)
            out[..., 3] = np.where(target[..., 3] >= 128, 255, np.where(donor[..., 3] >= 128, 255, 0)) if (target[..., 3] < 128).any() else 255
        pe = pmd.get(name)
        base = name if name in manifest or not os.path.exists(root + ('item/' if isItem else 'block/') + name + '.png') else 'dq_' + name
        if name in manifest: base = os.path.basename(manifest[name]['file'])[:-4]
        rel = ('item/' if isItem else 'block/') + base + '.png'
        Image.fromarray(out.clip(0, 255).astype(np.uint8), 'RGBA').save(root + rel)
        if not pe: pe = [name, [], []]; pm.append(pe); pmd[name] = pe
        lst = pe[2] if isItem else pe[1]
        if base not in lst: lst.append(base)          # after the pack names it doesn't have
        manifest[name] = {'file': rel, 'donor': donor_name}
        made += 1
        if sheet and len(rows) < 400: rows.append((target, out, name))
    json.dump(pm, open(f'{d}/packmap.json', 'w'), separators=(',', ':'))
    json.dump(manifest, open(manifest_path, 'w'), indent=0, sort_keys=True)
    print(f'restyled {made} pictures into the base pack')
    if sheet and rows:
        cols = 10; S = 64
        img = Image.new('RGBA', (cols * S * 2 + cols * 6, ((len(rows) + cols - 1) // cols) * S), (40, 34, 30, 255))
        for k, (a, b, _) in enumerate(rows):
            x, y = (k % cols) * (S * 2 + 6), (k // cols) * S
            img.paste(Image.fromarray(a.astype(np.uint8), 'RGBA').resize((S, S), Image.NEAREST), (x, y))
            img.paste(Image.fromarray(b.clip(0, 255).astype(np.uint8), 'RGBA').resize((S, S), Image.NEAREST), (x + S, y))
        img.save(sheet)


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0], a[a.index('--sheet') + 1] if '--sheet' in a else None)
