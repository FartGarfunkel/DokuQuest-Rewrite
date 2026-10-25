// sim/inv_ops.cpp — see inv_ops.h. A line-by-line port of the slot rules in ui/invui.js and the
// Container / PlayerInventory helpers in game/inventory.js.
#include "sim/inv_ops.h"
#include "world/world.h"

namespace dq {

static const char* GRP_NAMES[] = {"main", "armor", "off", "back", "hook", "charm", "cape", "bp", "grid", "cont", "furn", "out"};
const char* grpName(Grp g) { return (int)g < (int)Grp::Count ? GRP_NAMES[(int)g] : "?"; }

static bool hasContents(const ItemStack& s) { return s.tag.is_object() && s.tag.contains("c"); }
bool stacksMerge(const ItemStack& a, const ItemStack& b) {
  if (a.empty() || b.empty() || a.id != b.id || a.d || b.d || hasContents(a) || hasContents(b) || maxStackOf(a.id) <= 1) return false;
  auto norm = [](const json& t) { return t.is_object() && !t.empty() ? t : json(); };
  return norm(a.tag) == norm(b.tag);   // dye colour, map id... must agree
}

static std::vector<ItemStack> loadArr(const json& j, size_t n) {
  std::vector<ItemStack> v(n);
  if (j.is_array()) for (size_t i = 0; i < n && i < j.size(); i++) v[i] = ItemStack::fromJson(j[i]);
  return v;
}
static json saveArr(const std::vector<ItemStack>& v) { json a = json::array(); for (auto& s : v) a.push_back(s.toJson()); return a; }
static int packSize(const ItemStack& s) { const ItemDef* d = s.empty() ? nullptr : s.def(); return d ? (int)d->num("backpack", 0) : 0; }

// (0.9.0.65) two chests side by side open as one large chest (invui.js chestPartner)
static bool chestPartner(World& w, int x, int y, int z, ivec3& out) {
  BlockId chest = reg().blockId("chest");
  int m = w.getMeta(x, y, z) & 3; ivec2 ax = (m & 1) ? ivec2(0, 1) : ivec2(1, 0);
  auto same = [&](int k) { int a = x + ax.x * k, c = z + ax.y * k; return w.getBlock(a, y, c) == chest && (w.getMeta(a, y, c) & 3) == m; };
  int below = 0; while (below < 16 && same(-(below + 1))) below++;
  int k = (below & 1) ? -1 : (same(1) ? 1 : 0);
  if (!k) return false;
  out = ivec3(x + ax.x * k, y, z + ax.y * k);
  return true;
}

InvSession::InvSession(Player& pl, World* wo, InvOpen op, bool cr) : p(pl), w(wo), o(std::move(op)), creative(cr) {
  W = o.mode == "table" ? 3 : 2;
  grid.assign(W * W, ItemStack());
  refresh();
}

json* InvSession::entity(ivec3 pos, bool create, const json& init) {
  if (!w) return nullptr;
  json* e = w->blockEntity(pos.x, pos.y, pos.z, create);
  if (e && create && (!e->is_object() || e->empty())) *e = init;
  return e;
}

void InvSession::refresh() {
  ok = true;
  auto pd = [&](const char* k, size_t n) { return loadArr(p.data.contains(k) ? p.data[k] : json(), n); };
  back = pd("back", 1); hook = pd("hook", 1); charm = pd("charm", 1); cape = pd("cape", 1); ender = pd("ender", 27);
  bp.clear();
  if (int n = packSize(back[0])) bp = loadArr(back[0].tag.is_object() && back[0].tag.contains("c") ? back[0].tag["c"] : json(), n);
  dbl = false; contIsPack = false; furnaceKind.clear();
  if (o.mode == "container") {
    if (o.heldPack) {
      ItemStack& h = p.held(); int n = packSize(h);
      if (!n) { ok = false; cont.clear(); }
      else { cont = loadArr(h.tag.is_object() && h.tag.contains("c") ? h.tag["c"] : json(), n); title = h.def()->name; contIsPack = true; }
    } else if (o.ender) { cont = ender; title = "Ender Chest"; }
    else if (o.entItems) {
      json* it = o.entItems();
      if (!it || !it->is_array()) { ok = false; cont.clear(); }
      else { contN = (int)it->size(); cont = loadArr(*it, it->size()); title = o.entTitle; }
    } else if (o.hasPos && w) {
      const BlockDef& b = reg().block(w->getBlock(o.pos.x, o.pos.y, o.pos.z));
      if (b.has("container")) {
        int n = (int)b.num("container", 27); contN = n;
        json init = {{"type", "chest"}, {"items", json::array()}};
        for (int i = 0; i < n; i++) init["items"].push_back(nullptr);
        json* e = entity(o.pos, true, init);
        ivec3 q;
        if (b.key == "chest" && !o.single && chestPartner(*w, o.pos.x, o.pos.y, o.pos.z, q) && entity(q, true, init)) {
          bool lo = (q.x + q.z) < (o.pos.x + o.pos.z);
          posA = lo ? q : o.pos; posB = lo ? o.pos : q; dbl = true; nA = n;
          json* a = entity(posA, true, init); json* bb = entity(posB, true, init);
          cont = loadArr((*a)["items"], n); auto second = loadArr((*bb)["items"], n);
          cont.insert(cont.end(), second.begin(), second.end());
          title = "Large Chest";
        } else if (e) { posA = o.pos; cont = loadArr((*e)["items"], n); title = b.name; }
        else ok = false;
      } else if (b.key == "backpack_block") {
        json* e = entity(o.pos, false, nullptr);
        ItemStack st = e && e->contains("stack") ? ItemStack::fromJson((*e)["stack"]) : ItemStack();
        int n = packSize(st);
        if (!n) ok = false;
        else { cont = loadArr(st.tag.is_object() && st.tag.contains("c") ? st.tag["c"] : json(), n); title = st.def()->name; contIsPack = true; }
      } else ok = false;
    } else ok = false;
  } else if (o.mode == "furnace") {
    if (o.hasPos && w) {
      const BlockDef& b = reg().block(w->getBlock(o.pos.x, o.pos.y, o.pos.z));
      if (b.has("furnace")) {
        furnaceKind = b.props["furnace"].is_string() ? b.props["furnace"].get<std::string>() : "furnace";
        json* e = entity(o.pos, true, {{"type", "furnace"}, {"items", {nullptr, nullptr, nullptr}}, {"burn", 0}, {"burnMax", 0}, {"cook", 0}});
        furn = loadArr(e ? (*e)["items"] : json(), 3);
        title = b.name;
      } else ok = false;
    } else ok = false;
  } else if (o.mode == "table") title = "Crafting";
  match = matchGrid(grid, W);
}

void InvSession::store(Grp g) {
  switch (g) {
    case Grp::Back: p.data["back"] = saveArr(back); break;
    case Grp::Hook: p.data["hook"] = saveArr(hook); break;
    case Grp::Charm: p.data["charm"] = saveArr(charm); break;
    case Grp::Cape: p.data["cape"] = saveArr(cape); break;
    case Grp::Bp:
      if (packSize(back[0])) { if (!back[0].tag.is_object()) back[0].tag = json::object(); back[0].tag["c"] = saveArr(bp); p.data["back"] = saveArr(back); }
      break;
    case Grp::Cont:
      if (o.mode != "container" || !ok) break;
      if (o.heldPack) { ItemStack& h = p.held(); if (packSize(h)) { if (!h.tag.is_object()) h.tag = json::object(); h.tag["c"] = saveArr(cont); } }
      else if (o.ender) { ender = cont; p.data["ender"] = saveArr(ender); }
      else if (o.entItems) { if (json* it = o.entItems()) *it = saveArr(cont); }
      else if (o.hasPos && w) {
        const BlockDef& b = reg().block(w->getBlock(o.pos.x, o.pos.y, o.pos.z));
        if (b.key == "backpack_block") {
          json* e = entity(o.pos, false, nullptr);
          if (e && e->contains("stack") && (*e)["stack"].is_object()) (*e)["stack"]["c"] = saveArr(cont);
        } else if (dbl) {
          std::vector<ItemStack> a(cont.begin(), cont.begin() + std::min<size_t>(nA, cont.size())), bb(cont.begin() + std::min<size_t>(nA, cont.size()), cont.end());
          if (json* e = entity(posA, false, nullptr)) (*e)["items"] = saveArr(a);
          if (json* e = entity(posB, false, nullptr)) (*e)["items"] = saveArr(bb);
          w->markModified(posB.x, posB.y, posB.z);
        } else if (json* e = entity(posA, false, nullptr)) (*e)["items"] = saveArr(cont);
        w->markModified(o.pos.x, o.pos.y, o.pos.z);
      }
      break;
    case Grp::Furn:
      if (o.mode == "furnace" && ok && w) { if (json* e = entity(o.pos, false, nullptr)) (*e)["items"] = saveArr(furn); w->markModified(o.pos.x, o.pos.y, o.pos.z); }
      break;
    default: break;
  }
}
void InvSession::changed(Grp g) {
  // everything an op may have touched is written back; the backpack's contents follow the backpack
  if (g != Grp::Back) store(Grp::Bp);
  for (Grp s : {Grp::Back, Grp::Hook, Grp::Charm, Grp::Cape}) store(s);
  if (o.mode == "container") store(Grp::Cont);
  if (o.mode == "furnace") store(Grp::Furn);
  if (!o.ender) p.data["ender"] = saveArr(ender);
  if (onChanged) onChanged(g);
  refresh();
}

std::vector<ItemStack>* InvSession::arr(Grp g) {
  switch (g) {
    case Grp::Main: return &p.inv;
    case Grp::Armor: return &p.armor;
    case Grp::Off: return &p.offhand;
    case Grp::Back: return &back;
    case Grp::Hook: return &hook;
    case Grp::Charm: return &charm;
    case Grp::Cape: return &cape;
    case Grp::Bp: return &bp;
    case Grp::Grid: return &grid;
    case Grp::Cont: return &cont;
    case Grp::Furn: return &furn;
    default: return nullptr;
  }
}
int InvSession::groupSize(Grp g) { if (g == Grp::Out) return 1; auto* a = arr(g); return a ? (int)a->size() : 0; }
const ItemStack* InvSession::at(Grp g, int i) {
  static ItemStack out;
  if (g == Grp::Out) { out = match ? match.result() : ItemStack(); return &out; }
  auto* a = arr(g); return a && i >= 0 && i < (int)a->size() ? &(*a)[i] : nullptr;
}

bool InvSession::accepts(Grp g, int i, const ItemStack& s) const {
  const ItemDef* d = s.def();
  if (!d) return false;
  switch (g) {
    case Grp::Armor: return d->has("armor") && d->props["armor"].value("slot", -1) == i;
    case Grp::Back: return d->has("backpack");
    case Grp::Hook: return d->has("hook");
    case Grp::Charm: return d->has("charm");
    case Grp::Cape: return d->has("cape");
    case Grp::Bp: return !d->has("backpack");
    case Grp::Cont: return !(contIsPack && d->has("backpack"));
    case Grp::Furn: return i == 0 ? true : i == 1 ? fuelValue(s.id) > 0 : false;
    case Grp::Out: return false;
    default: return true;
  }
}

Bags InvSession::bags() { Bags b; b.main = &p.inv; b.bp = bp.empty() ? nullptr : &bp; return b; }
int InvSession::bagAdd(ItemStack& s) {
  int left = addToSlots(p.inv, s);
  if (left > 0 && !bp.empty()) { left = addToSlots(bp, s); store(Grp::Bp); }
  return left;
}
void InvSession::giveBack(const ItemStack& s0) {
  if (s0.empty()) return;
  ItemStack s = s0;
  int left = bagAdd(s);
  if (left > 0 && onThrow) { ItemStack t = s0; t.n = left; onThrow(t); }
}

// ---- clicking slots ------------------------------------------------------------------------------------
void InvSession::slotClick(Grp g, int i, bool right, bool shift) {
  refresh();
  if (g == Grp::Out) { takeOutput(shift); return; }
  auto* a = arr(g);
  if (!a || i < 0 || i >= (int)a->size()) return;
  ItemStack& s = (*a)[i];
  ItemStack& cur = p.cursor;
  if (shift && !s.empty()) { quickMove(g, i); changed(g); sound("click"); return; }
  if (g == Grp::Furn && i == 2) {
    // furnace output: take only
    if (s.empty()) return;
    if (cur.empty()) { cur = s; s.clear(); }
    else if (stacksMerge(cur, s) && cur.n + s.n <= maxStackOf(s.id)) { cur.n += s.n; s.clear(); }
    else return;
    changed(g); sound("click"); return;
  }
  bool hadS = !s.empty(), hadCur = !cur.empty();
  ItemStack before = s;
  if (!right) {
    if (cur.empty()) { if (!s.empty()) { cur = s; s.clear(); } }
    else if (s.empty()) { if (accepts(g, i, cur)) { s = cur; cur.clear(); } }
    else if (stacksMerge(s, cur)) { int mv = std::min(maxStackOf(s.id) - s.n, cur.n); s.n += mv; cur.n -= mv; if (cur.n <= 0) cur.clear(); }
    else if (accepts(g, i, cur)) std::swap(s, cur);
  } else {
    if (cur.empty() && !s.empty()) { int half = (s.n + 1) / 2; cur = s; cur.n = half; s.n -= half; if (s.n <= 0) s.clear(); }
    else if (!cur.empty()) {
      if (s.empty() && accepts(g, i, cur)) { s = cur; s.n = 1; cur.n--; }
      else if (!s.empty() && stacksMerge(s, cur) && s.n < maxStackOf(s.id)) { s.n++; cur.n--; }
      if (cur.n <= 0) cur.clear();
    }
  }
  bool gear = g == Grp::Armor || g == Grp::Back || g == Grp::Hook || g == Grp::Off || g == Grp::Charm || g == Grp::Cape;
  if (gear && !s.empty() && !(s.id == before.id && s.n == before.n)) sound("equip");
  if (hadS || hadCur) sound("click");
  if (g == Grp::Grid) ghost.clear();
  changed(g);
}

// Can a drag spread items into this slot? (empty, or the same thing with room)
bool InvSession::dragTarget(Grp g, int i) {
  const ItemStack& st = drag.active ? drag.orig : p.cursor;
  if (st.empty() || g == Grp::Out || (g == Grp::Furn && i == 2)) return false;
  auto* a = arr(g); if (!a || i < 0 || i >= (int)a->size()) return false;
  const ItemStack& s = (*a)[i];
  if (!accepts(g, i, st)) return false;
  return s.empty() || (stacksMerge(s, st) && s.n < maxStackOf(s.id));
}
void InvSession::dragBegin(Grp g, int i, bool right) {
  refresh();
  drag = Drag(); drag.active = true; drag.right = right; drag.orig = p.cursor;
  dragAdd(g, i);
}
void InvSession::dragAdd(Grp g, int i) {
  if (!drag.active) return;
  for (auto& [g0, i0] : drag.slots) if (g0 == g && i0 == i) return;
  if (!dragTarget(g, i)) return;
  auto* a = arr(g);
  drag.slots.push_back({g, i}); drag.base.push_back((*a)[i]);
  if (drag.right) {
    // one into each slot you pass over
    if (p.cursor.empty()) return;
    ItemStack& s = (*a)[i];
    if (s.empty()) { s = p.cursor; s.n = 1; } else s.n++;
    if (--p.cursor.n <= 0) p.cursor.clear();
  } else {
    // share the stack evenly over every slot dragged across (what doesn't divide stays in hand)
    int k = (int)drag.slots.size(), each = drag.orig.n / k;
    if (each < 1) { drag.slots.pop_back(); drag.base.pop_back(); return; }
    int used = 0;
    for (size_t j = 0; j < drag.slots.size(); j++) {
      auto& [g0, i0] = drag.slots[j];
      auto& arr0 = *arr(g0); const ItemStack& base = drag.base[j];
      int room = maxStackOf(drag.orig.id) - (base.empty() ? 0 : base.n), put = std::min(each, room);
      if (put > 0) { arr0[i0] = drag.orig; arr0[i0].n = (base.empty() ? 0 : base.n) + put; } else arr0[i0] = base;
      used += std::max(0, put);
    }
    int left = drag.orig.n - used;
    if (left > 0) { p.cursor = drag.orig; p.cursor.n = left; } else p.cursor.clear();
  }
  sound("click");
  for (auto& [g0, i0] : drag.slots) if (g0 == Grp::Grid) ghost.clear();
  changed(g);
}
void InvSession::dragEnd() {
  if (!drag.active) return;
  Drag d = drag; drag = Drag();
  // a plain left click (no drag) puts the whole stack down, or swaps, as before
  if (!d.right && d.slots.size() == 1) {
    auto [g0, i0] = d.slots[0];
    (*arr(g0))[i0] = d.base[0]; p.cursor = d.orig;
    slotClick(g0, i0, false, false);
  }
}

// Number key over a slot: swap it with that hotbar slot.
void InvSession::hotbarSwap(Grp g, int i, int k) {
  refresh();
  if (g == Grp::Out || (g == Grp::Furn && i == 2)) return;
  auto* a = arr(g); if (!a || i < 0 || i >= (int)a->size() || k < 0 || k > 8) return;
  if (g == Grp::Main && i == k) return;
  ItemStack s = (*a)[i], h = p.inv[k];
  if (!h.empty() && !accepts(g, i, h)) return;
  (*a)[i] = h; p.inv[k] = s;
  changed(g); sound("click");
}

void InvSession::quickMove(Grp g, int i) {
  auto* a = arr(g); if (!a) return;
  ItemStack& s = (*a)[i];
  if (s.empty()) return;
  const ItemDef* d = s.def(); bool isPack = d && d->has("backpack");
  auto into = [](std::vector<ItemStack>& list, ItemStack& st) { return addToSlots(list, st); };
  auto toBag = [&]() {
    int left = addToSlots(p.inv, s);
    if (left > 0 && !bp.empty() && !isPack) left = into(bp, s);
    if (left <= 0) s.clear();
  };
  if (g == Grp::Cont) {
    std::vector<int> order; for (int k = 0; k < 9; k++) order.push_back(k); for (int k = 0; k < 27; k++) order.push_back(k + 9);
    int left = addToSlots(p.inv, s, &order);
    if (left > 0 && !bp.empty() && !isPack) left = into(bp, s);
    if (left <= 0) s.clear();
    return;
  }
  if (g != Grp::Main && g != Grp::Bp) { toBag(); return; }
  // from the bag
  if (o.mode == "container") {
    if (!ok || (contIsPack && isPack)) return;
    if (into(cont, s) <= 0) s.clear();
    return;
  }
  if (o.mode == "furnace" && ok) {
    auto one = [&](int k) { std::vector<ItemStack> tmp{furn[k]}; int left = into(tmp, s); furn[k] = tmp[0]; if (left <= 0) s.clear(); };
    if (reg().smelting.count(s.id) && (furn[0].empty() || stacksMerge(furn[0], s))) { one(0); return; }
    if (fuelValue(s.id) > 0 && (furn[1].empty() || stacksMerge(furn[1], s))) { one(1); return; }
  }
  if (o.mode == "inventory" || o.mode == "table") {
    if (d && d->has("armor")) { int slot = d->props["armor"].value("slot", -1); if (slot >= 0 && slot < (int)p.armor.size() && p.armor[slot].empty()) { p.armor[slot] = s; s.clear(); sound("equip"); return; } }
    if (isPack && back[0].empty() && g == Grp::Main) { back[0] = s; s.clear(); sound("equip"); return; }
    if (d && d->has("hook") && hook[0].empty() && o.mode == "inventory") { hook[0] = s; hook[0].n = 1; if (--s.n <= 0) s.clear(); sound("equip"); return; }
  }
  if (g == Grp::Bp) { int left = addToSlots(p.inv, s); if (left <= 0) s.clear(); return; }
  std::vector<int> order;
  if (i < 9) for (int k = 0; k < 27; k++) order.push_back(k + 9); else for (int k = 0; k < 9; k++) order.push_back(k);
  ItemStack moving = s; s.clear();
  int left = addToSlots(p.inv, moving, &order);
  if (left > 0) (*a)[i] = moving;
}

int InvSession::takeOutput(bool shift) {
  refresh();
  int made = 0; std::string rare;
  for (int k = 0; k < (shift ? 64 : 1); k++) {
    CraftMatch r = matchGrid(grid, W);
    if (!r) break;
    ItemStack out = r.result();
    // upgrading a backpack keeps what was inside it
    const ItemDef* od = out.def();
    for (auto& s : grid) if (!s.empty() && hasContents(s) && s.def() && s.def()->has("backpack") && od && od->has("backpack")) { if (!out.tag.is_object()) out.tag = json::object(); out.tag["c"] = s.tag["c"]; break; }
    if (!shift && !p.cursor.empty() && !(stacksMerge(p.cursor, out) && p.cursor.n + out.n <= maxStackOf(out.id))) break;
    if (onCraft && !onCraft(out)) break;
    const json* keep = r.recipe && r.recipe->props.contains("keep") ? &r.recipe->props["keep"] : nullptr;
    for (auto& s : grid) {
      if (s.empty()) continue;
      // buckets and bottles come back empty (milk for a cake, honey for a honey block)
      std::string back = keep && keep->contains(s.id) && (*keep)[s.id].is_string() ? (*keep)[s.id].get<std::string>() : "";
      if (--s.n <= 0) { if (!back.empty()) s = ItemStack(back, 1); else s.clear(); }
      else if (!back.empty()) giveBack(ItemStack(back, 1));
    }
    if (shift) { ItemStack o2 = out; int left = bagAdd(o2); if (left > 0 && onThrow) { ItemStack t = out; t.n = left; onThrow(t); } }
    else if (!p.cursor.empty()) p.cursor.n += out.n; else p.cursor = out;
    made += out.n;
    if (od && od->has("rarity")) rare = out.id;
    if (shift && made >= 64) break;
  }
  if (!made) return 0;
  sound("craft");
  if (!rare.empty() && onRareCraft) onRareCraft(rare);
  changed(Grp::Grid);
  return made;
}

// Creative: every item, free.
void InvSession::creativePick(const std::string& key, bool all) {
  ItemStack st(key, maxStackOf(key));
  if (all) bagAdd(st);
  else if (!p.cursor.empty() && p.cursor.id == key) p.cursor.n = maxStackOf(key);
  else p.cursor = st;
  sound("click");
  changed(Grp::Main);
}
void InvSession::trashCursor() { if (!p.cursor.empty()) { p.cursor.clear(); sound("click"); } }

std::string InvSession::takeOne(const std::string& key) {
  Bags b = bags();
  std::vector<std::string> ids;
  if (!key.empty() && key[0] == '#') { auto it = reg().tags.find(key); if (it != reg().tags.end()) ids = it->second; } else ids = {key};
  for (auto& id : ids) if (b.count(id) > 0) { b.remove(id, 1); return id; }
  return "";
}

void InvSession::bookPick(int idx, bool all, bool instant) {
  refresh();
  if (idx < 0 || idx >= (int)reg().recipes.size()) return;
  const Recipe& r = reg().recipes[idx];
  bool again = bookSel == idx;
  bookSel = idx;
  // put whatever is on the grid back first
  for (auto& s : grid) if (!s.empty()) { ItemStack t = s; s.clear(); giveBack(t); }
  int rw, rh; recipeDims(r, rw, rh);
  if (rw > W || rh > W) { ghost.clear(); changed(Grp::Grid); return; }
  auto layout = recipeLayout(r, W);
  Bags b = bags();
  if (!canCraft(b, r) || !(again || all || instant)) { ghost = layout; sound("click"); changed(Grp::Grid); return; }
  // how many sets: one, or as many as the ingredients allow (shift)
  int times = 1;
  if (all) {
    times = 64;
    for (auto& [k, n] : recipeIngredients(r)) times = std::min(times, b.countTag(k) / n);
    for (auto& k : layout) if (!k.empty()) times = std::min(times, maxStackOf(ingredientIcon(k)));
    times = std::max(1, times);
  }
  for (size_t i = 0; i < layout.size(); i++) {
    const std::string& k = layout[i]; if (k.empty()) continue;
    for (int t = 0; t < times; t++) {
      std::string id = takeOne(k); if (id.empty()) break;
      if (!grid[i].empty() && grid[i].id == id) grid[i].n++;
      else if (grid[i].empty()) grid[i] = ItemStack(id, 1);
      else giveBack(ItemStack(id, 1));
    }
  }
  ghost.clear();
  store(Grp::Bp);
  sound("click");
  changed(Grp::Grid);
}

void InvSession::dropFromSlot(Grp g, int i, bool all) {
  refresh();
  if (g == Grp::Out) return;
  auto* a = arr(g); if (!a || i < 0 || i >= (int)a->size() || (*a)[i].empty()) return;
  ItemStack& s = (*a)[i];
  ItemStack t = s; t.n = all ? s.n : 1;
  s.n -= t.n; if (s.n <= 0) s.clear();
  if (onThrow) onThrow(t);
  changed(g);
}
void InvSession::throwCursor(int n) {
  if (p.cursor.empty()) return;
  ItemStack t = p.cursor; t.n = std::min(n, p.cursor.n);
  p.cursor.n -= t.n; if (p.cursor.n <= 0) p.cursor.clear();
  if (onThrow) onThrow(t);
}

void InvSession::close() {
  refresh();
  // anything left on the crafting grid or the cursor goes back to the bag
  for (auto& s : grid) if (!s.empty()) { ItemStack t = s; s.clear(); giveBack(t); }
  if (!p.cursor.empty()) { ItemStack t = p.cursor; p.cursor.clear(); giveBack(t); }
  changed(Grp::Main);
  p.selected = clampv(p.selected, 0, 8);
}

} // namespace dq
