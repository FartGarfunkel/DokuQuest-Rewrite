#!/usr/bin/env python3
"""tools/port/plant_heads.py — the sunflower's head: a picture of its own (the front, and the back), drawn as a face
on the top half of the plant (render/mesh_plants.cpp emitSunflowerHead). A pack draws it as separate pictures
(sunflower_front / sunflower_back) beside the stalk (sunflower_top), so the game's one "upper" picture was the stalk
alone. Painted fallbacks here (data/textures/block), the pack's own pictures over them (data/packmap.json).
  python3 tools/port/plant_heads.py [data-dir]"""
import json, math, pathlib, sys
from PIL import Image

def main(d):
    d = pathlib.Path(d)
    tj = json.loads((d / 'tiles.json').read_text()); pm = json.loads((d / 'packmap.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    (d / 'textures/block').mkdir(parents=True, exist_ok=True)

    def paint(back):
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); px = im.load()
        for y in range(32):
            for x in range(32):
                dx, dy = x - 15.5, y - 15.5; r = math.hypot(dx, dy); a = math.atan2(dy, dx)
                if r < 7 and not back: px[x, y] = (96, 52, 18, 255) if (x + y) & 1 else (122, 72, 28, 255)   # the seeds
                elif r < 7: px[x, y] = (88, 120, 40, 255)                                                     # the green back
                elif r < 15 and (r < 11 or math.cos(a * 12) > -0.3):                                           # the petals
                    px[x, y] = (250, 206, 40, 255) if not back else (226, 176, 32, 255)
        return im

    for name, back in (('sunflower_front', False), ('sunflower_back', True)):
        rel = 'textures/block/%s.png' % name
        paint(back).save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if not any(isinstance(e, list) and e and e[0] == name for e in pm): pm.append([name, [name], [name]])
    # the top half's own picture is the stalk (the head is drawn on its own now): not the head
    for e in pm:
        if isinstance(e, list) and e and e[0] == 'sunflower_upper': e[1] = ['sunflower_top', 'sunflower_upper']
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), ensure_ascii=False))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / 'data')
