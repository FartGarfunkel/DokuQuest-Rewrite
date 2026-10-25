#include "gen/gendefs.h"
#include <cstring>

namespace dq {

const char* biomeName(int b) {
  static const char* n[] = {"Ocean", "Beach", "Plains", "Forest", "Desert", "Taiga", "Mountains"};
  return b >= 0 && b < BIOME_COUNT ? n[b] : "?";
}

void GenBlocks::resolve() {
  const Registry& r = reg();
#define DQ_GB_RESOLVE(k) k = r.blockId(#k);
  DQ_GEN_BLOCKS(DQ_GB_RESOLVE)
#undef DQ_GB_RESOLVE
  solid = r.tSolid; liquid = r.tLiquid; wlog = r.tWlog;
  axis.assign(MAX_BLOCKS, 0); dbl.assign(MAX_BLOCKS, 0);
  for (const BlockDef& b : r.blocks) if (b.id < MAX_BLOCKS) { axis[b.id] = b.has("axisBlock"); dbl[b.id] = b.has("double"); }
}

void GenBuf::store(Chunk& c) const {
  for (int sy = 0; sy < SECS; sy++) {
    const BlockId* src = blocks + sy * 4096;
    const u8* msrc = meta + sy * 4096;
    u16 n = 0;
    for (int i = 0; i < 4096; i++) n += src[i] != 0;
    if (!n) { if (c.secs[sy]) { std::memset(c.secs[sy]->blocks, 0, sizeof(c.secs[sy]->blocks)); c.secs[sy]->solidCount = 0; } continue; }
    Section* s = c.ensure(sy);
    std::memcpy(s->blocks, src, sizeof(s->blocks));
    std::memcpy(s->meta, msrc, sizeof(s->meta));
    s->solidCount = n;
  }
  c.recomputeMaxY();
}

GenBuf& GenBuf::local() {
  thread_local std::unique_ptr<GenBuf> b;
  if (!b) b = std::make_unique<GenBuf>();
  b->clear();
  return *b;
}

} // namespace dq
