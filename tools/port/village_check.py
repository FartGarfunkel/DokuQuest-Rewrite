#!/usr/bin/env python3
"""tools/port/village_check.py — check every village piece (python3 tools/port/village_check.py data/villages) for things hanging in the air: frames, torches and shutters with no wall behind them,
lanterns with nothing over (hanging) or under them, fences and blocks touching nothing, closed top trapdoors."""
import collections, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import nbt_io

OPP = {'north': 'south', 'south': 'north', 'east': 'west', 'west': 'east'}
D = {'north': (0, 0, -1), 'south': (0, 0, 1), 'west': (-1, 0, 0), 'east': (1, 0, 0), 'up': (0, 1, 0), 'down': (0, -1, 0)}
SOFT = ('air', 'structure_void', 'jigsaw', 'item_pile', 'item_frame', 'wall_torch', 'torch', 'lantern', 'soul_lantern', 'vine', 'snow', 'carpet', 'cobweb')


def check(path):
    t = nbt_io.read(path); pal = t['palette']
    cells = {}
    for b in t['blocks']:
        e = pal[b['state']]; n = e['Name'].split(':')[-1]
        if n in ('air', 'structure_void', 'jigsaw'): continue
        cells[tuple(b['pos'])] = (n, e.get('Properties', {}))
    def solid(c):
        v = cells.get(c)
        return v is not None and not any(v[0].endswith(s) or v[0] == s for s in SOFT) and 'carpet' not in v[0] and 'pressure_plate' not in v[0]
    out = []
    for (x, y, z), (n, pr) in cells.items():
        f = pr.get('facing')
        def at(d, c=(x, y, z)): dx, dy, dz = D[d]; return (c[0] + dx, c[1] + dy, c[2] + dz)
        if n.endswith('_door') and pr.get('half') == 'lower' and f:   # the way through: clear both sides, both heights
            for d in (f, OPP[f]):
                for dy in (0, 1):
                    c = at(d, (x, y + dy, z)); v = cells.get(c)
                    if v and not (v[0].endswith('carpet') or 'pressure_plate' in v[0] or v[0] == 'snow'): out.append(('in a doorway', v[0], c))
        if (n == 'item_frame' or n == 'wall_weapon_rack' or (n.endswith('_shelf') and 'standing' not in n)) and f and not solid(at(OPP[f])): out.append(('off its wall', n, (x, y, z)))
        elif n == 'wall_torch' and f and not solid(at(OPP[f])): out.append(('torch off its wall', n, (x, y, z)))
        elif n.endswith('_trapdoor'):
            if pr.get('open') == 'true' and f and not solid(at(OPP[f])): out.append(('shutter off its wall', n, (x, y, z)))
            elif pr.get('open') != 'true' and pr.get('half') == 'top' and not solid(at('up')): out.append(('trapdoor plate in the air', n, (x, y, z)))
        elif n in ('lantern', 'soul_lantern'):
            if pr.get('hanging') == 'true' and not solid(at('up')): out.append(('hanging lantern on nothing', n, (x, y, z)))
            elif pr.get('hanging') != 'true' and not solid(at('down')) and cells.get(at('down'), ('',))[0] not in ('oak_fence',) and not cells.get(at('down')): out.append(('lantern on nothing', n, (x, y, z)))
        elif n.endswith('_fence') or n.endswith('_wall'):
            if y > 0 and not cells.get(at('down')) and not any(cells.get(at(d)) for d in ('north', 'south', 'east', 'west', 'up')): out.append(('fence in the air', n, (x, y, z)))
        elif 'leaves' in n or n in ('vine',): continue
        elif y > 0 and not any(cells.get(at(d)) for d in D): out.append(('block in the air', n, (x, y, z)))
    return out


def main(root):
    root = pathlib.Path(root)
    tally = collections.Counter(); ex = collections.defaultdict(list)
    for p in sorted(root.rglob('*.nbt')):
        for kind, n, c in check(p):
            tally[kind] += 1
            if len(ex[kind]) < 6: ex[kind].append('%s %s %s' % (p.relative_to(root), n, c))
    for k, v in tally.most_common(): print('%5d  %s' % (v, k)); [print('        ' + e) for e in ex[k]]
    if not tally: print('nothing in the air')


main(sys.argv[1] if len(sys.argv) > 1 else 'data/villages')
