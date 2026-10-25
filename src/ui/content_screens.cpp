// ui/content_screens.cpp — waystones (travel.js WayMenu), the stable (stable.js StableUI) and the journal of
// places. Plain ImGui; controllers: d-pad / stick moves the selection, A presses it, B closes.
#include "ui/content_screens.h"
#include "ui/ui.h"
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
    beginCard(a, "##waystones", 520, 460);
    ImGui::Text("%s", hereName.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 140); ImGui::TextDisabled("You have %d level%s", p.level, p.level == 1 ? "" : "s");
    ImGui::Separator();
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
    ImGui::Separator();
    ImGui::TextDisabled("The further, the more it costs (a level for every 600 blocks, at most 5) \xC2\xB7 E to close");
    ImGui::End();
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
  void draw(App& a) override {
    GameSession* s = sess(a);
    if (!s) return;
    Level& lv = *s->level; Player& p = *s->player;
    if (lv.isRemote() && g_haveRemote) list = g_remoteList;
    beginCard(a, "##stable", 640, 560);
    ImGui::Text("Your Stable");
    ImGui::SameLine(ImGui::GetWindowWidth() - 160);
    if (lv.isRemote() && !g_haveRemote) ImGui::TextDisabled("Calling\xE2\x80\xA6");
    else ImGui::TextDisabled("%d companion%s", (int)list.size(), list.size() == 1 ? "" : "s");
    ImGui::Separator();
    if ((!lv.isRemote() || g_haveRemote) && list.empty())
      ImGui::TextWrapped("No companions yet. Tame an animal, raise a drake from a dragon egg, or breed your pets, and they join your stable.");
    std::vector<StableEntry> mounts, pets;
    for (auto& c : list) (c.mount ? mounts : pets).push_back(c);
    auto sorter = [](const StableEntry& x, const StableEntry& y) { return x.fav != y.fav ? x.fav : x.name < y.name; };
    std::sort(mounts.begin(), mounts.end(), sorter); std::sort(pets.begin(), pets.end(), sorter);
    // the buttons, in order, for the controller
    nButtons = 0;
    bool pressed = pad.step(a, lastButtons);
    ImGui::BeginChild("##list", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 2.6f), ImGuiChildFlags_NavFlattened); ui::padScrollHere();   // a pad reaches the buttons below too
    for (auto* group : {&mounts, &pets}) {
      if (group->empty()) continue;
      ImGui::SeparatorText(group == &mounts ? "Mounts" : "Pets");
      for (auto& c : *group) card(a, lv, p, c, pressed);
    }
    ImGui::EndChild();
    lastButtons = nButtons;
    ImGui::Separator();
    ImGui::TextWrapped("H or a whistle opens this \xC2\xB7 Summon brings it to your side, wherever it is \xC2\xB7 Dismiss sends it home \xC2\xB7 \xE2\x98\x85 marks your favourite mount: sneak + H calls it or sends it home");
    ImGui::End();
    if (!pendingOp.empty()) { std::string op = pendingOp, uid = pendingUid; json extra = pendingExtra; pendingOp.clear(); act(a, op, uid, extra); }
  }
private:
  std::vector<StableEntry> list;
  double t = 1;
  bool editing = false;
  std::string confirm, pendingOp, pendingUid; json pendingExtra;
  std::unordered_map<std::string, std::string> names;
  PadSel pad; int nButtons = 0, lastButtons = 0;

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
  bool btn(const std::string& label, bool pressed, bool primary = false, bool disabled = false) {
    int i = nButtons++;
    bool sel = pad.sel == i;
    return selButton(label, sel, primary, disabled) || (pressed && sel && !disabled);
  }
  void card(App& a, Level& lv, Player& p, const StableEntry& c, bool pressed) {
    ImGui::PushID(c.uid.c_str());
    GLuint face = creatureFace(a, c.type, c.variant);
    if (face) ImGui::Image((ImTextureID)(intptr_t)face, ImVec2(40, 40)); else ImGui::Dummy(ImVec2(40, 40));
    ImGui::SameLine();
    ImGui::BeginGroup();
    std::string& nm = names[c.uid];
    if (!editing) nm = c.named ? c.name : "";
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##name", c.name.c_str(), &nm);
    if (ImGui::IsItemActivated()) editing = true;
    if (ImGui::IsItemDeactivated()) { editing = false; if (nm != (c.named ? c.name : "")) { pendingOp = "rename"; pendingUid = c.uid; pendingExtra = {{"name", nm}}; } }
    ImGui::TextDisabled("%s%s%s%s", c.kind.c_str(), c.baby ? " \xC2\xB7 young" : "", c.saddled ? " \xC2\xB7 saddled" : "", c.mount ? " \xC2\xB7 mount" : "");
    ImGui::ProgressBar(clampv(c.hp / std::max(1.f, c.maxHp), 0.f, 1.f), ImVec2(200, 6), "");
    if (c.state == "resting") ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.4f, 1), "Resting \xC2\xB7 %d:%02d", c.rest / 60, c.rest % 60);
    else if (c.state == "stabled") ImGui::TextUnformatted("In the stable");
    else if (c.here) { int d = (int)std::lround(std::hypot(c.x - p.pos.x, c.z - p.pos.z)); ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "%s", d < 12 ? "With you" : ("Out \xC2\xB7 " + std::to_string(d) + " m away").c_str()); }
    else ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "Out%s", c.dim != jsDimOf(p) ? (" in " + jsDimName(c.dim)).c_str() : "");
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::BeginGroup();
    bool out = c.state == "out", rest = c.state == "resting";
    if (c.mount) { if (btn(c.fav ? "\xE2\x98\x85##fav" : "\xE2\x98\x86##fav", pressed)) { pendingOp = "fav"; pendingUid = c.uid; } ImGui::SameLine(); }
    if (out && c.here) {
      if (btn("Here!", pressed)) { confirm.clear(); pendingOp = "summon"; pendingUid = c.uid; }
      ImGui::SameLine(); if (btn("Dismiss", pressed)) { confirm.clear(); pendingOp = "dismiss"; pendingUid = c.uid; }
    } else if (out) {
      if (btn("Summon", pressed, true)) { confirm.clear(); pendingOp = "summon"; pendingUid = c.uid; }
      ImGui::SameLine(); if (btn("Dismiss", pressed)) { confirm.clear(); pendingOp = "dismiss"; pendingUid = c.uid; }
    } else if (btn("Summon", pressed, true, rest)) { confirm.clear(); pendingOp = "summon"; pendingUid = c.uid; }
    ImGui::SameLine();
    if (btn(confirm == c.uid ? "Sure?" : "Release", pressed)) { if (confirm == c.uid) { confirm.clear(); pendingOp = "release"; pendingUid = c.uid; } else confirm = c.uid; }
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::PopID();
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
    beginCard(a, "##places", 560, 520);
    ImGui::Text("Places you've found");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120); ImGui::TextDisabled("%d in all", (int)all.size());
    ImGui::Separator();
    if (all.empty()) ImGui::TextWrapped("Nothing yet. Walk into a village, find a dungeon or one of the far places of the Skylands and the Depths, and it goes in here (and on your maps).");
    static const char* KINDS[5][2] = {{"village", "Villages"}, {"place", "Places"}, {"dungeon", "Dungeons"}, {"waystone", "Waystones"}, {"home", "Your houses"}};
    ImGui::BeginChild("##pl", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 1.6f), ImGuiChildFlags_NavFlattened); ui::padScrollHere();
    for (auto& K : KINDS) {
      std::vector<FoundPlace> g;
      for (auto& f : all) if (f.kind == K[0]) g.push_back(f);
      if (g.empty()) continue;
      std::sort(g.begin(), g.end(), [&](const FoundPlace& x, const FoundPlace& y) { return std::hypot(x.x - p.pos.x, x.z - p.pos.z) < std::hypot(y.x - p.pos.x, y.z - p.pos.z); });
      ImGui::SeparatorText(K[1]);
      for (auto& f : g) {
        ImGui::TextUnformatted((f.name + (f.cleared ? " (cleared)" : "")).c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.55f);
        if (f.dim != dim) ImGui::TextDisabled("in %s", jsDimName(f.dim).c_str());
        else ImGui::TextDisabled("%d blocks %s \xC2\xB7 x %d, z %d", (int)std::lround(std::hypot(f.x - p.pos.x, f.z - p.pos.z)), dirWord(f.x - p.pos.x, f.z - p.pos.z).c_str(), (int)f.x, (int)f.z);
      }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("They're on your maps too \xC2\xB7 E or Esc to close");
    ImGui::End();
  }
private:
  PadSel in;
};
}

std::unique_ptr<Screen> makeWaystoneScreen(const std::string& hereId) { return std::make_unique<WaystoneScreen>(hereId); }
std::unique_ptr<Screen> makeStableScreen() { g_haveRemote = false; return std::make_unique<StableScreen>(); }
std::unique_ptr<Screen> makePlacesScreen() { return std::make_unique<PlacesScreen>(); }
void stableListArrived(const json& l) { g_remoteList = stableListFromJson(l); g_haveRemote = true; }

} // namespace dq
