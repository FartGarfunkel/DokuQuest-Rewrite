// sim/build.cpp — building rules (port of game/build.js, the player side of game/blocks2.js,
// blockfx.js flint and steel, furniture.js sitting and shelves, local.js Carry On / ground storage).
#include "sim/controller.h"
#include "sim/vehicles.h"
#include "sim/rules.h"
#include "sim/blockfx.h"
#include "sim/worldsys.h"
#include <random>

namespace dq {

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

// ---- block entities in the original's format ---------------------------------------------------
json* containerEntity(World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  const BlockDef& b = reg().block(id);
  json* e = w.blockEntity(x, y, z);
  if (e && e->is_object() && e->contains("type")) return e;
  json make;
  if (bhas(id, BF::Container)) { make = {{"type", "chest"}, {"items", json::array()}}; for (int i = 0, n = (int)b.num("container", 27); i < n; i++) make["items"].push_back(nullptr); }
  else if (bhas(id, BF::Furnace)) make = {{"type", "furnace"}, {"items", {nullptr, nullptr, nullptr}}, {"burn", 0}, {"burnMax", 0}, {"cook", 0}};
  else if (b.has("shelf") || b.has("bookcase")) { make = {{"type", b.has("shelf") ? "shelf" : "bookcase"}, {"items", json::array()}}; for (int i = 0, n = (int)b.num(b.has("shelf") ? "shelf" : "bookcase", 3); i < n; i++) make["items"].push_back(nullptr); }
  else if (b.has("pile")) make = {{"type", "pile"}, {"items", {nullptr, nullptr, nullptr, nullptr}}, {"rot", {0, 0, 0, 0}}};
  else return e;
  w.setBlockEntity(x, y, z, make);
  return w.blockEntity(x, y, z);
}
bool chestPartner(World& w, int x, int y, int z, ivec3& out) {
  BlockId chest = KB().chest;
  if (!chest || w.getBlock(x, y, z) != chest) return false;
  int m = w.getMeta(x, y, z) & 3, ax = (m & 1) ? 0 : 1, az = (m & 1) ? 1 : 0;
  auto same = [&](int k) { return w.getBlock(x + ax * k, y, z + az * k) == chest && (w.getMeta(x + ax * k, y, z + az * k) & 3) == m; };
  int below = 0; while (below < 16 && same(-(below + 1))) below++;
  int k = (below & 1) ? -1 : (same(1) ? 1 : 0);
  if (!k) return false;
  out = {x + ax * k, y, z + az * k};
  return true;
}

static std::string doorSound(BlockId id, bool open) {
  const std::string& k = reg().block(id).key;
  std::string kind = (k.rfind("iron", 0) == 0 || k.find("_iron") != std::string::npos || k.rfind("copper", 0) == 0 || k.find("_copper") != std::string::npos) ? "door_iron"
                   : bhas(id, BF::Trapdoor) ? "trapdoor" : reg().tShape[id] == ShapeKind::Gate ? "gate" : "door";
  return kind + (open ? "_open" : "_close");
}

// ---- placing --------------------------------------------------------------------------------------
bool PlayerController::placement(const RayHit& hit, BlockId bid, Placement& out) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  out = Placement{};
  for (auto& h : hooks().place) { int r = h(*this, hit, bid, out); if (r == 0) return false; if (r == 1) return true; }
  const BlockDef& d = R.block(bid);
  int kind = R.tShape[bid];
  u8 hitMeta = w.getMeta(hit.block.x, hit.block.y, hit.block.z);
  ivec3 t = placeTarget(hit);
  int x = t.x, y = t.y, z = t.z;
  BlockId cur = w.getBlock(x, y, z);
  bool wet = R.tLiquid[cur] == 1 && (w.getMeta(x, y, z) & 15) == 0;
  int nx = hit.normal.x, ny = hit.normal.y, nz = hit.normal.z;
  auto set = [&](int xx, int yy, int zz, u8 m, bool merge = false) { out.x = xx; out.y = yy; out.z = zz; out.meta = m; out.merge = merge; return true; };
  // ---- placeSpecial (blocks2.js) ----
  if (kind == ShapeKind::Candle) {
    if (hit.id == bid && (hitMeta & 3) < 3) return set(hit.block.x, hit.block.y, hit.block.z, hitMeta + 1, true);
    if (!R.tSolid[w.getBlock(x, y - 1, z)]) return false;
    return set(x, y, z, wet ? WLOG : 0);
  }
  if (kind == ShapeKind::Cake) { if (!R.tSolid[w.getBlock(x, y - 1, z)]) return false; return set(x, y, z, 0); }
  if (kind == ShapeKind::Rod) return set(x, y, z, (u8)((nx ? 1 : nz ? 2 : 0) | (wet && R.tWlog[bid] ? WLOG : 0)));
  if (kind == ShapeKind::Frame) {
    if (ny != 0) { toastMsg("Hang item frames on a wall"); return false; }
    int f = dirOf((float)-nx, (float)-nz);
    if (!R.tSolid[w.getBlock(x + DIRX[f], y, z + DIRZ[f])]) return false;
    return set(x, y, z, (u8)f);
  }
  if (d.has("facingModel")) return set(x, y, z, (u8)(d.str("station") == "anvil" ? (lookDir(p) + 1) & 3 : lookDir(p)));
  if (bid == B.pointed_dripstone && bid) return set(x, y, z, (u8)((ny == -1 ? 1 : 0) | (wet ? WLOG : 0)));
  if (bhas(bid, BF::Hangs) && ny != -1 && w.getBlock(x, y + 1, z) != bid && !R.tSolid[w.getBlock(x, y + 1, z)]) { toastMsg("This hangs from a ceiling"); return false; }
  // ---- the usual rules (build.js) ----
  vec3 fr = hitFrac(hit);
  bool alt = placeMode == 1, side = ny == 0;
  int look = lookDir(p);
  u8 meta = 0;
  if (kind == ShapeKind::Slab) {
    int hk = hitMeta & 7;
    auto comp = [](int a, int b) { return (a == 0 && b == 1) || (a == 1 && b == 0) || (a >= 2 && a <= 5 && b >= 2 && b <= 5 && ((a - 2 + 2) & 3) == b - 2); };
    if (hit.id == bid && hk != 6) {
      bool into = (hk == 0 && ny == 1) || (hk == 1 && ny == -1) || (hk >= 2 && hk <= 5 && dirOf((float)-nx, (float)-nz) == hk - 2 && side);
      if (into) return set(hit.block.x, hit.block.y, hit.block.z, 6, true);
    }
    int k = alt ? 2 + (side ? dirOf((float)-nx, (float)-nz) : look) : (ny == 1 ? 0 : ny == -1 ? 1 : fr.y > 0.5f ? 1 : 0);
    if (cur == bid && comp(w.getMeta(x, y, z) & 7, k)) return set(x, y, z, 6, true);
    meta = (u8)k;
  } else if (kind == ShapeKind::Stairs) {
    meta = (u8)(look | (alt || ny == -1 || (side && fr.y > 0.5f) ? 4 : 0));
  } else if (kind == ShapeKind::Gate) {
    meta = (u8)look;
  } else if (kind == ShapeKind::Door) {
    BlockId up = w.getBlock(x, y + 1, z);
    if (!inWorldY(y + 1) || (!R.tReplaceable[up] && up != AIR)) return false;
    if (!R.tSolid[w.getBlock(x, y - 1, z)]) return false;
    int f = look, right = (f + 1) & 3, left = (f + 3) & 3;
    BlockId ln = w.getBlock(x + DIRX[left], y, z + DIRZ[left]), rn = w.getBlock(x + DIRX[right], y, z + DIRZ[right]);
    bool hingeRight;
    if (bhas(ln, BF::Door)) hingeRight = true;
    else if (bhas(rn, BF::Door)) hingeRight = false;
    else hingeRight = DIRX[right] * (fr.x - 0.5f) + DIRZ[right] * (fr.z - 0.5f) > 0;
    meta = (u8)(f | (hingeRight ? 8 : 0));
    out.also.push_back({x, y + 1, z, bid, meta | 16});
  } else if (kind == ShapeKind::Trapdoor) {
    int f = side ? dirOf((float)-nx, (float)-nz) : (look + 2) & 3;
    meta = (u8)(f | (alt || ny == -1 || (side && fr.y > 0.5f) ? 8 : 0));
  } else if (kind == ShapeKind::Ladder) {
    if (!side) return false;
    int f = dirOf((float)-nx, (float)-nz);
    if (!R.tSolid[w.getBlock(x + DIRX[f], y, z + DIRZ[f])]) return false;
    meta = (u8)f;
  } else if (kind == ShapeKind::Bed) {
    int f = look, hx = x + DIRX[f], hz = z + DIRZ[f];
    auto freeAt = [&](int a, int b) { BlockId id = w.getBlock(a, y, b); return id == AIR || R.tReplaceable[id]; };
    if (!freeAt(hx, hz) || !R.tSolid[w.getBlock(x, y - 1, z)] || !R.tSolid[w.getBlock(hx, y - 1, hz)]) { toastMsg("Not enough room for the bed"); return false; }
    meta = (u8)f;
    out.also.push_back({hx, y, hz, bid, f | 4});
  } else if (bhas(bid, BF::Double)) {
    BlockId up = w.getBlock(x, y + 1, z);
    if (!inWorldY(y + 1) || (up && !R.tReplaceable[up])) return false;
    out.also.push_back({x, y + 1, z, bid, 1});
  } else if (bhas(bid, BF::AxisBlock)) {
    meta = (u8)(nx ? 1 : nz ? 2 : 0);
  } else if (bhas(bid, BF::Sign)) {
    if (side) meta = (u8)(16 | dirOf((float)-nx, (float)-nz));
    else if (ny == 1) meta = (u8)((int)std::round(std::fmod(std::fmod(p.yaw, TAU) + TAU, TAU) / TAU * 16) & 15);
    else return false;
  } else if (bhas(bid, BF::FacingFront)) {
    meta = (u8)facingMeta(p);
    // a chest set beside a lone chest turns to match it so the two join
    if (bid == B.chest && bid) {
      int pick = -1;
      for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        int ax = x + dx, az = z + dz;
        if (w.getBlock(ax, y, az) != B.chest) continue;
        int nm = w.getMeta(ax, y, az) & 3;
        ivec3 partner;
        if (((nm & 1) ? 0 : 1) != (dx ? 1 : 0) || chestPartner(w, ax, y, az, partner)) continue;
        if (nm == (meta & 3)) { pick = nm; break; }
        if (pick < 0) pick = nm;
      }
      if (pick >= 0) meta = (u8)((meta & ~3) | pick);
    }
  }
  // supports
  BlockId below = w.getBlock(x, y - 1, z);
  if (bhas(bid, BF::Crop) && below != B.farmland) { toastMsg("Plant seeds on farmland \xE2\x80\x94 till dirt with a hoe"); return false; }
  if (bhas(bid, BF::AlwaysWet) && !waterAt(w, x, y, z)) { toastMsg("This grows under water"); return false; }
  if (bid == B.sugar_cane && bid) {
    bool waterSide = false;
    for (auto [a, b] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) if (waterAt(w, x + a, y - 1, z + b)) waterSide = true;
    bool okSoil = below == B.sugar_cane || ((below == B.grass || below == B.dirt || below == B.sand || below == B.red_sand) && below && waterSide);
    if (!okSoil) { toastMsg("Sugar cane needs water beside it"); return false; }
  }
  // standing in water: waterloggable blocks keep it
  if (R.tWlog[bid] && ((R.tLiquid[cur] == 1 && (w.getMeta(x, y, z) & 15) == 0) || bhas(bid, BF::AlwaysWet))) meta |= WLOG;
  out.x = x; out.y = y; out.z = z; out.meta = meta;
  return true;
}

void PlayerController::placeBlock(const RayHit& hit, const ItemDef& def, ItemStack& held) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  BlockId bid = R.blockId(def.block);
  if (!bid) return;
  const BlockDef& bdef = R.block(bid);
  if (bid == B.lily_pad) {
    vec3 L = lookVector(p.yaw, p.pitch);
    RayHit h = raycastBlocks(w, glm::dvec3(p.pos.x, p.pos.y + p.eye, p.pos.z), L, 5, true);
    if (!h.hit || R.tLiquid[h.id] != 1 || w.getBlock(h.block.x, h.block.y + 1, h.block.z) != AIR) return;
    w.setBlock(h.block.x, h.block.y + 1, h.block.z, bid, 0, SetFlags::Urgent);
    consumeHeld(); swingArm(); sfxMaterial(lv, "grass", "place", vec3(h.block));
    return;
  }
  ivec3 t = placeTarget(hit);
  int x = t.x, y = t.y, z = t.z;
  if (!inWorldY(y)) return;
  u8 meta = 0;
  std::vector<std::array<int, 5>> also;
  int nx = hit.normal.x, ny = hit.normal.y, nz = hit.normal.z;
  if (R.tRender[bid] == (u8)Render::Torch) {
    BlockId cur = w.getBlock(x, y, z);
    if (!R.tReplaceable[cur] && cur != AIR) return;
    BlockId below = w.getBlock(x, y - 1, z);
    if (ny == 1 || (R.tReplaceable[hit.id] && R.tSolid[below])) { if (!R.tSolid[below]) return; meta = 0; }
    else if (ny == -1) return;
    else {
      // on a wall only against a whole face: not the side of a fence post, pane or slab
      BlockId sid = hit.id; int sk = R.tShape[sid]; int sm = w.getMeta(hit.block.x, hit.block.y, hit.block.z) & 7;
      Render rr = (Render)R.tRender[sid];
      bool whole = R.tSolid[sid] && (!sk ? (rr == Render::Cube || rr == Render::Cutout)
                                         : sk == ShapeKind::Slab && (sm == 6 || (sm >= 2 && (sm - 2 == dirOf((float)-nx, (float)-nz) || sm - 2 == dirOf((float)nx, (float)nz)))));
      if (!whole) { toastMsg("Torches fix to the full face of a block \xE2\x80\x94 or stand one on top"); return; }
      meta = nx == 1 ? 1 : nx == -1 ? 2 : nz == 1 ? 3 : 4;
    }
    if (R.tLiquid[cur]) return;
  } else if (bid == B.lantern || bid == B.soul_lantern) {
    BlockId up = w.getBlock(x, y + 1, z);
    if (ny == -1 && R.tSolid[up]) meta = 1;
    else if (R.tSolid[w.getBlock(x, y - 1, z)]) meta = 0;
    else if (R.tSolid[up] || R.block(up).str("shape") == "chain") meta = 1;
    else return;
    if (R.tLiquid[w.getBlock(x, y, z)] == 1 && (w.getMeta(x, y, z) & 15) == 0) meta |= WLOG;
  } else {
    Placement pl;
    if (!placement(hit, bid, pl)) return;
    x = pl.x; y = pl.y; z = pl.z; meta = pl.meta; also = pl.also;
    if (pl.merge) {
      if (lv.events && !lv.events->emit("blockPlace", json::array({p.name, x, y, z, bdef.key}))) return;
      w.setBlock(x, y, z, bid, meta, SetFlags::Urgent);
      consumeHeld(); swingArm(); sfxMaterial(lv, bdef.sound, "place", vec3(x, y, z) + vec3(0.5f));
      return;
    }
  }
  BlockId cur = w.getBlock(x, y, z);
  if (cur != AIR && !R.tReplaceable[cur]) return;
  bool door = bhas(bid, BF::Door);
  if (bhas(bid, BF::NeedsSupport) && !bhas(bid, BF::Crop) && !bhas(bid, BF::AlwaysWet) && R.tRender[bid] != (u8)Render::Torch && bid != B.lantern && bid != B.soul_lantern && bid != B.sugar_cane && !door) {
    if (!supportOK(bid, x, y, z, meta)) return;
  }
  if (R.tSolid[bid] && !door && blockedByBody(x, y, z)) return;
  for (auto& a : also) { BlockId c2 = w.getBlock(a[0], a[1], a[2]); if (c2 != AIR && !R.tReplaceable[c2]) return; if (R.tSolid[bid] && !door && blockedByBody(a[0], a[1], a[2])) return; }
  if (lv.events && !lv.events->emit("blockPlace", json::array({p.name, x, y, z, bdef.key}))) return;
  ItemStack placed = held;   // (contents travel with chests and barrels)
  w.setBlock(x, y, z, bid, meta, SetFlags::Urgent);
  for (auto& a : also) w.setBlock(a[0], a[1], a[2], (BlockId)a[3], (u8)a[4], SetFlags::Urgent);
  if (bid == B.chest || bid == B.barrel) {
    json items = json::array();
    if (placed.tag.is_object() && placed.tag.contains("c") && placed.tag["c"].is_array()) items = placed.tag["c"];
    else for (int i = 0, n = (int)bdef.num("container", 27); i < n; i++) items.push_back(nullptr);
    w.setBlockEntity(x, y, z, {{"type", "chest"}, {"items", items}});
  } else if (bhas(bid, BF::Furnace)) containerEntity(w, x, y, z);
  if (bhas(bid, BF::Sign)) { w.setBlockEntity(x, y, z, {{"type", "sign"}, {"lines", {"", "", "", ""}}}); editSign(x, y, z); }
  consumeHeld();
  swingArm();
  sfxMaterial(lv, bdef.sound, "place", vec3(x, y, z) + vec3(0.5f));
  if (bhas(bid, BF::Falls)) checkFall(lv, w, x, y, z);
  if (bhas(bid, BF::HardensTo)) checkHarden(lv, w, x, y, z);
}

// Does the ground (or ceiling) suit this plant or small thing? (blocks2.js supportOK)
bool PlayerController::supportOK(BlockId bid, int x, int y, int z, u8 meta) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  const BlockDef& d = R.block(bid);
  BlockId below = w.getBlock(x, y - 1, z), above = w.getBlock(x, y + 1, z);
  auto& SOIL = soilIds();
  auto in = [&](BlockId v, std::initializer_list<BlockId> l) { if (!v) return false; for (BlockId a : l) if (a && a == v) return true; return false; };
  auto inSoil = [&](BlockId v) { return v && std::find(SOIL.begin(), SOIL.end(), v) != SOIL.end(); };
  if (d.has("rs") || d.has("rail") || d.has("tripwire")) return d.has("attached") || R.tSolid[below];   // redstone parts: the redstone module refines this
  if (bhas(bid, BF::Hangs) || (d.has("altTile") && (meta & 1)))
    return R.tSolid[above] || above == bid || (bhas(above, BF::Hangs) && startsWith(R.block(above).key, "cave_vines") && startsWith(d.key, "cave_vines"));
  if (bid == B.cactus) return in(below, {B.sand, B.red_sand, B.desert_sand, B.cactus});
  if (bid == B.dead_bush || bid == B.dry_grass || bid == B.marram_grass) return in(below, {B.sand, B.red_sand, B.desert_sand, B.silver_sand, B.terracotta}) || inSoil(below);
  if (bid == B.bamboo) return in(below, {B.bamboo, B.sand, B.red_sand, B.gravel}) || inSoil(below);
  if (bid == B.twisting_vines) return R.tSolid[below] || below == bid;
  if (bid == B.crimson_roots || bid == B.warped_roots || bid == B.nether_sprouts || bid == B.crimson_fungus || bid == B.warped_fungus)
    return in(below, {B.crimson_nylium, B.warped_nylium, B.netherrack, B.soul_soil, B.soul_sand}) || inSoil(below);
  if (bid == B.brown_mushroom || bid == B.red_mushroom || bid == B.amethyst_cluster || bid == B.small_amethyst_bud || bid == B.medium_amethyst_bud ||
      bid == B.large_amethyst_bud || bid == B.loose_stones || bid == B.sea_pickle || bid == B.pointed_dripstone) return R.tSolid[below];
  if (bid == B.cattail) return in(below, {B.sand, B.clay, B.gravel}) || inSoil(below);
  return inSoil(below);
}

// Seeds, beans, berries and warts: plant them where they grow.
bool PlayerController::plantItem(const ItemDef& def, const RayHit& hit, ItemStack& held) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  BlockId bid = R.blockId(def.str("plants"));
  if (!bid) return false;
  ivec3 t = placeTarget(hit);
  BlockId cur = w.getBlock(t.x, t.y, t.z);
  if (cur != AIR && !R.tReplaceable[cur]) return false;
  u8 meta = 0;
  bool food = def.has("food");
  if (bid == B.cocoa) {
    bool logOK = hit.id && (hit.id == B.jungle_log || hit.id == B.jungle_wood || hit.id == B.stripped_jungle_log || hit.id == B.stripped_jungle_wood);
    if (!logOK || hit.normal.y != 0) { toastMsg("Plant cocoa beans on the side of a jungle log"); return true; }
    meta = (u8)(dirOf((float)-hit.normal.x, (float)-hit.normal.z) << 4);
  } else if (bid == B.nether_wart) {
    if (hit.id != B.soul_sand || hit.normal.y != 1) { toastMsg("Nether wart grows on soul sand"); return true; }
  } else if (bid == B.cave_vines) {
    BlockId up = w.getBlock(t.x, t.y + 1, t.z);
    bool ceiling = R.tSolid[up] || startsWith(R.block(up).key, "cave_vines");
    if (!ceiling) { if (food) return false; toastMsg("Glow berries grow down from a ceiling"); return true; }
  } else {
    if (hit.id != B.farmland || !B.farmland || hit.normal.y != 1) { if (food) return false; toastMsg("Plant seeds on farmland \xE2\x80\x94 till dirt with a hoe"); return true; }
  }
  if (blockedByBody(t.x, t.y, t.z) && R.tSolid[bid]) return true;
  w.setBlock(t.x, t.y, t.z, bid, meta, SetFlags::Urgent);
  consumeHeld(); swingArm(); sfxMaterial(lv, "grass", "place", vec3(t));
  (void)held;
  return true;
}

// Bone meal: crops ripen, saplings grow, grass blooms.
void PlayerController::useBonemeal(const RayHit& hit, ItemStack& held) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  int x = hit.block.x, y = hit.block.y, z = hit.block.z;
  BlockId id = hit.id; const BlockDef& d = R.block(id);
  u8 meta = w.getMeta(x, y, z);
  bool ok = false;
  if (bhas(id, BF::Crop)) {
    int st = meta & 15, max = d.props["crop"].value("stages", 1) - 1;
    if (st < max) { w.setBlock(x, y, z, id, (u8)((meta & ~15) | std::min(max, st + 2 + (int)(rnd() * 3)))); ok = true; }
    else if (d.props["crop"].contains("fruit")) {
      BlockId fruit = R.blockId(d.props["crop"].value("fruit", std::string()));
      for (int k = 0; k < 4 && fruit; k++) {
        int fx = x + DIRX[k], fz = z + DIRZ[k];
        if (w.getBlock(fx, y, fz) == fruit) break;
        auto& s = soilIds();
        if (w.getBlock(fx, y, fz) == AIR && std::find(s.begin(), s.end(), w.getBlock(fx, y - 1, fz)) != s.end()) { w.setBlock(fx, y, fz, fruit, 0); break; }
      }
      ok = true;
    }
  } else if (bhas(id, BF::Tree)) {
    ok = true;
    if (rnd() < 0.45) growTree(lv, w, x, y, z, rnd());
  } else if (id && (id == B.grass || id == B.moss_block || id == B.dirt_with_dry_grass)) {
    ok = true;
    static const char* flowers[] = {"flower_red", "flower_yellow", "oxeye_daisy", "azure_bluet", "cornflower", "allium", "red_tulip", "white_tulip"};
    for (int i = 0; i < 26; i++) {
      int ax = x + (int)std::round((rnd() - 0.5) * 6), az = z + (int)std::round((rnd() - 0.5) * 6);
      for (int ay = y + 2; ay >= y - 2; ay--) {
        BlockId base = w.getBlock(ax, ay, az);
        if (base && (base == B.grass || base == B.moss_block || base == B.dirt_with_dry_grass) && w.getBlock(ax, ay + 1, az) == AIR) {
          std::string k = id == B.moss_block ? (rnd() < 0.6 ? "moss_carpet" : "azalea") : id == B.dirt_with_dry_grass ? "dry_grass" : rnd() < 0.8 ? "tall_grass" : flowers[(int)(rnd() * 8)];
          if (BlockId nb = R.blockId(k)) w.setBlock(ax, ay + 1, az, nb, 0);
          break;
        }
      }
    }
  } else if (id && (id == B.sugar_cane || id == B.bamboo || id == B.cactus || id == B.twisting_vines || id == B.kelp)) {
    int ty = y; while (w.getBlock(x, ty + 1, z) == id) ty++;
    BlockId above = w.getBlock(x, ty + 1, z);
    if (above == AIR || (id == B.kelp && R.tLiquid[above] == 1)) { w.setBlock(x, ty + 1, z, id, id == B.kelp ? WLOG : 0); ok = true; }
  } else if (bhas(id, BF::Hangs) && bhas(id, BF::StackPlant)) {
    int ty = y; while (bhas(w.getBlock(x, ty - 1, z), BF::Hangs) && bhas(w.getBlock(x, ty - 1, z), BF::StackPlant)) ty--;
    if (w.getBlock(x, ty - 1, z) == AIR) { w.setBlock(x, ty - 1, z, id == B.cave_vines ? (rnd() < 0.4 ? B.cave_vines_lit : B.cave_vines) : id, 0); ok = true; }
    if (id == B.cave_vines) { w.setBlock(x, y, z, B.cave_vines_lit, meta); ok = true; }
  } else if (id && id == B.sea_pickle && (meta & 3) < 3) { w.setBlock(x, y, z, id, meta + 1); ok = true; }
  else if (id && id == B.berry_bush_empty) { w.setBlock(x, y, z, B.berry_bush); ok = true; }
  if (!ok) return;
  consumeHeld();
  lv.fx->particles("growth", vec3(x + 0.5f, y + 0.6f, z + 0.5f), 8);
  sfxMaterial(lv, "grass", "place", vec3(x, y, z)); swingArm();
  (void)held;
}

// Flint and steel: light TNT, a portal, or a fire on whatever you point at.
void PlayerController::useFlint(const RayHit* hit, ItemStack& held) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  if (!hit) return;
  if (bhas(hit->id, BF::Tnt)) { w.setBlock(hit->block.x, hit->block.y, hit->block.z, AIR); primeTNT(lv, w, hit->block.x, hit->block.y, hit->block.z, 4); wearHeld(lv, p, 1); return; }
  const std::string& hk = R.block(hit->id).key;
  if (hk == "campfire" || hk == "soul_campfire") return;
  ivec3 t = placeTarget(*hit);
  vec3 c = vec3(t) + vec3(0.5f);
  if (hooks().ignitePortal && hooks().ignitePortal(w, t.x, t.y, t.z, "ember")) { sfxAt(lv, "ignite", c); sfxUi(lv, p, "portal"); toastMsg("The portal roars open", "boss"); swingArm(); wearHeld(lv, p, 1); return; }
  if (w.getBlock(t.x, t.y, t.z) != AIR || !R.tSolid[w.getBlock(t.x, t.y - 1, t.z)]) return;
  BlockId below = w.getBlock(t.x, t.y - 1, t.z);
  w.setBlock(t.x, t.y, t.z, below && (below == B.soul_soil || below == B.ash) && B.soul_fire ? B.soul_fire : B.fire, 0, SetFlags::Urgent);
  sfxAt(lv, "ignite", c); swingArm(); wearHeld(lv, p, 1);
  (void)held;
}

// ---- buckets & waterlogging ----
void PlayerController::useBucket(const ItemDef& def, ItemStack& held) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  std::string kind = def.str("bucket");
  if (kind == "empty") {
    vec3 L = lookVector(p.yaw, p.pitch);
    RayHit h = raycastBlocks(w, glm::dvec3(p.pos.x, p.pos.y + p.eye, p.pos.z), L, 5, true);
    if (!h.hit || !R.tLiquid[h.id]) return;
    if ((w.getMeta(h.block.x, h.block.y, h.block.z) & 15) != 0) return;
    w.setBlock(h.block.x, h.block.y, h.block.z, AIR, 0, SetFlags::Urgent);
    ItemStack full(R.tLiquid[h.id] == 1 ? "water_bucket" : "lava_bucket", 1);
    if (--held.n <= 0) held = full; else if (p.give(full) > 0) dropItem(lv, p.realm, full, p.pos + vec3(0, 1, 0), false);
    sfxUi(lv, p, "bucket"); swingArm();
    return;
  }
  if (!target.hit) return;
  if (kind == "water" && hooks().ignitePortal && hooks().ignitePortal(w, target.block.x, target.block.y, target.block.z, "sky")) {
    held = ItemStack("bucket", 1); sfxUi(lv, p, "portal"); toastMsg("Light pours through \xE2\x80\x94 the Skylands await", "rare"); return;
  }
  ivec3 t = placeTarget(target);
  BlockId cur = w.getBlock(t.x, t.y, t.z);
  if (!R.tReplaceable[cur] && cur != AIR) return;
  if (kind == "water" && p.realm == REALM_MAIN && dimOfY(t.y) == DIM_NETHER) {
    sfxUi(lv, p, "fizz"); burst(lv, vec3(t) + vec3(0.5f), "white", 10, 0.6f); held = ItemStack("bucket", 1); return;
  }
  w.setBlock(t.x, t.y, t.z, kind == "water" ? B.water : B.lava, 0, SetFlags::Urgent);
  if (!p.creative()) held = ItemStack("bucket", 1);
  sfxUi(lv, p, "bucket"); swingArm();
}
bool PlayerController::bucketWaterlog(const ItemDef& def, ItemStack& held) {
  World& w = world();
  const Registry& R = reg();
  if (!target.hit || !R.tWlog[target.id]) return false;
  ivec3 b = target.block;
  u8 m = w.getMeta(b.x, b.y, b.z);
  std::string kind = def.str("bucket");
  if (kind == "water" && !(m & WLOG) && !(R.tShape[target.id] == ShapeKind::Slab && (m & 7) == 6)) {
    w.setBlock(b.x, b.y, b.z, target.id, m | WLOG); if (!p.creative()) held = ItemStack("bucket", 1); sfxUi(lv, p, "bucket"); swingArm(); return true;
  }
  if (kind == "empty" && (m & WLOG)) {
    w.setBlock(b.x, b.y, b.z, target.id, m & ~WLOG);
    ItemStack full("water_bucket", 1);
    if (--held.n <= 0) held = full; else if (p.give(full) > 0) dropItem(lv, p.realm, full, p.pos + vec3(0, 1, 0), false);
    sfxUi(lv, p, "bucket"); swingArm(); return true;
  }
  return false;
}

// ---- using placed blocks (build.js useBlockAt + blocks2.js useBlockNew + furniture.js) ----
static bool isBook(const std::string& id) {
  if (id.empty() || id.find("bookshelf") != std::string::npos || id.find("bookcase") != std::string::npos) return false;
  auto* d = reg().item(id);
  return id.find("book") != std::string::npos || (d && (d->has("lore") || d->has("tome") || d->icon.find("book") != std::string::npos));
}
static const std::unordered_map<std::string, float> FURN_SEAT = {{"chair", 8}, {"stool", 10}, {"bench", 7}};
static const float FURN_FACE_ANGLE[4] = {0, -PI / 2, PI, PI / 2};

bool PlayerController::useBlockAt(const RayHit& hit, ItemStack* held, const ItemDef* def) {
  World& w = world();
  const Registry& R = reg(); const KnownBlocks& B = KB();
  int x = hit.block.x, y = hit.block.y, z = hit.block.z;
  BlockId id = hit.id;
  const BlockDef& b = R.block(id);
  u8 meta = w.getMeta(x, y, z);
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  std::string tool = def && def->hasTool ? def->tool.type : "";
  bool hasHeld = held && !held->empty();
  // ---- furniture.js: sitting, shelves and bookcases ----
  if (!p.sneaking && !p.riding) {
    if (FURN_SEAT.count(b.str("furniture"))) { swingArm(); return sitOn(x, y, z); }
    float top, yaw; bool hy;
    if (!hasHeld && (R.tShape[id] == ShapeKind::Stairs || R.tShape[id] == ShapeKind::Slab) && seatAt(w, x, y, z, top, yaw, hy) && hit.normal.y == 1) return sitOn(x, y, z);
  }
  if (id == B.bookshelf && id && hasHeld && isBook(held->id) && !p.sneaking) { toastMsg("This bookshelf is full already. Books go in a Bookcase (planks and sticks: P S P / P \xC2\xB7 P / P S P)"); return true; }
  if (b.has("shelf") || b.has("bookcase")) {
    bool bookcase = b.has("bookcase");
    vec3 fr = hitFrac(hit);
    int f = meta & 3;
    float X = clampv(fr.x, 0.f, 0.999f) * 16, Z = clampv(fr.z, 0.f, 0.999f) * 16;
    float mx = f == 0 ? X : f == 1 ? Z : f == 2 ? 16 - X : 16 - Z;
    int col = std::min(2, (int)std::floor(mx / (16.f / 3))), row = b.str("furniture") == "shelf" ? 0 : fr.y < 0.5f ? 0 : 1;
    int k = row * 3 + col;
    json* e = containerEntity(w, x, y, z);
    if (!e) return true;
    json& items = (*e)["items"];
    while ((int)items.size() <= k) items.push_back(nullptr);
    ItemStack here = ItemStack::fromJson(items[k]);
    if (hasHeld && (!bookcase || isBook(held->id))) {
      if (!here.empty()) { toastMsg(bookcase ? "There is a book there already" : "Something is there already"); return true; }
      int take = bookcase ? 1 : held->n;
      ItemStack put = *held; put.n = take; items[k] = put.toJson();
      held->n -= take; if (held->n <= 0) held->clear();
      w.blockEntityChanged(x, y, z); sfxMaterial(lv, "wood", "place", c); swingArm(); return true;
    }
    if (hasHeld && bookcase) { toastMsg("Bookcases hold books"); return true; }
    if (!here.empty()) { items[k] = nullptr; w.blockEntityChanged(x, y, z); giveItem(lv, p, here); sfxUi(lv, p, "pop"); swingArm(); return true; }
    return true;
  }
  // ---- hooks (redstone...) and the 0.5 blocks (blocks2.js useBlockNew) ----
  for (auto& h : hooks().useBlock) if (h(*this, hit, held, def)) return true;
  if (b.has("grave")) { swingArm(); openGrave(lv, p, x, y, z); return true; }
  if (bhas(id, BF::Candle)) {
    if (def && def->str("igniter") == "flint" && !(meta & 4) && !(meta & WLOG)) { w.setBlock(x, y, z, id, meta | 4); sfxAt(lv, "ignite", c); wearHeld(lv, p, 1); swingArm(); return true; }
    if (!hasHeld && (meta & 4)) { w.setBlock(x, y, z, id, meta & ~4); sfxAt(lv, "fizz", c); swingArm(); return true; }
    return false;
  }
  if (b.has("cake") && (!def || def->block.empty())) {
    if (p.hp >= p.maxHp) { toastMsg("You are already at full health"); return true; }
    p.heal(2); sfxUi(lv, p, "eat"); p.eatAnim = 0.45f;
    int bites = (meta & 7) + 1;
    if (bites >= 7) w.setBlock(x, y, z, AIR); else w.setBlock(x, y, z, id, (u8)bites);
    floatText(lv, p, vec3(x + 0.5f, y + 0.9f, z + 0.5f), "+1 \xE2\x99\xA5", "#ff8a8a");
    return true;
  }
  if (b.has("composter")) {
    int lvl = meta & 15;
    if (lvl >= 8) { w.setBlock(x, y, z, id, 0); dropItem(lv, p.realm, ItemStack("bone_meal", 1), vec3(x + 0.5f, y + 1.1f, z + 0.5f), true); sfxAt(lv, "dig_gravel", c); swingArm(); return true; }
    // how likely each plant item is to raise the level
    float ch = 0;
    if (hasHeld) {
      const std::string& k = held->id;
      auto* d = R.item(k); BlockId bb = d && !d->block.empty() ? R.blockId(d->block) : 0;
      static const std::unordered_set<std::string> c30 = {"wheat_seeds", "beetroot_seeds", "pumpkin_seeds", "melon_seeds", "kelp", "dried_kelp", "glow_berries", "berries", "sweet_berries", "seagrass", "tall_grass", "straw"};
      static const std::unordered_set<std::string> c65 = {"wheat", "carrot", "potato", "beetroot", "melon_slice", "apple", "cocoa_beans", "nether_wart_item", "sugar_cane", "cactus", "bamboo", "vine", "lily_pad", "moss_carpet", "glow_lichen", "fern", "dead_bush"};
      static const std::unordered_set<std::string> c85 = {"pumpkin", "carved_pumpkin", "melon", "hay_bale", "bread", "cookie", "baked_potato", "moss_block", "brown_mushroom_block", "red_mushroom_block", "nether_wart_block", "warped_wart_block", "shroomlight"};
      const std::string bk = bb ? R.block(bb).key : "";
      if (c30.count(k) || (bb && (bhas(bb, BF::IsLeaves) || bhas(bb, BF::Tree)))) ch = 0.3f;
      else if (c65.count(k) || (bb && (bhas(bb, BF::Flower) || endsWith(bk, "mushroom") || endsWith(bk, "fungus") || endsWith(bk, "_roots")))) ch = 0.65f;
      else if (c85.count(k)) ch = 0.85f;
      else if (k == "pumpkin_pie" || k == "cake") ch = 1;
    }
    if (!ch) return false;
    if (lvl < 7) {
      if (--held->n <= 0) held->clear();
      if (rnd() < ch) { w.setBlock(x, y, z, id, lvl + 1 == 7 ? 8 : lvl + 1); burst(lv, vec3(x + 0.5f, y + 1, z + 0.5f), "bone_meal", 5, 0.4f); }
      sfxMaterial(lv, "grass", "place", c); swingArm();
    }
    return true;
  }
  if (b.has("jukebox")) {
    json* e = w.blockEntity(x, y, z);
    if (e && e->contains("disc")) {
      std::string disc = (*e)["disc"];
      if (lv.events) lv.events->emit("jukebox", json::array({x, y, z, nullptr}));
      dropItem(lv, p.realm, ItemStack(disc, 1), vec3(x + 0.5f, y + 1.1f, z + 0.5f), true);
      w.setBlockEntity(x, y, z, nullptr); swingArm(); return true;
    }
    if (def && def->has("disc")) {
      w.setBlockEntity(x, y, z, {{"type", "jukebox"}, {"disc", held->id}});
      if (lv.events) lv.events->emit("jukebox", json::array({x, y, z, def->str("disc")}));
      std::string nm = def->name; const std::string pre = "Music Disc \xE2\x80\x94 "; if (startsWith(nm, pre)) nm = nm.substr(pre.size());
      if (--held->n <= 0) held->clear();
      toastMsg("Now playing: " + nm, "good"); swingArm(); return true;
    }
    return false;
  }
  if (b.has("enderChest")) { swingArm(); sfxUi(lv, p, "chest"); openScreen(lv, p, "container", {{"ender", true}}); return true; }
  if (b.has("station")) {
    swingArm();
    if (b.str("station") == "cartography") { openScreen(lv, p, "table"); toastMsg("Map table: a map ringed with 8 paper zooms out \xC2\xB7 a map with empty maps makes copies"); return true; }
    openScreen(lv, p, "station", {{"kind", b.str("station")}, {"pos", {x, y, z}}});
    return true;
  }
  if (b.has("lectern")) {
    json* e = w.blockEntity(x, y, z);
    if (!e && hasHeld && held->id == "book") { w.setBlockEntity(x, y, z, {{"type", "lectern"}, {"book", true}}); if (--held->n <= 0) held->clear(); swingArm(); return true; }
    // (0.9.0.65) an old book (a "lore_" item) lies open on the lectern at its page: use it to read and pick the page,
    // sneak with an empty hand to take it
    std::string loreHeld = hasHeld && held->def() ? held->def()->str("lore") : "";
    if (!e && !loreHeld.empty()) { w.setBlockEntity(x, y, z, {{"type", "lectern"}, {"book", true}, {"lore", loreHeld}}); if (--held->n <= 0) held->clear(); swingArm(); return true; }
    if (e && e->value("book", false) && e->contains("lore") && (*e)["lore"].is_string()) {
      if (p.sneaking && !hasHeld) { giveItem(lv, p, ItemStack("lore_" + (*e)["lore"].get<std::string>(), 1)); w.setBlockEntity(x, y, z, nullptr); swingArm(); return true; }
      swingArm(); openScreen(lv, p, "lectern", {{"pos", {x, y, z}}}); return true;
    }
    if (e && e->value("book", false) && !hasHeld) { giveItem(lv, p, ItemStack("book", 1)); w.setBlockEntity(x, y, z, nullptr); swingArm(); return true; }
    return false;
  }
  if (b.has("frame")) {
    json* e = w.blockEntity(x, y, z);
    if (e && e->contains("item")) { (*e)["rot"] = (e->value("rot", 0) + 1) & 7; w.blockEntityChanged(x, y, z); sfxAt(lv, "click", c); swingArm(); return true; }
    if (hasHeld) { ItemStack one = *held; one.n = 1; w.setBlockEntity(x, y, z, {{"type", "frame"}, {"item", one.toJson()}, {"rot", 0}}); if (--held->n <= 0) held->clear(); sfxAt(lv, "click", c); swingArm(); return true; }
    return false;
  }
  if (b.has("hive")) {
    bool full = (meta & 0x0c) == 0x0c;
    if (full && def && def->has("shears")) { w.setBlock(x, y, z, id, meta & 3); dropItem(lv, p.realm, ItemStack("honeycomb", 3), vec3(x + 0.5f + hit.normal.x * 0.6f, y + 0.5f, z + 0.5f + hit.normal.z * 0.6f), true); wearHeld(lv, p, 1); sfxUi(lv, p, "shear"); swingArm(); return true; }
    if (full && def && def->has("bottle")) { w.setBlock(x, y, z, id, meta & 3); ItemStack hb("honey_bottle", 1); if (--held->n <= 0) *held = hb; else giveItem(lv, p, hb); sfxUi(lv, p, "bucket"); swingArm(); return true; }
    if (!full && def && (def->has("shears") || def->has("bottle"))) { toastMsg("The nest is not full of honey yet"); return true; }
    return false;
  }
  if (id == B.cave_vines_lit && id) { w.setBlock(x, y, z, B.cave_vines, meta); dropItem(lv, p.realm, ItemStack("glow_berries", 1), vec3(x + 0.5f, y + 0.3f, z + 0.5f), true); sfxMaterial(lv, "grass", "dig", c); swingArm(); return true; }
  if (tool == "shovel" && id && (id == B.grass || id == B.dirt || id == B.coarse_dirt || id == B.podzol || id == B.mycelium || id == B.rooted_dirt || id == B.dirt_with_dry_grass || id == B.dirt_with_coniferous_litter || id == B.dirt_with_rainforest_litter) && hit.normal.y != -1) {
    BlockId up = w.getBlock(x, y + 1, z);
    if (up == AIR || (R.tReplaceable[up] && !R.tLiquid[up])) {
      if (up) w.setBlock(x, y + 1, z, AIR);
      w.setBlock(x, y, z, B.dirt_path, 0); sfxMaterial(lv, "gravel", "dig", c); wearHeld(lv, p, 1); swingArm(); return true;
    }
  }
  if (tool == "shovel" && id && (id == B.campfire || id == B.soul_campfire)) { sfxAt(lv, "fizz", c); return true; }
  // ---- build.js ----
  if (bhas(id, BF::Door)) {
    int lower = (meta & 16) ? y - 1 : y;
    u8 m0 = w.getMeta(x, lower, z) ^ 4;
    w.setBlock(x, lower, z, id, m0 & ~16, SetFlags::Urgent); w.setBlock(x, lower + 1, z, id, m0 | 16, SetFlags::Urgent);
    sfxAt(lv, doorSound(id, m0 & 4), vec3(x + 0.5f, lower + 1, z + 0.5f)); swingArm(); return true;
  }
  if (bhas(id, BF::Trapdoor) || R.tShape[id] == ShapeKind::Gate) {
    u8 m = meta ^ 4;
    if (R.tShape[id] == ShapeKind::Gate && (m & 4)) { int f = lookDir(p); if ((((m & 3) + 2) & 3) == f) m = (u8)((m & ~3) | f); }   // gates swing away from you
    w.setBlock(x, y, z, id, m, SetFlags::Urgent); sfxAt(lv, doorSound(id, m & 4), c); swingArm(); return true;
  }
  if (bhas(id, BF::Bed)) { sleep(x, y, z, meta); return true; }
  if (b.has("noteBlock")) {
    int n = ((meta & 31) + 1) % 25;
    w.setBlock(x, y, z, id, (u8)(n | (meta & 0x20)));
    lv.fx->sound("note", c, 48, 1, std::pow(2.f, (n - 12) / 12.f));
    if (lv.events) lv.events->emit("note", json::array({x, y, z, n, R.block(w.getBlock(x, y - 1, z)).key}));
    lv.fx->particles("note", vec3(x + 0.5f, y + 1.2f, z + 0.5f), 1, {{"n", n}});
    swingArm(); return true;
  }
  if (b.has("bellBlock")) { sfxAt(lv, "bell", vec3(x, y, z), 48); swingArm(); return true; }
  if (bhas(id, BF::Sign)) { editSign(x, y, z); return true; }
  if (b.has("stonecutter")) { openScreen(lv, p, "stonecutter"); return true; }
  if (b.has("machine")) { openScreen(lv, p, "machine", {{"pos", {x, y, z}}}); return true; }
  if (b.has("cauldron")) {
    std::string bk = def ? def->str("bucket") : "";
    if (bk == "water" && (meta & 3) < 3) { w.setBlock(x, y, z, id, 3); if (!p.creative()) *held = ItemStack("bucket", 1); sfxUi(lv, p, "bucket"); return true; }
    if (bk == "empty" && (meta & 3) == 3) { w.setBlock(x, y, z, id, 0); ItemStack full("water_bucket", 1); if (--held->n <= 0) *held = full; else giveItem(lv, p, full); sfxUi(lv, p, "bucket"); return true; }
    return false;
  }
  if (b.has("pot")) {
    json* e = w.blockEntity(x, y, z, true);
    if (!e->contains("type")) *e = {{"type", "pot"}, {"plant", nullptr}};
    bool pottable = false;
    if (def && !def->block.empty()) {
      BlockId pb = R.blockId(def->block);
      static const std::unordered_set<std::string> extra = {"flower_red", "flower_yellow", "fern", "dead_bush", "brown_mushroom", "red_mushroom", "cactus", "berry_bush"};
      pottable = pb && (bhas(pb, BF::Flower) || bhas(pb, BF::Tree) || extra.count(def->block));
    }
    bool hasPlant = (*e)["plant"].is_string();
    if (!hasPlant && pottable) { (*e)["plant"] = def->block; if (--held->n <= 0) held->clear(); }
    else if (hasPlant && !hasHeld) { giveItem(lv, p, ItemStack((*e)["plant"].get<std::string>(), 1)); (*e)["plant"] = nullptr; }
    else return false;
    w.blockEntityChanged(x, y, z); w.markBlockDirty(x, y, z, true); swingArm(); return true;
  }
  if (bhas(id, BF::Tnt) && def && def->str("igniter") == "flint") { w.setBlock(x, y, z, AIR); primeTNT(lv, w, x, y, z, 4); wearHeld(lv, p, 1); return true; }
  // tools on blocks
  if (tool == "axe" && b.has("strip")) { w.setBlock(x, y, z, R.blockId(b.str("strip")), meta); sfxMaterial(lv, "wood", "dig", c); wearHeld(lv, p, 1); swingArm(); return true; }
  // chisel (Cubyz-style hand tool): carve placed blocks into their worked variant
  // chisel (Cubyz-style): aim at one of a full block's 8 corners to carve it away, or re-chisel
  // a carved corner to put it back. The corner bits live in meta>>4; the mesher and physics draw/
  // collide them as octant boxes. Stairs/slab families also keep their trim chain (stairs->slab->base).
  if (tool == "chisel") {
    bool chiselable = R.tSolid[id] && R.tOpaque[id] && !R.tShape[id] && !R.tWlog[id] && !(b.has("family") || b.has("base"));   // keep source's exclusion: wlog blocks (lanterns, snow…) carry live meta state elsewhere
    u8 carve = w.getCarve(x, y, z);   // octant bits live in the dedicated carve byte; meta keeps its legacy meaning
    if (chiselable) {
      // The crosshair raycast already hit this block; its entry point IS the geometry under the
      // cursor. Pick that octant and TOGGLE it: carve it if present, put it back if already carved.
      // When the crosshair passed through a carved-away notch, physics finishes the hit on the
      // block's cell and the entry point still lies inside the aimed (carved) octant, so the same
      // toggle restores it. Carving all 8 is NOT an auto-revert — each quadrant toggles on its own.
      // Aimed-octant pick: the crosshair raycast already hit this block; its entry point IS
      // the geometry under the cursor (verified from all 8 diagonal stances). Pick that octant and
      // TOGGLE it: carve it if present, put it back if already carved — so you can remove any
      // quadrant and re-add it by clicking the same spot again. When the crosshair passed through
      // a carved-away notch, physics finishes the hit on the block's cell and the entry point
      // still lies inside the aimed (carved) octant, so the same toggle restores it. Carving all
      // 8 is NOT an auto-revert — each quadrant toggles on its own.
      int enc = -1;
      {
        double fx = hit.point.x - x, fy = hit.point.y - y, fz = hit.point.z - z;
        int hx = fx >= 0.5, hy = fy >= 0.5, hz = fz >= 0.5;
        enc = (hx << 2) | (hy << 1) | hz;   // entry-point containing-octant
      }
      bool carvedBit = carve && ((carve >> enc) & 1);
      u8 next = carvedBit ? (u8)(carve & ~(1 << enc))
                          : (u8)(carve | (1 << enc));   // toggle the aimed octant; plain blocks get their first octant set
      bool blockSame = (next != 0) == (carve != 0);
      if (!blockSame) w.setBlock(x, y, z, id, meta, SetFlags::Urgent);   // first carve or full restore: touch the block so entities/remesh rules refresh
      w.setCarve(x, y, z, next, SetFlags::Urgent);                      // toggle the aimed octant in the carve byte
      if (next) for (int d = 0; d < 6; d++) {
        int dx = d == 0 ? 1 : d == 1 ? -1 : 0, dy = d == 2 ? 1 : d == 3 ? -1 : 0, dz = d == 4 ? 1 : d == 5 ? -1 : 0;
        BlockId nid = w.getBlock(x + dx, y + dy, z + dz);
        if (nid != AIR && !R.tLiquid[nid])
          w.markBlockDirty(x + dx, y + dy, z + dz, true);   // any solid neighbour may share a face with the new notch
      }
      sfxMaterial(lv, "stone", "dig", c); wearHeld(lv, p, 1); swingArm(); return true;
    }
    // trim chain: stairs -> slab -> base (family/base live in props JSON)
    std::string fam = b.str("family"), base = b.str("base");
    if (fam == "stairs") {
      BlockId slab = R.blockId(b.key + "_slab");   // stone_brick_stairs -> stone_brick_slab
      if (slab != AIR) { w.setBlock(x, y, z, slab, meta); sfxMaterial(lv, "stone", "dig", c); wearHeld(lv, p, 1); swingArm(); return true; }
      if (!base.empty()) { BlockId out = R.blockId(base); if (out != AIR) { w.setBlock(x, y, z, out, 0); sfxMaterial(lv, "stone", "dig", c); wearHeld(lv, p, 1); swingArm(); return true; } }
    } else if (fam == "slab") {
      if (!base.empty()) { BlockId out = R.blockId(base); if (out != AIR) { w.setBlock(x, y, z, out, 0); sfxMaterial(lv, "stone", "dig", c); wearHeld(lv, p, 1); swingArm(); return true; } }
    }
    return false;
  }
  static const std::unordered_set<std::string> HOE = {"grass", "dirt", "coarse_dirt", "rooted_dirt", "snowy_grass", "sky_grass", "podzol", "mycelium", "dirt_path", "dirt_with_dry_grass", "dirt_with_coniferous_litter", "dirt_with_rainforest_litter"};
  if (tool == "hoe" && HOE.count(b.key) && w.getBlock(x, y + 1, z) == AIR && B.farmland) { w.setBlock(x, y, z, B.farmland, 0); sfxMaterial(lv, "gravel", "dig", c); wearHeld(lv, p, 1); swingArm(); return true; }
  if ((tool == "sword" || (def && def->has("shears"))) && id == B.pumpkin && id) {
    w.setBlock(x, y, z, B.carved_pumpkin, (u8)facingMeta(p));
    dropItem(lv, p.realm, ItemStack("pumpkin_seeds", 4), vec3(x + 0.5f + hit.normal.x * 0.6f, y + 0.5f, z + 0.5f + hit.normal.z * 0.6f), true);
    swingArm(); return true;
  }
  // ripe crops: pick and replant in one go
  if (bhas(id, BF::Crop) && !hasHeld && (meta & 15) >= b.props["crop"].value("stages", 1) - 1) {
    dropBlockItems(lv, w, x, y, z, id, false, meta);
    w.setBlock(x, y, z, id, meta & ~15);
    sfxMaterial(lv, "grass", "dig", c); swingArm(); return true;
  }
  return false;
}

// What's left after a block goes (water if it was waterlogged) and the other half of doors, beds, tall plants.
void PlayerController::afterBreak(int x, int y, int z, BlockId id, u8 meta) {
  World& w = world();
  const Registry& R = reg();
  if (R.tWlog[id] && (meta & WLOG)) w.setBlock(x, y, z, KB().water, 0);
  if (bhas(id, BF::Door)) { int oy = (meta & 16) ? y - 1 : y + 1; if (w.getBlock(x, oy, z) == id) w.setBlock(x, oy, z, AIR, 0, SetFlags::Urgent); }
  if (bhas(id, BF::Double)) { int oy = (meta & 1) ? y - 1 : y + 1; if (w.getBlock(x, oy, z) == id) w.setBlock(x, oy, z, AIR, 0, SetFlags::Urgent); }
  if (bhas(id, BF::Bed) && !R.block(id).has("bag")) {
    int f = meta & 3, s = (meta & 4) ? -1 : 1, ox = x + DIRX[f] * s, oz = z + DIRZ[f] * s;
    if (w.getBlock(ox, y, oz) == id) w.setBlock(ox, y, oz, AIR, 0, SetFlags::Urgent);
  }
  for (auto& h : hooks().afterBreak) h(*this, x, y, z, id, meta);
}

void PlayerController::editSign(int x, int y, int z) {
  World& w = world();
  json* e = w.blockEntity(x, y, z, true);
  if (!e->contains("lines")) { *e = {{"type", "sign"}, {"lines", {"", "", "", ""}}}; w.blockEntityChanged(x, y, z); }
  openScreen(lv, p, "sign", {{"pos", {x, y, z}}});
}

// ---- beds ----
void PlayerController::sleep(int x, int y, int z, u8 meta) {
  p.spawnPoint = vec3(x + 0.5f, y + 0.6f, z + 0.5f);
  p.spawnRealm = p.realm;
  if (p.realm != REALM_MAIN || dimOfY(y) != DIM_OVERWORLD) { toastMsg("Beds only work in the Overworld \xE2\x80\x94 your spawn is set here anyway", "warn"); return; }
  if (!isNightForSleep(lv)) { toastMsg("Spawn point set. You can only sleep at night.", "good"); return; }
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != p.realm || !isMob(*e) || e->hp <= 0) continue;
    if (reg().mobs[e->type].value("hostile", false) && std::hypot(e->pos.x - x, e->pos.z - z) < 8) { toastMsg("You may not rest now, there are monsters nearby", "warn"); return; }
  }
  p.sleeping = true;
  p.vel.x = p.vel.z = 0;
  toastMsg("Sleeping\xE2\x80\xA6 spawn point set", "good");
  int awake = 0;
  for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed && e.get() != &p && !static_cast<Player*>(e.get())->sleeping && !static_cast<Player*>(e.get())->dead) awake++;
  if (awake) toastAll(lv, "Everyone must sleep to skip the night");
  (void)meta;
}

// ---- sitting (furniture.js) ----
bool seatAt(World& w, int x, int y, int z, float& top, float& yaw, bool& hasYaw) {
  BlockId id = w.getBlock(x, y, z);
  const BlockDef& b = reg().block(id);
  u8 m = w.getMeta(x, y, z);
  auto it = FURN_SEAT.find(b.str("furniture"));
  if (it != FURN_SEAT.end()) { top = it->second; yaw = PI + FURN_FACE_ANGLE[m & 3]; hasYaw = true; return true; }
  if (reg().tShape[id] == ShapeKind::Stairs && !(m & 4)) { static const float yw[4] = {PI, PI / 2, 0, -PI / 2}; top = 8; yaw = yw[m & 3]; hasYaw = true; return true; }
  if (reg().tShape[id] == ShapeKind::Slab && (m & 7) == 0) { top = 8; yaw = 0; hasYaw = false; return true; }
  return false;
}
class SeatEntity : public Entity {
public:
  SeatEntity() { type = "seat"; w = 0.3f; h = 0.3f; noGravity = true; }
  void tick(Level& lv, double) override {
    Entity* r = lv.entity(data.value("rider", 0u));
    Player* p = r && r->isPlayer() ? static_cast<Player*>(r) : nullptr;
    if (!p || p->dead || p->riding != id) { removed = true; if (p && p->riding == id) p->riding = 0; return; }
    World& w = lv.world(realm);
    if (w.getBlock(data.value("bx", 0), data.value("by", 0), data.value("bz", 0)) != reg().blockId(data.value("block", std::string()))) { standUp(lv, *p); return; }
    p->pos = pos - vec3(0, 0.25f, 0); p->vel = vec3(0); p->hasFallStart = false; p->onGround = true;
  }
  void standUp(Level& lv, Player& p) {
    removed = true; p.riding = 0;
    World& w = lv.world(realm);
    int bx = data.value("bx", 0), by = data.value("by", 0), bz = data.value("bz", 0);
    int fx = (int)std::round(-std::sin(yaw)), fz = (int)std::round(-std::cos(yaw));
    for (auto [dx, dz] : {std::pair{fx, fz}, {-fz, fx}, {fz, -fx}, {-fx, -fz}}) {
      int x = bx + dx, z = bz + dz;
      for (int y : {by, by + 1, by - 1}) if (reg().tSolid[w.getBlock(x, y - 1, z)] && !collides(w, x + 0.2, y, z + 0.2, x + 0.8, y + 1.8, z + 0.8)) { p.pos = vec3(x + 0.5f, y, z + 0.5f); p.hasFallStart = false; return; }
    }
    p.pos.y = by + 1.f; p.hasFallStart = false;
  }
};
bool PlayerController::sitOn(int x, int y, int z) {
  World& w = world();
  float top, yaw; bool hasYaw;
  if (!seatAt(w, x, y, z, top, yaw, hasYaw) || p.riding || !p.carrying.is_null()) return false;
  BlockId above = w.getBlock(x, y + 1, z);
  if (reg().tSolid[above] && !reg().block(above).has("furniture")) { toastMsg("No room to sit there"); return true; }
  for (auto& e : lv.entities()) if (!e->removed && e->type == "seat" && e->data.value("bx", 0) == x && e->data.value("by", 0) == y && e->data.value("bz", 0) == z) { toastMsg("Someone is sitting there"); return true; }
  BlockId id = w.getBlock(x, y, z);
  auto s = std::make_unique<SeatEntity>();
  s->realm = p.realm; s->yaw = hasYaw ? yaw : p.yaw;
  s->pos = vec3(x + 0.5f, y + top / 16, z + 0.5f);
  if (reg().block(id).str("furniture") == "chair") { s->pos.x += -std::sin(s->yaw) * 0.08f; s->pos.z += -std::cos(s->yaw) * 0.08f; }   // a touch toward the front
  s->data = {{"bx", x}, {"by", y}, {"bz", z}, {"block", reg().block(id).key}, {"rider", p.id}};
  Entity* seat = lv.spawn(std::move(s));
  p.riding = seat->id; p.vel = vec3(0); p.yaw = seat->yaw;
  p.pos = seat->pos - vec3(0, 0.25f, 0);
  toastMsg("Sneak to stand up");
  sfxMaterial(lv, reg().block(id).sound, "step", vec3(x, y, z));
  return true;
}
void PlayerController::dismount() {
  Entity* e = p.riding ? lv.entity(p.riding) : nullptr;
  if (auto* s = dynamic_cast<SeatEntity*>(e)) s->standUp(lv, p);
  if (auto* v = asVehicle(e)) { v->dismount(lv, p); return; }   // carts, boats, the ship (sim/vehicles)
  p.riding = 0;
}

// ---- ground storage: set any item down ----
bool PlayerController::putDown(const RayHit& hit, ItemStack& held) {
  World& w = world();
  const Registry& R = reg();
  int x = hit.block.x, y = hit.block.y + 1, z = hit.block.z;
  if (!R.tSolid[hit.id] || !KB().item_pile) return false;
  BlockId cur = w.getBlock(x, y, z);
  if (cur != AIR && !(R.tReplaceable[cur] && !R.tLiquid[cur])) return false;
  w.setBlock(x, y, z, KB().item_pile, 0, SetFlags::Urgent);
  json items = {nullptr, nullptr, nullptr, nullptr}, rot = {0, 0, 0, 0};
  int q = std::max(0, targetQuarter());
  items[q] = held.toJson(); rot[q] = std::round(-p.yaw / (PI / 8)) * (PI / 8);
  w.setBlockEntity(x, y, z, {{"type", "pile"}, {"items", items}, {"rot", rot}});
  held.clear();
  swingArm(); sfxMaterial(lv, "wood", "place", vec3(x, y, z));
  return true;
}
void PlayerController::usePile(const RayHit& hit, ItemStack* held) {
  World& w = world();
  int x = hit.block.x, y = hit.block.y, z = hit.block.z;
  json* e = w.blockEntity(x, y, z);
  if (!e || !e->contains("items")) { w.setBlock(x, y, z, AIR); return; }
  json& items = (*e)["items"];
  while (items.size() < 4) items.push_back(nullptr);
  if (!(*e).contains("rot")) (*e)["rot"] = {0, 0, 0, 0};
  vec3 c = vec3(x, y, z);
  if (held && !held->empty() && p.sneaking) {
    // add to the pile: the spot you point at first, then merge or take any free spot
    int q = targetQuarter();
    ItemStack here = q >= 0 ? ItemStack::fromJson(items[q]) : ItemStack();
    int mx = maxStackOf(held->id);
    if (q >= 0 && !here.empty() && canMergeStacks(here, *held) && here.n < mx) { int k = std::min(mx - here.n, held->n); here.n += k; held->n -= k; if (!held->n) held->clear(); items[q] = here.toJson(); w.blockEntityChanged(x, y, z); swingArm(); sfxMaterial(lv, "wood", "place", c); return; }
    if (q >= 0 && here.empty()) { items[q] = held->toJson(); (*e)["rot"][q] = std::round(-p.yaw / (PI / 8)) * (PI / 8); held->clear(); w.blockEntityChanged(x, y, z); swingArm(); sfxMaterial(lv, "wood", "place", c); return; }
    for (int i = 0; i < 4; i++) {
      ItemStack s = ItemStack::fromJson(items[i]);
      if (!s.empty() && canMergeStacks(s, *held) && s.n < mx) { int k = std::min(mx - s.n, held->n); s.n += k; held->n -= k; if (!held->n) held->clear(); items[i] = s.toJson(); w.blockEntityChanged(x, y, z); swingArm(); return; }
    }
    for (int i = 0; i < 4; i++) if (items[i].is_null()) { items[i] = held->toJson(); held->clear(); w.blockEntityChanged(x, y, z); swingArm(); sfxMaterial(lv, "wood", "place", c); return; }
    toastMsg("This pile is full");
    return;
  }
  // take back the stack you point at (or the most recent one)
  int tq = targetQuarter();
  std::vector<int> order = tq >= 0 && !items[tq].is_null() ? std::vector<int>{tq} : std::vector<int>{3, 2, 1, 0};
  for (int i : order) {
    if (items[i].is_null()) continue;
    ItemStack s = ItemStack::fromJson(items[i]); items[i] = nullptr;
    int left = giveItem(lv, p, s);
    if (left > 0) { s.n = left; items[i] = s.toJson(); toastMsg("No room to pick that up", "warn"); }
    break;
  }
  bool any = false; for (auto& s : items) if (!s.is_null()) any = true;
  w.blockEntityChanged(x, y, z);
  if (!any) w.setBlock(x, y, z, AIR);
  swingArm();
}

// ---- Carry On ----
void PlayerController::pickUpBlock(int x, int y, int z) {
  World& w = world();
  const Registry& R = reg();
  BlockId id = w.getBlock(x, y, z);
  u8 meta = w.getMeta(x, y, z);
  json* e = w.blockEntity(x, y, z);
  json ent = e ? *e : json();
  BlockId keep = id == KB().lit_furnace && id ? KB().furnace : id;
  p.carrying = {{"kind", "block"}, {"key", R.block(keep).key}, {"meta", meta}, {"ent", ent}};
  w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
  sfxMaterial(lv, R.block(id).sound, "place", vec3(x, y, z)); swingArm();
  toastMsg("Carrying " + R.block(id).name + " \xE2\x80\x94 use to set it down", "good");
}
void PlayerController::placeCarried() {
  World& w = world();
  const Registry& R = reg();
  json c = p.carrying;
  if (!target.hit) { toastMsg("Look at the ground to set it down"); return; }
  ivec3 t = placeTarget(target);
  if (!inWorldY(t.y) || !R.tReplaceable[w.getBlock(t.x, t.y, t.z)]) return;
  if (c.value("kind", "") == "block") {
    if (blockedByBody(t.x, t.y, t.z)) return;
    BlockId bid = R.blockId(c.value("key", std::string()));
    if (!bid) return;
    w.setBlock(t.x, t.y, t.z, bid, bhas(bid, BF::FacingFront) ? (u8)facingMeta(p) : (u8)c.value("meta", 0), SetFlags::Urgent);
    if (c.contains("ent") && c["ent"].is_object()) w.setBlockEntity(t.x, t.y, t.z, c["ent"]);
    sfxMaterial(lv, R.block(bid).sound, "place", vec3(t));
  } else {
    auto m = createEntity(c.value("type", std::string("boar")));
    m->realm = p.realm; m->pos = vec3(t.x + 0.5f, t.y, t.z + 0.5f); m->yaw = p.yaw;
    if (c.contains("hp") && c["hp"].is_number()) m->hp = c["hp"];
    if (c.contains("data") && c["data"].is_object()) m->data = c["data"];
    if (R.mobs.contains(m->type) && R.mobs[m->type].value("farm", false)) m->data["kept"] = true;
    lv.spawn(std::move(m));
    sfxUi(lv, p, "pop");
  }
  p.carrying = nullptr;
  swingArm();
}

void registerBuildEntities() {
  registerEntityFactory("seat", [](const std::string&) { return std::unique_ptr<Entity>(new SeatEntity()); });
}

} // namespace dq
