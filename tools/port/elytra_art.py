#!/usr/bin/env python3
"""tools/port/elytra_art.py — the elytra in the DokuQuest Base pack's style, shaped like the classic elytra: two panels
of charcoal membrane on a light grey frame (a leading-edge bone along the top, the hinge bone down the shoulder side,
three ribs fanning from the hinge to a ragged trailing edge). Drawn here (no Minecraft picture is used), then given the
pack's own grain: the membrane takes the pack's black wool's light and shade, the bones its bone block's, with the
pack's dark outline round the silhouette.

Writes the 128 x 64 entity picture (the standard 64 x 32 elytra layout at 2x: the 10 x 20 x 2 wing box at u 22) and the
32 x 32 item icon into the local overlay pack "Mod Art (local)" (packs.cpp lays it over the chosen pack; never commit it):

  python3 tools/port/elytra_art.py [data dir] [overlay pack dir]
"""
import math, pathlib, sys
import numpy as np
from PIL import Image

S = 2   # pixels per vanilla texel (the pack is 32x)


def grain(path, w, h):
    """the pack picture's light and shade round its mean, tiled to w x h (-1..1 roughly)"""
    im = np.asarray(Image.open(path).convert('RGB')).astype(np.float32)
    l = im[..., 0] * 0.299 + im[..., 1] * 0.587 + im[..., 2] * 0.114
    l = (l - l.mean()) / max(1.0, l.std() * 2.2)
    reps = (h // l.shape[0] + 1, w // l.shape[1] + 1)
    return np.tile(l, reps)[:h, :w]


def wing_face(gm, gb, inner=False):
    """one 20 x 40 face (2x of 10 x 20), the hinge on the left: RGBA. As the classic elytra (the reference picture): a
    bone bar along the top, bone down the hinge side and the outer edge, ribs fanning down in a zig-zag, and the
    membrane between them cut back in deep scallops, so the trailing edge ends in long bony fingers."""
    W, H = 10 * S, 20 * S
    out = np.zeros((H, W, 4), np.float32)
    # the trailing edge: the bones' tips (x, y), hinge to outer edge; between two tips the membrane sags up
    tips = [(0.0, 31.0), (7.0, 39.5), (14.0, 37.5), (19.5, 27.0)]
    def bottom(x):
        for (x0, y0), (x1, y1) in zip(tips, tips[1:]):
            if x0 <= x <= x1:
                u = (x - x0) / (x1 - x0)
                return y0 + (y1 - y0) * u - 7.0 * math.sin(math.pi * u) ** 0.8
        return tips[0][1] if x < tips[0][0] else tips[-1][1]
    def inside(x, y):
        b = bottom(x + 0.5)
        if y + 0.5 > b: return False
        # a ragged, feathered edge: every other column a step shorter near the cut
        return not (y + 0.5 > b - 1.2 and (x // 2) % 2 == 1 and (x, y) not in TIP_PIX)
    TIP_PIX = set()
    for tx, ty in tips: TIP_PIX.update({(int(tx) + dx, int(ty) - dy) for dx in (-1, 0, 1) for dy in range(3)})
    # the bones: (from, to, half-width) in face pixels
    BONES = [((0.8, 1.0), (0.8, 31.0), 1.1),        # down the hinge
             ((2.5, 2.5), (7.0, 39.0), 0.85),       # the first rib, hinge to the long finger
             ((10.5, 2.5), (14.0, 37.0), 0.85),     # the second, from the middle of the bar
             ((19.0, 1.0), (19.0, 27.0), 1.0),      # down the outer edge
             ((19.0, 9.0), (8.5, 23.0), 0.75),      # struts across: the zig-zag (a diamond with the ribs)
             ((4.0, 20.0), (12.5, 30.0), 0.7)]
    def seg_d(x, y, a, b):
        (ax, ay), (bx, by) = a, b
        vx, vy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
        return math.hypot(x - ax - t * vx, y - ay - t * vy)
    def is_bone(x, y):
        if y < 3: return True                                                    # the leading-edge bar
        if x < 4 and y < 5: return True                                          # the knuckle at the hinge
        return any(seg_d(x + 0.5, y + 0.5, a, b) < w for a, b, w in BONES)
    mem = np.array([0.11, 0.11, 0.13]) * 255 * (0.8 if inner else 1.0)
    bone = np.array([0.66, 0.66, 0.68]) * 255 * (0.86 if inner else 1.0)
    for y in range(H):
        for x in range(W):
            if not inside(x, y): continue
            if is_bone(x, y):
                c = bone * (1 + gb[y, x] * 0.10)
                if y == 0 or (y < 3 and x < 3) or not is_bone(x - 1, y): c = c * 1.14   # lit on its top and left
                elif not is_bone(x + 1, y): c = c * 0.8                                 # shaded on its right
            else:
                c = mem * (1 + gm[y, x] * 0.25)
                c = c * (1.0 - 0.2 * (y / H))                                            # darker toward the fingers
                if is_bone(x, y - 1) or is_bone(x - 1, y): c = c * 0.7                   # tucked under the bones
            out[y, x, :3] = np.clip(c, 0, 255); out[y, x, 3] = 255
    a = out[..., 3] > 0
    edge = a & ~(np.roll(a, 1, 0) & np.roll(a, -1, 0) & np.roll(a, 1, 1) & np.roll(a, -1, 1))
    edge[0, :] = False; edge[:, 0] = False    # (the hinge side and the top edge carry on round the box)
    out[edge, :3] *= 0.6
    return out


def entity_sheet(base, ov):
    tex = base / 'assets/minecraft/textures'
    W, H = 10 * S, 20 * S
    gm = grain(tex / 'block/black_wool.png', W, H)
    gb = grain(tex / 'block/bone_block_side.png', W, H) if (tex / 'block/bone_block_side.png').exists() else gm
    back = wing_face(gm, gb)                       # the outer face (+z, seen from behind when folded): hinge left
    front = wing_face(gm, gb, inner=True)[:, ::-1]   # the inner face, the hinge on the right
    sheet = np.zeros((32 * S, 64 * S, 4), np.float32)
    def put(img, u, v): sheet[v * S:v * S + img.shape[0], u * S:u * S + img.shape[1]] = img
    put(front, 24, 2); put(back, 36, 2)
    # the box's thin sides: the top is bone, the hinge side bone, the outer side and the bottom membrane
    bone_strip = np.zeros((2 * S, W, 4), np.float32); bone_strip[...] = back[1:2, :, :]; bone_strip[..., 3] = 255
    put(bone_strip, 24, 0)
    # the bottom face stays clear: a solid one filled the box's last row and joined the fingers into a straight edge
    put(np.zeros((2 * S, W, 4), np.float32), 34, 0)
    hinge = np.zeros((H, 2 * S, 4), np.float32); hinge[...] = back[:, 0:1, :] * np.array([1, 1, 1, 1]); hinge[..., 3] = np.where(back[:, 0, 3] > 0, 255, 0)[:, None]
    put(hinge, 22, 2)
    outer = np.zeros((H, 2 * S, 4), np.float32); outer[...] = front[:, 0:1, :]; outer[..., 3] = np.where(front[:, 0, 3] > 0, 255, 0)[:, None]
    put(outer, 34, 2)
    p = ov / 'assets/minecraft/textures/entity/elytra.png'; p.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(sheet.astype(np.uint8), 'RGBA').save(p)
    return p


def item_icon(base, ov):
    """two folded wings side by side, frames up, as the item"""
    tex = base / 'assets/minecraft/textures'
    gm = grain(tex / 'block/black_wool.png', 32, 32)
    gb = grain(tex / 'block/bone_block_side.png', 32, 32) if (tex / 'block/bone_block_side.png').exists() else gm
    face = wing_face(gm[:40, :20] if gm.shape[0] >= 40 else np.tile(gm, (2, 1))[:40, :20], np.tile(gb, (2, 1))[:40, :20])
    small = np.asarray(Image.fromarray(face.astype(np.uint8), 'RGBA').resize((12, 24), Image.NEAREST)).astype(np.float32)
    out = np.zeros((32, 32, 4), np.float32)
    left = small[:, ::-1]   # the left wing: hinge toward the middle
    out[4:28, 3:15] = left; out[4:28, 17:29] = small
    a = out[..., 3] > 0
    ring = ~a & (np.roll(a, 1, 0) | np.roll(a, -1, 0) | np.roll(a, 1, 1) | np.roll(a, -1, 1))
    out[ring] = [18, 18, 22, 255]   # the pack's items have a dark outline
    p = ov / 'assets/minecraft/textures/item/elytra.png'; p.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(out.astype(np.uint8), 'RGBA').save(p)
    return p


def main(data, ov=None):
    data = pathlib.Path(data)
    ov = pathlib.Path(ov) if ov else pathlib.Path.home() / '.local/share/DokuQuest/resourcepacks/Mod Art (local)'
    base = data / 'resourcepacks/DokuQuest Base'
    if not (base / 'assets/minecraft/textures/block/black_wool.png').exists(): print('elytra_art: no DokuQuest Base pack - skipped'); return
    print("elytra_art:", entity_sheet(base, ov), item_icon(base, ov))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data', sys.argv[2] if len(sys.argv) > 2 else None)
