// ui/options.cpp — Options, laid out like Better than Adventure 8.0's options screen (ScreenOptions / OptionsPages):
// the pages General, Player, Audio, Video, Controls, Accessibility, Asset Packs, Language Packs and Search down the
// left, each page folding categories of rows (ui/paged.h). Every setting of ui.js buildSettings is here, under the
// BTA page and category it belongs to; keys and controller buttons are rebound right on the Controls page
// (input/controls.js: every key, mouse button and controller button, clashes in red). Also the controller setup
// wizard (input/padsetup.js: press each button and move each stick once; the pad is remembered by its name).
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/tileset.h"
#include "audio/audio.h"
#include "render/gl.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>

namespace dq { std::vector<std::string> listShaderPacks(); int reloadAllShaders(); }   // render/gl.cpp

namespace dq {
using namespace ui;

namespace {
int controlGroup(const std::string& id) {
  if (id == "attack" || id == "use" || id == "pick" || id == "drop" || id == "swap" || id == "place" || id == "spell" || id == "castQuick" || id == "castLeft" || id == "stable") return 0;
  if (id == "forward" || id == "back" || id == "left" || id == "right" || id == "jump" || id == "sneak" || id == "sprint" || id == "crawl") return 1;
  if (id == "inventory" || id == "chat" || id == "command" || id == "skills" || id == "map" || id == "journal" || id == "spellbook" || id == "playerList" || id == "pause") return 2;
  if (id == "hideHud") return 3;
  if (id.rfind("slot", 0) == 0 || id == "prev" || id == "next" || id == "hotbarRow") return 4;
  if (id == "view" || id == "shoulder" || id == "zoom" || id == "debug") return 5;
  return 6;
}
// BTA's key categories on the Controls page
const char* const KEY_CATS[] = {"Interactions", "Movement", "User Interface", "HUD Editor", "Hotbar", "View", "Miscellaneous"};
std::string pct(float v) { return std::to_string((int)std::lround(v * 100)) + "%"; }
void openFolder(const fs::path& p) {
  std::error_code ec;
  fs::create_directories(p, ec);
  std::string g = fs::absolute(p, ec).generic_string();
#ifdef _WIN32
  std::string url = "file:///" + g;
#else
  std::string url = "file://" + g;
#endif
  if (SDL_OpenURL(url.c_str()) != 0) DQ_WARN("could not open %s: %s", url.c_str(), SDL_GetError());
}
template <class T, size_t N> int indexOf(const T (&v)[N], const T& x, int dflt = 0) { for (size_t i = 0; i < N; i++) if (v[i] == x) return (int)i; return dflt; }
int indexOfStr(const char* const* v, int n, const std::string& x, int dflt = 0) { for (int i = 0; i < n; i++) if (x == v[i]) return i; return dflt; }

enum Page { P_GENERAL, P_PLAYER, P_AUDIO, P_VIDEO, P_CONTROLS, P_ACCESS, P_PACKS, P_LANG, P_SEARCH, P_COUNT };
const std::vector<PagedPage>& optionPages() {
  // BTA's icons: grass, a statue, a record, a flag, a boat, a spawner wand, a painting, a book, glass
  // the same pictures as the launcher's Options tab (the pack's flat block and item pictures)
  static const std::vector<PagedPage> p = {{"General", "block/grass_block_side"}, {"Player", "item/armor_stand"}, {"Audio", "item/music_disc_cat"},
    {"Video", "item/painting"}, {"Controls", "item/oak_boat"}, {"Accessibility", "item/spyglass"}, {"Asset Packs", "item/flower_banner_pattern"},
    {"Language Packs", "item/book"}, {"Search", "block/glass"}};
  return p;
}
}

class OptionsScreen : public Screen {
public:
  explicit OptionsScreen(int startPage = 0) { kit.page = startPage; }
  const char* id() const override { return "settings"; }
  void onClose(App& a) override { a.applySettings(); a.input.setupActive = false; ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; }
  bool onBack(App&) override { return !capKey.empty() || padDoneAt > 0 || backConsumedByText(); }

  // ---- key / button capture (input/controls.js) ----
  std::string capKey; int capFrames = 0; bool capPad = false; double capT = 0, padDoneAt = -1;
  void stopCapture() { capKey.clear(); capPad = false; ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; }
  void update(App& a, double) override {
    Settings& S = a.settings; Input& in = a.input;
    if (padDoneAt > 0 && a.time >= padDoneAt) { padDoneAt = -1; in.setupActive = false; }
    if (capKey.empty()) return;
    if (++capFrames < 3) return;
    if (!capPad) {
      if (in.lastCode.empty()) return;
      std::string code = in.lastCode;
      if (code == "Escape") { stopCapture(); return; }
      json arr = json::array(); if (code != "Backspace" && code != "Delete") arr.push_back(code);
      S.keyBinds[capKey] = arr; in.reloadBinds(); S.save(); stopCapture();
    } else {
      if (a.time - capT > 10) { in.setupActive = false; stopCapture(); return; }
      if (in.lastPadButton < 0 || in.lastPadButton >= 16) return;
      S.padBinds[capKey] = json::array({in.lastPadButton}); in.reloadBinds(); S.save();
      stopCapture(); padDoneAt = a.time + 0.25;   // let go before the menu reads it
    }
  }

  void draw(App& a) override {
    if (a.topScreen() != this) return;
    applyNow = applyLight = false;
    kit.begin(a, "##options", "Options", optionPages());
    kit.beginContent();
    if (kit.page == P_SEARCH) {
      searchBox(a);
      if (kit.filter.empty()) kit.note("Type part of a setting's name: every page is searched.");
      else for (int p = 0; p < P_SEARCH; p++) drawPage(a, p);
      if (!kit.filter.empty() && ImGui::GetCursorPosY() < 40 * kit.u()) { kit.filter.swap(searchHold); kit.note("Nothing matches \xE2\x80\x9C" + searchHold + "\xE2\x80\x9D."); kit.filter.swap(searchHold); }
    } else {
      drawPage(a, kit.page);
    }
    kit.endContent();
    int f = kit.footer({"Done"}, {!capKey.empty()});
    kit.end();
    Settings& s = a.settings;
    if (applyLight && a.audio) a.audio->setVolumes(s);
    if (applyNow) { a.applySettings(); SDL_SetWindowFullscreen(a.window, s.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
    if (f == 0) { a.popScreen(); return; }
    // while listening for a key, a cover over everything takes a mouse click as the new binding
    if (!capKey.empty() && !capPad) {
      ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2((float)a.width, (float)a.height));
      ImGui::Begin("##capcover", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
      ImGui::End();
    }
  }

private:
  Paged kit;
  bool applyNow = false, applyLight = false;
  float pendingUi = -1;
  std::string packPick = "DokuQuest Base", searchHold;
  bool packReady = false;

  void searchBox(App& a) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float U = kit.u();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 2 * U));
    float fh = textSize(F_BODY, "Ag").y;
    ImGui::PushFont(font(F_BODY));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * U, std::max(0.f, (20 * U - fh) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.f, Z()));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(160, 160, 160, 255));
    ImGui::SetNextItemWidth(kit.cw);
    if (kit.pageChanged) { ImGui::SetKeyboardFocusHere(); kit.skipFocus(); }
    ImGui::InputTextWithHint("##search", "Search\xE2\x80\xA6", &kit.filter);
    ImGui::PopStyleColor(2); ImGui::PopStyleVar(3); ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 26 * U));
    ImGui::Dummy(ImVec2(kit.cw, 0));
    (void)a;
  }

  void drawPage(App& a, int pg) {
    Settings& s = a.settings;
    static const Settings D{};
    bool rel = false;
    switch (pg) {
    case P_GENERAL: {
      if (kit.category("Gameplay")) {
        if (kit.range("Field of View", &s.fov, 55, 100, 1, std::to_string((int)std::lround(s.fov)) + "\xC2\xB0", D.fov, "How wide you see.", &rel)) applyLight = true;
        if (a.session && a.session->level && !a.session->level->info.remote) {   // BTA's Difficulty: this world's
          static const char* DIFFS[] = {"peaceful", "easy", "normal", "hard"};
          std::string& d = a.session->level->info.difficulty;
          int cur = indexOfStr(DIFFS, 4, d, 2);
          if (kit.cycle("Difficulty", &cur, {"Peaceful", "Easy", "Normal", "Hard"}, 2, "This world's difficulty. Peaceful: no monsters appear on their own.")) d = DIFFS[cur];
        }
        if (kit.toggle("View Bobbing", &s.viewBob, D.viewBob, "The view sways as you walk.")) applyNow = true;
        if (kit.toggle("Pause on Lost Focus", &s.pauseOnLostFocus, D.pauseOnLostFocus, "Opens the pause menu a moment after this window is no longer the one in front. A world shared with others keeps going.")) applyNow = true;
        { static const char* v[] = {"auto", "vertical", "horizontal"}; int cur = indexOfStr(v, 3, s.split);
          if (kit.cycle("Two-Player Split", &cur, {"Automatic", "Side by Side", "Top and Bottom"}, 0, "How the screen splits when a second player joins with a controller.", 120)) { s.split = v[cur]; applyNow = true; } }
      }
      if (kit.category("User Interface")) {
        float ui = 45; const std::string& g = s.guiScale;
        if (!g.empty() && isdigit((unsigned char)g[0])) ui = (float)std::atof(g.c_str()); else if (g == "small") ui = 80; else if (g == "normal") ui = 100; else if (g == "large") ui = 125;
        if (pendingUi > 0) ui = pendingUi;
        bool released = false;
        if (kit.range("GUI Scale", &ui, 45, 300, 5, ui < 50 ? "Auto" : std::to_string((int)ui) + "%", 45, "Menus, hotbar and text. Auto is 100% on 1080p, 125% on 2K, 150% on 3K and 200% on 4K. All the way left is Auto.", &released)) pendingUi = ui;
        if (pendingUi > 0 && (released || !ImGui::IsMouseDown(0))) { s.guiScale = pendingUi < 50 ? "auto" : std::to_string((int)pendingUi); pendingUi = -1; }
        if (kit.toggle("FPS in Overlay", &s.showFps, D.showFps, "The frame rate in the corner.")) applyNow = true;
        { bool dk = s.extra.value("bookDark", false);
          if (kit.toggle("Dark Book Pages", &dk, false, "The Help, Journal, Skills, Spellbook, Stable and the book inventory on dark pages with light writing, easier on the eyes. The moon on any open book switches it too.")) { s.extra["bookDark"] = dk; s.save(); } }
        { bool bi = s.extra.value("bookInventory", true);
          if (kit.toggle("Book Inventory", &bi, true, "Your inventory as an open book: you, your gear and your skills on the left page, crafting and your bag on the right (in Creative, the item catalogue). Chests, furnaces, the trommel, crafting tables and every station open as books too: the block on the left, your bag on the right. Off: the classic panels.")) { s.extra["bookInventory"] = bi; s.save(); } }
        if (a.session) { if (kit.action("HUD Layout", "Edit HUD\xE2\x80\xA6", "Move the HUD's pieces where you like them.")) a.pushScreen(makeHudEditScreen()); }
        else if (s.extra.contains("hudLayout") && !s.extra["hudLayout"].empty()) { if (kit.action("HUD Layout", "Reset", "Put the HUD back where it was.")) { s.extra.erase("hudLayout"); s.save(); } }
      }
      if (kit.category("Miscellaneous")) {
        float f = (float)(clampv(s.autosaveSec, 0, 115) / 5);
        int step = (int)f;
        if (kit.range("Autosave", &f, 0, 23, 1, step == 0 ? "Off" : std::to_string(step * 5) + " seconds", (float)(D.autosaveSec / 5), "How often the world saves itself, in steps of 5 seconds, up to 115. Off still saves when you leave.")) s.autosaveSec = (int)std::lround(f) * 5;
        { bool he = s.extra.value("holidayEvents", true); if (kit.toggle("Holiday Events", &he, true, "Seasonal surprises from your calendar, like Halloween in October.")) s.extra["holidayEvents"] = he; }
        { bool eb = s.extra.value("engineBridge", false); if (kit.toggle("Engine Bridge", &eb, false, "Lets the launcher's Engine page and coding tools on this computer (like Strata) talk to the game while it runs: commands, shaders reloaded live, screenshots. Port 25590, this computer only.")) s.extra["engineBridge"] = eb; }
        if (kit.action("Worlds Folder", "Open", "The folder your saved worlds live in.")) openFolder(a.savesDir());
        if (kit.action("Screenshots Folder", "Open", "Pictures taken in the game.")) openFolder(userSub("screenshots"));
      }
      break;
    }
    case P_PLAYER: {
      if (kit.category("Player")) {
        if (kit.toggle("Body in First Person", &s.fpBody, D.fpBody, "Look down to see your chest, legs and armour.")) applyNow = true;
        if (kit.toggle("Held Item Sway", &s.heldSway, D.heldSway, "What you hold lags a touch behind when you turn.")) applyNow = true;
      }
      break;
    }
    case P_AUDIO: {
      if (kit.category("Audio")) {
        if (kit.toggle("Subtitles", &s.captions, D.captions, "Words at the side of the screen for the sounds around you, with the way they come from.")) applyNow = true;
        if (kit.toggle("Mute on Lost Focus", &s.muteOnLostFocus, D.muteOnLostFocus, "Silence while another window is in front. Your volume sliders stay as they are.")) applyNow = true;
        if (kit.range("Master Volume", &s.volume, 0, 1, 0.05f, s.volume > 0 ? pct(s.volume) : "Off", D.volume)) applyLight = true;
        if (kit.range("Music", &s.music, 0, 1, 0.05f, s.music > 0 ? pct(s.music) : "Off", D.music, "Pieces of the place and the hour, a new one every time.")) applyLight = true;
        { int cur = clampv(s.musicFreq, 0, 3); if (kit.cycle("Music Frequency", &cur, {"Rarely", "Now and Then", "Often", "Nearly Always"}, D.musicFreq, "How long the quiet lasts between pieces.", 120)) { s.musicFreq = cur; applyLight = true; } }
        if (kit.range("World Sounds", &s.ambience, 0, 1, 0.05f, s.ambience > 0 ? pct(s.ambience) : "Off", D.ambience, "Wind, birds, crickets, rain, and the sounds of caves.")) applyLight = true;
        if (kit.range("Entity Sounds", &s.mobVol, 0, 1, 0.05f, s.mobVol > 0 ? pct(s.mobVol) : "Off", D.mobVol, "Voices and footsteps of animals and monsters.")) applyLight = true;
      }
      break;
    }
    case P_VIDEO: {
      if (kit.category("Display")) {
        std::string rn = rendererName(gfx.vulkan ? RendererKind::Vulkan : gfx.modern ? RendererKind::Modern : RendererKind::Legacy); if (!rn.empty()) rn[0] = (char)toupper((unsigned char)rn[0]);
        std::string tip = std::string(gfx.vulkan ? "Vulkan" : "OpenGL ") + (gfx.vulkan ? "" : std::to_string(gfx.major) + "." + std::to_string(gfx.minor)) + ". Change it in the launcher's Options.";
        kit.action("Renderer", rn.c_str(), tip.c_str(), true);
        if (kit.toggle("Full Screen", &s.fullscreen, D.fullscreen, "F11 switches at any time.")) applyNow = true;
        { static const int v[] = {0, 30, 60, 120, 144, 240, -1}; int cur = s.vsync ? 0 : 6; if (!s.vsync) for (int i = 1; i < 6; i++) if (s.fpsLimit == v[i]) cur = i;
          if (kit.cycle("FPS Limit", &cur, {"VSync", "30 FPS", "60 FPS", "120 FPS", "144 FPS", "240 FPS", "Unlimited"}, 0, "VSync matches the display. 30 saves battery.")) { s.fpsLimit = v[cur]; s.vsync = s.fpsLimit == 0; applyNow = true; } }
        if (kit.range("Render Scale", &s.scale, 0.4f, 1, 0.05f, pct(s.scale), D.scale, "Render fewer pixels for smoother play.", &rel)) applyLight = true;
      }
      if (kit.category("Visuals")) {
        { int cur = clampv(s.extra.value("dreamLight", 0), 0, 2); if (kit.cycle("Lighting", &cur, {"Classic", "Dreamlike", "Dreamlike (Strong)"}, 0, "Dreamlike: a soft glow round bright things, golden light when the sun is low, soft focus (after Horizon's shaders).", 120)) { s.extra["dreamLight"] = cur; applyNow = true; } }
        if (kit.range("Brightness", &s.brightness, 0, 1, 0.05f, s.brightness <= 0 ? "Moody" : s.brightness >= 1 ? "Bright" : pct(s.brightness), D.brightness, "Lights up caves and the night, like Minecraft's Brightness. Full daylight stays the same.", &rel)) applyLight = true;
        {   // Settings::applyColourStyle; moving a slider makes it Custom (render/colour_filter_layer.cpp draws it)
          static const char* styles[] = {"vibrant", "natural", "classic", "custom"};
          int cur = 3; for (int i = 0; i < 4; i++) if (s.colourStyle == styles[i]) cur = i;
          int pick = cur;
          if (kit.cycle("Colour Style", &pick, {"Vibrant", "Natural", "Classic", "Custom"}, 1, "Vibrant: the colours as drawn. Natural: a little softer. Classic: muted and flatter, like Minecraft Alpha and Beta (blacks less deep on an OLED).", 120) && pick != cur) {
            if (pick < 3) s.applyColourStyle(styles[pick]); else s.colourStyle = "custom";
            applyLight = true;
          }
          if (kit.range("Saturation", &s.saturation, 0, 1.5f, 0.05f, pct(s.saturation), D.saturation, "How colourful the world is. 100% is the colours as drawn.", &rel)) { s.colourStyle = "custom"; applyLight = true; }
          if (kit.range("Contrast", &s.contrast, 0.5f, 1.5f, 0.05f, pct(s.contrast), D.contrast, "How far apart the darks and lights are. Lower is flatter and softer.", &rel)) { s.colourStyle = "custom"; applyLight = true; }
        }
        { bool cl = s.extra.value("colouredLight", true); if (kit.toggle("Coloured Light", &cl, true, "Redstone glows red, soul fire blue, sea lanterns cyan, the coloured lamps their colour (BTA).")) { s.extra["colouredLight"] = cl; applyNow = true; } }
        { int cur = clampv(s.microVoxels, 0, 2); if (kit.cycle("Block Detail", &cur, {"Flat", "Shaded", "Raised"}, D.microVoxels, "Every block is 32\xC3\x97" "32\xC3\x97" "32 little voxels coloured from its texture. Flat is fastest; Raised gives the voxels depth.")) { s.microVoxels = cur; applyNow = true; } }
        { int cur = clampv(s.waterQuality, 0, 2); if (kit.cycle("Water", &cur, {"Flat", "Waves", "Reflections"}, D.waterQuality, "Reflections are waves and reflections.")) { s.waterQuality = cur; applyNow = true; } }
        { bool sh = s.shadows != 0; if (kit.toggle("Sun Shadows", &sh, D.shadows != 0, "Soft shadows from the sun and moon (Modern renderer).")) { s.shadows = sh ? 1 : 0; applyNow = true; } }
        if (kit.toggle("Clouds", &s.clouds, D.clouds, "Big soft clouds drifting with the wind.")) applyNow = true;
        { bool vc = s.extra.value("volumetricClouds", true);
          if (kit.toggle("Volumetric Clouds", &vc, true, "Clouds as real volumes that thicken and darken into the storms. Off: the voxel heaps (lighter on old GPUs).")) { s.extra["volumetricClouds"] = vc; applyNow = true; } }
        if (kit.toggle("Fancy Leaves", &s.fancyLeaves, D.fancyLeaves, "See through tree canopies. Costs some speed.")) applyNow = true;
        { static const char* v[] = {"near", "all", "flat"}; int cur = indexOfStr(v, 3, s.plantDetail);
          if (kit.cycle("3D Plants", &cur, {"Near You", "Everywhere", "Flat"}, 0, "Solid 3D grass, flowers and crops near you; flat further out. Everywhere is the slowest.")) { s.plantDetail = v[cur]; applyNow = true; } }
        { bool g = s.extra.value("grass3d", s.preset != "potato"); if (kit.toggle("3D Grass", &g, s.preset != "potato", "Blades of grass waving near you.")) { s.extra["grass3d"] = g; applyNow = true; } }
        if (kit.toggle("Waving Plants", &s.wavingPlants, D.wavingPlants, "Grass, leaves and water move in the wind.")) applyNow = true;
        if (kit.toggle("See Through Portals", &s.portalViews, D.portalViews, "Portals show the other side; you step straight through.")) applyNow = true;
      }
      if (kit.category("Weather")) {   // render/weather_layer.cpp (after Particle Rain)
        { static const int amounts[] = {0, 600, 1500, 3000}; int cur = 2; for (int i = 0; i < 4; i++) if (s.weatherParticles == amounts[i]) cur = i;
          int pick = cur;
          if (kit.cycle("Weather Particles", &pick, {"Off", "Low", "Normal", "High"}, 2, "Rain, snow, dust and fog around you. Fewer is faster.") && pick != cur) { s.weatherParticles = amounts[pick]; applyNow = true; } }
        { float d = (float)s.weatherDist; if (kit.range("Weather Distance", &d, 12, 32, 2, std::to_string((int)d) + " blocks", (float)D.weatherDist, "How far around you rain and snow fall.", &rel)) { s.weatherDist = (int)d; applyNow = true; } }
        if (kit.toggle("Splashes & Ripples", &s.weatherSurfaceFx, D.weatherSurfaceFx, "Rain splashing, rings on water, drops running down walls.")) { applyNow = true; }
        if (kit.toggle("Ground Fog", &s.groundFog, D.groundFog, "Banks of fog in swamps and jungles, and after the rain.")) { applyNow = true; }
        if (kit.toggle("Height Fog", &s.heightFog, D.heightFog, "Fog lying in the valleys, thinning as you climb.")) { applyNow = true; }
      }
      if (kit.category("Post-Processing")) {
        std::vector<std::string> sps = listShaderPacks();
        std::vector<std::string> items = {"None"};
        int cur = 0;
        for (size_t i = 0; i < sps.size(); i++) { items.push_back(sps[i]); if (sps[i] == s.shaderPack) cur = (int)i + 1; }
        int c = cur;
        if (kit.cycle("Shader Pack", &c, items, 0, "In shaderpacks/: our own packs (folders of the game's shader files), and Iris/OptiFine packs (.zip or folder: their colour, bloom and other after-effects run; their own lighting and shadows don't). Switches at once.", 140) && c != cur) {
          s.shaderPack = c == 0 ? "" : sps[c - 1]; s.save(); setShaderPack(s.shaderPack); reloadAllShaders();
        }
        int lv = std::clamp(s.extra.value("shaderLevel", 0), 0, 4);
        { float cc = std::clamp(s.extra.value("colorCorrectionNether", 0.5f), 0.f, 1.f);
          if (kit.range("Nether Color Correction", &cc, 0, 1, 0.05f, pct(cc), 0.5f, "Better than Adventure's warm grade over the Nether: darker, redder and more golden. 0% leaves the picture as it is.")) s.extra["colorCorrectionNether"] = cc; }
        { float hh = std::clamp(s.extra.value("heatHaze", 0.5f), 0.f, 1.f);
          if (kit.range("Heat Haze", &hh, 0, 1, 0.05f, pct(hh), 0.5f, "Better than Adventure's shimmer over the hot Nether's distance. 0% turns it off.")) s.extra["heatHaze"] = hh; }
        if (kit.cycle("Shaders", &lv, {"Off", "Low", "Medium", "High", "Ultra"}, 0, "The game's own shaders over the picture. Low: a film grade, vignette, a wobble under water. Medium: and a soft bloom, eyes that adjust to the dark. High: and sun rays, a lens flare, water you see into, sun shadows. Ultra: all of it, stronger, with reflections in the water and shading in the corners. Works with a shader pack too.")) { s.extra["shaderLevel"] = lv; s.save(); }
        { static const int v[] = {1, 2, 4, 8, 16}; int cur = indexOf(v, s.extra.value("anisotropic", 4), 2);
          if (kit.cycle("Texture Filtering", &cur, {"Off", "2x", "4x", "8x", "16x"}, 2, "Anisotropic filtering: ground and walls seen at a slant stay sharp into the distance. 4x costs next to nothing.")) { s.extra["anisotropic"] = v[cur]; s.save(); a.applyTextureFiltering(); } }
      }
      if (kit.category("Performance")) {
        { float f = (float)s.renderDist; if (kit.range("Render Distance", &f, 2, 32, 1, std::to_string(s.renderDist) + " chunks", (float)D.renderDist, "Chunks of full detail around you. Split screen caps this a little.", &rel)) s.renderDist = (int)std::lround(f); }
        { static const int v[] = {0, 512, 1024, 2048}; int cur = indexOf(v, s.farTerrain, 3);
          if (kit.cycle("Distant Terrain", &cur, {"Off", "512 Blocks", "1,024 Blocks", "2,048 Blocks"}, 3, "Simplified land past the render distance (see forever). Off is fastest.", 120)) { s.farTerrain = v[cur]; applyNow = true; } }
        { static const int v[] = {0, 768, 1536, 3072}; int mb = s.extra.value("terrainMemoryMB", 0), cur = indexOf(v, mb);
          if (kit.cycle("Terrain Memory", &cur, {"No Limit", "768 MB", "1.5 GB", "3 GB"}, 0, "The graphics memory the land may use. Past it, the chunks farthest out are drawn as distant terrain instead.")) { s.extra["terrainMemoryMB"] = v[cur]; applyNow = true; } }
        { int cur = s.particles == 2 ? 0 : s.particles == 1 ? 1 : 2; if (kit.cycle("Particles", &cur, {"All", "Decreased", "Minimal"}, 0, "Dust, sparks, splashes, smoke.")) { s.particles = 2 - cur; applyNow = true; } }
      }
      if (rel) applyNow = true;
      break;
    }
    case P_CONTROLS: drawControls(a); break;
    case P_ACCESS: {
      if (kit.category("Accessibility")) {
        if (kit.toggle("Subtitles", &s.captions, D.captions, "Words at the side of the screen for the sounds around you, with the way they come from.")) applyNow = true;
        if (kit.toggle("View Bobbing", &s.viewBob, D.viewBob, "The view sways as you walk.")) applyNow = true;
        if (kit.toggle("Screen Motion Effects", &s.screenShake, D.screenShake, "Explosions and boss slams shake the view.")) applyNow = true;
        if (kit.toggle("Damage Tilt", &s.hurtTilt, D.hurtTilt, "The view tilts a little when you are hurt.")) applyNow = true;
        { int cur = clampv(s.colorFilter, 0, 3); if (kit.cycle("Colorblind Assist", &cur, {"None", "Protanopia", "Deuteranopia", "Tritanopia"}, 0, "Shifts the colours you may not tell apart into ones you can: red-blind, green-blind or blue-blind.")) { s.colorFilter = cur; applyNow = true; } }
      }
      break;
    }
    case P_PACKS: drawPacks(a); break;
    case P_LANG: {
      if (kit.category("Language Packs")) {
        if (!kit.searching()) {
          bool dbl = false;
          kit.listRow("lang-en", 18, true, &dbl);
          ImDrawList* dl = ImGui::GetWindowDrawList();
          ImVec2 p = kit.rowMin();
          text(dl, F_MID, p + ImVec2(4, 2) * kit.u(), IM_COL32(255, 255, 255, 255), "English (US)");
          text(dl, F_SMALL, p + ImVec2(4, 11) * kit.u(), IM_COL32(128, 128, 128, 255), "The game's own words");
          kit.note("No other language packs found. A pack in the resource packs folder can add its own words later.");
          if (kit.rowButton("Open Asset Packs Folder", 160)) openFolder(userSub("resourcepacks"));
        }
      }
      break;
    }
    default: break;
    }
  }

  // ---- Controls: mouse, keyboard and controller settings, then every key in BTA's categories ----
  void drawControls(App& a) {
    Settings& S = a.settings; Input& in = a.input;
    static const Settings D{};
    if (kit.category("Mouse")) {
      kit.range("Sensitivity", &S.sens, 0.2f, 3, 0.05f, strFormat("%.2f\xC3\x97", S.sens), D.sens, "Mouse and touch look speed.");
      if (kit.toggle("Invert Mouse", &S.invertY, D.invertY, "Look up and down the other way round.")) applyNow = true;
      if (kit.toggle("Raw Mouse Input", &S.rawMouse, D.rawMouse, "The mouse reports its own movement. Off warps the pointer instead, which some remote desktops prefer.")) applyNow = true;
    }
    if (kit.category("Keyboard")) {
      if (kit.toggle("Toggle Sneaking", &S.toggleSneak, D.toggleSneak, "Press once to crouch, again to stand.")) applyNow = true;
      if (kit.toggle("Toggle Sprinting", &S.toggleSprint, D.toggleSprint, "Sprint stays on while you move forward.")) applyNow = true;
      bool anyKeys = (S.keyBinds.is_object() && !S.keyBinds.empty()) || (S.padBinds.is_object() && !S.padBinds.empty());
      if (kit.action("Reset All Keybinds", "Reset", "Every key and controller button back to how it came.", !anyKeys)) { S.keyBinds = json::object(); S.padBinds = json::object(); in.reloadBinds(); S.save(); }
    }
    if (kit.category("Controller")) {
      kit.range("Look Sensitivity", &S.padSens, 0.3f, 3, 0.05f, strFormat("%.2f\xC3\x97", S.padSens), D.padSens, "How fast the right stick turns the view.");
      { static const char* v[] = {"xbox", "playstation", "nintendo"}; int cur = indexOfStr(v, 3, S.padLayout);
        if (kit.cycle("Button Layout", &cur, {"Xbox", "PlayStation", "Nintendo"}, 0, "Xbox: A jumps. PlayStation: \xE2\x9C\x95 jumps. Nintendo: B jumps.")) { S.padLayout = v[cur]; applyNow = true; } }
      int n = S.padMaps.is_object() ? (int)S.padMaps.size() : 0;
      if (kit.action("Set Up Controller", "Set Up\xE2\x80\xA6", "If a controller's buttons are mixed up, press each one when asked.")) a.pushScreen(makePadSetupScreen());
      std::string forget = n ? std::to_string(n) + " controller" + (n == 1 ? "" : "s") + " set up by hand. Forget them." : "No controllers set up by hand.";
      if (kit.action("Forget Set-Ups", "Reset", forget.c_str(), n == 0)) { S.padMaps = json::object(); in.reloadBinds(); S.save(); }
      if (kit.toggle("Auto-Jump (Touch)", &S.autoJump, D.autoJump, "Step up blocks by walking into them on a touch screen.")) applyNow = true;
    }
    // which codes / buttons are used more than once
    std::map<std::string, int> kUse; std::map<int, int> pUse;
    auto keysOf = [&](const ActInfo& r) { std::vector<std::string> v = r.keys; auto it = S.keyBinds.find(r.id); if (it != S.keyBinds.end() && it->is_array()) { v.clear(); for (auto& c : *it) if (c.is_string()) v.push_back(c); } return v; };
    auto padsOf = [&](const ActInfo& r) { std::vector<int> v = r.pad; auto it = S.padBinds.find(r.id); if (it != S.padBinds.end() && it->is_array()) { v.clear(); for (auto& c : *it) if (c.is_number_integer()) v.push_back(c); } return v; };
    for (auto& r : actionTable()) { if (r.keyRow) for (auto& c : keysOf(r)) kUse[c]++; if (r.padRow) for (int k : padsOf(r)) pUse[k]++; }
    const float U = kit.u();
    for (int cat = 0; cat < 7; cat++) {
      if (!kit.category(KEY_CATS[cat])) continue;
      if (cat == 3 && a.session && kit.action("HUD Editor", "Open\xE2\x80\xA6", "Move the HUD's pieces where you like them.")) a.pushScreen(makeHudEditScreen());
      for (auto& r : actionTable()) {
        if (controlGroup(r.id) != cat) continue;
        const bool custom = S.keyBinds.contains(r.id) || S.padBinds.contains(r.id);
        ImVec2 cp;
        std::string id = r.id;
        if (!kit.custom(r.label, "Click the key box, then press the key (Backspace clears it, Esc cancels). Click the controller box, then press the button. Red: two actions share it, and both will happen.", 204, &cp, !custom,
              [&]() { S.keyBinds.erase(id); S.padBinds.erase(id); in.reloadBinds(); S.save(); })) continue;
        float avail = std::min(204 * U, ImGui::GetContentRegionMax().x);
        float bw = std::floor((avail - 4 * U) * 0.5f);
        // keyboard
        {
          auto ks = keysOf(r); std::string t;
          for (auto& c : ks) t += (t.empty() ? "" : " / ") + keyName(c);
          if (t.empty()) t = "NONE";
          bool clash = false; for (auto& c : ks) clash |= kUse[c] > 1;
          bool listening = capKey == r.id && !capPad;
          std::string lb = (listening ? "> ? <" : t) + "###k";
          ImGui::SetCursorScreenPos(cp);
          if (!r.keyRow) ImGui::Dummy(ImVec2(bw, 20 * U));
          else {
            if (classicButton(lb.c_str(), ImVec2(bw, 20 * U), !capKey.empty() && !listening)) { capKey = r.id; capPad = false; capFrames = 0; ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; }
            if (clash || listening) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), listening ? IM_COL32(255, 255, 160, 255) : IM_COL32(255, 85, 85, 255), 0, 0, std::max(1.f, U));
          }
        }
        // controller
        {
          auto ps = padsOf(r); std::string t;
          for (int k : ps) t += (t.empty() ? "" : " / ") + padButtonName(k, S.padLayout);
          if (t.empty()) t = "NONE";
          bool clash = false; for (int k : ps) clash |= pUse[k] > 1;
          bool listening = capKey == r.id && capPad;
          std::string lb = (listening ? "> ? <" : t) + "###p";
          ImGui::SetCursorScreenPos(ImVec2(cp.x + bw + 4 * U, cp.y));
          if (!r.padRow) ImGui::Dummy(ImVec2(bw, 20 * U));
          else {
            if (classicButton(lb.c_str(), ImVec2(bw, 20 * U), !capKey.empty() && !listening)) { capKey = r.id; capPad = true; capFrames = 0; capT = a.time; in.setupActive = true; }
            if (clash || listening) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), listening ? IM_COL32(255, 255, 160, 255) : IM_COL32(255, 85, 85, 255), 0, 0, std::max(1.f, U));
          }
        }
        kit.customEnd();
      }
    }
    if (!kit.searching()) kit.note("Esc always pauses and can't be changed. On a controller, tap sneak twice to crawl. A controller with mixed-up buttons? Set it up first (Controller \xE2\x86\x92 Set Up), then change actions here.");
  }

  // ---- Asset Packs: BTA's Textures, Selected and Available lists ----
  void drawPacks(App& a) {
    Settings& s = a.settings;
    std::vector<std::string> packs = listResourcePacks();
    auto baseChoice = [](const std::string& n) { return n.empty() || n == "auto" || n == "none" || n == "builtin" || n == "DokuQuest Base"; };
    packs.erase(std::remove(packs.begin(), packs.end(), std::string("DokuQuest Base")), packs.end());
    if (!packReady) { packPick = baseChoice(s.resourcePack) ? "DokuQuest Base" : s.resourcePack; packReady = true; }
    if (packPick != "DokuQuest Base" && std::find(packs.begin(), packs.end(), packPick) == packs.end()) packPick = "DokuQuest Base";
    const std::string used = a.tiles.packName.empty() ? std::string("DokuQuest Base") : a.tiles.packName;
    const float U = kit.u();
    if (kit.category("Textures")) {
      bool pending = packPick != (baseChoice(s.resourcePack) ? std::string("DokuQuest Base") : s.resourcePack);
      if (kit.action("Apply Asset Packs", "Apply", "Keeps the packs you picked. They load the next time you start.", !pending)) {
        s.resourcePack = packPick.empty() ? "DokuQuest Base" : packPick; s.save();
        a.toast("Asset pack saved. It loads the next time you start.", "good");
      }
      if (kit.action("Asset Packs Folder", "Open", "Put packs (folders or .zip) here.")) openFolder(userSub("resourcepacks"));
      kit.note("In use now: " + used + ". DokuQuest Base always stays underneath.");
    }
    auto packRow = [&](const std::string& name, const char* sub, bool sel) {
      bool hit = kit.listRow(name.c_str(), 22, sel);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = kit.rowMin();
      pageIcon(dl, "item/painting", p + ImVec2(2, 3) * U, 16 * U);
      text(dl, F_MID, p + ImVec2(22, 2) * U, IM_COL32(255, 255, 255, 255), ellipsize(F_MID, name, kit.cw - 26 * U).c_str());
      text(dl, F_SMALL, p + ImVec2(22, 12) * U, IM_COL32(128, 128, 128, 255), sub);
      return hit;
    };
    if (kit.category("Selected Asset Packs") && !kit.searching()) {
      if (packPick != "DokuQuest Base" && packRow(packPick, "Click to take it off", true)) { packPick = "DokuQuest Base"; playClick(); }
      packRow("DokuQuest Base", "Always on. The game's own art.", packPick == "DokuQuest Base");
    }
    if (kit.category("Available Asset Packs") && !kit.searching()) {
      int shown = 0;
      for (auto& pk : packs) {
        if (pk == packPick) continue;
        shown++;
        if (packRow(pk, "Click to put it on", false)) { packPick = pk; playClick(); }
      }
      if (!shown) kit.note("No asset packs found :(");
    }
  }
};
std::unique_ptr<Screen> makeOptionsScreen() { return std::make_unique<OptionsScreen>(); }
// BTA keeps the keys on the Controls page; the old Controls screen opens it there
std::unique_ptr<Screen> makeControlsScreen() { return std::make_unique<OptionsScreen>(P_CONTROLS); }

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
