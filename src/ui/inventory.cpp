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
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {
using namespace ui;

float drawSheet(ImDrawList* dl, ImVec2 p, float w, float is, const Player& pl);
void drawDoll(ImDrawList* dl, ImVec2 a, ImVec2 b, const Player& pl, double t);

namespace {
const std::pair<const char*, const char*> BOOK_CATS[] = {{"all", "All"}, {"blocks", "Blocks"}, {"redstone", "Redstone"}, {"tools", "Tools"}, {"armor", "Armor"}, {"food", "Food"}, {"misc", "Other"}};
struct BookState { bool open = false; std::string cat = "all"; bool only = false; std::string query; std::string creativeWorld; };
BookState& book() { static BookState b; return b; }

enum HK { H_SLOT, H_BOOKR, H_CI, H_CAT, H_BOOKBTN, H_BOOKCLOSE, H_TRASH };
struct Hit { HK k; Grp g = Grp::Main; int i = 0; std::string s; ImVec2 a, b; bool contains(ImVec2 p) const { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; } };
bool sameKey(const Hit& x, const Hit& y) { return x.k == y.k && x.g == y.g && x.i == y.i && x.s == y.s; }
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string creativeCat(const ItemDef& d) {
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
  bool hasFocus = false; Hit focus; Hit focusRect;
  ImVec2 panelA, panelB, bookA, bookB; bool bookShown = false;
  float is = 40;
  std::string info;                 // .ip-info line for the controller
  bool creative = false;
  ImVec2 mouseDownAt;

  const char* id() const override { return "inventory"; }
  bool pausesGame() const override { return false; }
  Player* player(App& a) { return a.session ? a.session->player : nullptr; }

  void onOpen(App& a) override {
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
  bool pendingBookFocus = false;

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
    if (o.mode == "table" && (in.pressed(Act::MenuTabLeft) || in.pressed(Act::MenuTabRight))) toggleBook(a);
    // spatial navigation between everything focusable
    ivec2 nav = in.textActive ? ivec2(0) : in.navStep();
    if (nav != ivec2(0) && in.usingPad()) {
      if (!hasFocus || !findHit(focus)) { hasFocus = true; focus = Hit{H_SLOT, Grp::Main, 9}; }
      else moveFocus(nav.x, nav.y);
    } else if (nav != ivec2(0) && !in.usingPad() && hasFocus) moveFocus(nav.x, nav.y);
    const Hit* f = hasFocus ? findHit(focus) : nullptr;
    bool A = in.padButtonPressed(0), X = in.padButtonPressed(2), Y = in.padButtonPressed(3);
    if (f && (A || X || Y)) activate(a, *f, X, Y, true);
    // keyboard over the hovered slot: 1-9 swaps with the hotbar, Q drops one (Ctrl: the stack)
    if (!in.textActive) {
      const Hit* h = hovered();
      if (h && h->k == H_SLOT) {
        for (int k = 0; k < 9; k++) if (in.keyPressed((SDL_Scancode)(SDL_SCANCODE_1 + k))) sess->hotbarSwap(h->g, h->i, k);
        if (in.keyPressed(in.keyFor(Act::Drop)) && h->g != Grp::Out) sess->dropFromSlot(h->g, h->i, in.ctrlHeld());
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
      case H_SLOT: sess->slotClick(h.g, h.i, right, shift); break;
      case H_BOOKR: sess->bookPick(h.i, shift, pad); break;
      case H_CI: sess->creativePick(h.s, shift); break;
      case H_CAT: book().cat = h.s; playClick(); break;
      case H_BOOKBTN: case H_BOOKCLOSE: toggleBook(a); break;
      case H_TRASH: sess->trashCursor(); break;
    }
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
    Player& p = sess->p;
    float z = Z(), W = (float)a.width, H = (float)a.height, vw = W / z, vh = H / z;
    BookState& B = book();
    bool showBook = B.open && canBook();
    // layout(): fit the panel into the screen
    int bpRows = (int)((sess->groupSize(Grp::Bp) + 8) / 9);
    int contN = o.mode == "container" ? sess->groupSize(Grp::Cont) : 0;
    float topRows = o.mode == "inventory" ? 4.8f : o.mode == "table" ? 4.6f : o.mode == "furnace" ? 3.2f : std::ceil((contN ? contN : 27) / 9.f);
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
    else if (o.mode == "furnace") topH = is * 0.3f + std::max(is * 2 + is * 0.6f + 4 * z, is * 1.3f);
    else topH = ((contN + 8) / 9) * is;
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
    } else if (o.mode == "furnace") drawFurnace(a, dl, ImVec2(c.x, y), contentW);
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

  void craft(ImDrawList* dl, ImVec2 p, int Wd) {
    for (int i = 0; i < Wd * Wd; i++) slot(dl, Grp::Grid, i, ImVec2(p.x + (i % Wd) * is, p.y + (i / Wd) * is), is);
    float gx = p.x + Wd * is + is * 0.2f, mid = p.y + Wd * is * 0.5f;
    arrow(dl, ImVec2(gx, mid - is * 0.3f));
    slot(dl, Grp::Out, 0, ImVec2(gx + is * 0.9f + is * 0.2f, mid - is * 0.65f), is * 1.3f, nullptr, true);
    bookButton(dl, ImVec2(p.x, p.y + Wd * is + is * 0.15f));
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
    float x = p.x;
    for (auto& [txt, c] : lines) {
      std::string s = txt; if (!s.empty() && s[0] == '\x01') s = s.substr(9);
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
    if (h->k == H_SLOT) { const ItemStack* st = sess->at(h->g, h->i); if (st) s = *st; }
    else if (h->k == H_BOOKR) { const Recipe& r = reg().recipes[h->i]; s = ItemStack(r.out, r.n); }
    else if (h->k == H_CI) s = ItemStack(h->s, 1);
    if (s.empty()) return;
    std::vector<std::pair<std::string, u32>> lines;
    itemInfoText(s, &sess->p, &lines);
    float z = Z(), maxW = 260 * z - 16 * z, k = 13.f / 15.f;
    float w = 0, hgt = 0;
    for (auto& [t, c] : lines) { ImVec2 ts = textSize(F_BODY, t.c_str(), maxW, k); w = std::max(w, ts.x); hgt += ts.y; }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 p(m.x + 14 * z, m.y + 10 * z), size(w + 16 * z, hgt + 10 * z);
    p.x = std::max(4 * z, std::min(p.x, (float)a.width - size.x - 6 * z)); p.y = std::max(4 * z, std::min(p.y, (float)a.height - size.y - 6 * z));
    dl->AddRectFilled(p - ImVec2(4, 4) * z, p + size + ImVec2(4, 4) * z, col::black);
    dl->AddRectFilled(p - ImVec2(2, 2) * z, p + size + ImVec2(2, 2) * z, col::pnHi);
    dl->AddRectFilled(p, p + size, IM_COL32(18, 12, 8, 245));
    float y = p.y + 5 * z;
    for (auto& [t, c] : lines) {
      std::string s = t; float x = p.x + 8 * z;
      if (!s.empty() && s[0] == '\x01') { u32 dc = (u32)std::strtoul(s.c_str() + 2, nullptr, 16); float b = 10 * z; dl->AddRectFilled(ImVec2(x, y + 3 * z), ImVec2(x + b, y + 3 * z + b), col::rgb(dc)); dl->AddRect(ImVec2(x, y + 3 * z), ImVec2(x + b, y + 3 * z + b), col::black); x += b + 4 * z; s = s.substr(9); }
      text(dl, F_BODY, ImVec2(x, y), col::rgb(c), s.c_str(), maxW, k);
      y += textSize(F_BODY, s.c_str(), maxW, k).y;
    }
  }

  // ---- recipe book / creative catalogue ------------------------------------------------------------------
  void drawBook(App& a, ImDrawList* dl) {
    BookState& B = book(); float z = Z();
    panelBg(dl, bookA, bookB);
    float pad = is * 0.22f + 3 * z;
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
    for (auto& [k, l] : BOOK_CATS) {
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
    float detailH = is * 1.6f;
    ImVec2 l0(c.x, y), l1(c.x + w, bookB.y - pad - detailH - 4 * z);
    int perRow = std::max(1, (int)(w / is));
    std::string q = lower(B.query);
    Bags bags = sess->bags();
    struct E { HK k; int idx; std::string key; ItemStack st; int look; };   // look: 0 dim, 1 ok, 2 table, 3 catalogue
    std::vector<E> list;
    if (creative) {
      for (auto& d : reg().items) {
        if (d.has("hidden") || d.key.empty()) continue;
        if (B.cat != "all" && creativeCat(d) != B.cat) continue;
        if (!q.empty() && lower(d.name).find(q) == std::string::npos) continue;
        list.push_back({H_CI, 0, d.key, ItemStack(d.key, 1), 3});
      }
    } else {
      const auto& R = reg().recipes;
      for (size_t i = 0; i < R.size(); i++) {
        const Recipe& r = R[i];
        if (B.cat != "all" && recipeCategory(r) != B.cat) continue;
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
    if (creative) text(dl, F_BODY, d0, col::dim, "Creative: click an item for a full stack (shift puts it straight in your bag). Drop things outside the panel to get rid of them.", w, dsc);
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

  // ---- the mouse (invui.js pointer events) ----------------------------------------------------------------
  void mouse(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    if (a.input.usingPad()) return;
    ImVec2 m = io.MousePos;
    bool overWidget = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();
    const Hit* h = nullptr; for (auto& x : hits) if (x.contains(m)) h = &x;
    if (h && h->k == H_SLOT) { hasFocus = true; focus = *h; }
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
