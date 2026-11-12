#!/usr/bin/env python3
"""tools/check_village_pieces.py — checks the village buildings (data/villages, from tools/port/village_builder.py) can
be walked: the square you climb each ladder from is clear at its foot, nothing stands before a hearth's fire, and every igloo's tunnel is sealed and two high
from its door into the dome.

  python3 tools/check_village_pieces.py [data-dir]      exit code 0 = everything passed
"""
import glob, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'port'))
import nbt_io

FRONT = {'west': (-1, 0), 'east': (1, 0), 'north': (0, -1), 'south': (0, 1)}
OPEN = ('air', 'cave_air', 'ladder', 'wall_torch', 'jigsaw')


def load(f):
    t = nbt_io.read(f)
    t = t[1] if isinstance(t, tuple) else t
    pal = t['palette']
    return {tuple(b['pos']): pal[b['state']] for b in t['blocks']}


def name(g, p):
    s = g.get(p)
    return s['Name'].split(':')[1] if s else 'air'


def passable(n): return n in OPEN or n.endswith('_carpet') or n.endswith('_pressure_plate')


def main():
    data = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'data')
    bad = []
    files = sorted(glob.glob(str(data / 'villages' / '*' / 'houses' / '*.nbt')))
    ladders = igloos = hearths = 0
    for f in files:
        g = load(f)
        short = f.split('villages/')[1]
        for (x, y, z), st in g.items():   # a ladder's foot (no ladder under it): its front clear two high
            if not st['Name'].endswith(':ladder') or name(g, (x, y - 1, z)) == 'ladder': continue
            dx, dz = FRONT[st.get('Properties', {}).get('facing', 'west')]
            ladders += 1
            for yy in (y, y + 1):
                n = name(g, (x + dx, yy, z + dz))
                if not passable(n): bad.append('%s: ladder at %s blocked by %s' % (short, (x, y, z), n))
        for (x, y, z), st in g.items():   # a hearth's fire (a campfire set in the wall): the square before it clear
            if not st['Name'].endswith(':campfire'): continue
            dx, dz = FRONT[st.get('Properties', {}).get('facing', 'west')]
            if name(g, (x - dx, y, z - dz)) == 'air': continue   # (its back to the room: not set in a wall)
            hearths += 1
            n = name(g, (x + dx, y, z + dz))
            if not passable(n): bad.append('%s: hearth at %s blocked by %s' % (short, (x, y, z), n))
        if '/igloo_' in f:
            doors = [p for p, s in g.items() if s['Name'].endswith('_door') and p[1] == 1]
            if not doors: continue
            dx0, _, cz = doors[0]
            if name(g, (dx0, 1, cz - 2)) != 'air' or name(g, (dx0, 1, cz + 2)) != 'air': continue   # (a snow house: its door is in a straight wall, no tunnel)
            igloos += 1
            x = dx0 + 1
            while x < dx0 + 8:   # walk in from the door until the room opens out on both sides
                if name(g, (x, 1, cz - 1)) == 'air' and name(g, (x, 1, cz + 1)) == 'air': break
                for yy in (1, 2):
                    if name(g, (x, yy, cz)) != 'air': bad.append('%s: igloo tunnel blocked at %s' % (short, (x, yy, cz)))
                    for zz in (cz - 1, cz + 1):
                        if name(g, (x, yy, zz)) == 'air': bad.append('%s: igloo tunnel open to the side at %s' % (short, (x, yy, zz)))
                if name(g, (x, 3, cz)) == 'air': bad.append('%s: igloo tunnel open to the sky at %s' % (short, (x, 3, cz)))
                x += 1
            else:
                bad.append('%s: igloo tunnel never reaches the room' % short)
    for b in bad: print('FAIL', b)
    print('village pieces: %d files, %d ladders, %d igloos, %d hearths, %d problems' % (len(files), ladders, igloos, hearths, len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
