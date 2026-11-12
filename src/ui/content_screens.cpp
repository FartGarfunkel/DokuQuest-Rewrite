// ui/content_screens.cpp — waystones (travel.js WayMenu), the stable (stable.js StableUI) and the journal of
// places. Plain ImGui; controllers: d-pad / stick moves the selection, A presses it, B closes.
#include "ui/content_screens.h"
#include "ui/ui.h"
#include "ui/book.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "render/content_draw.h"
#include "sim/travel.h"
#include "sim/stable.h"
#include "sim/places.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {
namespace ui { void padScrollHere(); }   // ui/theme.cpp: the right stick scrolls

namespace {
GameSession* sess(App& a) { return a.session && a.session->player ? a.session.get() : nullptr; }
void beginCard(App& a, const char* id, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(w, a.width - 30.f), std::min(h, a.height - 30.f)));
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
}
// d-pad / stick selection over n buttons; returns true when A pressed the selected one
struct PadSel {
  int sel = 0;
  bool step(App& a, int n) {
    if (n <= 0) return false;
    Input& in = a.input;
    if (in.pressed(Act::MenuDown) || in.pressed(Act::MenuRight)) sel = (sel + 1) % n;
    if (in.pressed(Act::MenuUp) || in.pressed(Act::MenuLeft)) sel = (sel + n - 1) % n;
    sel = clampv(sel, 0, n - 1);
    return in.pressed(Act::MenuOk);
  }
};
bool selButton(const std::string& label, bool selected, bool primary = false, bool disabled = false) {
  if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
  else if (primary) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32f, 0.5f, 0.24f, 1));
  ImGui::BeginDisabled(disabled);
  bool r = ImGui::Button(label.c_str());
  ImGui::EndDisabled();
  if (selected || primary) ImGui::PopStyleColor();
  return r;
}

// ------------------------------------------------------------------ waystones
class WaystoneScreen : public Screen {
public:
  explicit WaystoneScreen(std::string here) : here(std::move(here)) {}
  const char* id() const override { return "waystones"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override { if (a.input.pressed(Act::Inventory)) a.popScreen(); }
  void draw(App& a) override {
    GameSession* s = sess(a);
    if (!s) return;
    Level& lv = *s->level; Player& p = *s->player;
    auto list = wayList(lv, p, here);
    const json W = p.stats.value("ways", json::object());
    std::string hereName = W.contains(here) ? W[here].value("name", std::string("Waystone")) : "Waystone";
    ui::bookPagesBegin(a, "##waystones");   // (written in the book, like the inventory)
    ui::bookPageTitle(a, hereName.c_str());
    ImGui::TextDisabled("You have %d level%s", p.level, p.level == 1 ? "" : "s");
    ImGui::Dummy(ImVec2(1, 4 * ui::Z()));
    bool pressed = pad.step(a, (int)list.size());
    std::string go;
    if (list.empty()) ImGui::TextWrapped("No other waystones yet. Use the waystone in each village you find (or one you've made) to attune to it, and you can come and go between them.");
    for (int i = 0; i < (int)list.size(); i++) {
      auto& o = list[i];
      ImGui::PushID(i);
      ImGui::BeginGroup();
      ImGui::TextColored(o.can ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", o.name.c_str());
      ImGui::TextDisabled("%d blocks %s", (int)std::lround(o.dist), o.dir.c_str());
      ImGui::EndGroup();
      ImGui::SameLine(ImGui::GetWindowWidth() - 130);
      std::string lbl = (i < 9 ? std::to_string(i + 1) + " \xC2\xB7 " : std::string()) + (creativeOf(p) ? "Go" : std::to_string(o.cost) + " level" + (o.cost > 1 ? "s" : ""));
      if (selButton(lbl, pad.sel == i, o.can) || (pressed && pad.sel == i)) go = o.id;
      if (i < 9 && ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i))) go = o.id;
      ImGui::PopID();
    }
    ui::bookPagesRight("##waystones-r");
    ui::bookPageTitle(a, "Travel");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Each waystone you've attuned to is one you can step to from any other. Use the waystone in every village you find (or one you've made) to attune to it.");
    ImGui::Dummy(ImVec2(1, 6 * ui::Z()));
    ImGui::TextDisabled("The further, the more it costs (a level for every 600 blocks, at most 5) \xC2\xB7 E to close");
    ImGui::PopTextWrapPos();
    ui::bookPagesEnd();
    if (!go.empty()) { a.popScreen(); wayGo(lv, p, go); }
  }
private:
  std::string here;
  PadSel pad;
};

// ------------------------------------------------------------------ the stable
std::vector<StableEntry> g_remoteList; bool g_haveRemote = false;

// a little face for each companion, from its model's head tile (stable.js creatureFace)
GLuint creatureFace(App& a, const std::string& type, const std::string& variant) {
  static std::unordered_map<std::string, GLuint> faces;
  std::string key = type + "|" + variant;
  auto it = faces.find(key);
  if (it != faces.end()) return it->second;
  GLuint tex = 0;
  const json& T = reg().mobs.is_object() && reg().mobs.contains(type) ? reg().mobs[type] : json::object();
  std::string mk = !variant.empty() ? variant : T.value("model", type);
  const json& M = reg().models;
  if (M.is_object() && M.contains(mk) && M[mk].contains("parts")) {
    const json* part = nullptr;
    for (auto& p : M[mk]["parts"]) if (p.value("name", "") == (M[mk].value("rig", "") == "dragon" ? "snout" : "head")) { part = &p; break; }
    if (!part) for (auto& p : M[mk]["parts"]) if (p.value("role", "").find("head") != std::string::npos) { part = &p; break; }
    std::string tn;
    if (part && part->contains("tex")) {
      const json& t = (*part)["tex"];
      tn = t.is_string() ? t.get<std::string>() : t.is_object() ? t.value("front", t.value("all", t.value("side", std::string()))) : "";
    }
    auto ti = a.tiles.index.find(tn);
    if (!tn.empty() && ti != a.tiles.index.end()) tex = makeTexRGBA(a.tiles.size, a.tiles.size, a.tiles.tile(ti->second), true, false);
  }
  faces[key] = tex;
  return tex;
}

class StableScreen : public Screen {
public:
  const char* id() const override { return "stable"; }
  bool pausesGame() const override { return false; }
  void onOpen(App& a) override { refresh(a); }
  void update(App& a, double dt) override {
    if (inventoryClosePressed(a, true)) { a.popScreen(); return; }   // (E is "next tab" here)
    if (!editing && (t -= dt) <= 0) { t = 1; refresh(a); }
  }
  // the stable as a book (ui/book.h): a bookmark for the mounts and one for the pets, a card for each companion — its
  // face, its name (click to rename), health, where it is, and the pack's buttons. Controller: ↑ ↓ choose a companion,
  // ← → one of its buttons, A presses it, X the next bookmark.
  void draw(App& a) override {
    using namespace ui;
    GameSession* s = sess(a);
    if (!s) return;
    Level& lv = *s->level; Player& p = *s->player;
    if (lv.isRemote() && g_haveRemote) list = g_remoteList;
    std::vector<StableEntry> mounts, pets;
    for (auto& c : list) (c.mount ? mounts : pets).push_back(c);
    auto sorter = [](const StableEntry& x, const StableEntry& y) { return x.fav != y.fav ? x.fav : x.name < y.name; };
    std::sort(mounts.begin(), mounts.end(), sorter); std::sort(pets.begin(), pets.end(), sorter);
    const int n = (int)(mounts.size() + pets.size());
    book.active = !editing; book.keysTurn = false;
    book.begin(a, "##stable", {{"Mounts", "saddle"}, {"Pets", "lead"}});
    bool press = false;
    if (!editing && !a.input.textActive && n) {
      Input& in = a.input;
      if (cur < 0) cur = 0;
      if (in.pressed(Act::MenuDown)) { cur = (cur + 1) % n; btn = 0; book.showSel(cur); }
      if (in.pressed(Act::MenuUp)) { cur = (cur + n - 1) % n; btn = 0; book.showSel(cur); }
      if (in.pressed(Act::MenuRight)) btn++;
      if (in.pressed(Act::MenuLeft)) btn--;
      press = in.pressed(Act::MenuOk);
    }
    cur = std::clamp(cur, 0, std::max(0, n - 1));
    const bool calling = lv.isRemote() && !g_haveRemote;
    BookRows rm, rp;
    int idx = 0;
    for (auto* group : {&mounts, &pets}) {
      BookRows& r = group == &mounts ? rm : rp;
      book.heading(r, group == &mounts ? "Mounts" : "Pets");
      std::string count = calling ? "Calling\xE2\x80\xA6" : std::to_string(group->size()) + (group == &mounts ? (group->size() == 1 ? " mount" : " mounts") : (group->size() == 1 ? " pet" : " pets"));
      book.para(r, count, ink::muted, F_SMALL);
      if (group->empty() && !calling)
        book.para(r, group == &mounts ? "No mounts yet. Tame one, or raise a drake from a dragon egg, and it joins your stable."
                                      : "No pets yet. Tame an animal or breed your pets, and they join your stable.", ink::text);
      for (auto& c : *group) card(a, lv, p, c, idx++, r, press);
    }
    book.draw({rm, rp});
    if (book.turned && !book.shownSel.empty() && std::find(book.shownSel.begin(), book.shownSel.end(), cur) == book.shownSel.end()) { cur = book.shownSel.front(); btn = 0; }
    book.hint(a.input.usingPad() ? "\xE2\x86\x91 \xE2\x86\x93 choose \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 its buttons \xC2\xB7 A: press \xC2\xB7 X: mounts / pets \xC2\xB7 Summon brings it to you, wherever it is"
                                 : "H or a whistle opens this \xC2\xB7 Summon brings it to you, wherever it is \xC2\xB7 Dismiss sends it home \xC2\xB7 the star marks your favourite mount: Shift + H calls it");
    book.end();
    if (!pendingOp.empty()) { std::string op = pendingOp, uid = pendingUid; json extra = pendingExtra; pendingOp.clear(); act(a, op, uid, extra); }
  }
private:
  std::vector<StableEntry> list;
  double t = 1;
  bool editing = false;
  std::string confirm, pendingOp, pendingUid; json pendingExtra;
  std::unordered_map<std::string, std::string> names;
  ui::Book book;
  int cur = -1, btn = 0;

  void refresh(App& a) {
    GameSession* s = sess(a);
    if (!s) return;
    if (s->level->isRemote()) contentAction(*s->level, *s->player, "stable", {{"op", "list"}});
    else list = stableList(*s->level, s->player->name);
  }
  void act(App& a, const std::string& op, const std::string& uid, const json& extra) {
    GameSession* s = sess(a);
    if (!s) return;
    json args = extra.is_object() ? extra : json::object();
    args["op"] = op; args["uid"] = uid;
    if (s->level->isRemote()) { contentAction(*s->level, *s->player, "stable", args); return; }
    std::string msg;
    Level& lv = *s->level; Player& p = *s->player;
    if (op == "summon") msg = stableSummon(lv, p, uid);
    else if (op == "dismiss") msg = stableDismiss(lv, p, uid);
    else if (op == "rename") stableRename(lv, p, uid, args.value("name", ""));
    else if (op == "release") stableRelease(lv, p, uid);
    else if (op == "fav") stableFav(lv, p, uid);
    if (!msg.empty()) toast(lv, p, msg);
    refresh(a);
  }
  void card(App& a, Level& lv, Player& p, const StableEntry& c, int me, ui::BookRows& rows, bool press) {
    using namespace ui;
    const float z = Z(), lh = textSize(F_BODY, "Ag").y + 2 * z, face = std::round(lh * 2.6f), bh = std::round(22 * z);
    BookRow row; row.sel = me; row.h = std::max(face, lh * 4 + 6 * z) + bh + 16 * z;
    // what it says where it is
    std::string where; ImU32 wc = ink::good;
    if (c.state == "resting") { char b[48]; std::snprintf(b, sizeof b, "Resting \xC2\xB7 %d:%02d", c.rest / 60, c.rest % 60); where = b; wc = ink::bad; }
    else if (c.state == "stabled") { where = "In the stable"; wc = ink::muted; }
    else if (c.here) { int d = (int)std::lround(std::hypot(c.x - p.pos.x, c.z - p.pos.z)); where = d < 12 ? "With you" : "Out \xC2\xB7 " + std::to_string(d) + " m away"; }
    else where = std::string("Out") + (c.dim != jsDimOf(p) ? " in " + jsDimName(c.dim) : "");
    std::string kind = c.kind + (c.baby ? " \xC2\xB7 young" : "") + (c.saddled ? " \xC2\xB7 saddled" : "");
    GLuint faceTex = creatureFace(a, c.type, c.variant);
    row.draw = [this, c, me, z, lh, face, bh, where, wc, kind, faceTex, press](ImDrawList* dl, ImVec2 q, float w) {
      const bool sel = me == cur;
      const float h = std::max(face, lh * 4 + 6 * z) + bh + 10 * z;
      if (sel) dl->AddRectFilled(ImVec2(q.x - 4 * z, q.y - 2 * z), ImVec2(q.x + w + 3 * z, q.y + h), ink::hilite, 3 * z);
      // the face in a little frame
      dl->AddRectFilled(q, ImVec2(q.x + face, q.y + face), col::rgb(0x2a1a10));
      if (faceTex) dl->AddImage((ImTextureID)(intptr_t)faceTex, ImVec2(q.x + 2 * z, q.y + 2 * z), ImVec2(q.x + face - 2 * z, q.y + face - 2 * z));
      if (c.fav) drawStar(dl, ImVec2(q.x + face - 3 * z, q.y + 3 * z), 7 * z, col::rgb(0xe2b84f), col::rgb(0x5a3a08));
      const float x = q.x + face + 8 * z, tw = w - face - 8 * z;
      // the name: a field on the paper
      std::string& nm = names[c.uid];
      if (!editing) nm = c.named ? c.name : "";
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ink::field); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ink::fieldHi);
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ink::fieldHi); ImGui::PushStyleColor(ImGuiCol_Text, ink::head);
      ImGui::PushStyleColor(ImGuiCol_TextDisabled, ink::muted); ImGui::PushStyleColor(ImGuiCol_Border, col::rgb(0x8a6a48));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
      ImGui::PushFont(font(F_BODY));
      ImGui::SetCursorScreenPos(ImVec2(x, q.y));
      ImGui::SetNextItemWidth(std::min(tw, 240 * z));
      ImGui::InputTextWithHint("##name", c.name.c_str(), &nm);
      if (ImGui::IsItemActivated()) editing = true;
      if (ImGui::IsItemDeactivated()) { editing = false; if (nm != (c.named ? c.name : "")) { pendingOp = "rename"; pendingUid = c.uid; pendingExtra = {{"name", nm}}; } }
      ImGui::PopFont(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(6);
      float y = q.y + lh + 6 * z;
      text(dl, F_SMALL, ImVec2(x, y), ink::muted, kind.c_str()); y += lh * 0.9f;
      // health
      const float hp = std::clamp(c.hp / std::max(1.f, c.maxHp), 0.f, 1.f);
      ImVec2 b0(x, y + 2 * z), b1(x + std::min(tw, 160 * z), y + 7 * z);
      dl->AddRectFilled(b0, b1, ink::faint); dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * hp, b1.y), col::rgb(0xa8322a));
      dl->AddRect(b0, b1, withAlpha(ink::text, 0.5f), 0, 0, std::max(1.f, z));
      char hb[32]; std::snprintf(hb, sizeof hb, "%d / %d", (int)std::ceil(c.hp), (int)c.maxHp);
      text(dl, F_SMALL, ImVec2(b1.x + 6 * z, y - 1 * z), ink::muted, hb); y += lh * 0.8f;
      text(dl, F_BODY, ImVec2(x, y), wc, where.c_str());
      // the buttons (the pack's): favourite, summon or here, dismiss, release
      const bool out = c.state == "out", rest = c.state == "resting";
      struct B { std::string label, op; bool off; };
      std::vector<B> bs;
      if (c.mount) bs.push_back({"##fav", "fav", false});   // a star drawn on it (filled: the favourite)
      bs.push_back({out && c.here ? "Here!" : "Summon", "summon", !out && rest});
      if (out) bs.push_back({"Dismiss", "dismiss", false});
      bs.push_back({confirm == c.uid ? "Sure?" : "Release", "release", false});
      if (sel) btn = std::clamp(btn, 0, (int)bs.size() - 1);
      float bx = q.x, by = q.y + std::max(face, lh * 4 + 6 * z) + 4 * z;
      for (int i = 0; i < (int)bs.size(); i++) {
        const float bw = bs[i].op == "fav" ? bh * 1.6f : std::round(std::max(72 * z, textSize(F_MID, bs[i].label.c_str()).x + 22 * z));
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        bool hit = classicButton(bs[i].label.c_str(), ImVec2(bw, bh), bs[i].off);
        if (bs[i].op == "fav") drawStar(dl, ImVec2(bx + bw * 0.5f, by + bh * 0.5f), bh * 0.32f, c.fav ? col::rgb(0xffd860) : 0, c.fav ? col::rgb(0x5a3a08) : col::rgb(0xe0e0e0));
        if (sel && i == btn) dl->AddRect(ImVec2(bx - 2 * z, by - 2 * z), ImVec2(bx + bw + 2 * z, by + bh + 2 * z), ink::key, 2 * z, 0, 2 * z);
        if (sel && i == btn && press && !bs[i].off) { hit = true; playClick(); }
        if (hit) {
          cur = me;
          if (bs[i].op == "release") { if (confirm == c.uid) { confirm.clear(); pendingOp = "release"; pendingUid = c.uid; } else confirm = c.uid; }
          else { confirm.clear(); pendingOp = bs[i].op; pendingUid = c.uid; }
        }
        bx += bw + 6 * z;
      }
    };
    rows.push_back(std::move(row));
  }
};

// ------------------------------------------------------------------ the journal of places
class PlacesScreen : public Screen {
public:
  const char* id() const override { return "places"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override { if (a.input.pressed(Act::Inventory)) a.popScreen(); }
  void draw(App& a) override {
    GameSession* s = sess(a);
    if (!s) return;
    Level& lv = *s->level; Player& p = *s->player;
    auto all = foundPlaces(lv, p);
    int dim = jsDimOf(p);
    ui::bookPagesBegin(a, "##places");   // (written in the book, like the inventory)
    ui::bookPageTitle(a, "Places you've found");
    ImGui::TextDisabled("%d in all", (int)all.size());
    if (all.empty()) ImGui::TextWrapped("Nothing yet. Walk into a village, find a dungeon or one of the far places of the Skylands and the Depths, and it goes in here (and on your maps).");
    static const char* KINDS[5][2] = {{"village", "Villages"}, {"place", "Places"}, {"dungeon", "Dungeons"}, {"waystone", "Waystones"}, {"home", "Your houses"}};
    ImGui::BeginChild("##pl", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground); ui::padScrollHere();
    for (auto& K : KINDS) {
      std::vector<FoundPlace> g;
      for (auto& f : all) if (f.kind == K[0]) g.push_back(f);
      if (g.empty()) continue;
      std::sort(g.begin(), g.end(), [&](const FoundPlace& x, const FoundPlace& y) { return std::hypot(x.x - p.pos.x, x.z - p.pos.z) < std::hypot(y.x - p.pos.x, y.z - p.pos.z); });
      ImGui::SeparatorText(K[1]);
      for (auto& f : g) {
        ImGui::TextUnformatted((f.name + (f.cleared ? " (cleared)" : "")).c_str());
        ImGui::Indent(8 * ui::Z());
        if (f.dim != dim) ImGui::TextDisabled("in %s", jsDimName(f.dim).c_str());
        else ImGui::TextDisabled("%d blocks %s \xC2\xB7 x %d, z %d", (int)std::lround(std::hypot(f.x - p.pos.x, f.z - p.pos.z)), dirWord(f.x - p.pos.x, f.z - p.pos.z).c_str(), (int)f.x, (int)f.z);
        ImGui::Unindent(8 * ui::Z());
      }
    }
    ImGui::EndChild();
    ui::bookPagesRight("##places-r");
    ui::bookPageTitle(a, "Your travels");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Every village, dungeon and far place you've walked into, nearest first. They're marked on your maps too.");
    ImGui::Dummy(ImVec2(1, 6 * ui::Z())); ImGui::TextDisabled("E or Esc to close");
    ImGui::PopTextWrapPos();
    ui::bookPagesEnd();
  }
private:
  PadSel in;
};
}

std::unique_ptr<Screen> makeWaystoneScreen(const std::string& hereId) { return std::make_unique<WaystoneScreen>(hereId); }
// ---- BTA's label: a name written on it (sim/bta_items.cpp names a creature with it) ----
namespace {
class LabelScreen : public Screen {
public:
  explicit LabelScreen(int slot) : slot(slot) {}
  const char* id() const override { return "label"; }
  bool pausesGame() const override { return false; }
  void draw(App& a) override {
    using namespace ui;
    GameSession* s = sess(a);
    if (!s || slot < 0 || slot >= (int)s->player->inv.size() || s->player->inv[slot].id != "label") { a.popScreen(); return; }
    const float z = Z(), w = 360 * z, h = 150 * z;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##label", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    label(F_BTN, col::rgb(0xf0e0b8), "Write a name on the label");
    ImGui::Dummy(ImVec2(1, 6 * z));
    if (first) { ImGui::SetKeyboardFocusHere(); first = false; }
    ImGui::SetNextItemWidth(w - 20 * z);
    bool enter = ImGui::InputTextWithHint("##name", "A name (up to 32 letters)", &name, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(1, 8 * z));
    bool ok = classicButton("Write it##ok", ImVec2(120 * z, 24 * z)) || enter;
    ImGui::SameLine(); bool cancel = classicButton("Cancel##no", ImVec2(120 * z, 24 * z));
    ImGui::End();
    if (ok && !name.empty()) {
      ItemStack& st = s->player->inv[slot];
      if (!st.tag.is_object()) st.tag = json::object();
      st.tag["name"] = name.substr(0, 32);
      a.popScreen(); return;
    }
    if (cancel) a.popScreen();
  }
private:
  int slot; std::string name; bool first = true;
};
}
std::unique_ptr<Screen> makeLabelScreen(int slot) { return std::make_unique<LabelScreen>(slot); }

std::unique_ptr<Screen> makeStableScreen() { g_haveRemote = false; return std::make_unique<StableScreen>(); }
std::unique_ptr<Screen> makePlacesScreen() { return std::make_unique<PlacesScreen>(); }
void stableListArrived(const json& l) { g_remoteList = stableListFromJson(l); g_haveRemote = true; }

} // namespace dq
