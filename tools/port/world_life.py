#!/usr/bin/env python3
"""tools/port/world_life.py — the living world (sim/world_life.cpp):
- "carriable": the natural blocks the tall stare-creature (enderman) may lift and carry about: soil, sand, gravel, clay,
  single flowers, small mushrooms, pumpkins, melons, cactus and TNT (never what a player put down: sim/skills.h).
- "randomTick" on the small mushrooms (they spread in the dark and wither in bright light) and the Overworld's vine
  (it creeps along walls and hangs further down).
  python3 tools/port/world_life.py [data-dir]"""
import json, pathlib, re, sys

CARRIABLE = {'grass', 'dirt', 'coarse_dirt', 'podzol', 'mycelium', 'rooted_dirt', 'sand', 'red_sand', 'gravel', 'clay',
             'brown_mushroom', 'red_mushroom', 'pumpkin', 'melon', 'cactus', 'tnt', 'flower_red', 'flower_yellow'}
NOT_CARRIABLE = {'spore_blossom'}   # (it hangs from a ceiling)
TICKS = {'brown_mushroom', 'red_mushroom', 'vine'}
# "natural": what the land is made of by itself. An enderman leaves a block be when anything else lies within two blocks of
# it (planks, bricks, glass, torches, stations...): a cheap way to know a build, for worlds saved before player-placed blocks
# were recorded. Unknown blocks (a mod's) count as built, so the guess errs on the safe side.
NATURAL_KEYS = {'air', 'bedrock', 'vine', 'kelp', 'kelp_plant', 'seagrass', 'tall_seagrass', 'sugar_cane', 'bamboo', 'lily_pad',
                'cactus', 'pumpkin', 'melon', 'cobweb', 'mushroom_stem', 'brown_mushroom_block', 'red_mushroom_block',
                'glow_lichen', 'cave_vines', 'cave_vines_lit', 'cave_vines_plant', 'cave_vines_plant_lit', 'pointed_dripstone',
                'sea_pickle', 'snow_layer', 'snow', 'puddle', 'bee_nest', 'hanging_roots', 'spore_blossom', 'twisting_vines', 'weeping_vines', 'obsidian'}
NATURAL_RE = re.compile(r'(^|_)(stone|rock|slate|basalt|granite|diorite|andesite|deepslate|tuff|calcite|limestone|marble|chalk|'
                        r'shale|gneiss|schist|quartzite|dolomite|sandstone|terracotta|ore|dirt|grass|sand|gravel|clay|mud|snow|'
                        r'ice|moss|netherrack|end_stone|dripstone|amethyst|coral|sculk|leaves|log|wood|mycelium|podzol|nylium|'
                        r'fern|bush|roots|fungus|wart_block|soul_soil|blackstone|magma_block|glowstone|lichen|petals)($|_|s$)')
BUILT_RE = re.compile(r'brick|polished|cut_|smooth|chiseled|_slab|_stairs|_wall|fence|gate|glazed|button|pressure|plate|tile|'
                      r'pillar|carved|lamp|stripped|plank|path|farmland|door|pane|sign|torch|lantern|bed$|_pot|chest|table|'
                      r'furnace|block_of|concrete|wool|carpet')

def natural(b):
    k = b.get('key', '')
    if b.get('carriable') or b.get('liquid') or k in NATURAL_KEYS: return True
    if BUILT_RE.search(k) or b.get('crop') or b.get('container') or b.get('station') or b.get('furniture'): return False
    if b.get('isLeaves') or b.get('isLog'): return True
    if b.get('render') == 3: return True   # a plant drawn as a cross (grasses, flowers, ferns)
    return b.get('render') in (1, 2) and not b.get('shapeKind') and bool(NATURAL_RE.search(k))

def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text())
    for b in bj['blocks']:
        k = b.get('key')
        if k not in NOT_CARRIABLE and (k in CARRIABLE or (b.get('flower') is True and not b.get('double') and b.get('render') == 3)):
            b['carriable'] = True
        else: b.pop('carriable', None)
        if k in TICKS: b['randomTick'] = True
        if natural(b): b['natural'] = True
        else: b.pop('natural', None)
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / 'data')
