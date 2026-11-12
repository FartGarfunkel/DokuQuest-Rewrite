#!/usr/bin/env python3
"""The way into magic, and a few things that had no use (idempotent; data_patches.py runs it last):
  python3 tools/port/magic_rework.py <data-dir>
- Starter tomes can be crafted: a book, lapis and what the spell is made of (fire, frost, light...), for the spells up to
  Magic 11. The deeper ones still come from librarians, chests and bosses. (Everyone also starts knowing Firebolt and
  Home Teleport, every spell casts bare-handed, and a tome too deep to learn teaches a little the first time it's read:
  src/sim/magic.cpp, magic_spells.cpp.)
- The Apprentice Staff from an amethyst shard or lapis too, not only an emerald.
- Venom sacs brew strong poison (they had no use). Dough is two flour and a water bucket (its recipe was bread's, so it
  could never be made)."""
import json, sys, pathlib

TOMES = {   # spell: the third thing beside the book and the lapis
    'tome_firebolt': 'coal', 'tome_frost': 'snowball', 'tome_light': 'glowstone_dust', 'tome_featherfall': 'feather',
    'tome_nightsight': 'golden_carrot', 'tome_heal': 'glistering_melon_slice', 'tome_flames': 'cinder_powder',
    'tome_telekinesis': 'ender_pearl', 'tome_healother': 'golden_apple', 'tome_waterbreath': 'pufferfish',
}


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); R = json.loads((d / 'recipes.json').read_text())
    mg = json.loads((d / 'magic.json').read_text())
    rec = R['recipes']

    def has(r): return any(x.get('out') == r.get('out') and x.get('items') == r.get('items') and x.get('shape') == r.get('shape') and x.get('key') == r.get('key') for x in rec)
    for tome, extra in TOMES.items():
        if tome in items and extra in items and 'book' in items and 'lapis_lazuli' in items:
            r = {'out': tome, 'n': 1, 'items': ['book', 'lapis_lazuli', extra], 'w': 2, 'h': 2, 'group': 'magic'}
            if not has(r): rec.append(r)
            ds = items[tome].get('desc', '')
            if 'Craft' not in ds: items[tome]['desc'] = ds + ' Craft: a book, lapis and ' + items[extra]['name'].lower() + '.'
    for gem in ('amethyst_shard', 'lapis_lazuli'):
        r = {'out': 'apprentice_staff', 'n': 1, 'shape': ['  G', ' S ', 'S  '], 'key': {'G': gem, 'S': 'stick'}, 'group': 'tools'}
        if gem in items and not has(r): rec.append(r)
    # every tome: what reading one above your Magic does
    for k, v in items.items():
        if v.get('tome') and 'studying' not in v.get('desc', ''):
            v['desc'] = v.get('desc', '') + ' Too deep for you yet? Studying it still teaches you something.'
    if 'goat_horn' in items: items['goat_horn']['desc'] = 'Knocked off when a goat rams something hard. Blow it: you and those round you are quicker and stronger for 10 s (once every 30 s).'
    # dough: two flour and a water bucket (the bucket comes back)
    rec[:] = [r for r in rec if not (r.get('out') == 'dough' and r.get('shape') == ['XXX'])]
    r = {'out': 'dough', 'n': 2, 'shape': ['FFW'], 'key': {'F': 'flour', 'W': 'water_bucket'}, 'keep': {'water_bucket': 'bucket'}, 'h': 1, 'w': 3}
    if 'dough' in items and not has(r): rec.append(r)
    # venom sacs brew strong poison
    if 'venom_sac' in items and 'potion_poison_strong' in items:
        mg.setdefault('brew', {}).setdefault('potion_awkward', {})['venom_sac'] = 'potion_poison_strong'
        if 'venom_sac' not in mg.setdefault('brewIngs', []): mg['brewIngs'].append('venom_sac')
        items['venom_sac']['desc'] = 'From snakes, scorpions and cave spiders. Brewed into an awkward potion: strong poison.'
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'recipes.json').write_text(json.dumps(R, separators=(',', ':'), ensure_ascii=False))
    (d / 'magic.json').write_text(json.dumps(mg, separators=(',', ':'), ensure_ascii=False))
    print('magic rework: %d tome recipes' % sum(1 for r in rec if r.get('out', '').startswith('tome_')))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
