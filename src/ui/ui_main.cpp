// ui/ui_main.cpp — start-up of the UI: theme, the HUD render layer, EventBus listeners, menu tabs,
// the --screen test hook, and small shared helpers.
#include "ui/screen.h"
#include <imgui_internal.h>
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/magic_screens.h"
#include "ui/content_screens.h"
#include <imgui.h>
#include <cmath>

namespace dq {

void registerHudLayer();            // ui/hud.cpp
void hudInitEvents(App& a);         // ui/hud.cpp
void registerInventoryTab();        // ui/inventory.cpp

static bool g_startupDone = false;
static std::vector<std::function<void(App&)>> g_deferred;
void uiDefer(std::function<void(App&)> f) { g_deferred.push_back(std::move(f)); }

// The simulation asks for a screen with EventBus "screenOpen" [player, kind, opts] (sim/gameplay openScreen):
// chests, barrels, shulker boxes, hoppers, the ender chest, backpacks -> container; crafting table -> table;
// furnaces; anvil / grindstone / smithing table / loom / fletching table -> station {kind}; the stonecutter;
// millstone / sawmill / crusher -> machine; signs. (Cart and boat chests, opts.entity, are the vehicles' own.)
static void onScreenOpen(App& a, const json& args) {
  if (!args.is_array() || args.size() < 2 || !args[1].is_string() || !a.session || !a.session->player) return;
  if (args[0].is_string() && args[0].get<std::string>() != a.session->player->name) return;
  std::string kind = args[1];
  json o = args.size() > 2 && args[2].is_object() ? args[2] : json::object();
  InvOpen io;
  if (o.contains("pos") && o["pos"].is_array() && o["pos"].size() == 3) { io.hasPos = true; io.pos = ivec3(o["pos"][0].get<int>(), o["pos"][1].get<int>(), o["pos"][2].get<int>()); }
  std::function<std::unique_ptr<Screen>()> make;
  if (kind == "container") { io.mode = "container"; io.ender = o.value("ender", false); io.heldPack = o.value("held", false); }
  else if (kind == "table") io.mode = "table";
  else if (kind == "furnace") io.mode = "furnace";
  else if (kind == "station") {
    io.mode = "station"; io.kind = o.value("kind", "");
    // the enchanting table and the brewing stand have their own panels (ui/magic_screens)
    if (io.kind == "enchant" && io.hasPos) { ivec3 p = io.pos; make = [p] { return makeEnchantScreen(p); }; }
    else if (io.kind == "brew" && io.hasPos) { ivec3 p = io.pos; make = [p] { return makeBrewScreen(p); }; }
  }
  else if (kind == "stonecutter") io.mode = "stonecutter";
  else if (kind == "machine") { io.mode = "machine"; if (!io.hasPos) return; }
  else if (kind == "sign") { if (!io.hasPos) return; ivec3 p = io.pos; make = [p] { return makeSignScreen(p); }; }
  else if (kind == "lectern") { if (!io.hasPos) return; ivec3 p = io.pos; make = [p] { return makeLecternScreen(p); }; }
  else { hudToast("That screen isn't in this build yet", "warn"); return; }
  if (io.mode == "container" && !io.hasPos && !io.ender && !io.heldPack) { if (!o.contains("entity")) hudToast("That screen isn't in this build yet", "warn"); return; }
  if (!make) make = [io] { return makeInventoryScreen(io); };
  uiDefer([make](App& a2) {
    if (!a2.session || !a2.session->player) return;
    while (a2.topScreen() && a2.topScreen()->id() == std::string("inventory")) a2.popScreen();
    a2.pushScreen(make());
  });
}

// menutabs.js MENU_TABS: Inventory, Map (greyed out without a map), Journal, Skills, Spellbook, Stable
static void registerMenuTabs() {
  registerInventoryTab();
  registerMenuTab({"map", "Map", [](App& a) { a.pushScreen(makeMapScreen(-1)); }, [](App& a) { return canOpenMapScreen(a); }});
  registerMenuTab({"journal", "Journal", [](App& a) { a.pushScreen(makeJournalScreen()); }, nullptr});
  registerMenuTab({"skills", "Skills", [](App& a) { a.pushScreen(makeSkillsScreen()); }, nullptr});
  registerMenuTab({"spells", "Spellbook", [](App& a) { a.pushScreen(makeSpellbookScreen()); }, nullptr});
  registerMenuTab({"stable", "Stable", [](App& a) { a.pushScreen(makeStableScreen()); }, nullptr});
}

void registerUi(App& a) {
  ui::init(a);
  registerHudLayer();
  hudInitEvents(a);
  registerMenuTabs();
  a.events.on("screenOpen", [&a](const json& args) { onScreenOpen(a, args); return true; });
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
  if (n == "stonecutter") { o.mode = "stonecutter"; return makeInventoryScreen(o); }
  if (n == "anvil" || n == "loom" || n == "smithing" || n == "grindstone" || n == "fletching") { o.mode = "station"; o.kind = n; return makeInventoryScreen(o); }
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

// Every pad the game understands (SDL game controllers and pads mapped in the setup wizard, in the standard
// layout with the Nintendo swap applied) drives ImGui's menu navigation: d-pad / left stick move the highlight,
// A presses, B backs out, LB/RB and the triggers page. ImGui's SDL backend only sees pads SDL knows by name.
static void feedPadsToImGui(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  if (!(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) || a.input.setupActive) return;
  bool b[17] = {}; float lx = 0, ly = 0; bool any = false;
  for (auto& p : a.input.pads()) {
    any = true;
    for (int k = 0; k < 17; k++) b[k] = b[k] || p.b[k];
    if (std::fabs(p.lstick.x) > std::fabs(lx)) lx = p.lstick.x;
    if (std::fabs(p.lstick.y) > std::fabs(ly)) ly = p.lstick.y;
  }
  if (!any) return;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  static const ImGuiKey MAP[16] = {ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft, ImGuiKey_GamepadFaceUp,
    ImGuiKey_GamepadL1, ImGuiKey_GamepadR1, ImGuiKey_GamepadL2, ImGuiKey_GamepadR2, ImGuiKey_GamepadBack, ImGuiKey_GamepadStart,
    ImGuiKey_GamepadL3, ImGuiKey_GamepadR3, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight};
  for (int k = 0; k < 16; k++) io.AddKeyEvent(MAP[k], b[k]);
  auto axis = [&](ImGuiKey neg, ImGuiKey pos, float v) { io.AddKeyAnalogEvent(neg, v < -0.35f, std::max(0.f, -v)); io.AddKeyAnalogEvent(pos, v > 0.35f, std::max(0.f, v)); };
  axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, lx);
  axis(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown, ly);
  // a pad used while a menu is up but no menu window has the focus (the mouse was used last, or the pad was
  // picked up after the screen opened): give the focus to the topmost menu window so the d-pad has a highlight
  bool active = lx * lx + ly * ly > 0.12f;
  for (bool v : b) active = active || v;
  Screen* top = a.topScreen();
  ImGuiContext& g = *GImGui;
  if (active && top && top->wantsMouse() && !g.NavWindow) {
    for (int i = g.Windows.Size - 1; i >= 0; i--) {
      ImGuiWindow* w = g.Windows[i];
      if (!w->WasActive || w->Hidden || (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoInputs))) continue;
      ImGui::FocusWindow(w); ImGui::SetNavCursorVisible(true);
      break;
    }
  }
}

void uiBeginFrame(App& a) {
  feedPadsToImGui(a);
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

bool inventoryClosePressed(App& a, bool tabScreen) {
  Input& in = a.input;
  if (!in.pressed(Act::Inventory)) return false;
  if (!tabScreen || !in.keyOnlyPressed(Act::Inventory)) return true;
  return in.sharesKey(Act::Inventory, Act::MenuTabRight) || in.sharesKey(Act::Inventory, Act::MenuTabLeft) ? in.padButtonPressed(3) : true;
}

static int g_tabClick = -1;
static void tabGo(App& a, int cur, int to, int dir) {
  // (the switch happens between frames, outside any screen's draw)
  if (!a.session || !a.session->player || a.session->player->dead) return;
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
// a click on the strip or LB / RB: where to go (menutabs.js update)
static bool tabsWant(App& a, const std::string& current, int& cur, int& to, int& dir) {
  auto& v = tabs(); int n = (int)v.size(); cur = -1;
  for (int i = 0; i < n; i++) if (v[i].key == current) cur = i;
  if (cur < 0 || n < 2 || (a.session && a.session->player && a.session->player->dead)) { g_tabClick = -1; return false; }
  if (g_tabClick >= 0 && g_tabClick != cur) { to = g_tabClick; g_tabClick = -1; dir = 1; return true; }
  g_tabClick = -1;
  if (a.input.textActive) return false;
  bool lb = a.input.pressed(Act::MenuTabLeft), rb = a.input.pressed(Act::MenuTabRight);
  if (lb || rb) { dir = rb ? 1 : -1; to = ((cur + dir) % n + n) % n; return true; }
  return false;
}
bool menuTabsUpdate(App& a, const std::string& current) {
  int cur, to, dir;
  if (!tabsWant(a, current, cur, to, dir)) return false;
  tabGo(a, cur, to, dir);
  return true;
}
// The other tabbed screens (map, journal, skills, spellbook, stable) get the strip and LB / RB from here,
// after the screens have drawn (the inventory does its own: its LB / RB also drive the recipe book).
static const char* tabKeyOfScreen(const std::string& id) {
  if (id == "map") return "map";
  if (id == "journal") return "journal";
  if (id == "skills") return "skills";
  if (id == "spellbook") return "spells";
  if (id == "stable") return "stable";
  return nullptr;
}
void uiAfterScreens(App& a) {
  Screen* s = a.topScreen();
  if (!s || !a.session || !a.session->player) return;
  const char* key = tabKeyOfScreen(s->id());
  if (!key) return;
  menuTabsDraw(a, key);
  // one LB/RB press = one tab: the screen a switch opens must not see the same press again
  static u64 lastSwitch = 0;
  if (a.frame <= lastSwitch + 2) return;
  int cur, to, dir;
  if (tabsWant(a, key, cur, to, dir)) {
    lastSwitch = a.frame;
    uiDefer([s, cur, to, dir](App& a2) { if (a2.topScreen() == s) { tabGo(a2, cur, to, dir); lastSwitch = a2.frame; } });
  }
}
void menuTabsDraw(App& a, const std::string& current) {
  auto& v = tabs(); if (v.size() < 2) return;
  using namespace ui;
  float z = Z();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  // .menu-tabs: top centre, LB · tabs · RB, 600 13px
  std::vector<float> w; float total = 0, gap = 4 * z;
  auto pill = [&](const char* t, float padX) { return textSize(F_MID, t).x + padX * 2; };
  // Q / E on a keyboard (the inventory's E shuts it instead, so only Q shows there), LB / RB on a pad
  bool kb = !a.input.usingPad();
  bool eShuts = current == "inventory" && a.input.sharesKey(Act::Inventory, Act::MenuTabRight);
  const char* lbT = kb ? "Q" : "LB"; const char* rbT = kb ? (eShuts ? "" : "E") : "RB";
  float lbw = pill(lbT, 7 * z), rbw = *rbT ? pill(rbT, 7 * z) : 0;
  for (auto& t : v) { w.push_back(pill(t.name.c_str(), 10 * z)); total += w.back() + gap; }
  total += lbw + rbw + gap * (*rbT ? 1 : 0);
  float x = std::round(a.width * 0.5f - total * 0.5f), y = 6 * z, h = textSize(F_MID, "Ag").y + 10 * z;
  auto drawPill = [&](const char* t, float wpx, ImU32 bg, ImU32 border, ImU32 fg, float hh, float r) {
    ImVec2 p0(x, y + (h - hh) * 0.5f), p1(x + wpx, y + (h + hh) * 0.5f);
    dl->AddRectFilled(p0, p1, bg, r); if (border) dl->AddRect(p0, p1, border, r);
    ImVec2 ts = textSize(F_MID, t); text(dl, F_MID, ImVec2(p0.x + (wpx - ts.x) * 0.5f, p0.y + (p1.y - p0.y - ts.y) * 0.5f), fg, t);
    // (not IsMouseHoveringRect: after the screens' windows have ended, the current window is ImGui's little "Debug"
    // window and that call clips to it, so a click on the strip never counted on the map / journal / skills / ... tabs)
    ImVec2 m = ImGui::GetIO().MousePos;
    bool hit = ImGui::IsMouseClicked(0) && m.x >= p0.x && m.x < p1.x && m.y >= p0.y && m.y < p1.y;
    x += wpx + gap; return hit;
  };
  drawPill(lbT, lbw, col::rgb(0x333333), 0, col::white, h - 4 * z, 5 * z);
  for (size_t i = 0; i < v.size(); i++) {
    bool on = v[i].key == current, off = v[i].can && !v[i].can(a);
    ImU32 bg = on ? col::rgb(0xc9a24a) : col::rgb(0x140e08, 217), br = on ? col::rgb(0xf0d58a) : col::rgb(0x6b5a3a), fg = on ? col::rgb(0x1a1208) : col::rgb(0xd8c9a8);
    if (off) { bg = withAlpha(bg, 0.45f); br = withAlpha(br, 0.45f); fg = withAlpha(fg, 0.45f); }
    if (drawPill(v[i].name.c_str(), w[i], bg, br, fg, h, 6 * z)) g_tabClick = (int)i;
  }
  if (*rbT) drawPill(rbT, rbw, col::rgb(0x333333), 0, col::white, h - 4 * z, 5 * z);
}

} // namespace dq
