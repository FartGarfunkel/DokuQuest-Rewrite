#!/usr/bin/env python3
"""Sheared sheep keep a short undercoat (as Minecraft's sheep do since 1.21.5): a thin layer of tufts over the bare
body, legs and the back of the head, in the sheep's own colour, until the wool grows back. Idempotent; data_patches.py
runs it after the art pass and before the creature looks (so every sheep look gets it too).
  - models.json: under / underCap / legUnder* parts (flag "undercoat": drawn only while sheared) on every model that
    has the sheep's wool parts.
  - data/textures/entity/sheep_wool_undercoat.png: our own picture of it, made from our wool tile (sparse short
    tufts, the rest clear). A resource pack's entity/sheep/sheep_wool_undercoat.png wins (render/creature_models.cpp).
"""
import json, pathlib, random
from PIL import Image

UNDER = [
    # name, parent, box (between the bare part and its wool)
    ('under', 'body', [-4.6, -3.6, -8.6, 4.6, 3.6, 8.6]),
    ('underCap', 'head', [-3.35, -0.6, -6.3, 3.35, 3.35, 0.35]),
    ('legUnderFL', 'legFL', [-2.3, -6, -2.3, 2.3, 0, 2.3]),
    ('legUnderFR', 'legFR', [-2.3, -6, -2.3, 2.3, 0, 2.3]),
    ('legUnderBL', 'legBL', [-2.3, -6, -2.3, 2.3, 0, 2.3]),
    ('legUnderBR', 'legBR', [-2.3, -6, -2.3, 2.3, 0, 2.3]),
]


def _texture(d):
    src = d / 'textures' / 'entity' / 'sheep_wool.png'
    out = d / 'textures' / 'entity' / 'sheep_wool_undercoat.png'
    if not src.exists():
        return
    w = Image.open(src).convert('RGBA')
    W, H = w.size
    rng = random.Random(1215)
    keep = [[False] * W for _ in range(H)]
    # clumps of short curls: seeds grown a step or two, about two fifths of the tile covered
    for _ in range(W * H // 9):
        x, y = rng.randrange(W), rng.randrange(H)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if (dx == 0 or dy == 0 or rng.random() < 0.35) and rng.random() < 0.8:
                    keep[(y + dy) % H][(x + dx) % W] = True
    o = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for y in range(H):
        for x in range(W):
            if keep[y][x]:
                r, g, b, a = w.getpixel((x, y))
                k = 0.9 + 0.08 * rng.random()   # a touch duller than full wool
                o.putpixel((x, y), (int(r * k), int(g * k), int(b * k), 255))
    o.save(out)


def main(d):
    d = pathlib.Path(d)
    models = json.loads((d / 'models.json').read_text())
    n = 0
    for key, m in models.items():
        parts = m.get('parts') or []
        names = {p.get('name') for p in parts}
        if 'wool' not in names or 'legWoolFL' not in names:
            continue
        wool = next(p for p in parts if p.get('name') == 'wool')
        if 'sheep_wool' not in json.dumps(wool.get('tex')):
            continue
        parts[:] = [p for p in parts if not p.get('undercoat')]   # idempotent
        for name, parent, box in UNDER:
            if parent not in names:
                continue
            parts.append({'name': name, 'parent': parent, 'at': [0, 0, 0], 'box': box, 'tex': 'sheep_wool_undercoat', 'undercoat': 1,
                          'pivot': [0, 0, 0], 'min': [b / 16 for b in box[:3]], 'max': [b / 16 for b in box[3:]]})
        n += 1
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    _texture(d)
    print(f'sheep undercoat: {n} models')


if __name__ == '__main__':
    main(pathlib.Path(__file__).resolve().parents[2] / 'data')
