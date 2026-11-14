// world/carve_forms.cpp — slabs and stairs become chiseled blocks (see carve_forms.h).
#include "world/carve_forms.h"
#include "world/chunk.h"
#include "world/shapes.h"
#include <vector>

namespace dq {

namespace {
struct Form { BlockId base = 0; u8 kind = 0; };   // kind 1 slab, 2 stairs
std::vector<Form> g_forms;                         // by block id

// octants kept on one side of the block: side 0 N (z low), 1 E (x high), 2 S (z high), 3 W (x low)
bool onSide(int side, int hx, int hz) { return side == 0 ? hz == 0 : side == 1 ? hx == 1 : side == 2 ? hz == 1 : hx == 0; }

// a stairs block's high half there (its facing side; with a corner, see STAIR_CORNER): a = facing | corner bits
bool stairHigh(int hx, int hz, int a) {
  const int s = a & 3;
  if (!(a & STAIR_CORNER)) return onSide(s, hx, hz);
  const int o = a & STAIR_RIGHT ? (s + 1) & 3 : (s + 3) & 3;
  return a & STAIR_OUTER ? onSide(s, hx, hz) && onSide(o, hx, hz) : onSide(s, hx, hz) || onSide(o, hx, hz);
}

u8 maskWhere(bool (*keep)(int, int, int, int), int arg) {
  u8 m = 0;
  for (int hx = 0; hx < 2; hx++) for (int hy = 0; hy < 2; hy++) for (int hz = 0; hz < 2; hz++)
    if (!keep(hx, hy, hz, arg)) m |= octBit(hx, hy, hz);
  return m;
}
} // namespace

void registerChiselForms() {
  Registry& R = reg();
  g_forms.assign(R.blocks.size(), Form{});
  std::unordered_set<std::string> gone;
  for (auto& b : R.blocks) {
    std::string fam = b.str("family"), base = b.str("base");
    if ((fam != "slab" && fam != "stairs") || base.empty()) continue;
    BlockId bid = R.blockId(base);
    if (!bid || !R.tSolid[bid] || !R.tOpaque[bid] || R.tShape[bid]) continue;   // only plain full bases can be carved
    g_forms[b.id] = {bid, (u8)(fam == "slab" ? 1 : 2)};
    gone.insert(b.key);
  }
  // their items leave the creative list and their recipes go: the chisel makes them now
  for (auto& it : R.items) if (gone.count(it.key)) { if (!it.props.is_object()) it.props = json::object(); it.props["hidden"] = true; }
  size_t before = R.recipes.size();
  R.recipes.erase(std::remove_if(R.recipes.begin(), R.recipes.end(), [&](const Recipe& r) { return gone.count(r.out) > 0; }), R.recipes.end());
  DQ_LOG("chisel: %zu slab/stairs blocks are chiseled forms now (%zu recipes retired)", gone.size(), before - R.recipes.size());
}

bool isSlabOrStairs(BlockId id) { return id < g_forms.size() && g_forms[id].base; }

CarveForm carveFormOf(BlockId id, u8 meta) {
  if (!isSlabOrStairs(id)) return {};
  const Form& f = g_forms[id];
  CarveForm out; out.base = f.base; out.meta = meta & WLOG;   // a waterlogged one keeps its water in the carved-away part
  if (f.kind == 1) {
    int m = meta & 7;
    if (m == 6) out.carve = 0;
    else if (m == 0) out.carve = maskWhere([](int, int hy, int, int) { return hy == 0; }, 0);
    else if (m == 1) out.carve = maskWhere([](int, int hy, int, int) { return hy == 1; }, 0);
    else if (m >= 2 && m <= 5) out.carve = maskWhere([](int hx, int, int hz, int s) { return onSide(s, hx, hz); }, m - 2);
  } else {
    int face = meta & 3; bool upside = meta & 4;
    // the full half (bottom, or top when upside-down) plus the high side of the other half; a corner: an inner one high on
    // its side to the left (right) as well, an outer one only where the two sides meet (a roof's corners whole: before,
    // every corner stair was a straight one, and a hipped roof had a hole at each corner)
    const int arg = face | (meta & (STAIR_CORNER | STAIR_OUTER | STAIR_RIGHT));
    out.carve = upside ? maskWhere([](int hx, int hy, int hz, int a) { return hy == 1 || stairHigh(hx, hz, a); }, arg)
                       : maskWhere([](int hx, int hy, int hz, int a) { return hy == 0 || stairHigh(hx, hz, a); }, arg);
  }
  if (out.carve == 0xFF) return {};
  return out;
}

void convertSlabsStairs(Chunk& c) {
  if (g_forms.empty()) return;
  for (int sy = 0; sy < SECS; sy++) {
    Section* s = c.section(sy);
    if (!s) continue;
    for (int i = 0; i < 4096; i++) {
      BlockId id = s->blocks[i];
      if (id >= g_forms.size() || !g_forms[id].base) continue;
      CarveForm f = carveFormOf(id, s->meta[i]);
      if (!f.base) continue;
      s->blocks[i] = f.base; s->meta[i] = f.meta; s->carve[i] = f.carve;
    }
  }
}

} // namespace dq
