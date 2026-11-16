#!/usr/bin/env python3
"""Soulrend's spawn rules for the creatures we share with Alex's Mobs (its config: alexsmobs.toml weights,
config/alexsmobs/<mob>_spawns.json biome conditions) into data/mobs.json "spawnIn" [[land, weight, fewest, most]]:
  python3 tools/local/spawn_import.py "<instance>/minecraft/config" [data-dir]

Each condition group (an AND of biome tags and names) becomes our lands by what it names (swamp, jungle, desert,
savanna, taiga, snowy, mountains, plains, forest, beach); a group that only says "the Overworld" means every land. The
weight is Soulrend's (halved and kept to 1..12, on our tables' scale: cows 10); the group sizes stay ours."""
import json, pathlib, re, sys

ALIAS = {'grizzly_bear': 'brown_bear', 'snow_leopard': 'snow_leopard', 'crocodile': 'crocodile', 'elephant': 'elephant', 'gazelle': 'gazelle',
         'kangaroo': 'kangaroo', 'raccoon': 'raccoon', 'roadrunner': 'roadrunner', 'rattlesnake': 'rattlesnake', 'bald_eagle': 'bald_eagle',
         'hummingbird': 'hummingbird', 'toucan': 'toucan', 'seagull': 'seagull', 'moose': 'moose', 'bison': 'bison', 'tiger': 'tiger',
         'crow': 'crow', 'orca': 'orca', 'lobster': 'lobster', 'dragonfly': 'dragonfly', 'mouse': 'mouse', 'anaconda': 'green_snake'}
ALL = ['plains', 'forest', 'taiga', 'snowy_plains', 'mountains', 'savanna', 'desert', 'jungle', 'swamp', 'beach']
RULES = [(r'swamp|bog|marsh|mire|wetland', 'swamp'), (r'jungle|rainforest|tropical', 'jungle'), (r'savanna', 'savanna'),
         (r'badlands|mesa', 'desert'), (r'desert|sand|dry|hot|oasis|dune', 'desert'), (r'taiga|boreal|conifer', 'taiga'),
         (r'snow|winter|ice|frozen|cold|tundra|glacier', 'snowy_plains'), (r'mountain|peak|highland|alpine|hill|slope|spires', 'mountains'),
         (r'beach|shore|coast', 'beach'), (r'forest|maple|wood|sakura|cherry|lavender|birch|grove', 'forest'),
         (r'plains|meadow|valley|clearing|field|prairie|steppe|shrub', 'plains')]
SKIP = r'ocean|sea|river|nether|end|skylands|cave|deep|mushroom|void'


def lands_of(value):
    ns = value.split(':', 1)[0] if ':' in value else 'minecraft'
    if ns not in ('minecraft', 'forge'): return set()   # (other mods' biomes (Terralith, BOP): not in our world)
    v = value.split(':', 1)[-1]
    if re.search(SKIP, v): return None
    for rx, land in RULES:
        if re.search(rx, v): return {land}
    return set()


def main(cfg, d='data'):
    cfg, d = pathlib.Path(cfg), pathlib.Path(d)
    toml = (cfg / 'alexsmobs.toml').read_text()
    weights = {m.group(1): int(m.group(2)) for m in re.finditer(r'(\w+)SpawnWeight\s*=\s*(\d+)', toml)}
    mobs = json.loads((d / 'mobs.json').read_text())
    done = []
    for f in sorted((cfg / 'alexsmobs').glob('*_spawns.json')):
        name = f.stem[:-len('_spawns')]
        ours = ALIAS.get(name, name)
        if ours not in mobs: continue
        camel = re.sub(r'_(\w)', lambda m: m.group(1).upper(), name)
        w = weights.get(camel)
        if not w: continue
        lands = set()
        for grp in json.loads(f.read_text()).get('biomes', []):
            pos = [c['value'] for c in grp if not c.get('negate')]
            if not pos: continue
            got = None
            for v in pos:
                if v in ('minecraft:is_overworld',): continue
                l = lands_of(v)
                if l is None: got = set(); break
                if l: got = (got or set()) | l
            if got is None and 'minecraft:is_overworld' in pos and len(pos) == 1: got = set(ALL)
            if got: lands |= got
        if not lands: continue
        old = {e[0]: e for e in mobs[ours].get('spawnIn', [])}
        ww = max(1, min(12, round(w / 2)))
        mobs[ours]['spawnIn'] = [[l, ww, old[l][2] if l in old else 1, old[l][3] if l in old else 2] for l in sorted(lands)]
        done.append('%s(%s w%d)' % (ours, ','.join(sorted(lands)), ww))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':')))
    print('soulrend_spawns: %d creatures:' % len(done), ' '.join(done))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'data')
