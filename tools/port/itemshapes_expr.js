// tools/port/itemshapes_expr.js — the jsdump.py expression that generated data/itemshapes.json:
// how every block item is built (models.js itemMesh + items3d.js: cube faces, SHAPES boxes, shapeItemBoxes,
// the 3D torch, or a flat picture) and which tiles are item pictures (ATLAS.items, for resource packs).
//   python3 tools/port/jsdump.py <js-root> /tmp/d.json "$(cat tools/port/itemshapes_expr.js)"
//   then: {"itemTiles": d.items, "blockItems": d.shapes} -> data/itemshapes.json
(() => {
  const NM = {}; for (const [k, v] of Object.entries(ATLAS.tile)) if (NM[v] === undefined) NM[v] = k;
  const nm = (i) => NM[i] !== undefined ? NM[i] : null;
  const out = {};
  for (const [key, def] of Object.entries(ITEMS)) {
    if (!def.block) continue;
    const b = BLOCKS[B[def.block]]; if (!b) continue;
    const flat = !def.icon.startsWith('block:');
    const F = BLOCK_FACES[b.id];
    if (b.render === RENDER.MODEL && SHAPES[b.shape] && !b.furniture) {
      out[key] = { k: 'model', boxes: SHAPES[b.shape].map(box => {
        const side = box.side, end = box.end || side, xt = box.x || side;
        return [...box.b, side, side, end, end, side, side].map((v, i) => i === 6 || i === 7 ? xt : v).concat([box.tv || 0]);
      }) };
    } else if (b.render === RENDER.TORCH) out[key] = { k: 'torch', tile: nm(F[0]) };
    else if (!flat && (b.render === RENDER.CUBE || b.render === RENDER.CUTOUT)) out[key] = { k: 'cube', faces: F.map(nm) };
    else if (!flat && b.render === RENDER.SHAPE) {
      out[key] = { k: 'shape', boxes: shapeItemBoxes(b.id).map(bx => {
        const side = bx[6] !== undefined ? ATLAS.tile[bx[6]] ?? F[0] : F[0];
        const top = bx[7] !== undefined ? ATLAS.tile[bx[7]] ?? F[2] : (bx[6] !== undefined ? side : F[2]);
        const bot = bx[7] !== undefined ? top : (bx[6] !== undefined ? side : F[3]);
        const t = []; for (let f = 0; f < 6; f++) t.push(nm(bx.faces ? (ATLAS.tile[bx.faces[f]] ?? side) : f === 2 ? top : f === 3 ? bot : f === 4 && bx[6] === undefined ? F[4] : side));
        return [bx[0], bx[1], bx[2], bx[3], bx[4], bx[5], ...t, 0];
      }) };
    } else out[key] = { k: 'sprite', tile: nm(ATLAS.tile[def.icon] ?? F[0]) };
  }
  const need = ['sleeve', 'skin', 'pack_body_l', 'pack_strap', 'leather', 'stripped_oak_log_side', 'white_wool', 'iron_block', 'dark_oak_planks', 'red_wool', 'stripped_birch_log_side', 'stripped_spruce_log_side', 'bag_top', 'rope', 'hook_metal', 'campfire_log', 'pot_iron'];
  return { items: [...(ATLAS.items || [])], shapes: out, need: need.map(n => [n, ATLAS.tile[n] ?? null]) };
})()
