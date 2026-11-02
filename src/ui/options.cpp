// ui/options.cpp — Settings (ui.js buildSettings, packRow), Settings → Controls (input/controls.js: rebind
// every key, mouse button and controller button, clashes in red) and the controller setup wizard
// (input/padsetup.js: press each button and move each stick once; the pad is remembered by its name).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/tileset.h"
#include "audio/audio.h"
#include "render/gl.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace dq { std::vector<std::string> listShaderPacks(); int reloadAllShaders(); }   // render/gl.cpp

namespace dq {
using namespace ui;

namespace {
// .setting row: label left, value right, the control under it, then the description
void settingHead(const char* lbl, const std::string& value, float w) {
  ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
  float k = 14.f / 15.f;
  text(dl, F_BODY, p, col::text, lbl, 0, k);
  if (!value.empty()) { ImVec2 vs = textSize(F_BODY, value.c_str(), 0, k); text(dl, F_BODY, ImVec2(p.x + w - vs.x, p.y), col::ember, value.c_str(), 0, k); }
  ImGui::Dummy(ImVec2(w, textSize(F_BODY, "Ag", 0, k).y));
}
void rowLabel(const char* lbl, float w) {
  ImVec2 p = ImGui::GetCursorScreenPos(); float h = 30 * Z();
  text(ImGui::GetWindowDrawList(), F_BODY, ImVec2(p.x, p.y + (h - textSize(F_BODY, "A").y) * 0.5f), col::text, lbl, 0, 14.f / 15.f);
  ImGui::Dummy(ImVec2(1, h));
}
void desc(const char* d, float w) { if (d && *d) { ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4 * Z()); label(F_SMALL, col::muted, d, w); } }
std::string pct(float v) { return std::to_string((int)std::lround(v * 100)) + "%"; }
}

class OptionsScreen : public Screen {
public:
  const char* id() const override { return "settings"; }
  void onClose(App& a) override { a.applySettings(); }
  bool onBack(App&) override { return backConsumedByText(); }
  // Laid out like the launcher's Options: sections under headings, each row with its name and a muted line about it on
  // the left, the control on the right, a faint rule under it; two columns when there's room, one on a small screen.
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    Settings& s = a.settings;
    const float z = Z();
    Panel pn;
    pn.begin("##settings", "Settings", 980, "Done");
    pn.beginBody();
    const float full = pn.width - 32 * z - ImGui::GetStyle().ScrollbarSize;
    const bool two = full > 760 * z;
    const float colGap = 24 * z, w = two ? (full - colGap) * 0.5f : full;
    bool applyNow = false, applyLight = false;
    ImGui::SetCursorPos(ImVec2(16 * z, 10 * z));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * z, 6 * z));
    float colX = ImGui::GetCursorPosX(), colTop = ImGui::GetCursorPosY(), leftBottom = colTop;

    // ---- the row kit -------------------------------------------------------------------------------------------
    struct Row {
      ImVec2 start; float w, ctrlW, textH;
      Row(const char* lbl, const char* d, const std::string& value, float w_, float ctrl) : w(w_) {
        const float z = Z();
        ctrlW = std::min(ctrl * z, w * 0.5f);
        start = ImGui::GetCursorPos();
        const float tw = w - ctrlW - 14 * z;
        ImGui::BeginGroup();
        std::string head = value.empty() ? std::string(lbl) : std::string(lbl);
        label(F_BODY, col::text, head.c_str(), tw);
        if (!value.empty()) { ImGui::SameLine(0, 8 * z); label(F_SMALL, col::ember, value.c_str()); }
        if (d && *d) label(F_SMALL, col::muted, d, tw);
        ImGui::EndGroup();
        textH = ImGui::GetItemRectSize().y;
        const float fh = ImGui::GetFrameHeight();
        ImGui::SetCursorPos(ImVec2(start.x + w - ctrlW, start.y + std::max(0.f, (textH - fh) * 0.5f)));
        ImGui::PushItemWidth(ctrlW);
        ImGui::BeginGroup();
      }
      ~Row() {
        ImGui::EndGroup();
        ImGui::PopItemWidth();
        const float z = Z();
        const float h = std::max(textH, ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY() - start.y);
        const float y = start.y + h + 6 * z;
        const ImVec2 wp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(wp.x + start.x, wp.y + y - ImGui::GetScrollY()), ImVec2(wp.x + start.x + w, wp.y + y - ImGui::GetScrollY()), IM_COL32(255, 255, 255, 18));
        ImGui::SetCursorPos(ImVec2(start.x, y + 7 * z));
      }
    };
    auto heading = [&](const char* t) {
      ImGui::Dummy(ImVec2(1, 4 * z));
      ImVec2 p = ImGui::GetCursorScreenPos();
      textShadow(ImGui::GetWindowDrawList(), F_MID, p, col::text, t, ImVec2(2, 2) * z, IM_COL32(63, 63, 63, 255));
      ImGui::Dummy(ImVec2(w, textSize(F_MID, t).y + 6 * z));
    };
    auto range = [&](const char* key, const char* lbl, float* v, float mn, float mx, float step, std::string shown, const char* d) {
      ImGui::PushID(key);
      { Row r(lbl, d, shown, w, 230);
        if (slider("##r", v, mn, mx, step, "", r.ctrlW)) applyLight = true;
        if (ImGui::IsItemDeactivated()) applyNow = true;
        if (ImGui::IsItemFocused() && a.input.navStep().x) applyNow = true; }
      ImGui::PopID();
    };
    auto rangeI = [&](const char* key, const char* lbl, int* v, int mn, int mx, std::string shown, const char* d) {
      float f = (float)*v; range(key, lbl, &f, (float)mn, (float)mx, 1, shown, d); *v = (int)std::lround(f);
    };
    auto check = [&](const char* key, const char* lbl, bool* v, const char* d = nullptr) {
      ImGui::PushID(key);
      { Row r(lbl, d, "", w, 40);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40 * z - ImGui::GetFrameHeight());
        if (ImGui::Checkbox("##c", v)) { applyNow = true; playClick(); } }
      ImGui::PopID();
    };
    auto select = [&](const char* key, const char* lbl, int cur, const std::vector<std::string>& names, const char* d, float ctrl = 230) -> int {
      ImGui::PushID(key);
      int c = cur;
      { Row r(lbl, d, "", w, ctrl);
        if (combo("##s", &c, names, std::min(ctrl * z, w * 0.5f))) applyNow = true; }
      ImGui::PopID();
      return c;
    };
    auto nextColumn = [&] {
      if (!two) return;
      leftBottom = ImGui::GetCursorPosY();
      colX += w + colGap;
      ImGui::SetCursorPos(ImVec2(colX, colTop));
    };
    auto keepX = [&] { if (two) ImGui::SetCursorPosX(colX); };

    // ================= left: graphics and packs =================
    ImGui::BeginGroup();
    heading("Graphics");
    { Row r("Renderer", ("OpenGL " + std::to_string(gfx.major) + "." + std::to_string(gfx.minor) + " \xC2\xB7 change it in the launcher's Options").c_str(), "", w, 230);
      std::string rn = rendererName(gfx.modern ? RendererKind::Modern : RendererKind::Legacy); if (!rn.empty()) rn[0] = (char)toupper((unsigned char)rn[0]);
      label(F_BODY, col::ember, rn.c_str()); }
    rangeI("renderDist", "Render distance", &s.renderDist, 2, 32, std::to_string(s.renderDist) + " chunks", "Chunks of full detail around you. Split screen caps this a little.");
    { static const int v[] = {0, 512, 1024, 2048}; int cur = 0; for (int i = 0; i < 4; i++) if (s.farTerrain == v[i]) cur = i;
      cur = select("farTerrain", "Distant terrain", cur, {"Off", "512 blocks", "1,024 blocks", "2,048 blocks"}, "Simplified land past the render distance (see forever). Off is fastest."); s.farTerrain = v[cur]; }
    { static const int v[] = {0, 768, 1536, 3072}; int mb = s.extra.value("terrainMemoryMB", 0), cur = 0; for (int i = 0; i < 4; i++) if (mb == v[i]) cur = i;
      int c = select("terrainMem", "Terrain memory", cur, {"No limit", "768 MB", "1.5 GB", "3 GB"}, "The graphics memory the land may use. Past it, the chunks farthest out are drawn as distant terrain instead.");
      if (c != cur) s.extra["terrainMemoryMB"] = v[c]; }
    range("scale", "Resolution", &s.scale, 0.4f, 1, 0.05f, pct(s.scale), "Render fewer pixels for smoother play.");
    range("fov", "Field of view", &s.fov, 55, 100, 1, std::to_string((int)std::lround(s.fov)) + "\xC2\xB0", nullptr);
    range("brightness", "Brightness", &s.brightness, 0, 1, 0.05f, pct(s.brightness), "How much you can see in the dark.");
    { int cur = clampv(s.microVoxels, 0, 2); s.microVoxels = select("microVoxels", "Block detail", cur, {"Flat faces (fastest)", "Shaded micro-voxels", "Raised micro-voxels"}, "Every block is 32\xC3\x97" "32\xC3\x97" "32 little voxels coloured from its texture."); }
    { int cur = clampv(s.waterQuality, 0, 2); s.waterQuality = select("waterQuality", "Water", cur, {"Flat", "Waves", "Waves and reflections"}, nullptr); }
    { bool sh = s.shadows != 0; check("shadows", "Sun shadows", &sh, "Modern renderer"); s.shadows = sh ? 1 : 0; }
    { int cur = s.particles == 2 ? 0 : s.particles == 1 ? 1 : 2; cur = select("particles", "Particles", cur, {"All", "Fewer", "Off"}, nullptr); s.particles = 2 - cur; }
    { bool cl = s.extra.value("colouredLight", true); check("colouredLight", "Coloured light", &cl, "Redstone glows red, soul fire blue, sea lanterns cyan, the coloured lamps their colour (BTA)."); s.extra["colouredLight"] = cl; }
    { int cur = clampv(s.extra.value("dreamLight", 1), 0, 2); cur = select("dreamLight", "Lighting", cur, {"Classic", "Dreamlike", "Dreamlike (strong)"}, "Dreamlike: a soft glow round bright things, golden light when the sun is low, soft focus (after Horizon's shaders)."); s.extra["dreamLight"] = cur; }
    { static const char* v[] = {"near", "all", "flat"}; int cur = 0; for (int i = 0; i < 3; i++) if (s.plantDetail == v[i]) cur = i;
      s.plantDetail = v[select("plantDetail", "3D plants", cur, {"Near you", "Everywhere (slowest)", "Flat (fastest)"}, "Solid 3D grass, flowers and crops near you; flat further out.")]; }
    { bool g = s.extra.value("grass3d", s.preset != "potato"); bool g0 = g;
      check("grass3d", "3D grass", &g, "Blades of grass waving near you.");
      if (g != g0) s.extra["grass3d"] = g; }
    check("fancyLeaves", "Fancy leaves", &s.fancyLeaves, "See through tree canopies. Costs some speed.");
    check("wavingPlants", "Waving grass, leaves & water", &s.wavingPlants);
    check("clouds", "Clouds", &s.clouds, "Big soft clouds drifting with the wind.");
    check("portalViews", "See through portals", &s.portalViews, "Portals show the other side; you step straight through.");
    { static const int v[] = {0, 30, 60, 120, 144, 240, -1}; int cur = 0; for (int i = 0; i < 7; i++) if (s.fpsLimit == v[i]) cur = i;
      int c = select("fpsLimit", "Frame rate limit", cur, {"Match display (VSync)", "30 fps (saves battery)", "60 fps", "120 fps", "144 fps", "240 fps", "Unlimited"}, nullptr);
      if (c != cur) { s.fpsLimit = v[c]; s.vsync = s.fpsLimit == 0; } }
    check("fullscreen", "Full screen", &s.fullscreen, "F11 switches at any time");
    keepX(); heading("Packs");
    {
      std::vector<std::string> packs = listResourcePacks();
      std::vector<std::string> items = {"Automatic (GeruDoku when installed)", "Built-in DokuQuest textures"};
      int cur = s.resourcePack == "auto" ? 0 : s.resourcePack.empty() || s.resourcePack == "builtin" ? 1 : 0;
      for (size_t i = 0; i < packs.size(); i++) { items.push_back(packs[i]); if (packs[i] == s.resourcePack) cur = (int)i + 2; }
      std::string now = "In use: " + (a.tiles.packName.empty() ? std::string("built-in DokuQuest textures") : a.tiles.packName) + ". Packs in resourcepacks/ (Java 1.13 to 1.21 work best); a new one loads next start.";
      int c = cur;
      ImGui::PushID("pack");
      { Row r("Resource pack", now.c_str(), "", w, 260); if (combo("##pack", &c, items, std::min(260 * z, w * 0.5f))) { s.resourcePack = c == 0 ? "auto" : c == 1 ? "builtin" : packs[c - 2]; s.save(); } }
      ImGui::PopID();
    }
    {
      std::vector<std::string> sps = listShaderPacks();
      std::vector<std::string> items = {"None (the game's own)"};
      int cur = 0;
      for (size_t i = 0; i < sps.size(); i++) { items.push_back(sps[i]); if (sps[i] == s.shaderPack) cur = (int)i + 1; }
      int c = cur;
      ImGui::PushID("shaderpack");
      { Row r("Shader pack", "In shaderpacks/: our own packs (folders of the game's shader files), and Iris/OptiFine packs (.zip or folder: their colour, bloom and other after-effects run; their own lighting and shadows don't). Switches at once.", "", w, 260);
        if (combo("##sp", &c, items, std::min(260 * z, w * 0.5f)) && c != cur) { s.shaderPack = c == 0 ? "" : sps[c - 1]; s.save(); setShaderPack(s.shaderPack); reloadAllShaders(); } }
      ImGui::PopID();
    }
    {
      static const std::vector<std::string> lv = {"Off", "Low: a film grade and vignette", "Medium: and bloom", "High: and sun rays, sharper", "Ultra: all of it, stronger"};
      int c = std::clamp(s.extra.value("shaderLevel", 0), 0, 4);
      ImGui::PushID("shaderlevel");
      { Row r("Shaders", "The game's own shaders over the picture, from a light grade to bloom and sun rays. Works with a shader pack too.", "", w, 260);
        if (combo("##sl", &c, lv, std::min(260 * z, w * 0.5f))) { s.extra["shaderLevel"] = c; s.save(); } }
      ImGui::PopID();
    }
    ImGui::EndGroup();
    nextColumn();

    // ================= right: sound, controls, game =================
    ImGui::BeginGroup();
    heading("Sound");
    range("volume", "Sound volume", &s.volume, 0, 1, 0.05f, pct(s.volume), nullptr);
    range("mobVol", "Mob sounds", &s.mobVol, 0, 1, 0.05f, s.mobVol > 0 ? pct(s.mobVol) : "Off", "Voices and footsteps of animals and monsters.");
    range("music", "Music", &s.music, 0, 1, 0.05f, s.music > 0 ? pct(s.music) : "Off", "Pieces of the place and the hour, a new one every time.");
    { int cur = clampv(s.musicFreq, 0, 3);
      int c = select("musicFreq", "Music frequency", cur, {"Rarely", "Now and then", "Often", "Nearly always"}, "How long the quiet lasts between pieces.");
      if (c != cur) { s.musicFreq = c; applyLight = true; } }
    range("ambience", "Ambient sounds", &s.ambience, 0, 1, 0.05f, pct(s.ambience), "Wind, birds, crickets, rain, and the sounds of caves.");
    keepX(); heading("Controls");
    { ImGui::PushID("controls");
      { Row r("Controls", "The key, mouse button or controller button for every action.", "", w, 200);
        if (button("Change controls\xE2\x80\xA6", BTN_NORMAL, true)) a.pushScreen(makeControlsScreen()); }
      ImGui::PopID(); }
    { int n = s.padMaps.is_object() ? (int)s.padMaps.size() : 0;
      std::string d = (n ? std::to_string(n) + " set up by hand. " : std::string()) + "If a controller's buttons are mixed up, press each one when asked.";
      ImGui::PushID("padsetup");
      { Row r("Controller", d.c_str(), "", w, 230);
        if (button("Set up buttons\xE2\x80\xA6", BTN_NORMAL, true)) a.pushScreen(makePadSetupScreen());
        ImGui::SameLine();
        if (button("Reset##padmaps", BTN_GHOST, true, 0, n == 0)) { s.padMaps = json::object(); a.input.reloadBinds(); s.save(); } }
      ImGui::PopID(); }
    { static const char* v[] = {"xbox", "playstation", "nintendo"}; int cur = 0; for (int i = 0; i < 3; i++) if (s.padLayout == v[i]) cur = i;
      s.padLayout = v[select("padLayout", "Controller buttons", cur, {"Xbox (A jumps)", "PlayStation (\xE2\x9C\x95 jumps)", "Nintendo (B jumps)"}, nullptr)]; }
    range("sens", "Mouse & touch look speed", &s.sens, 0.2f, 3, 0.05f, strFormat("%.2f\xC3\x97", s.sens), nullptr);
    range("padSens", "Controller look speed", &s.padSens, 0.3f, 3, 0.05f, strFormat("%.2f\xC3\x97", s.padSens), nullptr);
    check("invertY", "Invert look up/down", &s.invertY);
    check("toggleSneak", "Toggle sneak", &s.toggleSneak, "Press once to crouch, again to stand.");
    check("toggleSprint", "Toggle sprint", &s.toggleSprint, "Sprint stays on while you move forward.");
    check("autoJump", "Auto-jump (touch)", &s.autoJump);
    keepX(); heading("Game");
    { static const char* v[] = {"auto", "vertical", "horizontal"}; int cur = 0; for (int i = 0; i < 3; i++) if (s.split == v[i]) cur = i;
      s.split = v[select("split", "Two-player split", cur, {"Automatic", "Side by side", "Top and bottom"}, nullptr)]; }
    {
      float ui = 45; const std::string& g = s.guiScale;
      if (!g.empty() && isdigit((unsigned char)g[0])) ui = (float)std::atof(g.c_str()); else if (g == "small") ui = 80; else if (g == "normal") ui = 100; else if (g == "large") ui = 125;
      float before = ui;
      range("uiScale", "Interface size", &ui, 45, 200, 5, ui < 50 ? "Auto" : std::to_string((int)ui) + "%", "Menus, hotbar and text. All the way left is Auto.");
      if (ui != before) pendingUi = ui;
      if (pendingUi > 0 && !ImGui::IsMouseDown(0)) { s.guiScale = pendingUi < 50 ? "auto" : std::to_string((int)pendingUi); pendingUi = -1; }
    }
    // BTA's rearrangeable HUD: drag the hotbar, hearts, boss bar, chat and messages where you like (ui/hud_edit.cpp)
    if (a.session) { if (button("Move HUD pieces\xE2\x80\xA6", BTN_NORMAL, true)) a.pushScreen(makeHudEditScreen()); }
    else if (s.extra.contains("hudLayout") && !s.extra["hudLayout"].empty()) { if (button("Put the HUD back where it was", BTN_GHOST, true)) { s.extra.erase("hudLayout"); s.save(); } }
    check("fpBody", "Your body in first person", &s.fpBody, "Look down to see your chest, legs and armour.");
    check("heldSway", "Held item sway", &s.heldSway, "What you hold lags a touch behind when you turn.");
    { bool he = s.extra.value("holidayEvents", true);
      check("holidayEvents", "Holiday events", &he, "Seasonal surprises from your calendar, like Halloween in October.");
      s.extra["holidayEvents"] = he; }
    check("showFps", "Show FPS", &s.showFps);
    { bool eb = s.extra.value("engineBridge", false);
      check("engineBridge", "Engine bridge", &eb, "Lets the launcher's Engine page and coding tools on this computer (like Strata) talk to the game while it runs: commands, shaders reloaded live, screenshots. Port 25590, this computer only.");
      s.extra["engineBridge"] = eb; }
    // BTA's accessibility page: captions, the colour-blind filters, and the motion that can make some people unwell
    keepX(); heading("Accessibility");
    check("captions", "Captions", &s.captions, "Words at the side of the screen for the sounds around you, with the way they come from.");
    { int cur = clampv(s.colorFilter, 0, 3); s.colorFilter = select("colorFilter", "Colour filter", cur, {"Off", "Red-blind (protanopia)", "Green-blind (deuteranopia)", "Blue-blind (tritanopia)"}, "Shifts the colours you may not tell apart into ones you can."); }
    check("viewBob", "View bobbing", &s.viewBob);
    check("screenShake", "Screen shake", &s.screenShake, "Explosions and boss slams shake the view.");
    check("hurtTilt", "Damage tilt", &s.hurtTilt, "The view tilts a little when you are hurt.");
    ImGui::EndGroup();
    if (two && leftBottom > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(leftBottom);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(1, 8 * z));
    pn.endBody();
    pn.end();
    if (applyLight && a.audio) a.audio->setVolumes(s);
    if (applyNow) { a.applySettings(); SDL_SetWindowFullscreen(a.window, s.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
    if (pn.closed) a.popScreen();
  }
  float pendingUi = -1;
};
std::unique_ptr<Screen> makeOptionsScreen() { return std::make_unique<OptionsScreen>(); }

// ---- Settings → Controls ----------------------------------------------------------------------------------
class ControlsScreen : public Screen {
public:
  const char* id() const override { return "controls"; }
  std::string capKey; int capFrames = 0; bool capPad = false; double capT = 0, padDoneAt = -1;
  bool onBack(App&) override { return !capKey.empty() || padDoneAt > 0; }
  void onClose(App& a) override { a.input.setupActive = false; ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; }
  void stopCapture(App& a) { capKey.clear(); capPad = false; ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; }
  void update(App& a, double) override {
    Settings& S = a.settings; Input& in = a.input;
    if (padDoneAt > 0 && a.time >= padDoneAt) { padDoneAt = -1; in.setupActive = false; }
    if (capKey.empty()) return;
    if (++capFrames < 3) return;
    if (!capPad) {
      if (in.lastCode.empty()) return;
      std::string code = in.lastCode;
      if (code == "Escape") { stopCapture(a); return; }
      json arr = json::array(); if (code != "Backspace" && code != "Delete") arr.push_back(code);
      S.keyBinds[capKey] = arr; in.reloadBinds(); S.save(); stopCapture(a);
    } else {
      if (a.time - capT > 10) { in.setupActive = false; stopCapture(a); return; }
      if (in.lastPadButton < 0 || in.lastPadButton >= 16) return;
      S.padBinds[capKey] = json::array({in.lastPadButton}); in.reloadBinds(); S.save();
      stopCapture(a); padDoneAt = a.time + 0.25;   // let go before the menu reads it
    }
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    Settings& S = a.settings; Input& in = a.input;
    float z = Z();
    Panel pn;
    pn.begin("##controls", "Settings", 420, capKey.empty() ? "Done" : nullptr);
    pn.beginBody();
    float w = pn.width - 32 * z - ImGui::GetStyle().ScrollbarSize;
    ImGui::SetCursorPos(ImVec2(16 * z, 14 * z));
    ImGui::BeginGroup();
    bool back = button("\xE2\x80\xB9 Back to settings", BTN_NORMAL, true);
    ImGui::SameLine(w - textSize(F_MID, "Reset all to default").x - 20 * z + 16 * z);
    if (button("Reset all to default", BTN_GHOST, true)) { S.keyBinds = json::object(); S.padBinds = json::object(); in.reloadBinds(); S.save(); }
    ImGui::Dummy(ImVec2(1, 4 * z));
    // which codes / buttons are used more than once
    std::map<std::string, int> kUse; std::map<int, int> pUse;
    auto keysOf = [&](const ActInfo& r) { std::vector<std::string> v = r.keys; auto it = S.keyBinds.find(r.id); if (it != S.keyBinds.end() && it->is_array()) { v.clear(); for (auto& c : *it) if (c.is_string()) v.push_back(c); } return v; };
    auto padsOf = [&](const ActInfo& r) { std::vector<int> v = r.pad; auto it = S.padBinds.find(r.id); if (it != S.padBinds.end() && it->is_array()) { v.clear(); for (auto& c : *it) if (c.is_number_integer()) v.push_back(c); } return v; };
    for (auto& r : actionTable()) { if (r.keyRow) for (auto& c : keysOf(r)) kUse[c]++; if (r.padRow) for (int k : padsOf(r)) pUse[k]++; }
    float c1 = std::floor(w * 1.3f / 3.3f), c2 = std::floor((w - c1 - 16 * z) / 2);
    auto head = [&](const char* t, float x) { ImVec2 p = ImGui::GetCursorScreenPos(); text(ImGui::GetWindowDrawList(), F_SMALL, ImVec2(p.x + x, p.y), col::muted, t, 0, 12 / 12.5f); };
    head("ACTION", 0); head("KEYBOARD & MOUSE", c1 + 8 * z); head("CONTROLLER", c1 + c2 + 16 * z);
    ImGui::Dummy(ImVec2(w, 18 * z));
    for (auto& r : actionTable()) {
      ImGui::PushID(r.id);
      ImVec2 p = ImGui::GetCursorScreenPos();
      text(ImGui::GetWindowDrawList(), F_BODY, ImVec2(p.x, p.y + 6 * z), col::text, ellipsize(F_BODY, r.label, c1).c_str(), 0, 14.f / 15.f);
      ImGui::SetCursorScreenPos(ImVec2(p.x + c1 + 8 * z, p.y));
      if (r.keyRow) {
        auto ks = keysOf(r); std::string t;
        for (auto& c : ks) t += (t.empty() ? "" : " / ") + keyName(c);
        if (t.empty()) t = "\xE2\x80\x94";
        bool clash = false; for (auto& c : ks) clash |= kUse[c] > 1;
        bool listening = capKey == r.id && !capPad;
        cell(listening ? "Press a key\xE2\x80\xA6 (Backspace clears, Esc cancels)" : t, c2, clash, listening, "k", [&]() { capKey = r.id; capPad = false; capFrames = 0; ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; });
      } else ImGui::Dummy(ImVec2(c2, 30 * z));
      ImGui::SetCursorScreenPos(ImVec2(p.x + c1 + c2 + 16 * z, p.y));
      if (r.padRow) {
        auto ps = padsOf(r); std::string t;
        for (int k : ps) t += (t.empty() ? "" : " / ") + padButtonName(k, S.padLayout);
        if (t.empty()) t = "\xE2\x80\x94";
        bool clash = false; for (int k : ps) clash |= pUse[k] > 1;
        bool listening = capKey == r.id && capPad;
        cell(listening ? "Press a button on the controller\xE2\x80\xA6" : t, c2, clash, listening, "p", [&]() { capKey = r.id; capPad = true; capFrames = 0; capT = a.time; in.setupActive = true; });
      } else ImGui::Dummy(ImVec2(c2, 30 * z));
      ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 34 * z));
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1, 6 * z));
    label(F_SMALL, col::muted, "Esc always pauses and can't be changed. Red means two actions share a key: both will happen. On a controller, tap sneak twice to crawl. A controller with mixed-up buttons? Set it up first (Settings \xE2\x86\x92 Controller), then change actions here.", w);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 10 * z));
    pn.endBody();
    pn.end();
    if (pn.closed || back) { a.popScreen(); return; }
    // while listening, a cover over everything takes the mouse click as the new binding
    if (!capKey.empty() && !capPad) {
      ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2((float)a.width, (float)a.height));
      ImGui::Begin("##capcover", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
      ImGui::End();
    }
  }
  template <class F> void cell(const std::string& t, float w, bool clash, bool listening, const char* id, F onClick) {
    float z = Z();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton(id, ImVec2(w, 30 * z), ImGuiButtonFlags_EnableNav)) { playClick(); onClick(); }
    bool hot = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 border = listening ? col::ember : clash ? col::rgb(0xa8402e) : hot ? col::line : 0;
    if (border) dl->AddRect(p, p + ImVec2(w, 30 * z), border, 4 * z, 0, 2 * z);
    ImU32 tc = listening ? col::ember : clash ? col::rgb(0xff7a6a) : hot ? col::text : col::muted;
    std::string s = listening ? t : ellipsize(F_MID, t, w - 12 * z);
    float wrap = listening ? w - 12 * z : 0;
    ImVec2 ts = textSize(F_MID, s.c_str(), wrap);
    dl->PushClipRect(p, p + ImVec2(w, 30 * z), true);
    text(dl, F_MID, ImVec2(p.x + std::max(6 * z, (w - ts.x) * 0.5f), p.y + std::max(0.f, (30 * z - ts.y) * 0.5f)), tc, s.c_str(), wrap);
    dl->PopClipRect();
  }
};
std::unique_ptr<Screen> makeControlsScreen() { return std::make_unique<ControlsScreen>(); }

// ---- the controller setup wizard (padsetup.js) ------------------------------------------------------------
class PadSetupScreen : public Screen {
public:
  const char* id() const override { return "padsetup"; }
  struct Step { char kind; int idx; const char* what; const char* hint; };
  std::vector<Step> steps = {
    {'b', 0, "the BOTTOM face button", "A on Xbox \xC2\xB7 \xE2\x9C\x95 on PlayStation \xC2\xB7 B on Nintendo"},
    {'b', 1, "the RIGHT face button", "B on Xbox \xC2\xB7 \xE2\x97\xAF on PlayStation \xC2\xB7 A on Nintendo"},
    {'b', 2, "the LEFT face button", "X on Xbox \xC2\xB7 \xE2\x96\xA2 on PlayStation \xC2\xB7 Y on Nintendo"},
    {'b', 3, "the TOP face button", "Y on Xbox \xC2\xB7 \xE2\x96\xB3 on PlayStation \xC2\xB7 X on Nintendo"},
    {'b', 4, "the left shoulder button", "LB \xC2\xB7 L1 \xC2\xB7 L"}, {'b', 5, "the right shoulder button", "RB \xC2\xB7 R1 \xC2\xB7 R"},
    {'b', 6, "the left trigger", "LT \xC2\xB7 L2 \xC2\xB7 ZL"}, {'b', 7, "the right trigger", "RT \xC2\xB7 R2 \xC2\xB7 ZR"},
    {'b', 8, "the small left button", "View / Back \xC2\xB7 Share / Select \xC2\xB7 \xE2\x88\x92"}, {'b', 9, "the small right button", "Menu / Start \xC2\xB7 Options \xC2\xB7 +"},
    {'b', 10, "the left stick, pressed in", "L3"}, {'b', 11, "the right stick, pressed in", "R3"},
    {'b', 12, "the d-pad UP", ""}, {'b', 13, "the d-pad DOWN", ""}, {'b', 14, "the d-pad LEFT", ""}, {'b', 15, "the d-pad RIGHT", ""},
    {'a', 0, "the LEFT stick to the RIGHT", "push it all the way, then let go"}, {'a', 1, "the LEFT stick DOWN", "push it all the way, then let go"},
    {'a', 2, "the RIGHT stick to the RIGHT", "push it all the way, then let go"}, {'a', 3, "the RIGHT stick DOWN", "push it all the way, then let go"},
  };
  int padIndex = -1, step = 0; std::string padId;
  RawPad base, rest; bool haveBase = false, waitRelease = false; double idleT = 0;
  json map;
  void onOpen(App& a) override {
    a.input.setupActive = true;
    map = {{"b", json::array()}, {"ax", json::array({nullptr, nullptr, nullptr, nullptr})}};
    for (int i = 0; i < 16; i++) map["b"].push_back(nullptr);
  }
  void onClose(App& a) override { a.input.setupActive = false; }
  void finish(App& a, bool save) {
    if (save && !padId.empty()) {
      if (!a.settings.padMaps.is_object()) a.settings.padMaps = json::object();
      a.settings.padMaps[padId] = map;
      a.input.reloadBinds(); a.settings.save();
      a.toast("Controller set up", "good");
    }
    a.popScreen();
  }
  void next(App& a) { step++; haveBase = false; waitRelease = true; idleT = 0; if (step >= (int)steps.size()) finish(a, true); }
  bool used(const char* k, int v) { for (auto& m : map["b"]) if (m.is_object() && m.contains(k) && m[k] == v) return true; return false; }
  void update(App& a, double dt) override {
    Input& in = a.input;
    if (padIndex < 0) {
      // the first pad that does anything is the one we set up
      for (int i = 0; i < in.padCount(); i++) {
        RawPad s; if (!in.rawSnapshot(i, s)) continue;
        bool any = false; for (u8 b : s.buttons) any |= b != 0;
        if (any) { padIndex = i; padId = s.name; rest = s; base = s; std::fill(base.buttons.begin(), base.buttons.end(), 0); haveBase = true; waitRelease = true; break; }
      }
      return;
    }
    RawPad s; if (!in.rawSnapshot(padIndex, s)) { finish(a, false); return; }
    // let go of everything first: buttons up, sticks, triggers and the d-pad back where they rest
    if (waitRelease) {
      bool released = true;
      for (u8 b : s.buttons) released &= b == 0;
      for (size_t i = 0; i < s.axes.size(); i++) released &= std::fabs(s.axes[i] - (i < rest.axes.size() ? rest.axes[i] : 0.f)) < 0.3f;
      for (u8 h : s.hats) released &= h == 0;
      if (released) { waitRelease = false; base = s; haveBase = true; }
      return;
    }
    if (!haveBase) { base = s; haveBase = true; }
    const Step& S = steps[step];
    idleT += dt; if (idleT > 12) { next(a); return; }   // nothing happened: skip it
    if (S.kind == 'b') {
      for (size_t i = 0; i < s.buttons.size(); i++) if (s.buttons[i] && !(i < base.buttons.size() && base.buttons[i]) && !used("b", (int)i)) {
        map["b"][S.idx] = {{"b", (int)i}}; ui::playClick(); next(a); return;
      }
      for (size_t i = 0; i < s.hats.size(); i++) if (s.hats[i] && (i >= base.hats.size() || !base.hats[i])) {
        map["b"][S.idx] = {{"h", (int)i}, {"m", (int)s.hats[i]}}; ui::playClick(); next(a); return;
      }
      for (size_t i = 0; i < s.axes.size(); i++) {
        float d = s.axes[i] - (i < base.axes.size() ? base.axes[i] : 0.f);
        if (std::fabs(d) > 0.6f) { map["b"][S.idx] = {{"a", (int)i}, {"v", std::round(s.axes[i] * 1000) / 1000}, {"base", base.axes[i]}}; ui::playClick(); next(a); return; }
      }
    } else {
      for (size_t i = 0; i < s.axes.size(); i++) {
        float b0 = i < base.axes.size() ? base.axes[i] : 0.f, d = s.axes[i] - b0;
        if (std::fabs(d) > 0.7f && std::fabs(b0) < 0.3f) { map["ax"][S.idx] = {{"a", (int)i}, {"s", d > 0 ? 1 : -1}, {"base", std::round(b0 * 100) / 100}}; ui::playClick(); next(a); return; }
      }
    }
  }
  bool onBack(App& a) override { return padIndex >= 0; }   // a pad's B is a button to map here
  void draw(App& a) override {
    if (a.topScreen() != this || step >= (int)steps.size()) return;
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 150));
    float cw = std::min(440 * z, W - 32 * z);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(cw, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * z, 20 * z));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::rgb(0x1e1813, 245));
    ImGui::Begin("##padsetup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), col::line, 6 * z, 0, 2 * z);
    float iw = cw - 48 * z;
    auto center = [&](FontId f, const std::string& t, ImU32 c, float k = 1) {
      ImVec2 ts = textSize(f, t.c_str(), iw, k); ImVec2 p = ImGui::GetCursorScreenPos();
      text(dl, f, ImVec2(p.x + std::max(0.f, (iw - ts.x) * 0.5f), p.y), c, t.c_str(), iw, k); ImGui::Dummy(ImVec2(iw, ts.y + 4 * z));
    };
    center(F_H3, "Set up a controller", col::ember, 26 / 24.f);
    const Step& S = steps[step];
    center(F_BIG, std::string(S.kind == 'a' ? "Move " : "Press ") + S.what, col::text);
    center(F_MID, S.hint, col::muted);
    ImVec2 p = ImGui::GetCursorScreenPos(); p.y += 10 * z;
    dl->AddRectFilled(p, p + ImVec2(iw, 6 * z), IM_COL32(255, 255, 255, 26), 3 * z);
    dl->AddRectFilled(p, p + ImVec2(iw * step / (float)steps.size(), 6 * z), col::ember, 3 * z);
    ImGui::Dummy(ImVec2(iw, 24 * z));
    center(F_MID, padId.empty() ? "Use the controller you want to set up." : padId, col::muted);
    float bw = (iw - 8 * z) * 0.5f;
    if (button("Skip this one", BTN_GHOST, true, bw)) next(a);
    ImGui::SameLine(0, 8 * z);
    if (button("Cancel", BTN_GHOST, true, bw)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(); finish(a, false); return; }
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
  }
};
std::unique_ptr<Screen> makePadSetupScreen() { return std::make_unique<PadSetupScreen>(); }

} // namespace dq
