#!/usr/bin/env python3
"""tools/port/stacking.py — everything stacks (to 64) unless stacking would break it: things that wear out (tools,
weapons, armour, horse and wolf armour, shields, bows, the elytra), containers you carry (backpacks, quivers, baskets,
saddle bags: their contents ride on the stack), things used up into something else (filled buckets, soups, stews:
the whole stack would turn into one bucket or bowl) and personal ones written or bound as you use them. Stacks only
ever merge when neither carries anything (ItemStack::canMerge), so a chest picked up full never joins empty ones.
Idempotent; data_patches.py runs it last.

  python3 tools/port/stacking.py [data-dir]
"""
import json, pathlib, sys

WEARS = ('tool', 'armor', 'horseArmor', 'wolfArmor', 'shield', 'bow', 'durability', 'forged')
HOLDS = ('backpack', 'quiver', 'basket', 'bags')
SINGLE = {'elytra', 'milk_bucket', 'water_bucket', 'lava_bucket', 'powder_snow_bucket', 'watering_can', 'steel_bucket',
          'beetroot_soup', 'mushroom_stew', 'rabbit_stew', 'suspicious_stew', 'tomato_soup', 'garden_salad', 'vegetable_stew',
          'travel_journal', 'label', 'we_wand', 'summoning_wand', 'death_compass', 'whistle', 'pocket_key', 'treasure_map',
          'enchanted_book', 'written_book', 'writable_book', 'filled_map', 'leather_backpack', 'reinforced_backpack',
          'explorer_backpack', 'quiver', 'golden_quiver', 'wicker_basket', 'saddle_bags', 'ceramic_vessel', 'unfired_vessel', 'unfired_jug'}


def stays_single(k, v):
    if k in SINGLE or any(w in v for w in WEARS) or any(h in k for h in HOLDS): return True
    if k.endswith('_bucket') and k != 'bucket': return True
    if k.endswith(('_stew', '_soup')): return True
    return False


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text())
    n = 0
    for k, v in items.items():
        if v.get('maxStack', 64) == 1 and not stays_single(k, v): v['maxStack'] = 64; n += 1
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    print('stacking: %d more things stack' % n)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
