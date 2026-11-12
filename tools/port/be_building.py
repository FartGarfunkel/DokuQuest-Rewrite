#!/usr/bin/env python3
"""tools/port/be_building.py — Better End's buildings (idempotent; data_patches.py runs it after betterend_blocks.py):
  python3 tools/port/be_building.py <data-dir> [<Better End folder>]

1. The building blocks its structures are made of and the game lacked: per wood (mossy glowshroom, pythadendron, end
   lotus, lacugrove, dragon tree, tenanea, helix tree, umbrella tree, jellyshroom, lucernia) planks, stripped logs,
   bark, stairs, slabs, fences, doors, trapdoors, ladders, bookshelves, barrels and crafting tables; per stone
   (flavolite, violecite, sulphuric rock, the jadestones) bricks, tiles, polished, pillars, lanterns, stairs, slabs,
   walls, buttons and plates; thallasium and terminite bars and chains. Each is the game's own block of that kind
   (oak_door, oak_stairs, iron_bars...) with Better End's picture run through the art pass (tools/port/restyle.py via
   local_mod_art.fit: the base pack's picture of the same kind of thing in Better End's colours).
2. data/be_structures.json: Better End's land structures (data/betterend/structures/biome/**.nbt, MIT) with the lists
   and rarity of its building_list_features: in chorus forest, blossoming spires, foggy mushroomland, umbrella jungle,
   shadow forest and lantern woods, one chunk in N gets one of its land's ruins, houses, stumps or fallen trees.
   Each block is [key, meta for rotations 0..3] (stairs, logs, doors, trapdoors, ladders, buttons turn with it).
   gen/end_be_structures.cpp places them.
"""
import gzip, json, pathlib, struct, sys
import numpy as np
from PIL import Image

N = 32
PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'
WOODS = ['mossy_glowshroom', 'pythadendron', 'end_lotus', 'lacugrove', 'dragon_tree', 'tenanea', 'helix_tree', 'umbrella_tree', 'jellyshroom', 'lucernia']
STONES = ['flavolite', 'violecite', 'sulphuric_rock', 'azure_jadestone', 'sandy_jadestone', 'virid_jadestone']
LANDS = {'chorus_forest': 0, 'blossoming_spires': 1, 'foggy_mushroomland': 2, 'umbrella_jungle': 6, 'shadow_forest': 7, 'lantern_woods': 11}
# blocks with no counterpart we make: the nearest of ours (chests, signs, furnaces, plants)
NEAREST = {
    'shulker_box': 'chest', 'end_stone_smelter': 'furnace', 'azure_jadestone_furnace': 'furnace',
    'thallasium_bulb_lantern': 'lantern', 'iron_bulb_lantern_cyan': 'soul_lantern', 'terminite_bulb_lantern': 'lantern',
    'terminite_bulb_lantern_lime': 'lantern', 'eternal_pedestal': 'purpur_pillar', 'purpur_pedestal': 'purpur_pillar',
    'tenanea_outer_leaves': 'tenanea_leaves', 'lucernia_outer_leaves': 'lucernia_leaves', 'umbrella_moss_tall': 'umbrella_moss',
    'bulbis_stem': 'mossy_glowshroom_bark', 'bulbis_wood': 'mossy_glowshroom_bark', 'flavolite_runed_eternal': 'flavolite_runed',
}


def nbt(path):
    f = gzip.open(path); r = f.read
    def payload(t):
        if t == 1: return struct.unpack('>b', r(1))[0]
        if t == 2: return struct.unpack('>h', r(2))[0]
        if t == 3: return struct.unpack('>i', r(4))[0]
        if t == 4: return struct.unpack('>q', r(8))[0]
        if t == 5: return struct.unpack('>f', r(4))[0]
        if t == 6: return struct.unpack('>d', r(8))[0]
        if t == 7: n = struct.unpack('>i', r(4))[0]; return r(n)
        if t == 8: n = struct.unpack('>H', r(2))[0]; return r(n).decode('utf8')
        if t == 9: et = r(1)[0]; n = struct.unpack('>i', r(4))[0]; return [payload(et) for _ in range(n)]
        if t == 10:
            d = {}
            while True:
                tt = r(1)[0]
                if tt == 0: return d
                nl = struct.unpack('>H', r(2))[0]; name = r(nl).decode('utf8'); d[name] = payload(tt)
        if t == 11: n = struct.unpack('>i', r(4))[0]; return list(struct.unpack('>%di' % n, r(4 * n)))
        if t == 12: n = struct.unpack('>i', r(4))[0]; return list(struct.unpack('>%dq' % n, r(8 * n)))
        raise ValueError(t)
    t = r(1)[0]; nl = struct.unpack('>H', r(2))[0]; r(nl)
    return payload(t)


def main(d, be):
    d = pathlib.Path(d); be = pathlib.Path(be)
    sroot = be / 'data/betterend/structures'; troot = be / 'assets/betterend/textures/block'
    if not sroot.exists(): print('be_building: no Better End structures at', sroot, '- skipped'); return
    import importlib.util as ilu
    sp = ilu.spec_from_file_location('local_mod_art', pathlib.Path(__file__).with_name('local_mod_art.py')); LM = ilu.module_from_spec(sp); sp.loader.exec_module(LM)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; at = {b['key']: i for i, b in enumerate(blocks)}

    def pack(name):
        p = d / PACK / (name + '.png')
        if not p.exists(): return None
        im = Image.open(p).convert('RGBA'); w, h = im.size
        return im.crop((0, 0, w, w)) if h > w else im

    def tile(src, donor, how='ramp'):
        """be_<src>: Better End's picture through the art pass; None if Better End has no such picture"""
        name = 'be_' + src
        if name in tiles: return name
        p = troot / (src + '.png')
        if not p.exists(): return None
        im = Image.open(p).convert('RGBA'); w, h = im.size
        if h > w: im = im.crop((0, 0, w, w))
        dn = pack(donor) if donor else None
        out = LM.fit(im, dn, how if dn is not None else 'shape')
        rel = 'textures/block/%s.png' % name
        out.save(d / rel)
        tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return name

    def faces_of(tex):
        tid = lambda n: tiles[n]
        if isinstance(tex, dict):
            s = tex.get('side', tex.get('top')); return [tid(s), tid(s), tid(tex.get('top', s)), tid(tex.get('bottom', tex.get('top', s))), tid(s), tid(s)]
        return [tid(tex)] * 6

    def add(key, template, name, **over):
        if key in at: return key
        if template not in at: return None
        b = json.loads(json.dumps(blocks[at[template]]))
        b.update(over); b['key'] = key; b['name'] = name; b['drop'] = key; b['betterEnd'] = True
        for k in ('strip', 'itemIcon'): b.pop(k, None)
        tex = b['tex'] if 'base' not in over else blocks[at[over['base']]]['tex']
        if 'base' in over: b['tex'] = tex
        b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces_of(b['tex'])); at[key] = b['id']
        if key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
        return key

    title = lambda k: k.replace('_', ' ').title()
    made = []

    def resolve(full):
        """a Better End / Minecraft block name -> our key, making the block when it's one of the kinds we can"""
        ns, nm = full.split(':')
        if nm in NEAREST:
            k = NEAREST[nm]
            return k if k in at else resolve('betterend:' + k)
        for k in (nm, 'be_' + nm):
            if k in at: return k
        if nm.endswith('_runed'):
            t = tile('flavolite_runed_1', 'chiseled_stone_bricks'); return add(nm, 'chiseled_stone_bricks', title(nm), tex=t) if t else None
        for w in WOODS:
            if not nm.startswith(w + '_'): continue
            part = nm[len(w) + 1:]
            planks = None
            if (troot / (w + '_planks.png')).exists():
                planks = add(w + '_planks', 'planks', title(w) + ' Planks', tex=tile(w + '_planks', 'oak_planks'))
            if part == 'planks': return planks
            if part in ('stripped_log', 'stripped_bark', 'bark'):
                side = tile(w + ('_stripped_log_side' if 'stripped' in part else '_log_side'), 'oak_log')
                top = side if part.endswith('bark') else tile(w + '_stripped_log_top', 'oak_log_top')
                if not side: return None
                return add(nm, 'oak_wood', title(nm), tex={'top': top, 'bottom': top, 'side': side} if top != side else side)
            if part in ('stairs', 'slab', 'fence') and planks:
                return add(nm, 'oak_' + part, title(nm), base=planks)
            if part == 'door':
                lo, up = tile(w + '_door_bottom', 'oak_door_bottom', 'shape'), tile(w + '_door_top', 'oak_door_top', 'shape')
                if lo and up: return add(nm, 'oak_door', title(nm), tex={'side': lo, 'top': lo}, texUpper=up, upperTile=tiles[up], itemIcon=up)
                return None
            if part == 'trapdoor':
                t = tile(w + '_trapdoor', 'oak_trapdoor', 'shape'); return add(nm, 'oak_trapdoor', title(nm), tex=t) if t else None
            if part == 'ladder':
                t = tile(w + '_ladder', 'ladder', 'shape'); return add(nm, 'ladder', title(nm), tex=t) if t else None
            if part == 'bookshelf' and planks:
                t = tile(w + '_bookshelf', 'bookshelf'); return add(nm, 'bookshelf', title(nm), tex={'top': 'be_' + w + '_planks', 'bottom': 'be_' + w + '_planks', 'side': t}) if t else None
            if part == 'barrel':
                ts = [tile(w + s, 'barrel' + s) for s in ('_barrel_top', '_barrel_side', '_barrel_bottom')]
                return add(nm, 'barrel', title(nm), tex={'top': ts[0], 'side': ts[1], 'bottom': ts[2]}) if all(ts) else None
            if part == 'crafting_table':
                ts = [tile(w + s, d2) for s, d2 in (('_crafting_table_top', 'crafting_table_top'), ('_crafting_table_side', 'crafting_table_side'))]
                return add(nm, 'crafting_table', title(nm), tex={'top': ts[0], 'side': ts[1], 'bottom': 'be_' + w + '_planks'}) if all(ts) and planks else None
            if part == 'chest': return 'chest'
            if part == 'sign': return 'oak_sign' if 'oak_sign' in at else None
            return None
        for s in STONES:
            if not nm.startswith(s): continue
            part = nm[len(s):].lstrip('_')
            for suffix, tpl in (('_stairs', 'stone_brick_stairs'), ('_slab', 'stone_brick_slab'), ('_wall', 'stone_brick_wall')):
                if nm.endswith(suffix):
                    base = resolve('betterend:' + nm[:-len(suffix)])
                    return add(nm, tpl, title(nm), base=base) if base else None
            if part in ('bricks', 'tiles', 'polished'):
                donor = {'bricks': 'stone_bricks', 'tiles': 'polished_andesite', 'polished': 'polished_andesite'}[part]
                t = tile(nm, donor); return add(nm, 'stone_bricks', title(nm), tex=t) if t else None
            if part in ('pillar', 'pedestal'):
                pt, ps = tile(s + '_pillar_top', 'quartz_pillar_top'), tile(s + '_pillar_side', 'quartz_pillar')
                return add(s + '_pillar', 'quartz_pillar', title(s) + ' Pillar', tex={'top': pt, 'bottom': pt, 'side': ps}) if pt and ps else None
            if part == 'lantern':
                ts = [tile(s + x, 'redstone_lamp_on', 'shape') for x in ('_lantern_top', '_lantern_side', '_lantern_bottom')]
                return add(nm, 'stone_bricks', title(nm), tex={'top': ts[0], 'side': ts[1], 'bottom': ts[2]}, light=15) if all(ts) else None
            if part == '':
                t = tile(s, 'stone'); return add(s, 'stone', title(s), tex=t) if t else None
            if part in ('button', 'plate'):
                base = resolve('betterend:' + s)
                return add(nm, 'stone_button' if part == 'button' else 'stone_pressure_plate', title(nm), base=base) if base else None
            return None
        for m in ('thallasium', 'terminite'):
            if nm == m + '_bars':
                t = tile(m + '_bars', 'iron_bars', 'shape'); return add(nm, 'iron_bars', title(nm), tex=t) if t else None
            if nm == m + '_chain':
                t = tile(m + '_chain', 'chain', 'shape'); return add(nm, 'chain', title(nm), tex=t) if t else None
        if nm in ('purple_polypore', 'aurant_polypore', 'ruscus', 'shadow_berry'):   # small plants and shelf fungi
            t = tile(nm, None, 'shape'); return add(nm, 'brown_mushroom', title(nm), tex=t, needsSupport=False, light=6 if nm == 'aurant_polypore' else 0) if t else None
        if nm == 'small_jellyshroom': return 'jellyshroom_cap_purple' if 'jellyshroom_cap_purple' in at else None
        if nm == 'terminite_trapdoor':
            t = tile(nm, 'iron_trapdoor', 'shape'); return add(nm, 'oak_trapdoor', title(nm), tex=t) if t else None
        if nm.startswith('hydralux_petal_block'):
            t = tile(nm, 'white_wool'); return add(nm, 'white_wool', title(nm), tex=t) if t else None
        return None

    # ---- the structures ----
    FACE = {'north': 0, 'east': 1, 'south': 2, 'west': 3}
    def metas(name, props):
        """[meta at rotation 0..3]: facing bits turn with the structure, x/z axes swap at odd turns"""
        p = props or {}
        out = []
        for r in range(4):
            m = 0
            f = FACE.get(p.get('facing', ''), None)
            rf = None if f is None else (f + r) % 4
            if name.endswith('_stairs') and rf is not None: m = rf | (4 if p.get('half') == 'top' else 0)
            elif name.endswith('_slab'): m = {'bottom': 1, 'top': 0, 'double': 6}.get(p.get('type'), 1)
            elif name.endswith('_door') and rf is not None:
                m = rf | (4 if p.get('open') == 'true' else 0) | (8 if p.get('hinge') == 'right' else 0) | (16 if p.get('half') == 'upper' else 0)
            elif name.endswith('_trapdoor') and rf is not None: m = rf | (4 if p.get('open') == 'true' else 0) | (8 if p.get('half') == 'top' else 0)
            elif name.endswith('_ladder') and rf is not None: m = (rf + 2) % 4
            elif name.endswith('_button'):
                face = p.get('face', 'wall'); m = 0 if face == 'floor' else 1 if face == 'ceiling' else (2 + (rf if rf is not None else 0))
            elif 'axis' in p:
                ax = {'y': 0, 'x': 1, 'z': 2}[p['axis']]
                if r % 2 == 1 and ax: ax = 3 - ax
                m = ax
            out.append(m)
        return out

    cf = be / 'data/betterend/worldgen/configured_feature'; pf = be / 'data/betterend/worldgen/placed_feature'
    keymap = {}; missing = {}

    def convert(path):
        """an .nbt template -> {size, palette [[key, m0..m3]], blocks [x | y<<8 | z<<16 | palette<<24], crystal [x,y,z]}"""
        t = nbt(path); pal = t.get('palette') or t['palettes'][0]
        palette = []
        for e in pal:
            full = e['Name']
            if full not in keymap: keymap[full] = 'air' if full in ('minecraft:air', 'minecraft:cave_air') else (resolve(full) if full != 'minecraft:structure_void' else None)
            k = keymap[full]
            if k is None and full != 'minecraft:structure_void': missing[full] = missing.get(full, 0) + 1; k = 'end_stone'
            palette.append([k] + metas(full.split(':')[1], e.get('Properties')) if k else None)
        idx_of = {}; pal2 = []
        for i, e in enumerate(palette):
            if e is None: continue
            tkey = json.dumps(e)
            if tkey not in idx_of: idx_of[tkey] = len(pal2); pal2.append(e)
        remap = {i: idx_of[json.dumps(e)] for i, e in enumerate(palette) if e is not None}
        blk = []
        for b in t['blocks']:
            if b['state'] not in remap: continue
            x, y, z = b['pos']; blk.append(x | (y << 8) | (z << 16) | (remap[b['state']] << 24))
        out = {'size': t['size'], 'palette': pal2, 'blocks': blk}
        for e in t.get('entities', []):
            if e.get('nbt', {}).get('id') == 'minecraft:end_crystal': out['crystal'] = [round(v, 2) for v in e['pos']]
        return out
    lands = {}
    total = 0
    for land, idx in LANDS.items():
        try:
            conf = json.loads((cf / (land + '_structures.json')).read_text())['config']
            chance = next((p['chance'] for p in json.loads((pf / (land + '_structures.json')).read_text())['placement'] if p['type'] == 'minecraft:rarity_filter'), 10)
        except Exception as e:
            print('be_building: no list for', land, e); continue
        lst = []
        for s in conf['structures']:
            path = be / s['path'].lstrip('/')
            t = nbt(path); pal = t.get('palette') or t['palettes'][0]
            palette = []
            for e in pal:
                full = e['Name']
                if full not in keymap: keymap[full] = 'air' if full in ('minecraft:air', 'minecraft:cave_air') else (resolve(full) if full != 'minecraft:structure_void' else None)
                k = keymap[full]
                if k is None and full != 'minecraft:structure_void': missing[full] = missing.get(full, 0) + 1; k = 'end_stone'
                palette.append([k] + metas(full.split(':')[1], e.get('Properties')) if k else None)
            idx_of = {}; pal2 = []
            for i, e in enumerate(palette):
                if e is None: continue
                tkey = json.dumps(e)
                if tkey not in idx_of: idx_of[tkey] = len(pal2); pal2.append(e)
            remap = {i: idx_of[json.dumps(e)] for i, e in enumerate(palette) if e is not None}
            blk = []
            for b in t['blocks']:
                if b['state'] not in remap: continue
                x, y, z = b['pos']; blk.append(x | (y << 8) | (z << 16) | (remap[b['state']] << 24))
            lst.append({'name': path.stem, 'merger': s.get('terrain_merger', 'SURFACE'), 'offY': s.get('offset_y', 0), 'size': t['size'], 'palette': pal2, 'blocks': blk})
            total += 1
        lands[str(idx)] = {'chance': chance, 'list': lst}
    # the End's pillars as Better End rebuilds them (SpikeFeatureMixin): a base and a top for radius 2-5 (index 1-4),
    # the guarded ones with a cage
    pillars = {}
    for path in sorted((sroot / 'pillars').glob('*.nbt')): pillars[path.stem] = convert(path); total += 1
    (d / 'be_structures.json').write_text(json.dumps({'lands': lands, 'pillars': pillars}, separators=(',', ':')))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('be_building: %d structures in %d lands, %d blocks now; fell back to end stone for %s' % (total, len(lands), len(blocks), json.dumps(missing) if missing else 'nothing'))


if __name__ == '__main__':
    here = pathlib.Path(__file__).resolve().parents[2]
    main(sys.argv[1] if len(sys.argv) > 1 else 'data', sys.argv[2] if len(sys.argv) > 2 else str(here.parent / 'Better END'))
