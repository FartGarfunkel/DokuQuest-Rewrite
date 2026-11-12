#!/usr/bin/env python3
"""tools/port/audit.py — a whole-game data check: is everything connected, and does everything have a use?

  python3 tools/port/audit.py data [--src src]

Reports (counts first, then the lists):
- broken links: recipe/smelting/tag/drop/loot/trade entries that name an item or block that doesn't exist, items whose
  icon tile is missing
- unobtainable: survival items with no way to get them (no recipe, smelt, block drop, mob drop, loot, trade, fishing,
  forge, sieve, or mention in the C++ that hands it out)
- unused: items that are not placeable, not a tool/armour/food/drink/fuel/..., not an ingredient anywhere, and not
  named in the C++ (where a use would be coded)
"""
import json, os, re, sys
from collections import defaultdict


def load(d, n):
    p = os.path.join(d, n)
    return json.load(open(p)) if os.path.exists(p) else None


def main(d, src):
    items = load(d, 'items.json'); items = items.get('items', items)
    blocks = load(d, 'blocks.json'); blocks = blocks.get('blocks', blocks)
    rc = load(d, 'recipes.json'); tiles = load(d, 'tiles.json')['names']
    mobs = load(d, 'mobs.json'); mobs = mobs.get('mobs', mobs)
    bkeys = {b['key'] for b in blocks}
    ikeys = set(items)
    known = ikeys | bkeys
    tags = rc.get('tags', {})
    def expand(k):
        if isinstance(k, str) and k.startswith('#'): return tags.get(k, [])
        return [k]
    broken = []
    made = defaultdict(set)      # item -> how you get it
    used = defaultdict(set)      # item -> what it's used for
    for t, lst in tags.items():
        for k in lst:
            if k not in known: broken.append(f'tag {t}: {k}')
    for r in rc['recipes']:
        out = r.get('out')
        if out not in known: broken.append(f'recipe out: {out}')
        made[out].add('recipe')
        ins = list(r.get('key', {}).values()) + list(r.get('items', []) or [])
        for k in ins:
            if isinstance(k, list): ks = [x for y in k for x in expand(y)]
            else: ks = expand(k)
            if not ks: broken.append(f'recipe {out}: empty {k}')
            for x in ks:
                if x not in known: broken.append(f'recipe {out}: in {x}')
                used[x].add('ingredient')
    for a, b in rc.get('smelting', {}).items():
        out = b if isinstance(b, str) else (b.get('out') if isinstance(b, dict) else b[0])
        for x in expand(a):
            if x not in known: broken.append(f'smelt in: {x}')
            used[x].add('smelt')
        if out not in known: broken.append(f'smelt out: {out}')
        made[out].add('smelt')
    for b in blocks:
        dr = b.get('drop')
        if dr == 'none': pass
        elif isinstance(dr, str) and dr:
            if dr not in known: broken.append(f'block {b["key"]} drops {dr}')
            made[dr].add('block')
        elif isinstance(dr, list):
            for e in dr:
                k = e[0] if isinstance(e, list) else (e.get('id') if isinstance(e, dict) else e)
                if k and k not in known: broken.append(f'block {b["key"]} drops {k}')
                if k: made[k].add('block')
        elif dr is None or dr is True or dr == '':
            made[b['key']].add('block')
    for m, v in mobs.items():
        for e in v.get('drops', []) or []:
            k = e[0] if isinstance(e, list) else e
            if k not in known: broken.append(f'mob {m} drops {k}')
            made[k].add('mob')
    for k, v in items.items():
        ic = v.get('icon')
        if ic and ic.startswith('block:'):
            if ic[6:] not in bkeys: broken.append(f'item {k}: icon block {ic[6:]} missing')
        elif ic and ic not in tiles: broken.append(f'item {k}: no icon tile {ic}')
    # everything the C++ mentions by name (loot tables, trades, fishing, forge, sieve, use handlers...)
    code = ''
    for root, _, fs in os.walk(src):
        for f in fs:
            if f.endswith(('.cpp', '.h')): code += open(os.path.join(root, f), errors='ignore').read()
    for f in ('villagers.json', 'machines.json', 'magic.json', 'shapes.json', 'models.json'):
        p = os.path.join(d, f)
        if os.path.exists(p): code += open(p).read()
    named = set(re.findall(r'"([a-z0-9_]+)"', code))
    def has_use(k, v):
        if k in bkeys or v.get('block'): return True
        for f in ('tool', 'armor', 'food', 'drink', 'potion', 'splash', 'fuel', 'spawnEgg', 'forgeHead', 'tome', 'cape', 'dye',
                  'firework', 'plants', 'hat', 'mobHead', 'charm', 'cart', 'boat', 'staff', 'hook', 'bow', 'disc', 'bucket',
                  'backpack', 'compass', 'igniter', 'mapItem', 'snowball', 'bonemeal', 'bottle', 'weWand', 'heartCrystal', 'clock',
                  'spyglass', 'shears', 'summon', 'pocketKey', 'fishing', 'xpBottle', 'gustPuff', 'grapple', 'campKit', 'pearl',
                  'light', 'holy', 'school', 'lore', 'forged'):
            if f in v: return True
        return bool(used.get(k)) or k in named
    unobt, unused = [], []
    for k, v in sorted(items.items()):
        if v.get('hidden') or v.get('spawnEgg') or 'debug' in k: continue
        if not made.get(k) and k not in named and not v.get('forged'): unobt.append(k)
        if not has_use(k, v): unused.append(k)
    print(f'broken links: {len(broken)}   unobtainable: {len(unobt)}   no use: {len(unused)}')
    for t, lst in (('BROKEN', broken), ('UNOBTAINABLE', unobt), ('NO USE', unused)):
        if lst: print(f'\n{t}:\n  ' + '\n  '.join(lst[:400]))


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0] if a else 'data', a[a.index('--src') + 1] if '--src' in a else 'src')
