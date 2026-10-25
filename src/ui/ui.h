// ui/ui.h — the menus and HUD (ui/title.js, ui.js, hud.js, invui.js, sheet.js, game/menutabs.js,
// input/controls.js, padsetup.js). Other areas open screens and feed the HUD through these functions;
// the HUD also listens on the EventBus: "toast" [text, kind], "pickup" [itemKey, n], "chatLine" [text, kind],
// "dimName" [text], "saving" [bool], "floatText" [x, y, z, text, "#rrggbb"].
#pragma once
#include "ui/screen.h"
#include "sim/inv_ops.h"

namespace dq {

class App;
class GameSession;

void registerUi(App& a);         // once at start-up, after App::initImGui and the tiles: theme, HUD layer, events
void uiBeginFrame(App& a);       // every frame between ImGui_ImplSDL2_NewFrame and ImGui::NewFrame
void uiAfterScreens(App& a);     // every frame after the screens drew: the menu tab strip over map/journal/skills/...
// run something at the start of the next frame, outside any screen's draw (opening/leaving worlds, replacing the stack)
void uiDefer(std::function<void(App&)> f);

// ---- screens ----
std::unique_ptr<Screen> makeWorldsScreen();
std::unique_ptr<Screen> makeOptionsScreen();
std::unique_ptr<Screen> makeControlsScreen();
std::unique_ptr<Screen> makePadSetupScreen();
std::unique_ptr<Screen> makeHowToScreen();
std::unique_ptr<Screen> makeNewsScreen();
std::unique_ptr<Screen> makePlayersScreen();
std::unique_ptr<Screen> makeComingSoonScreen(const std::string& title, const std::string& text);
// provided by the characters workstream (ui/screen.h there); ui/menus.cpp has weak "coming soon" stand-ins
std::unique_ptr<Screen> makeCreatorScreen();
std::unique_ptr<Screen> makeSkinEditorScreen();
std::unique_ptr<Screen> makeWorldSettingsScreen(const fs::path& folder);   // empty = the world being played
std::unique_ptr<Screen> makeDeathScreen(const std::string& cause, int level);
std::unique_ptr<Screen> makeChatScreen(const std::string& prefill);
std::unique_ptr<Screen> makeInventoryScreen(const InvOpen& o);   // also stations, the stonecutter, machines (InvOpen::mode)
std::unique_ptr<Screen> makeSignScreen(ivec3 signPos);            // writing on a sign (ui/sign_screen.cpp)
std::unique_ptr<Screen> makeMultiplayerScreen();                    // title → Multiplayer (LAN list + address)
std::unique_ptr<Screen> makeLanHostScreen();                        // pause → Open to LAN
std::unique_ptr<Screen> makeJoiningScreen(const std::string& address, const std::string& label = "", const std::string& password = "");
std::unique_ptr<Screen> makeScreenByName(App& a, const std::string& name);   // --screen <name> (tests, screenshots)

// ---- in-game entry points for other areas ----
void openInventory(App& a, const InvOpen& o = InvOpen());   // chest/furnace/table: InvOpen{mode, hasPos, pos}
void openChat(App& a, const std::string& prefill = "");
void chatLine(const std::string& text, const std::string& kind = "");     // kind: "" | ok | err | dim
void hudToast(const std::string& text, const std::string& kind = "");     // good | warn | boss | rare | whisper | epic
void hudPickup(const std::string& itemKey, int n);
void hudDimName(const std::string& name);
void hudFloatText(vec3 pos, const std::string& text, u32 rgb = 0xffffff);
void hudSaving(bool on);
void throwStackFromPlayer(App& a, const ItemStack& s);   // drop in front of the local player (EventBus "throwStack")

// ---- menu tabs (game/menutabs.js): LB / RB between inventory, map, journal, skills, spellbook, stable ----
struct MenuTab {
  std::string key, name;
  std::function<void(App&)> open;                 // push the screen
  std::function<bool(App&)> can;                  // optional: greyed out (e.g. no map carried)
};
void registerMenuTab(MenuTab t);                  // other areas add map/journal/skills/spellbook/stable here
const std::vector<MenuTab>& menuTabs();
bool menuTabsUpdate(App& a, const std::string& current);   // LB/RB or a click: switched (current screen closed)
void menuTabsDraw(App& a, const std::string& current);

// ---- shared pieces ----
std::string itemInfoText(const ItemStack& s, const Player* p, std::vector<std::pair<std::string, u32>>* lines);   // tooltip lines
json ruleDefaults(bool creative);                 // game/rules.js RULE_DEFAULTS (+ commands in creative)
bool backConsumedByText();                        // Esc / B while typing only leaves the text box

} // namespace dq
