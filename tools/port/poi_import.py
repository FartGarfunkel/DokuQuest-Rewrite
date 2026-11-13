#!/usr/bin/env python3
"""Soulrend's structure mods as our points of interest (git-ignored local data):
  python3 tools/port/poi_import.py <unpacked mods dir> [data-dir]

For each overworld jigsaw structure of the mods below (data/<ns>/worldgen/structure/*.json): its start pool, depth,
start height, biomes (resolved through the biome tags to our lands) and spacing (its structure_set), and every template
pool it reaches (following the pools named in the pieces' own jigsaw blocks too). Pieces go to data/pois/<ns>/<path>.nbt,
the lot to data/pois/index.json; gen/structures_poi.cpp places them."""
import gzip, io, json, pathlib, shutil, struct, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from processors import rules_of

MODS = ['DungeonsArise-1.20.x-2.1.58-release', 'MoogsVoyagerStructures-universal-1.20-5.1.3', 'The_Graveyard_3.1_(FORGE)_for_1.20.1',
        'idas_forge-1.13.0+1.20.1']
SKIP_NS = {'minecraft'}   # (their own data only)

# Minecraft biomes -> our lands (gen/structures_poi.cpp landsAt)
LAND = {
    'plains': 'plains', 'sunflower_plains': 'plains', 'meadow': 'plains', 'cherry_grove': 'forest',
    'forest': 'forest', 'flower_forest': 'forest', 'birch_forest': 'forest', 'old_growth_birch_forest': 'forest', 'dark_forest': 'forest',
    'taiga': 'taiga', 'old_growth_pine_taiga': 'taiga', 'old_growth_spruce_taiga': 'taiga', 'grove': 'taiga',
    'snowy_plains': 'snowy', 'snowy_taiga': 'snowy', 'ice_spikes': 'snowy', 'snowy_slopes': 'snowy', 'frozen_peaks': 'snowy', 'jagged_peaks': 'mountains',
    'stony_peaks': 'mountains', 'windswept_hills': 'mountains', 'windswept_forest': 'mountains', 'windswept_gravelly_hills': 'mountains',
    'desert': 'desert', 'badlands': 'badlands', 'eroded_badlands': 'badlands', 'wooded_badlands': 'badlands',
    'savanna': 'savanna', 'savanna_plateau': 'savanna', 'windswept_savanna': 'savanna',
    'jungle': 'jungle', 'sparse_jungle': 'jungle', 'bamboo_jungle': 'jungle',
    'swamp': 'swamp', 'mangrove_swamp': 'swamp', 'mushroom_fields': 'mushroom', 'beach': 'beach', 'snowy_beach': 'beach', 'stony_shore': 'beach',
}


def nbt(b):
    f = io.BytesIO(gzip.decompress(b))
    def rd(t):
        if t == 1: return struct.unpack('>b', f.read(1))[0]
        if t == 2: return struct.unpack('>h', f.read(2))[0]
        if t == 3: return struct.unpack('>i', f.read(4))[0]
        if t == 4: return struct.unpack('>q', f.read(8))[0]
        if t == 5: return struct.unpack('>f', f.read(4))[0]
        if t == 6: return struct.unpack('>d', f.read(8))[0]
        if t == 7: n = rd(3); return f.read(n)
        if t == 8: n = struct.unpack('>H', f.read(2))[0]; return f.read(n).decode('utf-8', 'replace')
        if t == 9: et = f.read(1)[0]; n = rd(3); return [rd(et) for _ in range(n)]
        if t == 10:
            d = {}
            while True:
                tt = f.read(1)[0]
                if tt == 0: return d
                k = rd(8); d[k] = rd(tt)
        if t == 11: n = rd(3); return [rd(3) for _ in range(n)]
        if t == 12: n = rd(3); return [rd(4) for _ in range(n)]
    t = f.read(1)[0]; rd(8); return rd(t)


def split(rl): ns, _, p = rl.partition(':'); return (ns, p) if p else ('minecraft', ns)


def main(mods, d='data'):
    mods, d = pathlib.Path(mods), pathlib.Path(d)
    out = d / 'pois'
    out.mkdir(parents=True, exist_ok=True)
    roots = [mods / m for m in MODS if (mods / m).is_dir()]
    def find(kind, rl, ext='.json'):
        ns, p = split(rl)
        for r in roots:
            f = r / 'data' / ns / kind / (p + ext)
            if f.exists(): return f
        return None
    def biomes(ref, seen=None):
        seen = seen or set()
        if ref in seen: return set()
        seen.add(ref)
        if ref.startswith('#'):
            f = find('tags/worldgen/biome', ref[1:])
            if not f: return set()
            s = set()
            for v in json.loads(f.read_text()).get('values', []):
                s |= biomes(v['id'] if isinstance(v, dict) else v, seen)
            return s
        return {ref}
    index = {'source': 'Soulrend structure mods (tools/port/poi_import.py)', 'pieces': [], 'pools': {}, 'structures': []}
    pieces = set()
    def pool(name, todo):
        if name in index['pools'] or name in ('minecraft:empty',): return
        f = find('worldgen/template_pool', name)
        if not f: return
        j = json.loads(f.read_text())
        els = []
        def add(e, w):
            t = e.get('element_type', '')
            if t.endswith('list_pool_element'):
                for e2 in e.get('elements', []): add(e2, w)
                return
            loc = e.get('location')
            if not isinstance(loc, str): return
            src = find('structures', loc, '.nbt') or find('structure', loc, '.nbt')
            if not src: return
            ns, p = split(loc)
            rel = '%s/%s.nbt' % (ns, p)
            if rel not in pieces:
                to = out / rel; to.parent.mkdir(parents=True, exist_ok=True)
                if not to.exists() or to.stat().st_size != src.stat().st_size: shutil.copyfile(src, to)
                pieces.add(rel)
                try:
                    t2 = nbt(src.read_bytes())
                    pal = t2.get('palette') or (t2.get('palettes') or [[]])[0]
                    for b in t2.get('blocks', []):
                        n = b.get('nbt')
                        if n and 'pool' in n and 'jigsaw' in pal[b['state']]['Name']: todo.append(n['pool'])
                except Exception as ex:
                    print('  (unreadable', rel, ex, ')')
            rl = rules_of(roots, e.get('processors'))
            els.append([ns + ':' + p, w, e.get('projection', 'rigid')] + ([rl] if rl else []))
        for el in j.get('elements', []): add(el.get('element', {}), el.get('weight', 1))
        index['pools'][name] = els
        fb = j.get('fallback')
        if isinstance(fb, str): todo.append(fb)
    # structure sets: spacing per structure
    spacing = {}
    for r in roots:
        for f in (r / 'data').glob('*/worldgen/structure_set/*.json'):
            j = json.loads(f.read_text()); pl = j.get('placement', {})
            for s in j.get('structures', []):
                spacing[s.get('structure')] = (pl.get('spacing', 32), pl.get('separation', 8))
    for r in roots:
        for f in sorted((r / 'data').glob('*/worldgen/structure/*.json')):
            ns = f.parts[-4]
            if ns in SKIP_NS: continue
            j = json.loads(f.read_text())
            if 'start_pool' not in j: continue
            key = ns + ':' + f.stem
            bs = biomes(j['biomes']) if isinstance(j.get('biomes'), str) else set().union(*[biomes(b) for b in j.get('biomes', [])]) if j.get('biomes') else set()
            lands = sorted({LAND[b.split(':')[1]] for b in bs if b.startswith('minecraft:') and b.split(':')[1] in LAND})
            if not lands: continue   # (oceans, the Nether, the End, other mods' lands: not yet)
            sh = j.get('start_height', {})
            y = sh.get('absolute', 0) if isinstance(sh, dict) else 0
            todo = [j['start_pool']]
            while todo:
                p = todo.pop()
                pool(p, todo)
            if j['start_pool'] not in index['pools']: continue
            sp = spacing.get(key, (40, 12))
            index['structures'].append({'key': key, 'start': j['start_pool'], 'depth': min(int(j.get('size', 6)), 10), 'surface': 'project_start_to_heightmap' in j,
                                        'y': y, 'lands': lands, 'spacing': sp[0], 'separation': sp[1], 'reach': j.get('max_distance_from_center', 80)})
    index['pieces'] = sorted(pieces)
    (out / 'index.json').write_text(json.dumps(index, indent=0))
    (out / '.gitignore').write_text('*\n!.gitignore\n')
    print('poi_import: %d structures, %d pools, %d pieces' % (len(index['structures']), len(index['pools']), len(pieces)))
    for s in index['structures']: print('  ', s['key'], s['lands'], 'depth', s['depth'], 'spacing', s['spacing'])


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'data')
