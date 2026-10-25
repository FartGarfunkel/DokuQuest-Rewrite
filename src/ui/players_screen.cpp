// ui/players_screen.cpp — Players & skins (ui.js renderProfiles + creator.js's "Character creator…" button):
// your character turning in 3D, your name, "Choose skin.png…" (the system file chooser, like the original's
// file input; a skin dropped on the window works too; 64x64 and legacy 64x32 skins), Default, the arms
// (work it out / classic / slim), a cosmetic cape, the creator and the skin editor, and every skin and character
// you have, one click to wear. Mouse, keyboard and controller (the right stick turns the model).
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/char_screens.h"
#include "assets/cape_tex.h"
#include "ui/char_preview.h"
#include "ui/file_pick.h"
#include "client/app.h"
#include "client/session.h"
#include "core/registry.h"
#include <imgui.h>

namespace dq {
using namespace ui;

// import a skin picture (any 64n x 64n or 64n x 32n PNG) into the skins folder and wear it
bool importSkin(App& a, const fs::path& src, std::string& msg) {
  SkinImage s; std::string err;
  if (!loadSkinFile(src, s, &err)) { msg = err.empty() ? "That file is not a skin." : err; return false; }
  std::string stem = sanitizeName(src.stem().string());
  if (stem.empty()) stem = "skin";
  fs::path dst = skinsDir() / (stem + ".png");
  if (!saveSkinPng(dst, s)) { msg = "Could not copy it into your skins folder."; return false; }
  invalidateCharacter();
  wearSkin(a, stem + ".png");
  msg = std::string("Skin set") + (s.legacy ? " (an old 64\xC3\x97" "32 skin, its left arm and leg copied from the right)." : ".");
  return true;
}

namespace {
const char* ARMS[3][2] = {{"", "Work it out"}, {"classic", "Classic (Steve, 4 px)"}, {"slim", "Slim (Alex, 3 px)"}};

class PlayersScreen : public Screen {
public:
  const char* id() const override { return "players"; }
  bool wantsMouse() const override { return true; }
  std::string name, msg;
  FilePicker picker;
  CharPreview view;
  float yaw = PI + 0.45f, t = 0; bool drag = false; float dragX = 0, dragYaw = 0;
  std::vector<std::string> skins, chars; float listT = 0;

  void onOpen(App& a) override { name = a.settings.playerName; watchDroppedFiles(); refresh(); }
  bool onBack(App&) override { return backConsumedByText(); }
  void onClose(App& a) override { save(a); }
  void update(App& a, double dt) override {
    t += (float)dt;
    if (!drag) yaw += (float)dt * 0.35f;
    if ((listT -= (float)dt) <= 0) refresh();
    std::string path;
    if (picker.take(path)) {
      if (!path.empty() && importSkin(a, path, msg)) refresh();
      else if (!picker.error().empty()) msg = picker.error();
    }
    if (takeDroppedFile(path, "png") && importSkin(a, path, msg)) refresh();
  }
  void refresh() { skins = listSkins(); chars = listCharacters(); listT = 2; }
  void save(App& a) {
    std::string tt = name; while (!tt.empty() && tt.back() == ' ') tt.pop_back(); while (!tt.empty() && tt[0] == ' ') tt.erase(0, 1);
    a.settings.playerName = tt.empty() ? "Player 1" : tt;
    a.settings.save();
    if (a.session && a.session->player) a.session->player->name = a.settings.playerName;
  }
  std::string skinLabel(const std::string& ref) {
    if (ref.empty()) return "The Emberwild wanderer (default)";
    if (startsWith(ref, "creator:")) return ref.substr(8) + " (character)";
    return skinArms(ref).first;
  }

  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z();
    ImGuiIO& io = ImGui::GetIO();
    Panel pn;
    pn.begin("##players", "Players & skins", 860, "Done");
    pn.beginBody();
    ImGui::SetCursorPos(ImVec2(16 * z, 14 * z));
    ImGui::BeginGroup();
    label(F_MID, col::muted, "Up to four people can play on one screen. Player 1 uses the keyboard, mouse or touch (or a controller); everyone else plugs in a controller and presses Start to join. Any Minecraft or Mineclonia skin works \xE2\x80\x94 a 64\xC3\x97" "64 or 64\xC3\x97" "32 skin.png. Drop one on the window, or choose it.", pn.width - 32 * z);
    ImGui::Dummy(ImVec2(1, 6 * z));
    padScrollHere();
    // ---- .profile card for player one ----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cw = std::min(560 * z, pn.width - 32 * z), ch = 330 * z;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p0, p0 + ImVec2(cw, ch), col::slot, 4 * z); dl->AddRect(p0, p0 + ImVec2(cw, ch), col::line, 4 * z, 0, 2 * z);
    // .pf-skin: you, in 3D (wearing what you wear in the world)
    float vw = 150 * z, vh = ch - 40 * z;
    ImVec2 v0 = p0 + ImVec2(12, 30) * z;
    {
      ImVec2 hp = p0 + ImVec2(12, 9) * z;
      dl->AddCircleFilled(hp + ImVec2(5, 8) * z, 5 * z, col::rgb(0xffb07a));
      text(dl, F_BODY, hp + ImVec2(16 * z, 0), col::text, "Player 1");
      dl->AddRectFilledMultiColor(v0, v0 + ImVec2(vw, vh), col::rgb(0x0a0806), col::rgb(0x0a0806), col::rgb(0x2c241c), col::rgb(0x2c241c));
      bevel(dl, v0, v0 + ImVec2(vw, vh), col::slLo, col::slHi, 2 * z);
      auto spec = resolveCharacter(a.settings.skin);
      CharPreviewOpts o; o.yaw = yaw; o.elev = 0.12f; o.dist = 62; o.time = t; o.target = 15.5f;
      CharWear wear; CharPose ps; ps.time = t; ps.capeA = 0.12f; ps.capeSway = std::sin(t * 2.3f) * 0.03f;
      Player* pl = a.session ? a.session->player : nullptr;
      if (pl) { wear = charWearOf(*pl, *spec, false); wear.held.clear(); wear.off.clear(); }
      else { wear.cape = spec->cape; std::string pc = wornCapeName(a.settings); if (!pc.empty()) wear.cape = customCapeKey(pc); }
      o.wear = &wear; o.pose = &ps;
      GLuint tex = view.render(*spec, (int)(vw * io.DisplayFramebufferScale.x), (int)(vh * io.DisplayFramebufferScale.y), o);
      if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, v0, v0 + ImVec2(vw, vh), ImVec2(0, 1), ImVec2(1, 0));
      ImGui::SetCursorScreenPos(v0);
      ImGui::InvisibleButton("##turn", ImVec2(vw, vh));
      if (ImGui::IsItemActivated()) { drag = true; dragX = io.MousePos.x; dragYaw = yaw; }
      if (drag && ImGui::IsItemActive()) yaw = dragYaw + (io.MousePos.x - dragX) * 0.012f / z;
      if (!ImGui::IsItemActive()) drag = false;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to turn");
      yaw += (padAxis(ImGuiKey_GamepadRStickRight) - padAxis(ImGuiKey_GamepadRStickLeft)) * io.DeltaTime * 2.6f;
    }
    // the settings beside it
    float rx = v0.x + vw + 14 * z, rw = p0.x + cw - 12 * z - rx;
    ImGui::SetCursorScreenPos(ImVec2(rx, p0.y + 30 * z));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(rw);
    label(F_MID, col::muted, "Name");
    inputText("##pname", &name, nullptr, 16, rw);
    if (ImGui::IsItemDeactivatedAfterEdit()) save(a);
    ImGui::Dummy(ImVec2(1, 4 * z));
    std::string ref = a.settings.skin;
    bool png = !ref.empty() && !startsWith(ref, "creator:");
    label(F_MID, col::muted, ("Skin: " + skinLabel(ref)).c_str(), rw);
    if (button(picker.busy() ? "Choosing\xE2\x80\xA6##pick" : "Choose skin.png\xE2\x80\xA6##pick", BTN_NORMAL, true, 0, picker.busy())) { msg.clear(); picker.open("Choose a skin (64\xC3\x97" "64 or 64\xC3\x97" "32 PNG)", "png"); }
    ImGui::SameLine();
    if (button("Default##skin", BTN_GHOST, true, 0, ref.empty())) { wearSkin(a, ""); msg = "The default skin."; }
    // .pf-arms: work it out / classic / slim (a skin's own arms; a character's come from its build)
    {
      auto [file, arms] = skinArms(ref);
      int cur = arms == "classic" ? 1 : arms == "slim" ? 2 : 0;
      label(F_MID, col::muted, "Arms");
      ImGui::BeginDisabled(!png);
      if (button("<##arms", BTN_NORMAL, true)) cur = (cur + 2) % 3, wearSkin(a, file + (ARMS[cur][0][0] ? std::string("#") + ARMS[cur][0] : ""));
      ImGui::SameLine(); ImGui::AlignTextToFramePadding(); label(F_BODY, col::text, png ? ARMS[cur][1] : "From the character");
      ImGui::SameLine(); if (button(">##arms", BTN_NORMAL, true)) cur = (cur + 1) % 3, wearSkin(a, file + (ARMS[cur][0][0] ? std::string("#") + ARMS[cur][0] : ""));
      ImGui::EndDisabled();
    }
    ImGui::Dummy(ImVec2(1, 4 * z));
    if (button("Character creator\xE2\x80\xA6", BTN_PRIMARY, true)) a.pushScreen(makeCreatorScreen());
    ImGui::SameLine();
    if (button("Skin editor\xE2\x80\xA6", BTN_NORMAL, true)) a.pushScreen(makeSkinEditorScreen());
    ImGui::SameLine();
    if (button("Cape editor\xE2\x80\xA6", BTN_NORMAL, true)) a.pushScreen(makeCapeEditorScreen());
    ImGui::Dummy(ImVec2(1, 4 * z));
    {   // the cape you wear (the Cape slot of your inventory); a character can wear a cape of its own (the creator)
      Player* pl = a.session ? a.session->player : nullptr;
      std::string worn = pl ? wornItem(*pl, "cape") : "";
      const ItemDef* d = worn.empty() ? nullptr : reg().item(worn);
      auto spec = resolveCharacter(ref);
      std::string painted = wornCapeName(a.settings);
      std::string c = d ? d->name + (pl && gearHidden(*pl, 5) ? " (hidden)" : "") : !painted.empty() ? "painted \"" + painted + "\"" : !spec->cape.empty() ? "the character's own" : a.session ? "none \xE2\x80\x94 put one in the Cape slot" : "none";
      label(F_MID, col::muted, ("Cape: " + c).c_str(), rw);
    }
    if (!msg.empty()) label(F_MID, col::gold, msg.c_str(), rw);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(p0 + ImVec2(0, ch));
    ImGui::Dummy(ImVec2(cw, 12 * z));
    // ---- your skins and characters: one click to wear ----
    label(F_BODY, col::text, "Your skins");
    if (skins.empty() && chars.empty()) label(F_MID, col::muted, "None yet \xE2\x80\x94 choose a skin.png, drop one here, or make a character.", pn.width - 32 * z);
    float bx = 0, maxW = pn.width - 32 * z;
    auto chip = [&](const std::string& lab, const std::string& wear, int i) {
      bool on = a.settings.skin == wear || skinArms(a.settings.skin).first == wear;
      std::string l = (on ? "\xE2\x9C\x93 " : "") + lab + "##sk" + std::to_string(i);
      float w = textSize(F_BODY, lab.c_str()).x + 40 * z;
      if (bx > 0 && bx + w < maxW) ImGui::SameLine(); else bx = 0;
      bx += w + 8 * z;
      if (button(l.c_str(), on ? BTN_PRIMARY : BTN_NORMAL, true)) { invalidateCharacter(wear); wearSkin(a, wear); msg = "Wearing " + lab + "."; }
    };
    int i = 0;
    for (auto& c : chars) chip("\xE2\x98\xBA " + c, "creator:" + c, i++);
    for (auto& s : skins) chip(fs::path(s).stem().string(), s, i++);
    ImGui::Dummy(ImVec2(1, 8 * z));
    label(F_MID, col::muted, ("Skins folder: " + skinsDir().string()).c_str(), pn.width - 32 * z);
    ImGui::EndGroup();
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
}
std::unique_ptr<Screen> makePlayersScreen() { return std::make_unique<PlayersScreen>(); }

} // namespace dq
