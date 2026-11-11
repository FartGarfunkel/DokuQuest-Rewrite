// ui/inventory.cpp — the classic inventory (ui/invui.js): 2×2 crafting with armour, gear, the doll and the
// character sheet; the 3×3 crafting table; furnace / smoker / blast furnace; chests, barrels, backpacks and
// the ender chest; the recipe book and the creative catalogue (tabs + search). Mouse: left take/place, right
// split/place one, drag to spread (left evenly, right one each), shift quick-moves, 1-9 over a slot swaps with
// the hotbar, Q over a slot drops (Ctrl: the stack), a click outside the panel throws the cursor stack.
// Controller: d-pad/stick moves a cursor between slots, A take/place, X split, Y quick move, B closes,
// LB/RB switch menus (the recipe book at a crafting table). The rules themselves are in sim/inv_ops.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "audio/audio.h"
#include "render/chest_layer.h"
#include "sim/machines.h"
#include "sim/trommel.h"
#include "ui/creative_catalogue.h"
#include "ui/book.h"
#include "sim/enchanting.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {
using namespace ui;

float drawSheet(ImDrawList* dl, ImVec2 p, float w, float is, const Player& pl, bool paper = false);
void drawDoll(ImDrawList* dl, ImVec2 a, ImVec2 b, const Player& pl, double t);                              // sheet.cpp: the 3D doll
void drawGearVis(ImDrawList* dl, ImVec2 a, ImVec2 b, const Player& p, int k, bool focus, bool hover);   // show/hide what you wear
void toggleGearVis(Player& p, int k);

namespace {
const std::pair<const char*, const char*> BOOK_CATS[] = {{"all", "All"}, {"blocks", "Blocks"}, {"redstone", "Redstone"}, {"tools", "Tools"}, {"armor", "Armor"}, {"food", "Food"}, {"eggs", "Spawn Eggs"}, {"forged", "Forged"}, {"misc", "Other"}};
struct BookState { bool open = false; std::string cat = "all", ccat = "all"; bool only = false; std::string query; std::string creativeWorld; };   // ccat: the creative tab
BookState& book() { static BookState b; return b; }

enum HK { H_SLOT, H_BOOKR, H_CI, H_CAT, H_BOOKBTN, H_BOOKCLOSE, H_TRASH, H_VIS, H_CUT };
struct Hit { HK k; Grp g = Grp::Main; int i = 0; std::string s; ImVec2 a, b; bool contains(ImVec2 p) const { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; } };
bool sameKey(const Hit& x, const Hit& y) { return x.k == y.k && x.g == y.g && x.i == y.i && x.s == y.s; }
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string creativeCat(const ItemDef& d) {
  if (d.has("spawnEgg")) return "eggs";
  if (d.has("forged")) return "forged";
  if (d.hasTool || endsWith(d.key, "_hoe") || d.has("shears")) return "tools";
  if (d.has("armor")) return "armor";
  if (isRedstoneItem(&d)) return "redstone";
  if (d.has("food")) return "food";
  if (!d.block.empty()) return "blocks";
  return "misc";
}
}

class InventoryScreen : public Screen {
public:
  explicit InventoryScreen(InvOpen op) : o(std::move(op)) {}
  InvOpen o;
  std::unique_ptr<InvSession> sess;
  std::vector<Hit> hits, prevHits;
  bool hasFocus = false, focusFromMouse = false; Hit focus; Hit focusRect;
  ImVec2 panelA, panelB, bookA, bookB; bool bookShown = false;
  float is = 40;
  std::string info;                 // .ip-info line for the controller
  bool creative = false;
  ImVec2 mouseDownAt;

  const char* id() const override { return "inventory"; }
  bool pausesGame() const override { return false; }
  Player* player(App& a) { return a.session ? a.session->player : nullptr; }

  void onOpen(App& a) override {
    if (o.mode == "container" && o.hasPos) chestScreenOpened(o.pos, true);   // the chest's lid lifts (render/chest_layer.cpp)
    Player* p = player(a);
    if (!p) { uiDefer([](App& app) { app.popScreen(); }); return; }
    creative = p->creative() && o.mode == "inventory";
    sess = std::make_unique<InvSession>(*p, &a.session->world(), o, creative);
    sess->onThrow = [&a](const ItemStack& s) { throwStackFromPlayer(a, s); };
    sess->onSound = [&a](const std::string& n) { if (a.audio) a.audio->play(n); };
    sess->onCraft = [&a, p](const ItemStack& s) { return a.events.emit("craft", json::array({p->name, s.id, s.n})); };
    sess->onRareCraft = [&a, p](const std::string& id) {
      json& flags = a.session->level->info.data["flags"];
      if (!flags.is_object()) flags = json::object();
      std::string f = "made_" + id;
      if (flags.value(f, false)) return;
      flags[f] = true;
      // grantXP(10): the original's curve, 12 + 6·level a level
      p->xp += 10; while (p->xp >= 12 + p->level * 6) { p->xp -= 12 + p->level * 6; p->level++; }
      const ItemDef* d = reg().item(id); a.toast("Crafted " + (d ? d->name : id) + "!", "rare");
    };
    // creative: the catalogue opens the first time per world
    if (creative && book().creativeWorld != a.session->level->folder.string()) { book().open = true; book().creativeWorld = a.session->level->folder.string(); }
    if (a.input.usingPad() || a.input.padCount()) { hasFocus = true; focus = Hit{H_SLOT, Grp::Main, p->selected}; }
  }
  void onClose(App& a) override {
    if (o.mode == "container" && o.hasPos) chestScreenOpened(o.pos, false);
    if (!sess) return;
    sess->close();
    if (o.mode == "container" && o.hasPos && a.audio) {
      const BlockDef& b = reg().block(a.session->world().getBlock(o.pos.x, o.pos.y, o.pos.z));
      if (b.has("container")) a.audio->playAt("chest_close", vec3(o.pos) + vec3(0.5f));
    }
    sess.reset();
  }
  bool onBack(App& a) override {
    if (backConsumedByText()) return true;
    // (0.9.0.74) Esc shuts the recipe/creative book first, then the inventory
    if (a.input.pressed(Act::Pause) && book().open && canBook()) { toggleBook(a); return true; }
    return false;
  }
  bool canBook() const { return o.mode == "inventory" || o.mode == "table"; }
  void toggleBook(App& a) {
    book().open = !book().open;
    if (!book().open) sess->ghost.clear();
    playClick();
    // controller focus follows: into the book when it opens, back to its button when it closes
    if (hasFocus || a.input.padCount()) {
      if (book().open) { hasFocus = true; focus = Hit{creative ? H_CI : H_BOOKR}; focus.i = -1; pendingBookFocus = true; }
      else { hasFocus = true; focus = Hit{H_BOOKBTN}; }
    }
  }
  bool pendingBookFocus = false, padActed = false;

  // ---- per frame: controller, keys, closing ---------------------------------------------------------
  void update(App& a, double) override {
    if (!sess) return;
    Player* p = player(a); if (!p) return;
    sess->refresh();
    if (!sess->valid()) { a.popScreen(); return; }
    Input& in = a.input;
    if (p->dead) return;
    if (in.pressed(Act::Inventory) && !in.padButtonPressed(3) && !in.textActive) { a.popScreen(); return; }
    if (o.mode == "inventory" && !creative && menuTabsUpdate(a, "inventory")) return;
    if (o.mode == "inventory" && creative && menuTabsUpdate(a, "inventory")) return;
    if (o.mode == "table" && (in.padButtonPressed(4) || in.padButtonPressed(5))) toggleBook(a);   // (Q drops here, E closes)
    // spatial navigation between everything focusable
    ivec2 nav = in.textActive ? ivec2(0) : in.navStep();
    if (nav != ivec2(0) && in.usingPad()) { if (!padActed && focusFromMouse) { hasFocus = false; focusFromMouse = false; } padActed = true; }
    if (nav != ivec2(0) && in.usingPad()) {
      if (!hasFocus || !findHit(focus)) { hasFocus = true; focus = Hit{H_SLOT, Grp::Main, 9}; }
      else moveFocus(nav.x, nav.y);
    } else if (nav != ivec2(0) && !in.usingPad() && hasFocus) moveFocus(nav.x, nav.y);
    bool A = in.padButtonPressed(0), X = in.padButtonPressed(2), Y = in.padButtonPressed(3);
    // on a pad the selected hotbar slot has the focus from the start, so A / X / Y work on it at once
    // (and taking up the pad after the mouse starts there too, not wherever the pointer was left)
    if ((in.usingPad() || A || X || Y) && (!hasFocus || (focusFromMouse && !padActed)) && p->selected >= 0) { hasFocus = true; focus = Hit{H_SLOT, Grp::Main, p->selected}; focusFromMouse = false; }
    const Hit* f = hasFocus ? findHit(focus) : nullptr;
    if (A || X) padActed = true;
    // the pad's inventory button toggles the plain inventory: Y right after opening it (nothing moved, nothing on the
    // cursor) shuts it again; once you have moved around, or in a container, Y is the quick-move button it is in the original
    if (Y && o.mode == "inventory" && !in.textActive && !padActed && p->cursor.empty()) { a.popScreen(); return; }   // (a chest, furnace... opened with Use: Y is quick move at once)
    if (f && (A || X || Y)) activate(a, *f, X, Y, true);
    // sorting (BTA): the right stick's click over a bag, chest or backpack slot; the middle mouse button over one
    if (f && f->k == H_SLOT && in.padButtonPressed(11)) sess->sortGroup(f->g);
    if (!in.textActive && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) if (const Hit* h = hovered()) if (h->k == H_SLOT && !creative) sess->sortGroup(h->g);
    // keyboard over the hovered slot: 1-9 swaps with the hotbar, Q drops one (Ctrl: the stack)
    if (!in.textActive) {
      const Hit* h = hovered();
      if (h && h->k == H_SLOT) {
        for (int k = 0; k < 9; k++) if (in.keyPressed((SDL_Scancode)(SDL_SCANCODE_1 + k))) sess->hotbarSwap(h->g, h->i, k);
        // Q drops the hovered stack (Ctrl: all of it) -- except in the tabbed inventory, where Q is "previous tab" (Delete drops there)
        bool qIsTab = o.mode == "inventory" && in.sharesKey(Act::Drop, Act::MenuTabLeft);
        if ((in.keyPressed(SDL_SCANCODE_DELETE) || (!qIsTab && in.keyPressed(in.keyFor(Act::Drop)))) && h->g != Grp::Out) sess->dropFromSlot(h->g, h->i, in.ctrlHeld());
      }
    }
  }
  const Hit* findHit(const Hit& k) const { for (auto& h : prevHits) if (sameKey(h, k)) return &h; return nullptr; }
  const Hit* hovered() const { ImVec2 m = ImGui::GetIO().MousePos; for (auto& h : prevHits) if (h.contains(m)) return &h; return nullptr; }
  void moveFocus(int dx, int dy) {
    const Hit* cur = findHit(focus); if (!cur) return;
    ImVec2 c((cur->a.x + cur->b.x) * 0.5f, (cur->a.y + cur->b.y) * 0.5f);
    const Hit* best = nullptr; float bd = 1e30f;
    for (auto& h : prevHits) {
      if (sameKey(h, *cur)) continue;
      float x = (h.a.x + h.b.x) * 0.5f - c.x, y = (h.a.y + h.b.y) * 0.5f - c.y;
      float along = x * dx + y * dy;
      if (along <= 2) continue;
      float across = std::fabs(dx ? y : x), d = along + across * 2.2f;
      if (d < bd) { bd = d; best = &h; }
    }
    if (best) { focus = *best; if (app().audio) app().audio->play("tick"); if (best->k == H_BOOKR) sess->bookSel = best->i; }
  }
  void activate(App& a, const Hit& h, bool right, bool shift, bool pad) {
    switch (h.k) {
      case H_SLOT: if (h.g == Grp::SOut) takeStation(a, shift); else sess->slotClick(h.g, h.i, right, shift); break;
      case H_BOOKR: sess->bookPick(h.i, shift, pad); break;
      case H_CI: sess->creativePick(h.s, shift); break;
      case H_CAT: (creative ? book().ccat : book().cat) = h.s; bookScroll = 0; playClick(); break;
      case H_BOOKBTN: case H_BOOKCLOSE: toggleBook(a); break;
      case H_TRASH: sess->trashCursor(); break;
      case H_VIS: toggleGearVis(sess->p, h.i); break;
      case H_CUT: sess->cutPick(h.i, shift); break;
    }
  }
  // the work station's result: levels for enchantments, experience back from the grindstone (enchanting.js)
  void takeStation(App& a, bool shift) {
    Player& pl = sess->p;
    StationResult r = sess->staRes;
    if (!r.ok) return;
    if (r.lv && !pl.creative() && pl.level < r.lv) { a.toast("You need " + std::to_string(r.lv) + " level" + (r.lv > 1 ? "s" : "") + " for that", "warn"); if (a.audio) a.audio->play("click_off"); return; }
    bool broke = false;
    if (!sess->takeStation(shift, &broke)) return;
    if (r.lv && !pl.creative()) { spendLevels(pl, r.lv); if (a.audio) a.audio->play("enchant"); }
    if (r.xp) { vec3 at = pl.pos + vec3(0, 1.6f, 0); grantXP(*a.session->level, pl, r.xp, &at); }
    if (broke) { a.toast("The anvil broke", "warn"); uiDefer([](App& app) { if (app.topScreen() && app.topScreen()->id() == std::string("inventory")) app.popScreen(); }); }
  }

  // ---- layout + drawing -------------------------------------------------------------------------------
  void addHit(HK k, Grp g, int i, const std::string& s, ImVec2 a, ImVec2 b) { hits.push_back(Hit{k, g, i, s, a, b}); }
  bool isFocus(HK k, Grp g, int i, const std::string& s = "") const { return hasFocus && focus.k == k && focus.g == g && focus.i == i && focus.s == s && app().input.usingPad(); }
  bool isHover(ImVec2 a, ImVec2 b) const { ImVec2 m = ImGui::GetIO().MousePos; return !app().input.usingPad() && m.x >= a.x && m.y >= a.y && m.x < b.x && m.y < b.y; }

  void slot(ImDrawList* dl, Grp g, int i, ImVec2 p, float s, const char* ph = nullptr, bool out = false) {
    const ItemStack* st = sess->at(g, i);
    SlotLook lk;
    lk.sel = g == Grp::Main && i == sess->p.selected;
    lk.focus = isFocus(H_SLOT, g, i);
    lk.hover = isHover(p, p + ImVec2(s, s));
    lk.out = out; lk.ready = out && st && !st->empty();
    lk.full = st && !st->empty() && st->tag.is_object() && st->tag.contains("c") && st->tag["c"].is_array() && std::any_of(st->tag["c"].begin(), st->tag["c"].end(), [](const json& x) { return !x.is_null(); });
    drawSlot(dl, p, s, lk);
    if (st && !st->empty()) drawItem(dl, *st, p, s);
    else if (g == Grp::Grid && i < (int)sess->ghost.size() && !sess->ghost[i].empty()) { float in = s * 0.12f; drawIcon(dl, ingredientIcon(sess->ghost[i]), p + ImVec2(in, in), s - in * 2, 0.35f); }
    else if (ph) { float in = s * 0.22f; drawIcon(dl, ph, p + ImVec2(in, in), s - in * 2, 0.22f); }
    addHit(H_SLOT, g, i, "", p, p + ImVec2(s, s));
  }
  void grid9(ImDrawList* dl, Grp g, int from, int to, ImVec2 p) {
    for (int i = from; i < to; i++) { int k = i - from; slot(dl, g, i, ImVec2(p.x + (k % 9) * is, p.y + (k / 9) * is), is); }
  }
  float lbl(ImDrawList* dl, ImVec2 p, const char* t, ImU32 c, float k = 0.36f, FontId f = F_BODY) {
    float sc = is * k / Z() / fontPx(f);
    text(dl, f, p + ImVec2(1, 1) * Z(), col::black, t, 0, sc); text(dl, f, p, c, t, 0, sc);
    return textSize(f, t, 0, sc).y;
  }
  void arrow(ImDrawList* dl, ImVec2 p, float fill = -1) {
    // .arrow: a bar and a head in #5c4c3c (the furnace's fills gold as it cooks)
    float w = is * 0.9f, h = is * 0.6f;
    dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.35f), ImVec2(p.x + w * 0.7f, p.y + h * 0.65f), col::rgb(0x5c4c3c));
    dl->AddTriangleFilled(ImVec2(p.x + w - is * 0.32f, p.y), ImVec2(p.x + w, p.y + h * 0.5f), ImVec2(p.x + w - is * 0.32f, p.y + h), col::rgb(0x5c4c3c));
    if (fill > 0) dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.35f), ImVec2(p.x + std::min(w * 0.7f, w * fill), p.y + h * 0.65f), col::gold);
  }
  void bookButton(ImDrawList* dl, ImVec2 p) {
    ImVec2 b = p + ImVec2(is * 0.9f, is * 0.75f);
    float w = is * 0.9f;
    dl->AddRectFilled(p, ImVec2(p.x + w * 0.18f, b.y), col::rgb(0x4a2d16));
    dl->AddRectFilled(ImVec2(p.x + w * 0.18f, p.y), ImVec2(p.x + w * 0.82f, b.y), col::rgb(0x7a4a24));
    dl->AddRectFilled(ImVec2(p.x + w * 0.82f, p.y), b, col::rgb(0xd9cfae));
    float bx = p.x + w * 0.3f, by = p.y + is * 0.75f * 0.3f;
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + w * 0.35f, by + is * 0.75f * 0.14f), col::gold);
    dl->AddRectFilled(ImVec2(bx, by + is * 0.12f), ImVec2(bx + w * 0.35f, by + is * 0.12f + is * 0.75f * 0.14f), col::gold);
    bevel(dl, p, b, col::pnHi, col::pnLo, 2 * Z());
    if (book().open) dl->AddRect(p - ImVec2(2, 2) * Z(), b + ImVec2(2, 2) * Z(), col::gold, 0, 0, 2 * Z());
    if (isFocus(H_BOOKBTN, Grp::Main, 0)) { dl->AddRect(p - ImVec2(4, 4) * Z(), b + ImVec2(4, 4) * Z(), col::black, 0, 0, 2 * Z()); dl->AddRect(p - ImVec2(3, 3) * Z(), b + ImVec2(3, 3) * Z(), col::gold, 0, 0, 3 * Z()); }
    addHit(H_BOOKBTN, Grp::Main, 0, "", p, b);
  }
  void panelBg(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    // .ipanel: the stone texture multiplied with #6b5a4a under a dark wash, bevelled walnut edges, black rim
    float z = Z();
    for (int i = 4; i >= 1; i--) dl->AddRectFilled(a - ImVec2(0, -i * 3.f) * z - ImVec2(i * 3.f, 0) * z, b + ImVec2(i * 3.f, i * 6.f) * z, IM_COL32(0, 0, 0, 22), 6 * z);
    dl->AddRectFilled(a - ImVec2(2, 2) * z, b + ImVec2(2, 2) * z, col::black);
    dl->AddRectFilled(a, b, col::pn);
    drawTiled(dl, "stone", a, b, is * 1.6f, col::rgb(0x6b5a4a));
    dl->AddRectFilledMultiColor(a, b, IM_COL32(20, 15, 11, 140), IM_COL32(20, 15, 11, 140), IM_COL32(20, 15, 11, 179), IM_COL32(20, 15, 11, 179));
    bevel(dl, a, b, col::pnHi, col::pnLo, 3 * z);
  }

  void draw(App& a) override {
    if (!sess || a.topScreen() != this) return;
    if (a.settings.extra.value("bookInventory", true)) { drawBookInv(a); return; }   // (every screen: the inventory, chests, furnaces, stations...)
    Player& p = sess->p;
    float z = Z(), W = (float)a.width, H = (float)a.height, vw = W / z, vh = H / z;
    BookState& B = book();
    bool showBook = B.open && canBook();
    // layout(): fit the panel into the screen
    int bpRows = (int)((sess->groupSize(Grp::Bp) + 8) / 9);
    int contN = o.mode == "container" ? sess->groupSize(Grp::Cont) : 0;
    const bool trommel = o.mode == "furnace" && sess->furnaceKind == "trommel";
    float topRows = o.mode == "inventory" ? 4.8f : o.mode == "table" ? 4.6f : trommel ? 4.2f : o.mode == "furnace" ? 3.2f : o.mode == "machine" || o.mode == "station" ? 2.6f : o.mode == "stonecutter" ? 3.4f : std::ceil((contN ? contN : 27) / 9.f);
    float rows = topRows + 6.1f + (bpRows ? bpRows + 0.65f : 0) + (o.mode != "inventory" ? 0.55f : 0);
    bool wide = vw > vh * 1.25f && vw > 640;
    float bookW = showBook ? (wide ? 6.4f : 0) : 0;
    is = std::floor(clampv(std::min((vw - 28) / (9.9f + bookW), (vh - 28) / rows), 16.f, 44.f)) * z;
    // the backdrop (.inv)
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 3, 2, 153));
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##inventory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    hits.clear();
    float pad = is * 0.22f, bord = 3 * z;
    // ---- measure the panel ----
    float gearW = is, sheetW = is * 2.3f, dollW = is * 2, craftW = is * 2 + is * 0.9f + is * 1.3f + is * 0.4f;
    float contentW = 9 * is;
    if (o.mode == "inventory") contentW = std::max(contentW, is + is * 0.18f + dollW + is * 0.18f + gearW + is * 0.18f + sheetW + is * 0.3f + craftW);
    bool trash = creative;
    if (trash) contentW = std::max(contentW, 9 * is + is * 0.15f + is);
    std::string title = sess->title;
    if (o.mode == "inventory") title.clear();
    float titleH = title.empty() ? 0 : textSize(F_H3, title.c_str(), 0, is * 0.5f / z / fontPx(F_H3)).y + 2 * z;
    float labelH = textSize(F_BODY, "Ag", 0, is * 0.36f / z / fontPx(F_BODY)).y;
    float topH = 0;
    if (o.mode == "inventory") topH = std::max({4 * is, is * 3.4f + 8 * z + is * 0.4f, 5 * is * 1.2f + is * 0.1f, labelH + 2 * is + is * 0.15f + is * 0.75f});
    else if (o.mode == "table") topH = is * 0.4f + 3 * is + is * 0.15f + is * 0.75f;
    else if (trommel) topH = is * 0.3f + 3 * is + statusH();
    else if (o.mode == "furnace") topH = is * 0.3f + std::max(is * 2 + is * 0.6f + 4 * z, is * 1.3f);
    else if (o.mode == "machine" || o.mode == "station") topH = is * 0.3f + is * 1.3f + statusH();
    else if (o.mode == "stonecutter") topH = is * 0.24f + is * 2.6f + 8 * z;
    else topH = o.gearGet ? mountH(labelH) : ((contN + 8) / 9) * is;
    float infoH = std::max(is * 0.45f, textSize(F_BODY, "Ag", 0, is * 0.3f / z / fontPx(F_BODY)).y * 2.6f);
    float innerH = titleH + topH + 2 * z + (bpRows ? labelH + is * 0.15f + bpRows * is + 2 * z : 0) + is * 0.15f + labelH + 2 * z + 3 * is + is * 0.15f + is + 3 * z + infoH;
    float panelW = contentW + pad * 2 + bord * 2, panelH = innerH + pad * 2 + bord * 2;
    float bookPx = is * 6.2f;
    float totalW = panelW + (showBook && wide ? bookPx + 8 * z : 0);
    ImVec2 origin(std::round((W - totalW) * 0.5f), std::round((H - panelH) * 0.5f));
    panelA = origin + ImVec2(showBook && wide ? bookPx + 8 * z : 0, 0); panelB = panelA + ImVec2(panelW, panelH);
    panelBg(dl, panelA, panelB);
    ImVec2 c = panelA + ImVec2(pad + bord, pad + bord);
    float y = c.y;
    if (!title.empty()) { float sc = is * 0.5f / z / fontPx(F_H3); text(dl, F_H3, ImVec2(c.x, y) + ImVec2(1, 1) * z, col::black, title.c_str(), 0, sc); text(dl, F_H3, ImVec2(c.x, y), col::gold, title.c_str(), 0, sc); y += titleH; }
    // ---- the top section ----
    ImVec2 top(c.x, y);
    if (o.mode == "inventory") drawInvTop(a, dl, top, contentW, topH, labelH);
    else if (o.mode == "table") {
      float gw = 3 * is + is * 0.2f + is * 0.9f + is * 0.2f + is * 1.3f, gx = c.x + (contentW - gw) * 0.5f, gy = y + is * 0.2f;
      craft(dl, ImVec2(gx, gy), 3);
    } else if (trommel) drawTrommel(a, dl, ImVec2(c.x, y), contentW);
    else if (o.mode == "furnace") drawFurnace(a, dl, ImVec2(c.x, y), contentW);
    else if (o.mode == "machine") drawMachine(a, dl, ImVec2(c.x, y), contentW);
    else if (o.mode == "station") drawStation(a, dl, ImVec2(c.x, y), contentW);
    else if (o.mode == "stonecutter") drawCutter(a, dl, ImVec2(c.x, y), contentW);
    else if (o.gearGet) drawMount(dl, ImVec2(c.x, y), false);
    else grid9(dl, Grp::Cont, 0, contN, ImVec2(c.x, y));
    y += topH + 2 * z;
    if (bpRows) {
      y += is * 0.15f; lbl(dl, ImVec2(c.x, y), "Backpack", col::dim); y += labelH;
      grid9(dl, Grp::Bp, 0, sess->groupSize(Grp::Bp), ImVec2(c.x, y)); y += bpRows * is + 2 * z;
    }
    y += is * 0.15f; lbl(dl, ImVec2(c.x, y), "Inventory", col::dim); y += labelH + 2 * z;
    grid9(dl, Grp::Main, 9, 36, ImVec2(c.x, y)); y += 3 * is + is * 0.15f;
    grid9(dl, Grp::Main, 0, 9, ImVec2(c.x, y));
    if (trash) {
      ImVec2 t0(c.x + 9 * is + is * 0.15f, y), t1 = t0 + ImVec2(is, is);
      bool hot = isHover(t0, t1) || isFocus(H_TRASH, Grp::Main, 0);
      dl->AddRectFilled(t0, t1, hot ? col::rgb(0x8a3333) : col::rgb(0x5a2626)); dl->AddRect(t0, t1, col::rgb(0x2a0f0f), 0, 0, 2 * z);
      // a little bin
      float u = is / 10; ImVec2 q = t0 + ImVec2(3 * u, 3 * u);
      dl->AddRectFilled(q + ImVec2(-0.6f * u, 0), q + ImVec2(4.6f * u, 0.8f * u), col::white); dl->AddRectFilled(q + ImVec2(1.4f * u, -0.8f * u), q + ImVec2(2.6f * u, 0), col::white);
      dl->AddRectFilled(q + ImVec2(0, 1.2f * u), q + ImVec2(4 * u, 5 * u), col::white);
      if (isFocus(H_TRASH, Grp::Main, 0)) dl->AddRect(t0 - ImVec2(1, 1) * z, t1 + ImVec2(1, 1) * z, col::gold, 0, 0, 3 * z);
      addHit(H_TRASH, Grp::Main, 0, "", t0, t1);
    }
    y += is + 3 * z;
    // ---- .ip-info ----
    drawInfo(a, dl, ImVec2(c.x, y), contentW);
    // ---- the book ----
    bookShown = showBook;
    if (showBook) {
      if (wide) { bookA = origin; bookB = bookA + ImVec2(bookPx, std::min(panelH, is * 11)); }
      else { float bw = std::min(is * 9.4f, W * 0.96f), bh = std::min(is * 11, H * 0.94f); bookA = ImVec2((W - bw) * 0.5f, (H - bh) * 0.5f); bookB = bookA + ImVec2(bw, bh); }
      drawBook(a, dl);
    }
    finish(a);
  }
  // the end of a frame (both layouts): the mouse, the stack on the cursor, the tooltip, the menu tabs
  void finish(App& a) {
    Player& p = sess->p;
    // ---- input from the mouse ----
    mouse(a);
    prevHits = hits;
    if (pendingBookFocus) {
      pendingBookFocus = false;
      for (auto& h : hits) if (h.k == H_BOOKR || h.k == H_CI) { focus = h; if (h.k == H_BOOKR) sess->bookSel = h.i; break; }
    }
    // the stack on the cursor follows the mouse, or sits by the focused slot
    if (!p.cursor.empty()) {
      ImVec2 at = ImGui::GetIO().MousePos;
      if (a.input.usingPad()) if (const Hit* f = hasFocus ? findHit(focus) : nullptr) at = ImVec2(f->a.x + (f->b.x - f->a.x) * 0.75f, f->a.y + (f->b.y - f->a.y) * 0.75f);
      drawItem(ImGui::GetForegroundDrawList(), p.cursor, at - ImVec2(is, is) * 0.5f, is);
    } else if (!a.input.usingPad()) tooltip(a);
    if (o.mode == "inventory") menuTabsDraw(a, "inventory");
    ImGui::End();
    ImGui::PopStyleVar();
  }

  // ---- the book inventory (Options > User Interface > Book Inventory) ----------------------------------------
  // The open book (ui/book.h): the left page is you — armour, the doll and what's shown of your gear, the gear
  // slots, then your skills and stats; with the recipe book open, the recipe book. The right page is crafting and
  // your bag; in Creative it's the item catalogue over your bag, its tabs the bookmarks down both edges (the first,
  // a chest: back to crafting).
  // a mount's gear screen (sim/mount_gear.h): its saddle and armour slots (named under them), then what it carries, or
  // what would let it carry things. ink: on the book's parchment. Returns the height drawn (mountH: the same, unseen)
  float mountH(float labelH) {
    const int n = sess->groupSize(Grp::Cont);
    return is + is * 0.5f + (n ? labelH + 2 * Z() + ((n + 8) / 9) * is : labelH);
  }
  float drawMount(ImDrawList* dl, ImVec2 p, bool onInk) {
    const float z = Z();
    const int ng = sess->groupSize(Grp::Mount), n = sess->groupSize(Grp::Cont);
    static const char* NAMES[] = {"Saddle", "Armour"};
    const float sc = is * 0.22f / z / fontPx(F_BODY);
    for (int k = 0; k < ng; k++) {
      ImVec2 q(p.x + k * is * 1.3f, p.y);
      slot(dl, Grp::Mount, k, q, is, k < (int)o.gearHint.size() ? o.gearHint[k].c_str() : nullptr);
      const char* nm = k < 2 ? NAMES[k] : "";
      const ImVec2 tz = textSize(F_BODY, nm, 0, sc);
      text(dl, F_BODY, ImVec2(q.x + (is - tz.x) * 0.5f, q.y + is + 1 * z), onInk ? ink::muted : col::dim, nm, 0, sc);
    }
    float y = p.y + is + is * 0.5f;
    auto label = [&](const char* t) { return onInk ? inkLbl(dl, ImVec2(p.x, y), t) : lbl(dl, ImVec2(p.x, y), t, col::dim); };
    if (n) { y += label(n >= 27 ? "Chest" : "Saddle Bags") + 2 * z; grid9(dl, Grp::Cont, 0, n, ImVec2(p.x, y)); y += ((n + 8) / 9) * is; }
    else y += label("Saddle bags or a chest carry things");
    return y - p.y;
  }
  float inkLbl(ImDrawList* dl, ImVec2 p, const char* t, ImU32 c = 0) {
    float sc = is * 0.32f / Z() / fontPx(F_BODY);
    text(dl, F_BODY, p, c ? c : ink::muted, t, 0, sc);
    return textSize(F_BODY, t, 0, sc).y;
  }
  void drawBookInv(App& a) {
    BookState& B = book();
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    setBookTheme(a.settings.extra.value("bookDark", false));
    float k = std::min({(H - 110 * z) / BOOK_H, (W - 170 * z) / BOOK_W, 4.6f * z});
    k = std::max(1.f, std::floor(k * 4) / 4);
    is = std::floor(BOOK_TW * k / 9.1f);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 3, 2, 153));
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##inventory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    hits.clear();
    const ImVec2 org(std::round((W - BOOK_W * k) * 0.5f), std::round(std::max(44 * z, (H - BOOK_H * k) * 0.5f - 4 * z)));
    panelA = org; panelB = org + ImVec2(BOOK_W * k, BOOK_H * k);
    dropShadow(dl, panelA, panelB, 6 * z, 22 * z, 0.55f);
    // creative: the bookmarks (tabs) — "your inventory", then the catalogue's; half down each edge of the book
    struct Mark { std::string key, name, icon; };
    std::vector<Mark> marks;
    if (creative) { marks.push_back({"inv", "Your inventory & crafting", "chest"}); for (auto& tb : creativeTabs()) marks.push_back({tb.key, tb.name, tb.icon}); }
    const int nl = ((int)marks.size() + 1) / 2, nr = (int)marks.size() - nl;
    const float ts = marks.empty() ? 0 : std::min(k * 0.62f, (BOOK_H - 14) * k / std::max(nl, nr) / 30.f);
    auto markAt = [&](int i, bool& left, bool& first, bool& last) {
      left = i < nl; int j = left ? i : i - nl, n = left ? nl : nr; first = j == 0; last = j == n - 1;
      float x = left ? org.x - 32 * ts + 9 * ts : org.x + BOOK_W * k - 9 * ts;
      return ImVec2(x, org.y + 7 * k + j * 30 * ts);
    };
    std::string tip;
    auto drawMarks = [&](bool selected) {
      for (int i = 0; i < (int)marks.size(); i++) {
        const bool sel = B.ccat == marks[i].key || (marks[i].key == "inv" && B.ccat == "inv");
        if (sel != selected) continue;
        bool left, first, last; ImVec2 p0 = markAt(i, left, first, last);
        if (sel) p0.x += left ? -3 * ts : 3 * ts;
        ImVec2 p1 = p0 + ImVec2(32 * ts, 28 * ts);
        const bool hot = isHover(p0, p1) || isFocus(H_CAT, Grp::Main, 0, marks[i].key);
        drawBookTab(dl, p0, ts, i, sel, hot, marks[i].icon, left);
        if (isFocus(H_CAT, Grp::Main, 0, marks[i].key)) { ImVec2 f0 = left ? p0 - ImVec2(2 * ts, 0) : ImVec2(p0.x + 9 * ts, p0.y), f1 = left ? ImVec2(p1.x - 9 * ts, p1.y) : p1 + ImVec2(2 * ts, 0);
          ImGui::GetForegroundDrawList()->AddRect(f0 + ImVec2(0, 3 * ts), f1 - ImVec2(0, 3 * ts), col::gold, 3 * z, 0, 2 * z); }
        if (hot) tip = marks[i].name;
        // (the part past the page takes the click)
        ImVec2 h0 = left ? p0 : ImVec2(p0.x + 9 * ts, p0.y), h1 = left ? ImVec2(p1.x - 9 * ts, p1.y) : p1;
        addHit(H_CAT, Grp::Main, 0, marks[i].key, h0, h1);
      }
    };
    drawMarks(false); drawMarks(true);   // (all under the pages: the ribbons come out from between them)
    drawBookSpread(dl, org, k);
    const ImVec2 L = bookTextOrigin(org, k, 0), R = bookTextOrigin(org, k, 1);
    bookTextTop = L.y;
    const float pw = BOOK_TW * k, ph = BOOK_TH * k;
    bookShown = false;
    if (o.mode == "inventory") {
      bookPageLeft(a, dl, L, pw, ph);
      if (creative && B.ccat != "inv") bookPageCreative(a, dl, R, pw, ph);
      else bookPageCraft(a, dl, R, pw, ph);
    } else {   // a chest, a furnace, a station...: the block on the left page, you on the right
      bookPageBlock(a, dl, L, pw, ph);
      bookPageBag(a, dl, R, pw, ph);
    }
    if (!tip.empty()) { ImVec2 m = ImGui::GetIO().MousePos, s = textSize(F_MID, tip.c_str()); ImDrawList* fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilled(ImVec2(m.x + 14 * z, m.y - 4 * z), ImVec2(m.x + 26 * z + s.x, m.y + 4 * z + s.y), col::rgb(0x1a120c, 235), 3 * z); text(fg, F_MID, ImVec2(m.x + 20 * z, m.y), col::rgb(0xf0e0b8), tip.c_str()); }
    // the sun / moon under the book's corner
    { const float s = std::round(18 * z);
      if (bookThemeButton(a, dl, ImVec2(panelB.x - s, panelB.y + 6 * z), s)) { a.settings.extra["bookDark"] = !bookThemeDark(); a.settings.save(); playClick(); } }
    finish(a);
  }
  void bookPageLeft(App& a, ImDrawList* dl, ImVec2 p, float w, float h) {
    Player& pl = sess->p; const float z = Z();
    if (book().open && !creative) {   // the recipe book takes the left page
      onPage = true; bookA = p; bookB = p + ImVec2(w, h); bookShown = true; drawBook(a, dl); onPage = false;
      return;
    }
    bookShown = false;
    // a heading: your name and level
    { float sc = is * 0.5f / z / fontPx(F_H3);
      text(dl, F_H3, p, ink::head, pl.name.c_str(), 0, sc);
      std::string lv = "Level " + std::to_string(pl.level) + " \xC2\xB7 " + strFormat("%g", pl.hp / 2) + " / " + strFormat("%g", pl.maxHp / 2) + " hearts";
      float s2 = is * 0.28f / z / fontPx(F_BODY); ImVec2 ls = textSize(F_BODY, lv.c_str(), 0, s2);
      text(dl, F_BODY, ImVec2(p.x + w - ls.x, p.y + is * 0.12f), ink::muted, lv.c_str(), 0, s2);
      float hy = p.y + textSize(F_H3, "Ag", 0, sc).y + 2 * z;
      dl->AddRectFilled(ImVec2(p.x, hy), ImVec2(p.x + w, hy + std::max(1.f, z)), ink::faint);
      p.y = hy + is * 0.15f; }
    // armour, the doll, what's shown, the gear
    static const char* PH[] = {"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"};
    for (int i = 0; i < 4; i++) slot(dl, Grp::Armor, i, ImVec2(p.x, p.y + i * is), is, PH[i]);
    const ImVec2 d0(p.x + is * 1.15f, p.y), d1 = d0 + ImVec2(is * 2.5f, is * 3.4f);
    dl->AddRectFilled(d0, d1, IM_COL32(20, 14, 10, bookThemeDark() ? 90 : 40), 4 * z);
    drawDoll(dl, d0, d1, pl, a.time);
    {   // show / hide: helmet, chestplate, leggings, boots, backpack, cape (they still protect you)
      const float bw = (is * 3.65f) / 7, bh = is * 0.52f;
      for (int k2 = 0; k2 < 7; k2++) {
        ImVec2 b0(p.x + k2 * bw, p.y + 4 * is + is * 0.1f), b1 = b0 + ImVec2(bw - 2 * z, bh);
        drawGearVis(dl, b0, b1, pl, k2, isFocus(H_VIS, Grp::Main, k2), isHover(b0, b1));
        addHit(H_VIS, Grp::Main, k2, "", b0, b1);
      }
    }
    struct G { Grp g; const char* ph; const char* cap; };
    const G gear[] = {{Grp::Off, nullptr, "Off hand"}, {Grp::Hook, "lantern", "Hook"}, {Grp::Back, "leather_backpack", "Pack"}, {Grp::Charm, "vigor_charm", "Charm"}, {Grp::Cape, "red_cape", "Cape"}, {Grp::Quiver, "quiver", "Quiver"}};
    const float gx = d1.x + is * 0.3f, cs = is * 0.22f / z / fontPx(F_BODY);
    for (int i = 0; i < 6; i++) {
      const float x = gx + (i % 2) * is * 1.25f, y = p.y + (i / 2) * is * 1.42f;
      slot(dl, gear[i].g, 0, ImVec2(x, y), is, gear[i].ph);
      ImVec2 cz = textSize(F_BODY, gear[i].cap, 0, cs);
      text(dl, F_BODY, ImVec2(x + (is - cz.x) * 0.5f, y + is + 1 * z), ink::muted, gear[i].cap, 0, cs);
    }
    // your skills and stats
    float y = p.y + 4 * is + is * 0.75f;
    y += inkLbl(dl, ImVec2(p.x, y), "Skills & stats", ink::head) + 2 * z;
    drawSheet(dl, ImVec2(p.x, y), w, is * 1.25f, pl, true);
  }
  void bookPageCraft(App& a, ImDrawList* dl, ImVec2 p, float w, float h) {
    (void)a; (void)h;
    const float z = Z();
    float y = p.y;
    const float lblH = inkLbl(dl, ImVec2(p.x, y), "Crafting", ink::head);
    y += lblH + 3 * z;
    const float craftW = 2 * is + is * 0.2f + is * 0.9f + is * 0.2f + is * 1.3f;
    const int bpRows = (sess->groupSize(Grp::Bp) + 8) / 9, qn = sess->groupSize(Grp::Qv);
    // the quiver's four slots stand 2x2 left of the crafting grid (they add no height); and if the pack is so big the
    // page would still run over, the recipe book's button moves up beside the result
    float cx = p.x + (w - craftW) * 0.5f;
    if (qn) {
      cx = std::max(cx, p.x + 2 * is + is * 0.45f);
      for (int k = 0; k < qn; k++) slot(dl, Grp::Qv, k, ImVec2(p.x + (k % 2) * is, y + (k / 2) * is), is);
      const float sc = is * 0.22f / z / fontPx(F_BODY); const ImVec2 cz = textSize(F_BODY, "Quiver", 0, sc);
      text(dl, F_BODY, ImVec2(p.x + (2 * is - cz.x) * 0.5f, y + 2 * is + 1 * z), ink::muted, "Quiver", 0, sc);
    }
    const float below = (lblH + 2 * z) * (bpRows ? 2 : 1) + bpRows * is + is * 0.2f + 4 * is + is * 0.15f;
    const bool tight = y + 2 * is + is * 1.15f + below > p.y + h && cx + craftW + is * 1.05f <= p.x + w;
    craft(dl, ImVec2(cx, y), 2, tight ? ImVec2(cx + craftW + is * 0.15f, y + is * 0.15f) : ImVec2(-1, -1));
    y += tight ? 2 * is + is * (qn ? 0.38f : 0.25f) : 2 * is + is * 0.15f + is * 0.75f + is * 0.25f;
    if (bpRows) { y += inkLbl(dl, ImVec2(p.x, y), "Backpack") + 2 * z; grid9(dl, Grp::Bp, 0, sess->groupSize(Grp::Bp), ImVec2(p.x, y)); y += bpRows * is + is * 0.2f; }
    y += inkLbl(dl, ImVec2(p.x, y), "Inventory") + 2 * z;
    grid9(dl, Grp::Main, 9, 36, ImVec2(p.x, y)); y += 3 * is + is * 0.15f;
    grid9(dl, Grp::Main, 0, 9, ImVec2(p.x, y));
  }
  // the left page for a block's screen: its name, then its slots (a chest's grid, the furnace, the trommel, a machine,
  // a station, the stonecutter or forge, or the crafting table's 3x3 with the recipe book under it when it's open)
  void bookPageBlock(App& a, ImDrawList* dl, ImVec2 p, float w, float h) {
    const float z = Z();
    std::string title = sess->title.empty() ? std::string("Crafting") : sess->title;
    { float sc = is * 0.5f / z / fontPx(F_H3);
      text(dl, F_H3, p, ink::head, title.c_str(), 0, sc);
      float hy = p.y + textSize(F_H3, "Ag", 0, sc).y + 2 * z;
      dl->AddRectFilled(ImVec2(p.x, hy), ImVec2(p.x + w, hy + std::max(1.f, z)), ink::faint);
      p.y = hy + is * 0.25f; }
    paper = true;
    const bool trommel = o.mode == "furnace" && sess->furnaceKind == "trommel";
    if (o.mode == "table") {
      const float gw = 3 * is + is * 0.2f + is * 0.9f + is * 0.2f + is * 1.3f;
      craft(dl, ImVec2(p.x + (w - gw) * 0.5f, p.y), 3);
      const float y = p.y + 3 * is + is * 0.15f + is * 0.75f + is * 0.3f;
      if (book().open) { onPage = true; bookA = ImVec2(p.x, y); bookB = ImVec2(p.x + w, p.y + h - (p.y - bookTextTop)); bookShown = true; drawBook(a, dl); onPage = false; }
    } else if (trommel) drawTrommel(a, dl, p, w);
    else if (o.mode == "furnace") drawFurnace(a, dl, p, w);
    else if (o.mode == "machine") drawMachine(a, dl, p, w);
    else if (o.mode == "station") drawStation(a, dl, p, w);
    else if (o.mode == "stonecutter") drawCutter(a, dl, p, w);
    else if (o.gearGet) drawMount(dl, p, true);
    else grid9(dl, Grp::Cont, 0, sess->groupSize(Grp::Cont), p);
    paper = false;
  }
  // the right page for a block's screen: your backpack, bag and hotbar
  void bookPageBag(App& a, ImDrawList* dl, ImVec2 p, float w, float h) {
    (void)a; (void)h; (void)w;
    const float z = Z();
    float y = p.y;
    const int bpRows = (sess->groupSize(Grp::Bp) + 8) / 9;
    if (bpRows) { y += inkLbl(dl, ImVec2(p.x, y), "Backpack") + 2 * z; grid9(dl, Grp::Bp, 0, sess->groupSize(Grp::Bp), ImVec2(p.x, y)); y += bpRows * is + is * 0.3f; }
    if (const int qn = sess->groupSize(Grp::Qv)) { y += inkLbl(dl, ImVec2(p.x, y), "Quiver") + 2 * z; grid9(dl, Grp::Qv, 0, qn, ImVec2(p.x, y)); y += is + is * 0.3f; }   // (this page has room: no crafting grid)
    y += inkLbl(dl, ImVec2(p.x, y), "Inventory") + 2 * z;
    grid9(dl, Grp::Main, 9, 36, ImVec2(p.x, y)); y += 3 * is + is * 0.15f;
    grid9(dl, Grp::Main, 0, 9, ImVec2(p.x, y));
  }
  void bookPageCreative(App& a, ImDrawList* dl, ImVec2 p, float w, float h) {
    const float z = Z();
    const float labelH = textSize(F_BODY, "Ag", 0, is * 0.32f / z / fontPx(F_BODY)).y;
    const float invH = labelH + 2 * z + 3 * is + is * 0.15f + is;
    onPage = true; bookA = p; bookB = ImVec2(p.x + w, p.y + h - invH - is * 0.25f); bookShown = true;
    drawBook(a, dl);
    onPage = false;
    float y = bookB.y + is * 0.25f;
    y += inkLbl(dl, ImVec2(p.x, y), "Inventory") + 2 * z;
    grid9(dl, Grp::Main, 9, 36, ImVec2(p.x, y)); y += 3 * is + is * 0.15f;
    grid9(dl, Grp::Main, 0, 9, ImVec2(p.x, y));
  }

  void craft(ImDrawList* dl, ImVec2 p, int Wd, ImVec2 bookAt = ImVec2(-1, -1)) {   // bookAt: the recipe book's button elsewhere (default: under the grid)
    for (int i = 0; i < Wd * Wd; i++) slot(dl, Grp::Grid, i, ImVec2(p.x + (i % Wd) * is, p.y + (i / Wd) * is), is);
    float gx = p.x + Wd * is + is * 0.2f, mid = p.y + Wd * is * 0.5f;
    arrow(dl, ImVec2(gx, mid - is * 0.3f));
    slot(dl, Grp::Out, 0, ImVec2(gx + is * 0.9f + is * 0.2f, mid - is * 0.65f), is * 1.3f, nullptr, true);
    bookButton(dl, bookAt.x >= 0 ? bookAt : ImVec2(p.x, p.y + Wd * is + is * 0.15f));
  }

  void drawInvTop(App& a, ImDrawList* dl, ImVec2 p, float w, float h, float labelH) {
    Player& pl = sess->p; float z = Z(), x = p.x;
    static const char* PH[] = {"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"};
    for (int i = 0; i < 4; i++) slot(dl, Grp::Armor, i, ImVec2(x, p.y + i * is), is, PH[i]);
    x += is + is * 0.18f;
    // the doll and its stats
    ImVec2 d0(x + (is * 2 - is * 1.7f - is * 0.3f) * 0.5f, p.y), d1 = d0 + ImVec2(is * 1.7f + is * 0.3f, is * 3.4f + 4 * z);
    drawDoll(dl, d0, d1, pl, a.time);
    std::string st = "Lv " + std::to_string(pl.level) + " \xC2\xB7 " + strFormat("%g", pl.hp / 2) + "/" + strFormat("%g", pl.maxHp / 2) + " \xE2\x99\xA5";
    float sc = is * 0.26f / z / fontPx(F_BODY); ImVec2 ss = textSize(F_BODY, st.c_str(), 0, sc);
    text(dl, F_BODY, ImVec2(x + is - ss.x * 0.5f, d1.y + 2 * z), col::dim, st.c_str(), 0, sc);
    {   // .eqvis-row: show or hide your helmet, chestplate, leggings, boots, backpack and cape (they still protect you)
      float ew = is * 1.9f, bw = ew / 4, bh = is * 0.5f, ex = x + is - ew * 0.5f, ey = d1.y + 4 * z + ss.y;
      for (int k = 0; k < 7; k++) {
        ImVec2 b0(ex + (k % 4) * bw, ey + (k / 4) * (bh + 2 * z)), b1 = b0 + ImVec2(bw - 2 * z, bh);
        drawGearVis(dl, b0, b1, pl, k, isFocus(H_VIS, Grp::Main, k), isHover(b0, b1));
        addHit(H_VIS, Grp::Main, k, "", b0, b1);
      }
    }
    x += is * 2 + is * 0.18f;
    // gear: off hand, hook, pack, charm, cape with captions under them
    struct G { Grp g; const char* ph; const char* cap; };
    const G gear[] = {{Grp::Off, nullptr, "Off hand"}, {Grp::Hook, "lantern", "Hook"}, {Grp::Back, "leather_backpack", "Pack"}, {Grp::Charm, "vigor_charm", "Charm"}, {Grp::Cape, "red_cape", "Cape"}};
    float cs = is * 0.21f / z / fontPx(F_BODY), gy = p.y;
    for (auto& gr : gear) {
      slot(dl, gr.g, 0, ImVec2(x, gy), is, gr.ph);
      ImVec2 ts = textSize(F_BODY, gr.cap, 0, cs);
      text(dl, F_BODY, ImVec2(x + is * 0.5f - ts.x * 0.5f, gy + is + is * 0.01f), col::dim, gr.cap, 0, cs);
      gy += is * 1.2f;
    }
    x += is + is * 0.18f;
    // the character sheet
    drawSheet(dl, ImVec2(x, p.y), is * 2.3f, is, pl);
    // crafting, pushed to the right
    float craftW = is * 2 + is * 0.2f + is * 0.9f + is * 0.2f + is * 1.3f;
    float cx = p.x + w - craftW;
    lbl(dl, ImVec2(cx, p.y), "Crafting", col::ink2);
    craft(dl, ImVec2(cx, p.y + labelH + 2 * z), 2);
    // the quiver at the foot of it all, under the sheet: its slot, then its four slots of arrows once one is worn
    {
      const float qy = p.y + h - is - labelH - 2 * z;
      lbl(dl, ImVec2(x, qy), "Quiver", col::ink2);
      slot(dl, Grp::Quiver, 0, ImVec2(x, qy + labelH + 2 * z), is, "quiver");
      for (int k = 0, n = sess->groupSize(Grp::Qv); k < n; k++) slot(dl, Grp::Qv, k, ImVec2(x + is * 1.2f + k * is, qy + labelH + 2 * z), is);
    }
  }

  void drawFurnace(App& a, ImDrawList* dl, ImVec2 p, float w) {
    float z = Z();
    json* e = o.hasPos ? a.session->world().blockEntity(o.pos.x, o.pos.y, o.pos.z) : nullptr;
    double burn = e ? e->value("burn", 0.0) : 0, burnMax = e ? e->value("burnMax", 0.0) : 0, cook = e ? e->value("cook", 0.0) : 0;
    float colH = is * 2 + is * 0.6f + 4 * z, totalW = is + is * 0.4f + is * 0.9f + is * 0.4f + is * 1.3f;
    float x = p.x + (w - totalW) * 0.5f, y = p.y + is * 0.15f, mid = y + colH * 0.5f;
    slot(dl, Grp::Furn, 0, ImVec2(x, y), is);
    // the flame: clip-path polygon, filling from the bottom while fuel burns
    ImVec2 f0(x + is * 0.2f, y + is + 2 * z); float fs = is * 0.6f;
    auto P = [&](float u, float v) { return ImVec2(f0.x + u * fs, f0.y + v * fs); };
    ImVec2 poly[] = {P(0.5f, 0), P(0.85f, 0.45f), P(0.8f, 0.85f), P(0.5f, 1), P(0.2f, 0.85f), P(0.15f, 0.45f)};
    dl->AddConvexPolyFilled(poly, 6, col::rgb(0x3a2f25));
    float fl = burn > 0 && burnMax > 0 ? (float)std::max(0.04, burn / burnMax) : 0;
    if (fl > 0) {
      dl->PushClipRect(ImVec2(f0.x, f0.y + fs * (1 - fl)), ImVec2(f0.x + fs, f0.y + fs), true);
      ImVec2 hot[] = {P(0.5f, 0), P(0.85f, 0.45f), P(0.8f, 0.85f), P(0.5f, 1), P(0.2f, 0.85f), P(0.15f, 0.45f)};
      dl->AddConvexPolyFilled(hot, 6, col::rgb(0xff9a3c));
      dl->PopClipRect();
    }
    slot(dl, Grp::Furn, 1, ImVec2(x, y + is + fs + 4 * z), is, "coal");
    x += is + is * 0.4f;
    arrow(dl, ImVec2(x, mid - is * 0.3f), (float)(cook / SMELT_TIME));
    x += is * 0.9f + is * 0.4f;
    slot(dl, Grp::Furn, 2, ImVec2(x, mid - is * 0.65f), is * 1.3f, nullptr, true);
  }

  // BTA's trommel screen: the fuel under its flame on the left, the four inputs round the drum (it turns while it
  // sifts), and what the next input can give with its chances on the right; the finds pop out of the block's side
  void drawTrommel(App& a, ImDrawList* dl, ImVec2 p, float w) {
    float z = Z();
    json* e = o.hasPos ? a.session->world().blockEntity(o.pos.x, o.pos.y, o.pos.z) : nullptr;
    const double burn = e ? e->value("burn", 0.0) : 0, burnMax = e ? e->value("burnMax", 0.0) : 0, cook = e ? e->value("cook", 0.0) : 0;
    const int next = e ? e->value("next", 0) % 4 : 0;
    const float gap = is * 0.45f, yw = is * 3.2f, totalW = is + gap + 3 * is + gap + yw;
    float x = p.x + (w - totalW) * 0.5f, y = p.y + is * 0.15f;
    // fuel and its flame
    ImVec2 f0(x + is * 0.2f, y + is * 0.25f); float fs = is * 0.6f;
    auto P = [&](float u, float v) { return ImVec2(f0.x + u * fs, f0.y + v * fs); };
    ImVec2 poly[] = {P(0.5f, 0), P(0.85f, 0.45f), P(0.8f, 0.85f), P(0.5f, 1), P(0.2f, 0.85f), P(0.15f, 0.45f)};
    dl->AddConvexPolyFilled(poly, 6, col::rgb(0x3a2f25));
    float fl = burn > 0 && burnMax > 0 ? (float)std::max(0.04, burn / burnMax) : 0;
    if (fl > 0) { dl->PushClipRect(ImVec2(f0.x, f0.y + fs * (1 - fl)), ImVec2(f0.x + fs, f0.y + fs), true); dl->AddConvexPolyFilled(poly, 6, col::rgb(0xff9a3c)); dl->PopClipRect(); }
    slot(dl, Grp::Furn, 4, ImVec2(x, y + is), is, "coal");
    // the diamond of inputs round the drum
    const float cx = x + is + gap;
    const ImVec2 at[4] = {ImVec2(cx + is, y), ImVec2(cx, y + is), ImVec2(cx + 2 * is, y + is), ImVec2(cx + is, y + 2 * is)};
    static const char* PH[4] = {"dirt", "gravel", "sand", "clay"};
    for (int i = 0; i < 4; i++) slot(dl, Grp::Furn, i, at[i], is, PH[i]);
    {
      ImVec2 c(cx + is * 1.5f, y + is * 1.5f); float r = is * 0.36f, ang = burn > 0 ? (float)a.time * 3.f : 0.f;
      dl->AddCircleFilled(c, r, col::rgb(0x2a2219)); dl->AddCircle(c, r, col::rgb(0x8a8a90), 0, 2 * z);
      for (int k = 0; k < 6; k++) { float an = ang + k * 1.0471976f; dl->AddLine(c, ImVec2(c.x + std::cos(an) * r, c.y + std::sin(an) * r), col::rgb(0x6a6a70), 1.5f * z); }
      if (burn > 0) dl->PathArcTo(c, r + 3 * z, -1.5708f, -1.5708f + 6.2832f * (float)std::min(1.0, cook / TROMMEL_POP), 24), dl->PathStroke(col::gold, 0, 2 * z);
    }
    // what the next input can give
    const ItemStack* in = sess->at(Grp::Furn, next);
    std::string key = in && !in->empty() ? in->id : "";
    for (int k = 0; k < 4 && key.empty(); k++) if (const ItemStack* s2 = sess->at(Grp::Furn, k); s2 && !s2->empty()) key = s2->id;
    float yx = cx + 3 * is + gap, yy = y, ic = is * 0.42f, sc = is * 0.26f / z / fontPx(F_BODY);
    if (!key.empty()) {
      auto ys = trommelYields(key);
      for (size_t i = 0; i < ys.size() && i < 6; i++) {
        drawIcon(dl, ys[i].second, ImVec2(yx, yy), ic);
        std::string pc = ys[i].first >= 0.1f ? std::to_string((int)std::lround(ys[i].first * 100)) + "%" : strFormat("%.1f%%", ys[i].first * 100);
        const ItemDef* d = reg().item(ys[i].second);
        std::string t = pc + "  " + (d ? d->name : ys[i].second);
        text(dl, F_BODY, ImVec2(yx + ic + 4 * z, yy + (ic - textSize(F_BODY, "A", 0, sc).y) * 0.5f), paper ? ink::text : col::ink2, ellipsize(F_BODY, t, yw - ic - 4 * z).c_str(), 0, sc);
        yy += ic + 2 * z;
      }
    }
    std::string st = burn > 0 ? "Sifting \xE2\x80\x94 what it finds pops out of its left side (into a chest there, if there is one)"
                   : key.empty() ? "Put in dirt, gravel, sand, clay, mud, soul sand, brimsand or sulphur, and fuel"
                   : "Needs fuel: anything that burns in a furnace (it lasts twice as long here)";
    status(dl, ImVec2(p.x, y + 3 * is + is * 0.15f), w, st, burn > 0 ? col::gold : col::dim);
  }

  // .mach-status: one or two lines of dim text under the machine / station row
  float statusH() const { return textSize(F_BODY, "Ag", 0, is * 0.3f / Z() / fontPx(F_BODY)).y * 2.4f; }
  void status(ImDrawList* dl, ImVec2 p, float w, const std::string& t, ImU32 c = col::dim) {
    float sc = is * 0.3f / Z() / fontPx(F_BODY);
    ImVec2 ts = textSize(F_BODY, t.c_str(), w, sc);
    float x = p.x + std::max(0.f, (w - ts.x) * 0.5f);
    if (paper) { text(dl, F_BODY, ImVec2(x, p.y), c == col::gold ? ink::key : c == col::dim ? ink::muted : c, t.c_str(), w, sc); return; }   // in the book's ink
    text(dl, F_BODY, ImVec2(x, p.y) + ImVec2(1, 1) * Z(), col::black, t.c_str(), w, sc);
    text(dl, F_BODY, ImVec2(x, p.y), c, t.c_str(), w, sc);
  }
  void drawMachine(App& a, ImDrawList* dl, ImVec2 p, float w) {
    // .furn.mach: in · arrow (fills gold as it works) · out, then the status line (invui.js machineBars)
    float totalW = is + is * 0.4f + is * 0.9f + is * 0.4f + is * 1.3f;
    float x = p.x + (w - totalW) * 0.5f, y = p.y + is * 0.15f, mid = y + is * 0.65f;
    const json* tbl = machineTable(sess->machKind);
    std::string ph; if (tbl && tbl->is_object() && !tbl->empty()) ph = tbl->begin().key();
    slot(dl, Grp::Mach, 0, ImVec2(x, mid - is * 0.5f), is, ph.empty() ? nullptr : ph.c_str());
    x += is + is * 0.4f;
    arrow(dl, ImVec2(x, mid - is * 0.3f), sess->machProgress);
    x += is * 0.9f + is * 0.4f;
    slot(dl, Grp::Mach, 1, ImVec2(x, mid - is * 0.65f), is * 1.3f, nullptr, true);
    float sp = 0;
    if (o.hasPos) if (Mechanics* m = mechanicsOf(*a.session->level, sess->p.realm)) sp = m->speedAt(o.pos.x, o.pos.y, o.pos.z);
    std::string t = sp > 0.05f ? "Turning at " + std::to_string((int)std::lround(std::min(1.f, sp) * 100)) + "% \xE2\x80\x94 " +
        (sess->machKind == "sawmill" ? "logs become 6 planks, planks 6 sticks" : "ores grind into 2 crushed ore, cobble to gravel, gravel to sand, flowers to dye")
      : "Not turning. Join it to a windmill (high, in open air) or a waterwheel (in flowing water) with wooden axles.";
    status(dl, ImVec2(p.x, y + is * 1.3f + is * 0.15f), w, t, sp > 0.05f ? col::gold : col::dim);
  }
  void drawStation(App& a, ImDrawList* dl, ImVec2 p, float w) {
    // .furn.station: a + b → out, then what it does (invui.js renderStation)
    float z = Z(), plusW = is * 0.45f;
    float totalW = is + is * 0.4f + plusW + is * 0.4f + is + is * 0.4f + is * 0.9f + is * 0.4f + is * 1.3f;
    float x = p.x + (w - totalW) * 0.5f, y = p.y + is * 0.15f, mid = y + is * 0.65f;
    auto ph = stationPlaceholders(o.kind);
    slot(dl, Grp::Sta, 0, ImVec2(x, mid - is * 0.5f), is, ph.first);
    x += is + is * 0.4f;
    float sc = is * 0.6f / z / fontPx(F_DISP); ImVec2 ts = textSize(F_DISP, "+", 0, sc);
    text(dl, F_DISP, ImVec2(x + (plusW - ts.x) * 0.5f, mid - ts.y * 0.5f), paper ? ink::muted : col::dim, "+", 0, sc);
    x += plusW + is * 0.4f;
    slot(dl, Grp::Sta, 1, ImVec2(x, mid - is * 0.5f), is, ph.second);
    x += is + is * 0.4f;
    arrow(dl, ImVec2(x, mid - is * 0.3f));
    x += is * 0.9f + is * 0.4f;
    slot(dl, Grp::SOut, 0, ImVec2(x, mid - is * 0.65f), is * 1.3f, nullptr, true);
    const StationResult& r = sess->staRes;
    status(dl, ImVec2(p.x, y + is * 1.3f + is * 0.15f), w, r.ok ? r.note : stationHelp(o.kind));
  }
  void drawCutter(App& a, ImDrawList* dl, ImVec2 p, float w) {
    // .cutter: the input · arrow · a 5-wide list of what it cuts into (invui.js renderCutter)
    float z = Z(), listW = is * 5 + 8 * z, listH = is * 2.6f;
    float totalW = is + is * 0.3f + is * 0.9f + is * 0.3f + listW;
    float x = p.x + (w - totalW) * 0.5f, y = p.y + is * 0.12f, mid = y + listH * 0.5f;
    slot(dl, Grp::Cut, 0, ImVec2(x, mid - is * 0.5f), is, o.kind == "forge" ? "iron_ingot" : "cobblestone");
    x += is + is * 0.3f;
    arrow(dl, ImVec2(x, mid - is * 0.3f));
    x += is * 0.9f + is * 0.3f;
    ImVec2 l0(x, y), l1(x + listW, y + listH);
    dl->AddRectFilled(l0, l1, paper ? IM_COL32(26, 20, 15, 238) : IM_COL32(0, 0, 0, 89), paper ? 4 * z : 0);   // (on the paper: a dark card, as the recipe book)
    if (!paper) bevel(dl, l0, l1, col::slLo, col::slHi, 2 * z);
    const auto& opts = sess->cutOpts;
    if (opts.empty()) { text(dl, F_SMALL, l0 + ImVec2(6, 6) * z, col::dim, o.kind == "forge" ? "Put in a material (flint, an ingot, a gem, obsidian...) to forge a tool head (a pickaxe or axe head takes 3, a sword or hoe head 2, a shovel head 1). A head and a haft (a stick, bone, cinder rod or steel ingot) make the tool." : "Put a stone block in to see what it cuts into.", listW - 12 * z); return; }
    int rows = ((int)opts.size() + 4) / 5;
    float viewH = listH - 4 * z, maxScroll = std::max(0.f, rows * is - viewH);
    if (isHover(l0, l1)) cutScroll -= ImGui::GetIO().MouseWheel * is;
    cutScroll += a.input.padScroll();
    if (hasFocus && focus.k == H_CUT) { float ry = (focus.i / 5) * is; if (ry < cutScroll) cutScroll = ry; if (ry + is > cutScroll + viewH) cutScroll = ry + is - viewH; }
    cutScroll = clampv(cutScroll, 0.f, maxScroll);
    ImVec2 c0 = l0 + ImVec2(4, 2) * z;
    dl->PushClipRect(l0 + ImVec2(2, 2) * z, l1 - ImVec2(2, 2) * z, true);
    for (int i = 0; i < (int)opts.size(); i++) {
      ImVec2 s0(c0.x + (i % 5) * is, c0.y + (i / 5) * is - cutScroll);
      if (s0.y + is < l0.y || s0.y > l1.y) continue;
      SlotLook lk; lk.hover = isHover(s0, s0 + ImVec2(is, is)); lk.focus = isFocus(H_CUT, Grp::Main, i);
      drawSlot(dl, s0, is, lk);
      drawItem(dl, ItemStack(opts[i].first, opts[i].second), s0, is);
      ImVec2 h0(s0.x, std::max(s0.y, l0.y)), h1(s0.x + is, std::min(s0.y + is, l1.y));
      if (h1.y > h0.y) addHit(H_CUT, Grp::Main, i, "", h0, h1);
    }
    dl->PopClipRect();
  }
  float cutScroll = 0;

  void drawInfo(App& a, ImDrawList* dl, ImVec2 p, float w) {
    if (!a.input.usingPad() || !hasFocus) return;
    const Hit* f = findHit(focus);
    float sc = is * 0.3f / Z() / fontPx(F_BODY);
    std::string t;
    std::vector<std::pair<std::string, u32>> lines;
    if (f && f->k == H_SLOT) {
      const ItemStack* s = sess->at(f->g, f->i);
      if (s && !s->empty()) itemInfoText(*s, &sess->p, &lines);
      else t = std::string("A take/place \xC2\xB7 X split \xC2\xB7 Y quick move \xC2\xB7 ") + (o.mode == "table" ? "LB recipe book" : "LB/RB menus") + " \xC2\xB7 B close";
    } else if (f && f->k == H_CI) { if (const ItemDef* d = reg().item(f->s)) itemInfoText(ItemStack(d->key, 1), &sess->p, &lines); }
    else if (f && f->k == H_CUT && f->i < (int)sess->cutOpts.size()) { itemInfoText(ItemStack(sess->cutOpts[f->i].first, sess->cutOpts[f->i].second), &sess->p, &lines); lines.push_back({"  A one \xC2\xB7 Y all", 0xa8987e}); }
    float x = p.x;
    for (auto& [txt, c] : lines) {
      std::string s = txt; if (!s.empty() && s[0] == '\x02') continue; if (!s.empty() && s[0] == '\x01') s = s.substr(9);
      ImVec2 ts = textSize(F_BODY, (s + "  ").c_str(), 0, sc);
      if (x + ts.x > p.x + w) break;
      text(dl, F_BODY, ImVec2(x, p.y), col::rgb(c), s.c_str(), 0, sc); x += ts.x;
    }
    if (!t.empty()) text(dl, F_BODY, p, col::dim, t.c_str(), w, sc);
  }

  void tooltip(App& a) {
    const Hit* h = nullptr; ImVec2 m = ImGui::GetIO().MousePos;
    for (auto& x : hits) if (x.contains(m)) h = &x;
    if (!h) return;
    ItemStack s;
    if (h->k == H_SLOT) { const ItemStack* st = sess->at(h->g, h->i); if (st) { if (st->id == "enchanted_book") enchOf(*const_cast<ItemStack*>(st)); s = *st; } }   // a treasure book settles what it holds the first time it is looked at
    else if (h->k == H_BOOKR) { const Recipe& r = reg().recipes[h->i]; s = ItemStack(r.out, r.n); }
    else if (h->k == H_CI) s = ItemStack(h->s, 1);
    else if (h->k == H_CUT && h->i < (int)sess->cutOpts.size()) s = ItemStack(sess->cutOpts[h->i].first, sess->cutOpts[h->i].second);
    if (s.empty()) return;
    std::vector<std::pair<std::string, u32>> lines;
    itemInfoText(s, &sess->p, &lines);
    float z = Z(), maxW = 260 * z - 16 * z, k = 13.f / 15.f;
    float w = 0, hgt = 0;
    for (auto& [t, c] : lines) { if (!t.empty() && t[0] == '\x02') { w = std::max(w, 120 * z); hgt += 8 * z; continue; } ImVec2 ts = textSize(F_BODY, t.c_str(), maxW, k); w = std::max(w, ts.x); hgt += ts.y; }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 p(m.x + 14 * z, m.y + 10 * z), size(w + 16 * z, hgt + 10 * z);
    p.x = std::max(4 * z, std::min(p.x, (float)a.width - size.x - 6 * z)); p.y = std::max(4 * z, std::min(p.y, (float)a.height - size.y - 6 * z));
    dl->AddRectFilled(p - ImVec2(4, 4) * z, p + size + ImVec2(4, 4) * z, col::black);
    dl->AddRectFilled(p - ImVec2(2, 2) * z, p + size + ImVec2(2, 2) * z, col::pnHi);
    dl->AddRectFilled(p, p + size, IM_COL32(18, 12, 8, 245));
    float y = p.y + 5 * z;
    for (auto& [t, c] : lines) {
      std::string s = t; float x = p.x + 8 * z;
      if (!s.empty() && s[0] == '\x02') {   // .gearbar: 120 x 5, the level's progress
        float fr = (float)std::atof(s.c_str() + 1);
        dl->AddRectFilled(ImVec2(x, y + z), ImVec2(x + 120 * z, y + 6 * z), col::rgb(0x1a120a)); dl->AddRect(ImVec2(x, y + z), ImVec2(x + 120 * z, y + 6 * z), col::black);
        dl->AddRectFilled(ImVec2(x + z, y + 2 * z), ImVec2(x + z + 118 * z * clampv(fr, 0.f, 1.f), y + 5 * z), col::rgb(0x9bd34a));
        y += 8 * z; continue;
      }
      if (!s.empty() && s[0] == '\x01') { u32 dc = (u32)std::strtoul(s.c_str() + 2, nullptr, 16); float b = 10 * z; dl->AddRectFilled(ImVec2(x, y + 3 * z), ImVec2(x + b, y + 3 * z + b), col::rgb(dc)); dl->AddRect(ImVec2(x, y + 3 * z), ImVec2(x + b, y + 3 * z + b), col::black); x += b + 4 * z; s = s.substr(9); }
      text(dl, F_BODY, ImVec2(x, y), col::rgb(c), s.c_str(), maxW, k);
      y += textSize(F_BODY, s.c_str(), maxW, k).y;
    }
  }

  // ---- recipe book / creative catalogue ------------------------------------------------------------------
  void drawBook(App& a, ImDrawList* dl) {
    BookState& B = book(); float z = Z();
    // on a page of the book inventory: a dark card on the paper; the creative tabs are the book's bookmarks there
    if (onPage) dl->AddRectFilled(bookA, bookB, IM_COL32(26, 20, 15, 238), 6 * z), dl->AddRect(bookA, bookB, col::rgb(0x6b5a3a), 6 * z, 0, std::max(1.f, z));
    else panelBg(dl, bookA, bookB);
    float pad = onPage ? is * 0.15f : is * 0.22f + 3 * z;
    ImVec2 c = bookA + ImVec2(pad, pad); float w = bookB.x - bookA.x - pad * 2;
    // head: search · Craftable · ✕
    float hh = is * 0.7f;
    ImGui::SetCursorScreenPos(c);
    ImGui::PushFont(font(F_BODY));
    ImGui::SetWindowFontScale(std::max(0.6f, is * 0.32f / z / fontPx(F_BODY)));
    float onlyW = creative ? 0 : textSize(F_BODY, "Craftable", 0, is * 0.28f / z / fontPx(F_BODY)).x + hh + 6 * z;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::rgb(0x0a0806)); ImGui::PushStyleColor(ImGuiCol_Border, col::slHi);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * z, std::max(2.f, (hh - ImGui::GetFontSize()) * 0.5f))); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::SetNextItemWidth(w - hh - onlyW - 8 * z);
    ImGui::InputTextWithHint("##search", creative ? "Search all items" : "Search recipes", &B.query);
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
    if (!creative) {
      ImGui::SameLine(0, 4 * z);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
      if (ImGui::Checkbox("Craftable", &B.only)) playClick();
      ImGui::PopStyleVar();
    }
    ImGui::SetWindowFontScale(1);
    ImGui::PopFont();
    if (onPage && creative) {   // the bin where the close button would be
      ImVec2 t0(c.x + w - hh, c.y), t1 = t0 + ImVec2(hh, hh);
      bool hot = isHover(t0, t1) || isFocus(H_TRASH, Grp::Main, 0);
      dl->AddRectFilled(t0, t1, hot ? col::rgb(0x8a3333) : col::rgb(0x5a2626)); dl->AddRect(t0, t1, col::rgb(0x2a0f0f), 0, 0, 2 * z);
      float u = hh / 10; ImVec2 q0 = t0 + ImVec2(3 * u, 3 * u);
      dl->AddRectFilled(q0 + ImVec2(-0.6f * u, 0), q0 + ImVec2(4.6f * u, 0.8f * u), col::white); dl->AddRectFilled(q0 + ImVec2(1.4f * u, -0.8f * u), q0 + ImVec2(2.6f * u, 0), col::white);
      dl->AddRectFilled(q0 + ImVec2(0, 1.2f * u), q0 + ImVec2(4 * u, 5 * u), col::white);
      if (isFocus(H_TRASH, Grp::Main, 0)) dl->AddRect(t0 - ImVec2(1, 1) * z, t1 + ImVec2(1, 1) * z, col::gold, 0, 0, 3 * z);
      addHit(H_TRASH, Grp::Main, 0, "", t0, t1);
    } else
    { ImVec2 x0(c.x + w - hh, c.y), x1 = x0 + ImVec2(hh, hh);
      dl->AddRectFilled(x0, x1, col::rgb(0x3a3027)); bevel(dl, x0, x1, col::pnHi, col::pnLo, 2 * z);
      float u = hh * 0.3f; ImVec2 m = (x0 + x1) * 0.5f;
      dl->AddLine(m - ImVec2(u, u), m + ImVec2(u, u), col::ink2, 2 * z); dl->AddLine(m + ImVec2(-u, u), m + ImVec2(u, -u), col::ink2, 2 * z);
      if (isFocus(H_BOOKCLOSE, Grp::Main, 0)) dl->AddRect(x0 - ImVec2(2, 2) * z, x1 + ImVec2(2, 2) * z, col::gold, 0, 0, 3 * z);
      addHit(H_BOOKCLOSE, Grp::Main, 0, "", x0, x1); }
    float y = c.y + hh + 4 * z;
    // tabs
    float tsc = is * 0.28f / z / fontPx(F_BODY), tx = c.x;
    float tabH = textSize(F_BODY, "Ag", 0, tsc).y + 8 * z;
    if (creative && onPage) {   // (the bookmarks are the tabs): the tab's name
      std::string nm; for (auto& tb : creativeTabs()) if (B.ccat == tb.key) nm = tb.name;
      if (!B.query.empty()) nm = "Searching everything";
      text(dl, F_BODY, ImVec2(c.x, y), col::gold, nm.c_str(), 0, tsc); y += textSize(F_BODY, "Ag", 0, tsc).y + 1 * z;
      tabH = -4 * z;
    } else if (creative) {   // the creative tabs: an icon each (ui/creative_catalogue.cpp), the name under the pointer
      const auto& T = creativeTabs();
      const float ts = std::floor(std::min(is * 0.78f, (w + 2 * z) / 8.f - 2 * z));
      for (auto& tb : T) {
        if (tx + ts > c.x + w + 1) { tx = c.x; y += ts + 2 * z; }
        ImVec2 t0(tx, y), t1 = t0 + ImVec2(ts, ts);
        bool on = B.ccat == tb.key, hot = isHover(t0, t1);
        dl->AddRectFilled(t0, t1, on ? col::rgb(0x5a4632) : hot ? col::rgb(0x3a2e24) : col::rgb(0x241c16));
        bevel(dl, t0, t1, on ? col::gold : col::pnHi, col::pnLo, 2 * z);
        drawIcon(dl, tb.icon, t0 + ImVec2(ts * 0.14f, ts * 0.14f), ts * 0.72f, on || hot ? 1.f : 0.7f);
        if (isFocus(H_CAT, Grp::Main, 0, tb.key)) dl->AddRect(t0 - ImVec2(2, 2) * z, t1 + ImVec2(2, 2) * z, col::gold, 0, 0, 3 * z);
        if (hot || isFocus(H_CAT, Grp::Main, 0, tb.key)) catTip = tb.name;
        addHit(H_CAT, Grp::Main, 0, tb.key, t0, t1);
        tx += ts + 2 * z;
      }
      y += ts + 4 * z;
      { std::string nm; for (auto& tb : T) if (B.ccat == tb.key) nm = tb.name;
        text(dl, F_BODY, ImVec2(c.x, y), col::gold, (catTip.empty() ? nm : catTip).c_str(), 0, tsc); y += textSize(F_BODY, "Ag", 0, tsc).y + 3 * z; }
      catTip.clear();
      tabH = -4 * z;   // (the tabs are done)
    }
    for (auto& [k, l] : BOOK_CATS) {
      if (creative) break;
      if (!creative && std::string(k) == "eggs") continue;   // spawn eggs are creative-only: no recipe tab for them
      float tw = textSize(F_BODY, l, 0, tsc).x + 12 * z + 4 * z;
      if (tx + tw > c.x + w) { tx = c.x; y += tabH + 2 * z; }
      ImVec2 t0(tx, y), t1 = t0 + ImVec2(tw, tabH);
      bool on = B.cat == k;
      dl->AddRectFilled(t0, t1, on ? col::rgb(0x4a3c2f) : col::rgb(0x2a221b));
      bevel(dl, t0, t1, on ? col::gold : col::pnHi, col::pnLo, 2 * z);
      text(dl, F_BODY, t0 + ImVec2(8 * z, 4 * z), on ? col::rgb(0xfff6e0) : col::dim, l, 0, tsc);
      if (isFocus(H_CAT, Grp::Main, 0, k)) dl->AddRect(t0 - ImVec2(2, 2) * z, t1 + ImVec2(2, 2) * z, col::gold, 0, 0, 3 * z);
      addHit(H_CAT, Grp::Main, 0, k, t0, t1);
      tx += tw + 2 * z;
    }
    y += tabH + 4 * z;
    // list (scrolls) and detail
    float detailH = onPage && creative ? -2 * z : is * 1.6f;
    ImVec2 l0(c.x, y), l1(c.x + w, bookB.y - pad - detailH - 4 * z);
    int perRow = std::max(1, (int)(w / is));
    std::string q = lower(B.query);
    Bags bags = sess->bags();
    struct E { HK k; int idx; std::string key; ItemStack st; int look; };   // look: 0 dim, 1 ok, 2 table, 3 catalogue
    std::vector<E> list;
    if (creative) {
      // a search looks through everything; otherwise the tab's items, families together
      for (auto& key : creativeItems(q.empty() && B.ccat != "inv" ? B.ccat : "all")) {
        const ItemDef* d = reg().item(key); if (!d) continue;
        if (!q.empty() && lower(d->name).find(q) == std::string::npos) continue;
        list.push_back({H_CI, 0, key, ItemStack(key, 1), 3});
      }
    } else {
      const auto& R = reg().recipes;
      for (size_t i = 0; i < R.size(); i++) {
        const Recipe& r = R[i];
        if (B.cat != "all" && recipeCategory(r) != B.cat) continue;
        if (sess->w && sess->w->immersive && r.props.is_object() && r.props.value("classicOnly", false)) continue;   // off in an immersive world
        const ItemDef* d = reg().item(r.out); if (!d) continue;
        if (!q.empty() && lower(d->name).find(q) == std::string::npos) continue;
        bool ok = canCraft(bags, r, &sess->grid); int rw, rh; recipeDims(r, rw, rh); bool fits = rw <= sess->W && rh <= sess->W;
        if (B.only && !(ok && fits)) continue;
        list.push_back({H_BOOKR, (int)i, "", ItemStack(r.out, r.n), ok && fits ? 1 : ok ? 2 : 0});
      }
    }
    // keep the focused entry in view; the wheel and the right stick scroll
    int rowsTotal = ((int)list.size() + perRow - 1) / perRow;
    float viewH = l1.y - l0.y, maxScroll = std::max(0.f, rowsTotal * is - viewH);
    if (isHover(l0, l1)) bookScroll -= ImGui::GetIO().MouseWheel * is;
    bookScroll += a.input.padScroll();
    if (hasFocus && (focus.k == H_BOOKR || focus.k == H_CI)) {
      for (size_t n = 0; n < list.size(); n++) if ((list[n].k == H_BOOKR && focus.k == H_BOOKR && list[n].idx == focus.i) || (list[n].k == H_CI && focus.k == H_CI && list[n].key == focus.s)) {
        float ry = (n / perRow) * is; if (ry < bookScroll) bookScroll = ry; if (ry + is > bookScroll + viewH) bookScroll = ry + is - viewH;
      }
    }
    bookScroll = clampv(bookScroll, 0.f, maxScroll);
    dl->PushClipRect(l0, l1, true);
    if (list.empty()) text(dl, F_SMALL, l0 + ImVec2(2, 2) * z, col::dim, creative ? "Nothing matches." : "Nothing here yet.", w);
    for (size_t n = 0; n < list.size(); n++) {
      ImVec2 s0(l0.x + (n % perRow) * is, l0.y + (n / perRow) * is - bookScroll);
      if (s0.y + is < l0.y || s0.y > l1.y) continue;
      E& e = list[n];
      SlotLook lk; lk.hover = isHover(s0, s0 + ImVec2(is, is)); lk.focus = e.k == H_BOOKR ? isFocus(H_BOOKR, Grp::Main, e.idx) : isFocus(H_CI, Grp::Main, 0, e.key);
      if (e.look == 1) lk.bg = col::rgb(0x24301c);
      lk.sel = e.k == H_BOOKR && sess->bookSel == e.idx;
      drawSlot(dl, s0, is, lk);
      if (e.look == 1) dl->AddRect(s0 + ImVec2(2, 2) * z, s0 + ImVec2(is, is) - ImVec2(2, 2) * z, IM_COL32(150, 200, 90, 89));
      drawItem(dl, e.st, s0, is, e.look == 0 ? 0.5f : e.look == 2 ? 0.8f : 1.f);
      ImVec2 h0 = ImVec2(s0.x, std::max(s0.y, l0.y)), h1 = ImVec2(s0.x + is, std::min(s0.y + is, l1.y));
      if (h1.y > h0.y) addHit(e.k, Grp::Main, e.k == H_BOOKR ? e.idx : 0, e.key, h0, h1);
    }
    dl->PopClipRect();
    if (maxScroll > 0) { float th = viewH * viewH / (rowsTotal * is), ty = l0.y + (viewH - th) * bookScroll / maxScroll; dl->AddRectFilled(ImVec2(l1.x - 3 * z, ty), ImVec2(l1.x, ty + th), col::pnHi); }
    // detail
    ImVec2 d0(c.x, l1.y + 4 * z);
    float dsc = is * 0.28f / z / fontPx(F_BODY);
    if (creative && onPage) {}
    else if (creative) text(dl, F_BODY, d0, col::dim, "Creative: click an item for a full stack (shift puts it straight in your bag). Drop things outside the panel to get rid of them.", w, dsc);
    else if (sess->bookSel >= 0 && sess->bookSel < (int)reg().recipes.size()) {
      const Recipe& r = reg().recipes[sess->bookSel]; const ItemDef* d = reg().item(r.out);
      std::string rar = d ? d->str("rarity") : "";
      std::string nm = (d ? d->name : r.out) + (r.n > 1 ? " \xC3\x97" + std::to_string(r.n) : "");
      text(dl, F_BODY, d0, rar == "rare" ? col::frost : rar == "epic" ? col::epic : col::ink2, nm.c_str(), w, dsc);
      float yy = d0.y + textSize(F_BODY, "Ag", 0, dsc).y + 2 * z, xx = d0.x, ic = is * 0.45f;
      auto miss = recipeMissing(bags, r, &sess->grid);
      for (auto& [k, n] : recipeIngredients(r)) {
        std::string t = std::to_string(n) + " " + ingredientLabel(k);
        float tw = ic + 2 * z + textSize(F_BODY, t.c_str(), 0, dsc).x + 8 * z;
        if (xx + tw > d0.x + w) { xx = d0.x; yy += ic + 2 * z; }
        drawIcon(dl, ingredientIcon(k), ImVec2(xx, yy), ic);
        text(dl, F_BODY, ImVec2(xx + ic + 2 * z, yy + (ic - textSize(F_BODY, "A", 0, dsc).y) * 0.5f), miss.count(k) ? col::rgb(0xff8070) : col::ink2, t.c_str(), 0, dsc);
        xx += tw;
      }
      int rw, rh; recipeDims(r, rw, rh);
      const char* em = rw > sess->W || rh > sess->W ? "Needs a crafting table (3\xC3\x97" "3)" : !miss.empty() ? "Missing ingredients are shown in red" : "Click again to fill the grid \xC2\xB7 shift for a full stack";
      text(dl, F_BODY, ImVec2(d0.x, yy + ic + 2 * z), col::dim, em, w, dsc);
    } else text(dl, F_BODY, d0, col::dim, "Pick a recipe to see what it needs. Ones you can make now are lit.", w, dsc);
  }
  float bookScroll = 0;
  bool onPage = false;   // drawBook is drawing onto a page of the book inventory
  float bookTextTop = 0; // the pages' writing starts here (this frame)
  bool paper = false;    // a block's screen is being drawn onto a page: its words in the book's ink
  std::string catTip;

  // ---- the mouse (invui.js pointer events) ----------------------------------------------------------------
  void mouse(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    if (a.input.usingPad()) return;
    ImVec2 m = io.MousePos;
    bool overWidget = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();
    const Hit* h = nullptr; for (auto& x : hits) if (x.contains(m)) h = &x;
    // the hovered slot takes the focus when the mouse moves or clicks (a pointer left resting over a slot doesn't steal
    // it from the pad's focus)
    const bool moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1);
    if (h && h->k == H_SLOT && moved) { hasFocus = true; focus = *h; focusFromMouse = true; }
    // drag: spreading over more slots
    if (sess->dragging()) {
      if (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1)) { if (h && h->k == H_SLOT) sess->dragAdd(h->g, h->i); }
      else sess->dragEnd();
      return;
    }
    for (int btn = 0; btn < 2; btn++) {
      if (!ImGui::IsMouseClicked(btn) || overWidget) continue;
      bool right = btn == 1, shift = a.input.shiftHeld();
      if (!h) {
        // a click outside the panel with something on the cursor throws it
        bool inPanel = m.x >= panelA.x && m.y >= panelA.y && m.x < panelB.x && m.y < panelB.y;
        bool inBook = bookShown && m.x >= bookA.x && m.y >= bookA.y && m.x < bookB.x && m.y < bookB.y;
        if (!sess->p.cursor.empty() && !inPanel && !inBook) sess->throwCursor(right ? 1 : sess->p.cursor.n);
        return;
      }
      if (h->k == H_SLOT && !sess->p.cursor.empty() && !shift && sess->dragTarget(h->g, h->i)) { sess->dragBegin(h->g, h->i, right); return; }
      activate(a, *h, right, shift, false);
      return;
    }
  }
};

std::unique_ptr<Screen> makeInventoryScreen(const InvOpen& o) { return std::make_unique<InventoryScreen>(o); }
void openInventory(App& a, const InvOpen& o) {
  if (!a.session || !a.session->player) return;
  if (InventoryScreen* s = a.findScreen<InventoryScreen>()) { (void)s; return; }
  a.pushScreen(makeInventoryScreen(o));
}
void registerInventoryTab() {
  registerMenuTab({"inventory", "Inventory", [](App& a) { openInventory(a); }, nullptr});
}

} // namespace dq
