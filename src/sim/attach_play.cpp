// sim/attach_play.cpp — see attach_play.h.
#include "sim/attach_play.h"
#include "world/attach.h"
#include "sim/blockinfo.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/events.h"
#include "sim/combat.h"

namespace dq {

static vec3 partCentre(const World& w, int x, int y, int z, int slot) {
  std::vector<BoxF> b;
  if (!partWorldBoxes(w, x, y, z, slot, ShapePurpose::Select, b) || b.empty()) return vec3(x, y, z) + vec3(0.5f);
  vec3 lo = b[0].lo, hi = b[0].hi;
  for (auto& q : b) { lo = glm::min(lo, q.lo); hi = glm::max(hi, q.hi); }
  return (lo + hi) * 0.5f;
}

bool partBlocked(const World& w, int x, int y, int z, int slot, BlockId id, u8 newMeta) {
  if (!w.hasAttachments(x, y, z)) return false;
  return !attachRoomFree(w, x, y, z, id, newMeta, slot, slot < 0);
}

// ---- placing ----
static bool bigBlock(BlockId id) { return bhas(id, BF::Door) || bhas(id, BF::Double) || bhas(id, BF::Bed); }

int placeAttachment(Level& lv, Player* p, World& w, int x, int y, int z, BlockId bid, u8 meta) {
  const Registry& R = reg();
  const BlockId cur = w.getBlock(x, y, z);
  if (!cur || R.tLiquid[cur] || (R.tReplaceable[cur] && !w.hasAttachments(x, y, z))) return -1;
  auto say = [&](const char* m) { if (p) toast(lv, *p, m); };
  const auto* parts = w.attachments(x, y, z);
  if (attachable(bid)) {
    meta &= (u8)~WLOG;
    if (parts && (int)parts->size() >= MAX_ATTACH) { say("There's no room for any more there"); return 0; }
    if (!attachSupported(w, x, y, z, bid, meta)) { say("It needs something to hold on to"); return 0; }
    if (!attachRoomFree(w, x, y, z, bid, meta)) { say("There's no room for it there"); return 0; }
    if (p && lv.events && !lv.events->emit("blockPlace", json::array({p->name, x, y, z, R.block(bid).key}))) return 0;
    json be;
    if (bhas(bid, BF::Sign)) be = {{"type", "sign"}, {"lines", {"", "", "", ""}}};
    w.addAttachment(x, y, z, bid, meta, SetFlags::Urgent, be);
    sfxMaterial(lv, R.block(bid).sound, "place", partCentre(w, x, y, z, (int)w.attachments(x, y, z)->size() - 1));
    return 1;
  }
  // the cell's main block is a small part (a trapdoor flipped up against the wall, a torch on the wall of a tunnel): it
  // steps aside for this one. Rails too: track laid under the tunnel's torches (the rail stays the cell's main block, so
  // carts and the redstone find it where they always do)
  if (attachable(cur) && !bigBlock(bid) && (R.tSolid[bid] || traits(bid).rail)) {
    if (parts && (int)parts->size() >= MAX_ATTACH) { say("There's no room for it there"); return 0; }
    std::vector<PixBox> mine, other;
    const bool fullMine = !partRoom(w, x, y, z, bid, meta, true, mine);
    auto clash = [&](BlockId id, u8 m) {
      const bool full = !partRoom(w, x, y, z, id, m, false, other);
      if (fullMine || full) return true;
      for (auto& a : mine) for (auto& b : other)
        if (a.x0 < b.x1 - 0.01f && b.x0 < a.x1 - 0.01f && a.y0 < b.y1 - 0.01f && b.y0 < a.y1 - 0.01f && a.z0 < b.z1 - 0.01f && b.z0 < a.z1 - 0.01f) return true;
      return false;
    };
    bool fits = !clash(cur, w.getMeta(x, y, z));
    if (parts) for (const Attachment& a : *parts) if (fits && clash(a.id, a.meta)) fits = false;
    if (!fits) { say("There's no room for it there"); return 0; }
    return 2;
  }
  return -1;
}
// (placeAttachment said 2) the main block steps aside into the attachments, its entity with it
void attachStepAside(World& w, int x, int y, int z) {
  const BlockId cur = w.getBlock(x, y, z);
  json be; if (json* e = w.blockEntity(x, y, z)) be = *e;
  w.addAttachment(x, y, z, cur, (u8)(w.getMeta(x, y, z) & ~WLOG), SetFlags::Urgent, be);
}

// ---- using ----
static std::string doorSnd(BlockId id, bool open) {
  const std::string& k = reg().block(id).key;
  const bool iron = k.find("iron") != std::string::npos || k.find("copper") != std::string::npos;
  return std::string(iron ? "door_iron" : "trapdoor") + (open ? "_open" : "_close");
}

bool useAttachment(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack* held) {
  const auto* parts = w.attachments(hit.block.x, hit.block.y, hit.block.z);
  if (!parts || hit.part < 0 || hit.part >= (int)parts->size()) return false;
  const int x = hit.block.x, y = hit.block.y, z = hit.block.z, slot = hit.part;
  const Attachment a = (*parts)[slot];
  const Registry& R = reg();
  const BlockDef& d = R.block(a.id);
  const vec3 c = partCentre(w, x, y, z, slot);
  const int k = R.tShape[a.id];
  if (k == ShapeKind::Lever) {
    w.setAttachmentMeta(x, y, z, slot, a.meta ^ 8, SetFlags::Urgent);
    lv.fx->sound(a.meta & 8 ? "click_off" : "click", c, 16);
    return true;
  }
  if (k == ShapeKind::Button) {
    if (!(a.meta & 8)) { w.setAttachmentMeta(x, y, z, slot, a.meta | 8, SetFlags::Urgent); lv.fx->sound("click", c, 16); }
    return true;
  }
  if (bhas(a.id, BF::Trapdoor) || k == ShapeKind::Trapdoor) {
    if ((d.key == "iron_trapdoor" || d.has("rsOnly")) && !p.creative()) { toast(lv, p, "Iron doors open with redstone \xE2\x80\x94 try a button or lever beside it"); return true; }
    const u8 nm = a.meta ^ 4;
    if (partBlocked(w, x, y, z, slot, a.id, nm)) { toast(lv, p, "It's blocked"); return true; }
    w.setAttachmentMeta(x, y, z, slot, nm, SetFlags::Urgent);
    lv.fx->sound(doorSnd(a.id, nm & 4), c, 16);
    return true;
  }
  if (bhas(a.id, BF::Sign)) { openScreen(lv, p, "sign", {{"pos", {x, y, z}}, {"slot", slot}}); return true; }
  if (k == ShapeKind::Candle) {   // (build.cpp useBlock: another of the same candle joins them, flint lights them, a bare hand puts them out)
    const ItemDef* hd = held && !held->empty() ? R.item(held->id) : nullptr;
    if (hd && hd->block == d.key) {
      if ((a.meta & 3) < 3 && !partBlocked(w, x, y, z, slot, a.id, (u8)(a.meta + 1))) {
        w.setAttachmentMeta(x, y, z, slot, (u8)(a.meta + 1), SetFlags::Urgent);
        if (!p.creative() && --held->n <= 0) held->clear();
        sfxMaterial(lv, d.sound, "place", c);
      }
      return true;   // (four already: nothing, never the cell's main block)
    }
    if (hd && hd->str("igniter") == "flint" && !(a.meta & 4)) { w.setAttachmentMeta(x, y, z, slot, a.meta | 4, SetFlags::Urgent); lv.fx->sound("ignite", c, 16); wearHeld(lv, p, 1); return true; }
    if (!held && (a.meta & 4)) { w.setAttachmentMeta(x, y, z, slot, a.meta & ~4, SetFlags::Urgent); lv.fx->sound("fizz", c, 16); return true; }
    return false;
  }
  if (d.has("frame")) {
    json* e = w.attachmentEntity(x, y, z, slot);
    if (!e) return false;
    if (e->is_object() && e->contains("item")) { (*e)["rot"] = (e->value("rot", 0) + 1) & 7; w.attachmentEntityChanged(x, y, z); lv.fx->sound("click", c, 16); return true; }
    if (held && !held->empty()) {
      ItemStack one = *held; one.n = 1;
      *e = {{"type", "frame"}, {"item", one.toJson()}, {"rot", 0}};
      w.attachmentEntityChanged(x, y, z);
      if (--held->n <= 0) held->clear();
      lv.fx->sound("click", c, 16);
      return true;
    }
  }
  return false;
}

// ---- breaking ----
void breakAttachment(Level& lv, Player* p, World& w, int x, int y, int z, int slot, bool drops, const ToolInfo* tool) {
  const auto* parts = w.attachments(x, y, z);
  if (!parts || slot < 0 || slot >= (int)parts->size()) return;
  const Attachment a = (*parts)[slot];
  const BlockDef& d = reg().block(a.id);
  if (p && lv.events && !lv.events->emit("blockBreak", json::array({p->name, x, y, z, d.key}))) return;   // a mod said no
  const vec3 c = partCentre(w, x, y, z, slot);
  w.removeAttachment(x, y, z, slot, SetFlags::Urgent);
  sfxMaterial(lv, d.sound, "break", c);
  burst(lv, c, d.key, 8, 0.8f);
  if (!drops) return;
  if (a.be.is_object() && a.be.contains("item")) { ItemStack s = ItemStack::fromJson(a.be["item"]); if (!s.empty()) dropItem(lv, w.realm, s, c, true); }
  const std::string drop = d.drop.empty() ? d.key : d.drop;   // (the block's own drop: a lit redstone torch's is a torch)
  const int n = reg().tShape[a.id] == ShapeKind::Candle ? (a.meta & 3) + 1 : 1;   // (candles: each of them)
  if (drop != "air" && drop != "none" && reg().item(drop)) dropItem(lv, w.realm, ItemStack(drop, n), c, true);
  (void)tool;
}

void dropAllAttachments(Level& lv, World& w, int x, int y, int z, bool drops) {
  while (const auto* parts = w.attachments(x, y, z)) breakAttachment(lv, nullptr, w, x, y, z, (int)parts->size() - 1, drops);
}

// ---- the system: pop-offs, buttons letting go ----
namespace {
class AttachWatch : public WorldSystem {
public:
  AttachWatch(Level& lv) : lv(lv) {}
  const char* name() const override { return "attachments"; }
  Level& lv;
  struct Release { int x, y, z; BlockId id; double at; };
  std::vector<Release> releases;
  double clock = 0;
  void onBlockChanged(World& w, int x, int y, int z, BlockId oldId, BlockId newId) override {
    if (lv.isRemote() || oldId == newId) return;
    const Registry& R = reg();
    if (R.tSolid[newId]) return;
    static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto& n : N) {
      const int ax = x + n[0], ay = y + n[1], az = z + n[2];
      const auto* parts = w.attachments(ax, ay, az);
      if (!parts) continue;
      for (int s = (int)parts->size() - 1; s >= 0; s--) {
        parts = w.attachments(ax, ay, az);
        if (!parts || s >= (int)parts->size()) continue;
        const Attachment& a = (*parts)[s];
        ivec3 d;
        if (attachSupportDir(a.id, a.meta, d) && d == ivec3(-n[0], -n[1], -n[2])) breakAttachment(lv, nullptr, w, ax, ay, az, s, true);
      }
    }
  }
  void onAttachmentsChanged(World& w, int x, int y, int z) override {
    if (lv.isRemote()) return;
    const auto* parts = w.attachments(x, y, z);
    if (!parts) return;
    for (const Attachment& a : *parts)
      if (reg().tShape[a.id] == ShapeKind::Button && (a.meta & 8)) {
        bool have = false;
        for (auto& r : releases) if (r.x == x && r.y == y && r.z == z && r.id == a.id) have = true;
        if (!have) releases.push_back({x, y, z, a.id, clock + (reg().block(a.id).has("wooden") ? 1.5 : 1.0)});
      }
  }
  void tick(World& w, double dt) override {
    clock += dt;
    for (size_t i = 0; i < releases.size();) {
      Release r = releases[i];
      if (r.at > clock) { i++; continue; }
      releases[i] = releases.back(); releases.pop_back();
      if (const auto* parts = w.attachments(r.x, r.y, r.z))
        for (int s = 0; s < (int)parts->size(); s++) if ((*parts)[s].id == r.id && ((*parts)[s].meta & 8)) {
          w.setAttachmentMeta(r.x, r.y, r.z, s, (*parts)[s].meta & ~8);
          lv.fx->sound("click_off", vec3(r.x, r.y, r.z) + vec3(0.5f), 16);
          break;
        }
    }
  }
};

class AttachLevel : public LevelSystem {
public:
  const char* name() const override { return "attachments"; }
  std::unordered_map<int, World*> added;
  void tick(Level& lv, double) override {
    for (auto& [realm, wp] : lv.allRealms()) {
      World* w = wp.get();
      if (!w || (added.count(realm) && added[realm] == w)) continue;
      if (!w->system<AttachWatch>()) w->addSystem(std::make_shared<AttachWatch>(lv));
      added[realm] = w;
    }
  }
};

// collision and climbing (sim/physics.h): the attachments' own boxes
void extraBoxes(const World& w, double minX, double minY, double minZ, double maxX, double maxY, double maxZ, std::vector<DBox>& out) {
  const Registry& R = reg();
  static thread_local std::vector<PixBox> pb;
  for (int y = (int)std::floor(minY); y <= (int)std::floor(maxY - 1e-7); y++)
    for (int z = (int)std::floor(minZ); z <= (int)std::floor(maxZ - 1e-7); z++)
      for (int x = (int)std::floor(minX); x <= (int)std::floor(maxX - 1e-7); x++) {
        const auto* parts = w.attachments(x, y, z);
        if (!parts) continue;
        for (const Attachment& a : *parts) {
          if (!R.tSolid[a.id] || !partBoxes(w, x, y, z, a.id, a.meta, ShapePurpose::Collide, pb)) continue;
          for (auto& b : pb) {
            DBox d{x + b.x0 / 16.0, y + b.y0 / 16.0, z + b.z0 / 16.0, x + b.x1 / 16.0, y + b.y1 / 16.0, z + b.z1 / 16.0};
            if (d.x1 <= minX || d.x0 >= maxX || d.y1 <= minY || d.y0 >= maxY || d.z1 <= minZ || d.z0 >= maxZ) continue;
            out.push_back(d);
          }
        }
      }
}
bool extraClimb(const World& w, const Entity& e) {
  const int x0 = ifloor(e.pos.x - e.w), x1 = ifloor(e.pos.x + e.w), z0 = ifloor(e.pos.z - e.w), z1 = ifloor(e.pos.z + e.w);
  const int y0 = ifloor(e.pos.y), y1 = ifloor(e.pos.y + e.h - 0.01f);
  for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++)
    if (const auto* parts = w.attachments(x, y, z)) for (const Attachment& a : *parts) if (bhas(a.id, BF::Climb)) return true;
  return false;
}
} // namespace

void registerAttachments() {
  setExtraBoxes(&extraBoxes);
  addExtraClimb(&extraClimb);
  registerLevelSystem([](Level&) { return std::make_shared<AttachLevel>(); });
}

} // namespace dq
