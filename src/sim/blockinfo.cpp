#include "sim/blockinfo.h"

namespace dq {

static std::vector<BlockInfo> g_info;
static KnownBlocks g_kb;
static size_t g_builtFor = 0;

static void rebuild() {
  const Registry& R = reg();
  g_info.assign(R.blocks.size(), BlockInfo{});
  for (size_t i = 0; i < R.blocks.size(); i++) {
    const BlockDef& d = R.blocks[i];
    BlockInfo& b = g_info[i];
    auto flag = [&](const char* k, u32 f) { if (d.has(k)) b.f |= f; };
    flag("climb", BF::Climb); flag("scaffold", BF::Scaffold); flag("sticky", BF::Sticky); flag("fire", BF::Fire);
    flag("hot", BF::Hot); flag("bouncy", BF::Bouncy); flag("slippery", BF::Slippery); flag("bed", BF::Bed);
    flag("door", BF::Door); flag("trapdoor", BF::Trapdoor); flag("falls", BF::Falls); flag("needsSupport", BF::NeedsSupport);
    flag("crop", BF::Crop); flag("tree", BF::Tree); flag("isLog", BF::IsLog); flag("isLeaves", BF::IsLeaves);
    flag("container", BF::Container); flag("furnace", BF::Furnace); flag("double", BF::Double); flag("axisBlock", BF::AxisBlock);
    flag("sign", BF::Sign); flag("hangs", BF::Hangs); flag("alwaysWet", BF::AlwaysWet); flag("tnt", BF::Tnt); flag("carry", BF::Carry);
    flag("facingModel", BF::FacingModel); flag("candle", BF::Candle); flag("attached", BF::Attached); flag("stackPlant", BF::StackPlant);
    flag("hardensTo", BF::HardensTo); flag("flower", BF::Flower);
    if (d.props.contains("tex") && d.props["tex"].is_object() && d.props["tex"].contains("front")) b.f |= BF::FacingFront;
    b.slow = (float)d.num("slow", 1);
    if (d.has("furnace")) b.furnace = d.props["furnace"].is_string() ? d.props["furnace"].get<std::string>() : "furnace";
  }
  g_kb = KnownBlocks{};
#define DQ_KB_SET(n) g_kb.n = R.blockId(#n);
  DQ_KNOWN_BLOCKS(DQ_KB_SET)
#undef DQ_KB_SET
  g_builtFor = R.blocks.size();
}

const BlockInfo& binfo(BlockId id) {
  if (g_builtFor != reg().blocks.size()) rebuild();
  return id < g_info.size() ? g_info[id] : g_info[0];
}
const KnownBlocks& KB() {
  if (g_builtFor != reg().blocks.size()) rebuild();
  return g_kb;
}

} // namespace dq
