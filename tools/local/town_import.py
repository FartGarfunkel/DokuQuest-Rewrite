#!/usr/bin/env python3
"""Towns and Towers' villages (Soulrend) as more of our village styles:
  python3 tools/local/town_import.py <Towns-and-Towers mod folder> [data-dir]

Each of its full village styles (a town centre, houses, streets, street ends, decor) is read from its template pools
(data/kaisyn/worldgen/template_pool/village/<style>/<kind>.json) and its pieces copied to data/villages/<style>/<kind>/,
added to data/villages/index.json, and the style to data/villages/styles.json on the base style whose land it suits
(gen/structures_villages.cpp picks it there). The pieces' jigsaw pools ("kaisyn:village/<style>/houses",
"kaisyn:village/exclusives/<style>/houses") resolve to the same folders (gen/village_pieces.cpp VillagePieces::pool).
The pieces are the mod's: local game data, git-ignored (data/villages/.gitignore)."""
import json, pathlib, shutil, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from processors import rules_of

KINDS = ('town_centers', 'houses', 'streets', 'terminators', 'decor')
# style -> (base style, its own materials for our own buildings in it: the hall, the mine)
STYLES = {
    'meadow_swiss': ('plains', {'planks': 'spruce_planks', 'log': 'spruce_log'}),
    'birch_forest_romanian': ('plains', {'planks': 'birch_planks', 'log': 'birch_log'}),
    'flower_forest_japanese': ('plains', {'planks': 'cherry_planks', 'log': 'cherry_log'}),
    'forest_ruins': ('plains', {'planks': 'planks', 'log': 'log'}),
    'mushroom_fields_fantasy': ('plains', {}),
    'swamp_boat': ('plains', {'planks': 'mangrove_planks', 'log': 'mangrove_log'}),
    'jungle_tribal': ('plains', {'planks': 'jungle_planks', 'log': 'jungle_log'}),
    'sparse_jungle_polynesian': ('savanna', {'planks': 'bamboo_planks'}),
    'badlands_pueblo': ('desert', {}),
    'old_growth_taiga_polish': ('taiga', {}),
    'snowy_taiga_viking': ('snowy', {}),
    'tudor': ('plains', {}), 'classic': ('plains', {}), 'rustic': ('plains', {}), 'swedish': ('taiga', {}),
    'iberian': ('savanna', {}), 'mediterranean': ('plains', {}), 'nilotic': ('desert', {}),
}


def main(mod, d='data'):
    mod, d = pathlib.Path(mod), pathlib.Path(d)
    pools = mod / 'data/kaisyn/worldgen/template_pool/village'
    structs = mod / 'data/kaisyn/structures'
    if not pools.is_dir(): print('tnt_village_import: no Towns and Towers at', mod); return
    out = d / 'villages'
    idx = json.loads((out / 'index.json').read_text())
    sty = json.loads((out / 'styles.json').read_text())
    have = set(idx['pieces'])
    added = 0; done = []
    for style, (base, mats) in STYLES.items():
        pdir = pools / style if (pools / style).is_dir() else pools / 'exclusives' / style
        if not (pdir / 'town_centers.json').exists() or not (pdir / 'houses.json').exists(): continue
        n0 = added
        for kind in KINDS:
            pf = pdir / (kind + '.json')
            if not pf.exists(): continue
            for el in json.loads(pf.read_text()).get('elements', []):
                loc = el.get('element', {}).get('location')
                if not isinstance(loc, str) or not loc.startswith('kaisyn:'): continue
                src = structs / (loc[len('kaisyn:'):] + '.nbt')
                if not src.exists(): continue
                rel = '%s/%s/%s.nbt' % (style, kind, src.stem)
                to = out / rel
                to.parent.mkdir(parents=True, exist_ok=True)
                if not to.exists() or to.stat().st_size != src.stat().st_size: shutil.copyfile(src, to)
                if rel not in have: idx['pieces'].append(rel); have.add(rel); added += 1
                rl = rules_of([mod], el.get('element', {}).get('processors'))
                if rl: idx.setdefault('rules', {})[rel] = rl
        if added > n0 or any(p.startswith(style + '/') for p in have):
            done.append(style)
            if not any(e.get('key') == style for e in sty['styles']):
                e = {'key': style, 'base': base, 'source': 'Towns and Towers'}
                if mats: e['set'] = mats
                sty['styles'].append(e)
    idx['source'] = "DokuQuest's own (tools/port/village_builder.py) + Towns and Towers (tools/local/town_import.py)"
    (out / 'index.json').write_text(json.dumps(idx, indent=0))
    (out / 'styles.json').write_text(json.dumps(sty, indent=1))
    gi = out / '.gitignore'
    lines = set(gi.read_text().split('\n')) if gi.exists() else set()
    for s in done: lines.add(s + '/')
    gi.write_text('\n'.join(sorted(l for l in lines if l)) + '\n')
    print('tnt_village_import: %d styles (%s), %d new pieces' % (len(done), ', '.join(done), added))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'data')
