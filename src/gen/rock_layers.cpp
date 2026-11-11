// gen/rock_layers.cpp — rock layers (terrain version 12; after TerraFirmaCraft's rock layers): under the soil each region
// (about 200 blocks across, its edges wavering) has its own rocks, one of each group: a sedimentary rock on top (limestone,
// chalk, shale, claystone, conglomerate, sandstone) — a volcanic one instead in a volcanic region (basalt, andesite,
// rhyolite, dacite) — a metamorphic one under it (marble, slate, gneiss, schist, quartzite) and an igneous one at the bottom
// (granite, diorite, gabbro). Ores keep to their rocks as TFC's do: zinc (sphalerite) in metamorphic rock, bismuth in
// sedimentary, tin (cassiterite) in the igneous intrusive rocks, silver in granite and gneiss, gold in the igneous rocks;
// copper, iron and the rest anywhere.
#include "gen/overworld.h"

namespace dq {

BlockId OverworldGen::layerRock(int wx, int y, int wz, int H) const {
  const double warp = nDetail.noise2(wx / 60.0, wz / 60.0) * 48;
  const int rx = (int)std::floor((wx + warp) / 200.0), rz = (int)std::floor((wz - warp) / 200.0);
  auto pick = [&](int salt, int n) { return std::min(n - 1, (int)(hashFloat(rx * 31 + salt, rz, seed_, 77) * n)); };
  const BlockId SED[] = {B.limestone, B.chalk, B.shale, B.claystone, B.conglomerate, B.sandstone};
  const BlockId MET[] = {B.marble, B.slate, B.gneiss, B.schist, B.quartzite};
  const BlockId IGI[] = {B.granite, B.diorite, B.gabbro};
  const BlockId IGE[] = {B.basalt, B.andesite, B.rhyolite, B.dacite};
  const bool volcanic = hashFloat(rx * 31 + 7, rz, seed_, 78) < 0.25;
  const int top = 14 + (int)((nDetail.noise2(wx / 40.0 + 3, wz / 40.0) + 1) * 6);
  const int bottom = 18 + (int)((nDetail.noise2(wx / 50.0 - 5, wz / 50.0) + 1) * 8);
  BlockId id;
  if (y < bottom) id = IGI[pick(3, 3)];
  else if (H - y <= top) id = volcanic ? IGE[pick(4, 4)] : SED[pick(1, 6)];
  else id = MET[pick(2, 5)];
  return id ? id : B.stone;
}

void OverworldGen::initRockLayers() {
  if (!rockLayers) return;
  const BlockId all[] = {B.limestone, B.chalk, B.shale, B.claystone, B.conglomerate, B.sandstone, B.marble, B.slate, B.gneiss, B.schist, B.quartzite,
                         B.granite, B.diorite, B.gabbro, B.basalt, B.andesite, B.rhyolite, B.dacite};
  for (BlockId id : all) if (id) { carve_.carvable[id] = 1; rockRep_[id] = 1; oreRep_[id] = 1; }
  auto host = [&](std::vector<u8>& v, std::initializer_list<BlockId> ids) { v.assign(MAX_BLOCKS, 0); for (BlockId id : ids) if (id) v[id] = 1; };
  host(zincRep_, {B.marble, B.slate, B.gneiss, B.schist, B.quartzite});
  host(bismuthRep_, {B.limestone, B.chalk, B.shale, B.claystone, B.conglomerate, B.sandstone});
  host(tinRep_, {B.granite, B.diorite, B.gabbro});
  host(silverRep_, {B.granite, B.gneiss});
  host(goldRep_, {B.granite, B.diorite, B.gabbro, B.basalt, B.andesite, B.rhyolite, B.dacite});
}

const u8* OverworldGen::oreHost(BlockId ore) const {
  if (!rockLayers || !looseGround) return nullptr;   // a classic world: every ore where it always was, whatever the rock (immersive: TFC's hosts)
  if (ore == B.zinc_ore) return zincRep_.data();
  if (ore == B.bismuth_ore) return bismuthRep_.data();
  if (ore == B.tin_ore) return tinRep_.data();
  if (ore == B.silver_ore) return silverRep_.data();
  if (ore == B.gold_ore) return goldRep_.data();
  return nullptr;
}

} // namespace dq
