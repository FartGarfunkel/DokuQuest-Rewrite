// ui/menus.cpp — the pause menu ("Game Menu", ui.js pauseInfo), How to play, What's new
// and the "coming soon" panel used by entries whose workstream hasn't landed (skin editor, creator, LAN).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "ui/char_preview.h"
#include <imgui.h>

namespace dq {
using namespace ui;

bool backupWorldFolder(const fs::path& folder, const std::string& name, fs::path& out, std::string& err);   // worlds.cpp
std::string agoText(i64 ms);                                                                                  // worlds.cpp

// ---- pause ---------------------------------------------------------------------------------------------
class PauseScreen : public Screen {
public:
  const char* id() const override { return "pause"; }
  std::string info;
  bool focusPending = true;
  CharPreview headView;
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 115));
    float bw = std::min(400.f, 0.9f * W / z) * z, bh = 40 * z, gap = 8 * z, half = (bw - gap) * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(W, H - 32 * z));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    padScrollHere();
    // .mc-title
    { ImVec2 ts = textSize(F_BIG, "Game Menu"); ImVec2 p = ImGui::GetCursorScreenPos();
      textShadow(dl, F_BIG, ImVec2(p.x + (bw - ts.x) * 0.5f, p.y), col::white, "Game Menu", ImVec2(2, 2) * z, col::rgb(0x3f3f3f)); ImGui::Dummy(ImVec2(bw, ts.y + 14 * z)); }
    if (focusPending && a.input.padCount()) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    focusPending = false;
    Level* lv = a.session ? a.session->level.get() : nullptr;
    Player* p = a.session ? a.session->player : nullptr;
    bool creative = p && p->creative();
    if (mcButton("Back to Game", ImVec2(bw, bh))) { a.popScreen(); ImGui::End(); ImGui::PopStyleVar(); return; }
    if (mcButton("Options...", ImVec2(half, bh))) a.pushScreen(makeOptionsScreen());
    ImGui::SameLine(); if (mcButton("Players & Skins", ImVec2(half, bh))) a.pushScreen(makePlayersScreen());
    bool guest = netIsGuest(a);
    // (netui.js) a guest has no world settings, backups, mode switch or LAN of its own
    if (mcButton("How to Play", ImVec2(guest ? bw : half, bh))) a.pushScreen(makeHowToScreen());
    if (!guest) { ImGui::SameLine(); if (mcButton("World Settings", ImVec2(half, bh))) a.pushScreen(makeWorldSettingsScreen({})); }
    if (!guest) {
      if (mcButton(creative ? "Switch to Survival##mode" : "Switch to Creative##mode", ImVec2(half, bh)) && lv && p) {
        lv->info.mode = p->mode = creative ? "survival" : "creative";
        if (!p->creative()) p->flying = false;
        a.toast(p->creative() ? "Creative mode: every block in the inventory, double-tap jump to fly" : "Survival mode", "good");
      }
      ImGui::SameLine();
      if (mcButton("Back Up World", ImVec2(half, bh)) && lv) {
        a.session->save();
        fs::path out; std::string err;
        if (backupWorldFolder(lv->folder, lv->info.name, out, err)) { info = "Backup saved: " + out.filename().string(); a.toast("Backup saved", "good"); }
        else { info = "Backup failed: " + err; a.toast(info, "warn"); }
      }
      LanStatus ls = lanStatus(a);
      std::string lanLabel = ls.open ? "LAN: " + (ls.players.empty() ? std::string("open, nobody yet") : std::to_string(ls.players.size()) + " joined") + " \xC2\xB7 " + ls.address + "##lan" : "Open to LAN##lan";
      if (mcButton(lanLabel.c_str(), ImVec2(bw, bh))) a.pushScreen(makeLanHostScreen());
    }
    std::string quit = guest ? "Leave " + (lv ? lv->info.name : std::string("the world")) + "##quit" : "Save and Quit to Title##quit";
    if (mcButton(quit.c_str(), ImVec2(bw, bh))) uiDefer([](App& app) { app.leaveWorld(true); });
    // .pause-party: who is playing on this screen
    ImGui::Dummy(ImVec2(1, 6 * z));
    {
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      float rowH = 30 * z;
      std::string dev = std::string("Keyboard & mouse") + (a.input.padCount() ? " + Controller" : "");
      const char* hint = "Split-screen: plug in another controller and press Start on it to join.";
      float hintH = textSize(F_MID, hint, bw - 20 * z).y;
      float boxH = 8 * z + textSize(F_MID, "On this screen").y + 6 * z + rowH + 6 * z + hintH + 8 * z;
      dl->AddRectFilled(p0, p0 + ImVec2(bw, boxH), IM_COL32(0, 0, 0, 115));
      dl->AddRect(p0, p0 + ImVec2(bw, boxH), col::black, 0, 0, 2 * z);
      float y = p0.y + 8 * z;
      text(dl, F_MID, ImVec2(p0.x + 10 * z, y), col::muted, "On this screen"); y += textSize(F_MID, "On").y + 6 * z;
      ImVec2 head(p0.x + 10 * z, y);
      // charparts.js drawSkinHead: your head as a little 3D cube seen from above and to one side (here the real
      // voxel head: a 3D character's hair, ears, horns and hat too)
      if (p) {
        auto spec = resolveCharacter(p->skin);
        CharPreviewOpts ho; ho.headOnly = true; ho.yaw = PI - 0.62f; ho.elev = 0.48f; ho.dist = 30; ho.time = (float)a.time;
        CharPose hp; hp.still = true; ho.pose = &hp;
        float fb = ImGui::GetIO().DisplayFramebufferScale.x, hs = rowH * 1.25f;
        GLuint tex = headView.render(*spec, (int)(hs * fb), (int)(hs * fb), ho);
        if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, head + ImVec2(-rowH * 0.12f, -rowH * 0.14f), head + ImVec2(-rowH * 0.12f, -rowH * 0.14f) + ImVec2(hs, hs), ImVec2(0, 1), ImVec2(1, 0));
      }
      dl->AddRectFilled(head + ImVec2(0, rowH - 2 * z), head + ImVec2(rowH, rowH), col::rgb(0xffb07a), 2 * z);   // .phead.p1
      text(dl, F_BODY, ImVec2(head.x + rowH + 10 * z, y + (rowH - textSize(F_BODY, "A").y) * 0.5f), col::rgb(0xffb07a), p ? p->name.c_str() : "");
      ImVec2 ds = textSize(F_SMALL, dev.c_str());
      text(dl, F_SMALL, ImVec2(p0.x + bw - 10 * z - ds.x, y + (rowH - ds.y) * 0.5f), col::muted, dev.c_str());
      y += rowH + 6 * z;
      text(dl, F_MID, ImVec2(p0.x + 10 * z, y), col::rgb(0xd8d8d8), hint, bw - 20 * z);
      ImGui::Dummy(ImVec2(bw, boxH));
    }
    // .mc-hint: name · dimension · mode · last saved
    if (lv && p) {
      bool guest = netIsGuest(a);
      std::string s = lv->info.name + (guest ? " \xC2\xB7 hosted by someone" : "") + " \xC2\xB7 " + dimName(dimOfY(ifloor(p->pos.y))) + " \xC2\xB7 " + (p->creative() ? "Creative" : "Survival");
      if (lv->info.lastPlayed && !guest) s += " \xC2\xB7 Last saved " + agoText(lv->info.lastPlayed) + ".";
      if (!info.empty()) s = info;
      ImVec2 ts = textSize(F_MID, s.c_str(), bw); ImVec2 q = ImGui::GetCursorScreenPos();
      textShadow(dl, F_MID, ImVec2(q.x + std::max(0.f, (bw - ts.x) * 0.5f), q.y + 4 * z), col::rgb(0xd8d8d8), s.c_str(), ImVec2(1, 1) * z, col::black, bw);
      ImGui::Dummy(ImVec2(bw, ts.y + 8 * z));
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }
};
std::unique_ptr<Screen> makePauseScreen() { return std::make_unique<PauseScreen>(); }

// ---- simple text panels -------------------------------------------------------------------------------
namespace {
struct Block { int kind; std::string a, b; };   // 0 h3, 1 paragraph, 2 key row (a = keys, b = what), 3 numbered
class TextPanel : public Screen {
public:
  TextPanel(const char* id, std::string t, std::string close, float w, std::vector<std::vector<Block>> cols) : sid(id), title(std::move(t)), closeLbl(std::move(close)), width(w), columns(std::move(cols)) {}
  const char* sid; std::string title, closeLbl; float width; std::vector<std::vector<Block>> columns;
  const char* id() const override { return sid; }
  bool pausesGame() const override { return true; }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    Panel pn;
    pn.begin((std::string("##") + sid).c_str(), title.c_str(), width, closeLbl.c_str());
    pn.beginBody();
    float z = Z(), pad = 16 * z, gapc = 20 * z;
    int nc = (int)columns.size();
    if (pn.width < 560 * z) nc = 1;   // one column on narrow screens
    float colW = (pn.width - pad * 2 - gapc * (nc - 1)) / nc;
    ImGui::SetCursorPos(ImVec2(pad, pad));
    std::vector<Block> flat; std::vector<std::vector<Block>> cols = columns;
    if (nc == 1 && columns.size() > 1) { for (auto& c : columns) flat.insert(flat.end(), c.begin(), c.end()); cols = {flat}; }
    float top = ImGui::GetCursorPosY(), maxY = top;
    for (int c = 0; c < (int)cols.size(); c++) {
      float x = pad + c * (colW + gapc);
      ImGui::SetCursorPos(ImVec2(x, top));
      ImGui::BeginGroup();
      int num = 0;
      for (auto& b : cols[c]) {
        if (b.kind == 0) { ImGui::Dummy(ImVec2(1, 2 * z)); label(F_H3, col::text, b.a.c_str()); num = 0; }
        else if (b.kind == 1) { label(F_BODY, col::muted, b.a.c_str(), colW); ImGui::Dummy(ImVec2(1, 4 * z)); }
        else if (b.kind == 2) {
          float kw = 130 * z; ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
          float h1 = textSize(F_MID, b.a.c_str(), kw).y, h2 = textSize(F_MID, b.b.c_str(), colW - kw - 12 * z).y;
          text(dl, F_MID, p, col::ember, b.a.c_str(), kw); text(dl, F_MID, ImVec2(p.x + kw + 12 * z, p.y), col::muted, b.b.c_str(), colW - kw - 12 * z);
          ImGui::Dummy(ImVec2(colW, std::max(h1, h2) + 2 * z));
        } else {
          std::string n = std::to_string(++num) + ".";
          ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
          text(dl, F_BODY, p, col::muted, n.c_str());
          float hh = textSize(F_BODY, b.a.c_str(), colW - 22 * z).y;
          text(dl, F_BODY, ImVec2(p.x + 22 * z, p.y), col::muted, b.a.c_str(), colW - 22 * z);
          ImGui::Dummy(ImVec2(colW, hh + 4 * z));
        }
      }
      ImGui::EndGroup();
      maxY = std::max(maxY, ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY());
    }
    ImGui::SetCursorPos(ImVec2(0, maxY)); ImGui::Dummy(ImVec2(1, pad));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
}

std::unique_ptr<Screen> makeHowToScreen() {
  std::vector<Block> c1 = {
    {0, "Keyboard & mouse", ""},
    {2, "W A S D", "Move (double-tap W or hold Ctrl to sprint)"}, {2, "Space", "Jump \xC2\xB7 swim up"},
    {2, "Shift", "Sneak (won't walk off edges, chops one log at a time)"}, {2, "Left click", "Mine \xC2\xB7 attack"},
    {2, "Right click", "Place \xC2\xB7 use \xC2\xB7 eat \xC2\xB7 open"},
    {2, "Shift + right click", "Empty hand: pick up a chest, furnace or animal and carry it. Holding an item: set it down on the ground"},
    {2, "1\xE2\x80\x93" "9 \xC2\xB7 wheel", "Choose hotbar slot"}, {2, "R", "Placing mode: vertical slabs, upside-down stairs and trapdoors"},
    {2, "F", "Swap main hand and off hand"}, {2, "E", "Inventory and crafting"}, {2, "Q", "Drop item"},
    {2, "T \xC2\xB7 /", "Chat \xC2\xB7 commands (/help lists them)"}, {2, "F1 \xC2\xB7 F2", "Hide the HUD \xC2\xB7 take a screenshot"},
    {2, "F3 \xC2\xB7 F5", "Debug info \xC2\xB7 first or third person"}, {2, "Esc", "Pause"},
    {0, "Controller", ""},
    {2, "Sticks", "Move \xC2\xB7 look (click left stick to sprint)"}, {2, "RT \xC2\xB7 LT", "Mine \xC2\xB7 use"}, {2, "A \xC2\xB7 B", "Jump \xC2\xB7 sneak"},
    {2, "Y \xC2\xB7 X", "Inventory \xC2\xB7 drop"}, {2, "LB \xC2\xB7 RB", "Hotbar (in the inventory: recipe book)"}, {2, "D-pad down", "Swap hands"},
    {2, "Start", "Pause \xC2\xB7 on a new controller, join split-screen"},
    {0, "Touch", ""},
    {2, "Left thumb", "Move (push to the top edge to sprint)"}, {2, "Drag right side", "Look around"}, {2, "Tap", "Place \xC2\xB7 use"},
    {2, "Press and hold", "Mine (or hold the Mine button)"},
  };
  std::vector<Block> c2 = {
    {0, "Surviving", ""},
    {1, "There is no hunger. Eating any food heals you on the spot \xE2\x80\x94 apples, berries, and roast boar are your lifeline. You have ten hearts, as in the classic game. Heart Crystals (from bosses) add one for good; charms worn in the charm slot add more while you wear them, or guard against fire, water and falls.", ""},
    {1, "Under water, watch your air bubbles; when they run out you drown. Sprint while under water to swim. If you die, your things wait in a grave and you're given a compass that points to it.", ""},
    {1, "At night and in dark caves the monsters come out. Ghouls shamble at you, skeletons keep their distance and shoot, spiders climb walls and pounce, and fuselings creep close, hiss and burst \xE2\x80\x94 back away when you hear one. Slimes hop about deep underground. Spiders cling to cave roofs and drop on you from above. A good axe or sword can take a limb off: a ghoul without a leg crawls, a spider short of legs can't leap, a skeleton that loses its bow arm drops the bow (and may try to grab it back). Torches keep the dark away \xE2\x80\x94 hold one in your off hand, or hang a torch or lantern on your belt hook (it moves to your backpack when you wear one) and it lights up everything around you as you walk.", ""},
    {0, "Crafting", ""},
    {1, "Crafting works on the grid, the classic way: 2\xC3\x97" "2 in your inventory, 3\xC3\x97" "3 at a crafting table. Stuck? Open the recipe book beside the grid \xE2\x80\x94 it shows what you can make and can lay the ingredients out for you.", ""},
    {0, "Progression", ""},
    {3, "Punch trees. Chop the base of a tree and the whole thing falls.", ""}, {3, "Craft a crafting table, then wooden and stone tools.", ""},
    {3, "Smelt iron ore at a furnace. Coal or wood is fuel.", ""},
    {3, "Craft an Ember Effigy and summon the Cinder Golem. Watch the ground \xE2\x80\x94 every attack is marked before it lands.", ""},
    {3, "Its Cinder Core makes an Ember Igniter. Light an obsidian frame to enter the Ember Depths and mine cinderite.", ""},
    {3, "A frame of Glow Lanterns filled with water opens the Skylands. Aether crystals there make a Pocket Key \xE2\x80\x94 your own private dimension.", ""},
    {0, "Farms and animals", ""},
    {1, "Cows, pigs, sheep and chickens roam the grasslands. Feed two of a kind (wheat, carrots or seeds) and they'll have a baby. Milk cows with a bucket, shear sheep, and carry an animal home with an empty hand while sneaking \xE2\x80\x94 animals you set down stay put.", ""},
    {1, "Bone meal (from bones, or a full composter) ripens crops and saplings. Shovels make paths, hoes till soil, and scaffolding lets you climb and sneak back down.", ""},
    {0, "Workstations", ""},
    {1, "Anvils repair and join tools, the loom and smithing table dye and upgrade, the fletching table makes arrows, the stonecutter and sawmill cut blocks. A bow shoots arrows; a spyglass zooms; a compass and clock show their faces on screen when held.", ""},
    {0, "Redstone and rails", ""},
    {1, "Mine redstone ore (it glows when touched) and lay the dust on the ground to carry power from levers, buttons, pressure plates, tripwires and torches. Power fades one step per block of dust; a repeater sends it on at full strength. Redstone opens doors (iron ones need it), lights lamps, fires dispensers and droppers, and moves pistons \xE2\x80\x94 sticky ones pull blocks back, and slime or honey drags its neighbours along.", ""},
    {1, "Hoppers pass items between chests and furnaces. A comparator reads how full a container is. Observers pulse when the block they watch changes. Use a repeater to change its delay, a comparator to make it subtract, a daylight detector to invert it.", ""},
    {1, "Rails join up, climb slopes and turn corners as you lay them. Set a minecart on the track and right-click to ride; W gives a push, sneak to get out. Powered rails speed carts up (and stop them when unpowered), detector rails send a signal as a cart passes, activator rails tip riders out. Chest and hopper carts carry things; feed a furnace cart coal and it pushes itself.", ""},
    {0, "World settings and commands", ""},
    {1, "Each world has its own settings (World settings in the pause menu, or More options when you make one): its name, game mode, how long days and nights last, and game rules such as keep inventory, mob griefing and fire spread.", ""},
    {1, "Where commands are on (always in Creative), press T or / to type one: /time set day, /gamemode creative, /tp, /give, /fill and more. Builders get WorldEdit: //wand gives the wand (left click and right click mark two corners), then //set, //replace, //walls, //copy, //paste, //rotate, //stack, //sphere and //undo.", ""},
    {0, "Your worlds are safe", ""},
    {1, "The game saves every few seconds. Worlds keep working through updates, and you can make a backup from the pause menu at any time. Switch between Survival and Creative from the pause menu.", ""},
  };
  return std::make_unique<TextPanel>("howto", "How to play", "Close", 860, std::vector<std::vector<Block>>{c1, c2});
}

std::unique_ptr<Screen> makeNewsScreen() {
  // the same notes the launcher shows: data/news.json [{version, date, title, items[]}] when it is there
  std::vector<Block> b;
  if (auto j = readJson(dataDir() / "news.json"); j && j->is_array()) {
    for (auto& e : *j) {
      b.push_back({0, e.value("version", "") + (e.contains("date") ? "  " + e.value("date", "") : ""), ""});
      if (e.contains("title")) b.push_back({1, e.value("title", ""), ""});
      if (e.contains("items")) for (auto& it : e["items"]) if (it.is_string()) b.push_back({2, "\xC2\xB7", it.get<std::string>()});
    }
  }
  if (b.empty()) b = {{0, std::string("DokuQuest ") + DQ_VERSION, ""}, {1, "The patch notes for this version are in the launcher's News tab.", ""}};
  return std::make_unique<TextPanel>("news", "What's new", "Close", 860, std::vector<std::vector<Block>>{b});
}

std::unique_ptr<Screen> makeComingSoonScreen(const std::string& title, const std::string& txt) {
  return std::make_unique<TextPanel>("comingsoon", title, "Back", 460, std::vector<std::vector<Block>>{{{0, "Coming soon", ""}, {1, txt, ""}}});
}

// ---- players & skins: ui/players_screen.cpp


} // namespace dq
