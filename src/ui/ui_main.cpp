// ui/ui_main.cpp — start-up of the UI: theme, the HUD render layer, EventBus listeners, menu tabs,
// the --screen test hook, and small shared helpers.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include <imgui.h>

namespace dq {

void registerHudLayer();            // ui/hud.cpp
void hudInitEvents(App& a);         // ui/hud.cpp
void registerInventoryTab();        // ui/inventory.cpp

static bool g_startupDone = false;
static std::vector<std::function<void(App&)>> g_deferred;
void uiDefer(std::function<void(App&)> f) { g_deferred.push_back(std::move(f)); }

void registerUi(App& a) {
  ui::init(a);
  registerHudLayer();
  hudInitEvents(a);
  registerInventoryTab();
}

std::unique_ptr<Screen> makeScreenByName(App& a, const std::string& n) {
  if (n == "worlds") return makeWorldsScreen();
  if (n == "options" || n == "settings") return makeOptionsScreen();
  if (n == "controls") return makeControlsScreen();
  if (n == "padsetup") return makePadSetupScreen();
  if (n == "howto") return makeHowToScreen();
  if (n == "news") return makeNewsScreen();
  if (n == "players") return makePlayersScreen();
  if (n == "multiplayer") return makeMultiplayerScreen();
  if (n == "lanhost") return makeLanHostScreen();
  if (n == "wsettings") return makeWorldSettingsScreen({});
  if (n == "pause") return makePauseScreen();
  if (n == "chat") return makeChatScreen("/");
  if (n == "death") return makeDeathScreen("You fell from a high place", 3);
  if (n == "comingsoon") return makeComingSoonScreen("Skin editor", "Making your own skin comes with the character update.");
  InvOpen o;
  if (n == "inventory" || n == "creative") return makeInventoryScreen(o);
  if (n == "table") { o.mode = "table"; return makeInventoryScreen(o); }
  return nullptr;
}

// test hook: --screen <name> opens a menu once the title (or the world) is up; in-world screens get a few
// sample items so screenshots show something
static void startupScreen(App& a) {
  if (g_startupDone || a.args.screen.empty() || a.frame < 3) return;
  g_startupDone = true;
  const std::string& n = a.args.screen;
  if (a.session && a.session->player) {
    Player& p = *a.session->player;
    if (n == "creative") { p.mode = "creative"; a.session->level->info.mode = "creative"; }
    static const std::pair<const char*, int> sample[] = {{"oak_log", 12}, {"planks", 40}, {"cobblestone", 64}, {"torch", 16}, {"iron_pickaxe", 1}, {"apple", 5},
      {"coal", 9}, {"iron_ore", 6}, {"stick", 7}, {"iron_helmet", 1}, {"bread", 3}, {"red_dye", 2}, {"wool", 4}};
    int i = 0;
    for (auto& [k, c] : sample) if (reg().item(k)) { ItemStack s(k, c); if (k == std::string("iron_pickaxe")) s.d = 90; p.inv[i < 9 ? i : i + 3] = s; i++; }
    if (n == "furnace" || n == "chest") {
      // put the block in front of the player and open it
      ivec3 bp = ivec3(glm::floor(p.pos)) + ivec3(0, 0, -2);
      World& w = a.session->world();
      w.setBlock(bp.x, bp.y, bp.z, reg().blockId(n == "furnace" ? "furnace" : "chest"), 0);
      InvOpen o; o.mode = n == "furnace" ? "furnace" : "container"; o.hasPos = true; o.pos = bp;
      if (n == "furnace") { json* e = w.blockEntity(bp.x, bp.y, bp.z, true); *e = {{"type", "furnace"}, {"items", {ItemStack("iron_ore", 5).toJson(), ItemStack("coal", 3).toJson(), ItemStack("iron_ingot", 2).toJson()}}, {"burn", 20}, {"burnMax", 40}, {"cook", 2.5}}; }
      a.pushScreen(makeInventoryScreen(o));
      return;
    }
  }
  if (auto s = makeScreenByName(a, n)) a.pushScreen(std::move(s));
}

void uiBeginFrame(App& a) {
  ui::beginFrame(a);
  auto run = std::move(g_deferred); g_deferred.clear();
  for (auto& f : run) f(a);
  startupScreen(a);
}

bool backConsumedByText() { return ImGui::GetIO().WantTextInput; }

json ruleDefaults(bool creative) {
  return {{"dayMinutes", 10}, {"nightMinutes", 10}, {"daylightCycle", true}, {"keepInventory", false}, {"graves", true}, {"mobSpawning", true},
    {"mobGriefing", true}, {"tntExplodes", true}, {"fireSpread", true}, {"blockDrops", true}, {"fallDamage", true}, {"drowning", true},
    {"fireDamage", true}, {"naturalRegen", false}, {"grassSpread", true}, {"saplingsPlant", true}, {"showCoords", false}, {"commands", creative},
    {"downed", true}, {"_v", 1}};
}

void throwStackFromPlayer(App& a, const ItemStack& s) {
  if (s.empty() || !a.session || !a.session->player) return;
  Player& p = *a.session->player;
  vec3 dir(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  vec3 at = p.pos + vec3(0, 1.3f, 0), vel = dir * 4.f + vec3(0, 1.5f, 0);
  // the gameplay side drops it (and returns false to say it did); otherwise a plain "item" entity carries it
  if (!a.events.emit("throwStack", json::array({p.name, s.toJson(), at.x, at.y, at.z, vel.x, vel.y, vel.z}))) return;
  auto e = createEntity("item");
  e->type = "item"; e->realm = p.realm; e->pos = at; e->vel = vel; e->w = 0.125f; e->h = 0.25f;
  e->data["stack"] = s.toJson(); e->data["pickupDelay"] = 1.5;
  a.session->level->spawn(std::move(e));
}

// ---- menu tabs (game/menutabs.js) ---------------------------------------------------------------------------
static std::vector<MenuTab>& tabs() { static std::vector<MenuTab> t; return t; }
void registerMenuTab(MenuTab t) {
  static const char* order[] = {"inventory", "map", "journal", "skills", "spells", "stable"};
  auto rank = [](const std::string& k) { for (int i = 0; i < 6; i++) if (k == order[i]) return i; return 6; };
  auto& v = tabs();
  for (auto& o : v) if (o.key == t.key) { o = std::move(t); return; }
  auto it = std::find_if(v.begin(), v.end(), [&](const MenuTab& o) { return rank(o.key) > rank(t.key); });
  v.insert(it, std::move(t));
}
const std::vector<MenuTab>& menuTabs() { return tabs(); }

static int g_tabClick = -1;
static void tabGo(App& a, int cur, int to, int dir) {
  auto& v = tabs(); int n = (int)v.size();
  a.popScreen();
  for (int k = 0; k < n; k++, to = ((to + dir) % n + n) % n) {
    if (to == cur || (v[to].can && !v[to].can(a))) continue;
    size_t before = a.screens.size();
    v[to].open(a);
    if (a.screens.size() > before) { ui::playClick(); return; }
  }
  v[cur].open(a);   // nothing else opened: stay
}
bool menuTabsUpdate(App& a, const std::string& current) {
  auto& v = tabs(); int n = (int)v.size(), cur = -1;
  for (int i = 0; i < n; i++) if (v[i].key == current) cur = i;
  if (cur < 0 || n < 2) { g_tabClick = -1; return false; }
  if (g_tabClick >= 0 && g_tabClick != cur) { int to = g_tabClick; g_tabClick = -1; tabGo(a, cur, to, 1); return true; }
  g_tabClick = -1;
  bool lb = a.input.pressed(Act::MenuTabLeft), rb = a.input.pressed(Act::MenuTabRight);
  if (lb || rb) { int d = rb ? 1 : -1; tabGo(a, cur, ((cur + d) % n + n) % n, d); return true; }
  return false;
}
void menuTabsDraw(App& a, const std::string& current) {
  auto& v = tabs(); if (v.size() < 2) return;
  using namespace ui;
  float z = Z();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  // .menu-tabs: top centre, LB · tabs · RB, 600 13px
  std::vector<float> w; float total = 0, gap = 4 * z;
  auto pill = [&](const char* t, float padX) { return textSize(F_MID, t).x + padX * 2; };
  float lbw = pill("LB", 7 * z), rbw = pill("RB", 7 * z);
  for (auto& t : v) { w.push_back(pill(t.name.c_str(), 10 * z)); total += w.back() + gap; }
  total += lbw + rbw + gap;
  float x = std::round(a.width * 0.5f - total * 0.5f), y = 6 * z, h = textSize(F_MID, "Ag").y + 10 * z;
  auto drawPill = [&](const char* t, float wpx, ImU32 bg, ImU32 border, ImU32 fg, float hh, float r) {
    ImVec2 p0(x, y + (h - hh) * 0.5f), p1(x + wpx, y + (h + hh) * 0.5f);
    dl->AddRectFilled(p0, p1, bg, r); if (border) dl->AddRect(p0, p1, border, r);
    ImVec2 ts = textSize(F_MID, t); text(dl, F_MID, ImVec2(p0.x + (wpx - ts.x) * 0.5f, p0.y + (p1.y - p0.y - ts.y) * 0.5f), fg, t);
    bool hit = ImGui::IsMouseHoveringRect(p0, p1) && ImGui::IsMouseClicked(0);
    x += wpx + gap; return hit;
  };
  drawPill("LB", lbw, col::rgb(0x333333), 0, col::white, h - 4 * z, 5 * z);
  for (size_t i = 0; i < v.size(); i++) {
    bool on = v[i].key == current, off = v[i].can && !v[i].can(a);
    ImU32 bg = on ? col::rgb(0xc9a24a) : col::rgb(0x140e08, 217), br = on ? col::rgb(0xf0d58a) : col::rgb(0x6b5a3a), fg = on ? col::rgb(0x1a1208) : col::rgb(0xd8c9a8);
    if (off) { bg = withAlpha(bg, 0.45f); br = withAlpha(br, 0.45f); fg = withAlpha(fg, 0.45f); }
    if (drawPill(v[i].name.c_str(), w[i], bg, br, fg, h, 6 * z)) g_tabClick = (int)i;
  }
  drawPill("RB", rbw, col::rgb(0x333333), 0, col::white, h - 4 * z, 5 * z);
}

} // namespace dq
