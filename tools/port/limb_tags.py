#!/usr/bin/env python3
"""Severable limbs for the creatures that had none where they plainly have them (idempotent):
  python3 tools/port/limb_tags.py <data-dir>
A part with a `limb` tag can be cut off (sim/creature_rig.cpp tryDismember: the part a blow strikes, and all that hangs
from it). Wraiths' arms, werewolves' and kangaroos' tails, scorpions', lobsters' and aerwhales' tails (from the first
segment out), flying pigs', cows', phygs', valkyries' and bees' wings (a cut wing grounds a flyer), aerwhales' fins and
squids' tentacles had no tag: a blow there landed on the body. Birds' tail feathers and fish are left whole."""
import json, sys, pathlib

TAGS = {'armR': 'armR', 'armL': 'armL', 'tail': 'tail', 'tail1': 'tail', 'tailK': 'tail',
        'wingR': 'wingR', 'wingL': 'wingL', 'v_wingR': 'wingR', 'v_wingL': 'wingL', 'finR': 'finR', 'finL': 'finL'}
TAGS.update({'tent%d' % i: 'tent%d' % i for i in range(8)})
SKIP_RIGS = {'fish', 'wither', 'wither_storm'}   # fish stay whole; the Withers' "tail" is their spine (their fights are their own)

def main(d):
    d = pathlib.Path(d)
    models = json.loads((d / 'models.json').read_text())
    n = 0
    for key, m in models.items():
        if not isinstance(m, dict) or not m.get('rig') or m.get('rig') in SKIP_RIGS: continue
        parts = m.get('parts', []); by = {p.get('name'): p for p in parts}
        def tagged(p):   # it or what it hangs from already a limb
            q, guard = p, 0
            while q and guard < 16:
                if q.get('limb'): return True
                q = by.get(q.get('parent')); guard += 1
            return False
        for p in parts:
            t = TAGS.get(p.get('name'))
            if t and not tagged(p): p['limb'] = t; n += 1
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    print('limb_tags: %d parts tagged' % n)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
