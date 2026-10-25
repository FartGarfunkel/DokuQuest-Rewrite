// ui/creator_screen.cpp — the voxel character creator (ui/creator.js, grown into a Cube World / Veloren /
// Hytale-style creator): peoples, body proportions, head shape, face, hair, beards, ears, horns, clothes and
// their colours, accessories, hats, cloaks and capes, with a live turning 3D model. Saves a character file
// (userDir()/characters/<name>.json) you can wear (Settings::skin = "creator:<name>") and exports a 64x64 skin.
// Plain ImGui, self-contained, so the UI theme can restyle it.
#include "ui/char_screens.h"
#include "ui/char_preview.h"
#include "ui/file_pick.h"
#include "ui/layout_flow.h"
#include "core/registry.h"
#include "client/app.h"
#include "client/session.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {

void wearSkin(App& a, const std::string& ref) {
  a.settings.skin = ref;
  a.settings.save();
  invalidateCharacter(ref);
  if (a.session && a.session->player) a.session->player->skin = ref;
}

namespace {
ImVec4 hexCol(const std::string& h) {
  if (h.size() < 7) return ImVec4(0.5f, 0.5f, 0.5f, 1);
  unsigned v = (unsigned)std::strtoul(h.c_str() + 1, nullptr, 16);
  return ImVec4(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, 1);
}
std::string colHex(const float* c) { return strFormat("#%02x%02x%02x", clampv((int)std::lround(c[0] * 255), 0, 255), clampv((int)std::lround(c[1] * 255), 0, 255), clampv((int)std::lround(c[2] * 255), 0, 255)); }
std::string label(const CCList& l, const std::string& v) { for (auto& p : l) if (p.first == v) return p.second; return l.empty() ? v : l[0].second; }

class CreatorScreen : public Screen {
public:
  explicit CreatorScreen(const std::string& character) : startChar(character) {}
  const char* id() const override { return "creator"; }
  bool wantsMouse() const override { return true; }

  void onOpen(App& a) override {
    std::string ref = a.settings.skin;
    std::string want = !startChar.empty() ? startChar : startsWith(ref, "creator:") ? ref.substr(8) : "";
    SkinImage s;
    if (!want.empty() && loadCharacter(want, look, s)) { name = want; status = "Editing " + want; }
    else {
      look = ccDefaultLook(std::nullopt);
      name = a.settings.playerName.empty() ? "character" : sanitizeName(a.settings.playerName);
      status = "A starting look";
    }
    own = resolveCharacter(startsWith(ref, "creator:") ? "" : ref)->skin;   // your own skin, for "wear it under"
    watchDroppedFiles();
    apply(false);
    dirty = false;
  }

  void update(App& a, double dt) override {
    std::string path;   // creator.js importFile: an imported skin becomes the skin your 3D character wears
    if (picker.take(path)) { if (!path.empty()) importOwn(path); else if (!picker.error().empty()) status = picker.error(); }
    if (takeDroppedFile(path, "png")) importOwn(path);
    // the cape you wear in the world (the Cape slot) shows on the model when the character has none of its own
    wornCape.clear(); wornCapeName.clear();
    if (a.session && a.session->player && !gearHidden(*a.session->player, 5)) {
      std::string k = wornItem(*a.session->player, "cape");
      if (const ItemDef* d = k.empty() ? nullptr : reg().item(k)) { wornCape = d->str("cape").empty() ? k : d->str("cape"); wornCapeName = d->name; }
    }
    t += (float)dt;
    if (spin && !dragging) yaw += (float)dt * 0.5f;
  }

  bool onBack(App& a) override {
    if (confirmLeave) { confirmLeave = false; return true; }
    if (dirty) { confirmLeave = true; return true; }
    return false;
  }

  void draw(App& a) override {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 WS = flow::windowSize((float)a.width, (float)a.height, 1180, 760, 10);
    float W = WS.x, H = WS.y;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Character creator##cc", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    // ---- top bar (wraps when the window is narrow) ----
    ImGui::SetNextItemWidth(std::min(200.f * ui::Z(), W * 0.3f)); ImGui::InputText("Name", &name);
    flow::Row top; top.first = false;
    if (top.button("Random")) { CharLook o = look; push(); look = ccDefaultLook(std::nullopt); keep3d(o); apply(); status = "A new face"; }
    if (top.button("Random (same sex)")) { CharLook o = look; push(); look = ccDefaultLook(std::nullopt, o.sex); keep3d(o); apply(); status = "A new face"; }
    ImGui::BeginDisabled(undo.empty());
    if (top.button("Undo")) { redo.push_back(look); look = undo.back(); undo.pop_back(); apply(); }
    ImGui::EndDisabled(); ImGui::BeginDisabled(redo.empty());
    if (top.button("Redo")) { undo.push_back(look); look = redo.back(); redo.pop_back(); apply(); }
    ImGui::EndDisabled();
    if (top.button(picker.busy() ? "Choosing..." : "Import skin PNG...")) picker.open("Your own skin under the character (64x32 or 64x64 PNG)", "png");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wear your own skin under a 3D character (or drop a skin .png on the window)");
    if (top.button("Load...")) ImGui::OpenPopup("load##cc");
    if (ImGui::BeginPopup("load##cc")) {
      auto list = listCharacters();
      if (list.empty()) ImGui::TextDisabled("No saved characters yet");
      for (auto& n : list) if (ImGui::Selectable(n.c_str())) { SkinImage s; push(); if (loadCharacter(n, look, s)) { name = n; apply(false); status = "Loaded " + n; } }
      ImGui::EndPopup();
    }
    ImGui::Separator();
    // ---- options (left) and the model (right) ----
    float left = W * 0.48f;
    // what the bottom bar needs (status line + its rows of buttons) is kept free below the two panels
    ImGuiStyle& sty = ImGui::GetStyle();
    int botRows = flow::rowsNeeded({{"Save & wear", 150}, {"Save", 100}, {"Export skin PNG", 150}, {"Paint by hand...", 150}, {"Cape editor...", 150}, {"Back", 100}}, ImGui::GetContentRegionAvail().x);
    float botH = ImGui::GetTextLineHeightWithSpacing() + botRows * (ImGui::GetFrameHeight() + sty.ItemSpacing.y) + sty.WindowPadding.y;
    float bodyH = std::max(120.f, ImGui::GetContentRegionAvail().y - botH);
    ImGui::BeginChild("opts##cc", ImVec2(left, bodyH), ImGuiChildFlags_Borders);
    options();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("view##cc", ImVec2(0, bodyH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float ph = std::max(40.f, avail.y - ImGui::GetFrameHeightWithSpacing() - 4);
    CharPreviewOpts o; o.yaw = yaw; o.elev = elev; o.dist = dist; o.time = t; o.walk = walk;
    if (showArmor) o.armor = {"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"};
    if (look.cape == "none" && !wornCape.empty()) o.cape = wornCape;
    ImVec2 at = ImGui::GetCursorScreenPos();
    GLuint tex = preview.render(*spec, (int)(avail.x * io.DisplayFramebufferScale.x), (int)(ph * io.DisplayFramebufferScale.y), o);
    previewImage(tex, avail.x, ph);
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("turn##cc", ImVec2(avail.x, ph));
    dragging = ImGui::IsItemActive();
    if (dragging) { yaw += io.MouseDelta.x * 0.012f; elev = clampv(elev + io.MouseDelta.y * 0.01f, -1.3f, 1.3f); }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0) dist = clampv(dist * (io.MouseWheel < 0 ? 1.1f : 1 / 1.1f), 28.f, 140.f);
    // the right stick turns it (controller)
    yaw += (padAxis(ImGuiKey_GamepadRStickRight) - padAxis(ImGuiKey_GamepadRStickLeft)) * (float)io.DeltaTime * 2.6f;
    elev = clampv(elev + (padAxis(ImGuiKey_GamepadRStickDown) - padAxis(ImGuiKey_GamepadRStickUp)) * (float)io.DeltaTime * 1.8f, -1.3f, 1.3f);
    ImGui::Checkbox("Turn", &spin); ImGui::SameLine(); ImGui::Checkbox("Walk", &walk); ImGui::SameLine(); ImGui::Checkbox("Try on armour", &showArmor);
    ImGui::SameLine(); if (ImGui::Button("Reset view")) { yaw = PI + 0.45f; elev = 0.15f; dist = 70; }
    ImGui::EndChild();
    // ---- bottom bar ----
    ImGui::TextDisabled("%s", status.c_str());
    flow::Row bot;
    if (bot.button("Save & wear", 150)) save(a, true);
    if (bot.button("Save", 100)) save(a, false);
    if (bot.button("Export skin PNG", 150)) {
      fs::path p = skinsDir() / (sanitizeName(name) + ".png");
      status = saveSkinPng(p, skin) ? "Exported " + p.filename().string() + " to your skins folder" : "Could not export the skin";
    }
    if (bot.button("Paint by hand...", 150)) {
      save(a, false);
      a.pushScreen(makeSkinEditorScreen(skin, sanitizeName(name), sanitizeName(name)));
      ImGui::End(); return;
    }
    if (bot.button("Cape editor...", 150)) {   // paint the cape you wear (cape_screen.cpp)
      a.pushScreen(makeCapeEditorScreen());
      ImGui::End(); return;
    }
    if (bot.button("Back", 100)) { if (dirty) confirmLeave = true; else { ImGui::End(); a.popScreen(); return; } }
    if (confirmLeave) ImGui::OpenPopup("Unsaved changes##cc");
    if (ImGui::BeginPopupModal("Unsaved changes##cc", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Save this character before leaving?");
      bool close = false;
      if (ImGui::Button("Save & close")) { save(a, false); close = true; }
      ImGui::SameLine(); if (ImGui::Button("Discard")) close = true;
      ImGui::SameLine(); if (ImGui::Button("Keep editing")) { confirmLeave = false; ImGui::CloseCurrentPopup(); }
      if (close) { confirmLeave = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::End(); a.popScreen(); return; }
      ImGui::EndPopup();
    }
    ImGui::End();
  }

private:
  std::string startChar, name, status;
  CharLook look;
  SkinImage skin, own;
  std::shared_ptr<CharacterSpec> spec = std::make_shared<CharacterSpec>();
  std::vector<CharLook> undo, redo;
  CharPreview preview;
  float yaw = PI + 0.45f, elev = 0.15f, dist = 70, t = 0;
  bool spin = true, walk = false, showArmor = false, dragging = false, dirty = false, confirmLeave = false;

  FilePicker picker;
  std::string wornCape, wornCapeName;
  void importOwn(const std::string& path) {
    SkinImage s; std::string err;
    if (!loadSkinFile(path, s, &err)) { status = err.empty() ? "That file is not a skin" : err; return; }
    push();
    own = s;
    look.model3d = true; look.own = true;
    apply();
    status = "Imported " + fs::path(path).filename().string() + " as your character's skin";
  }
  void push() { undo.push_back(look); if (undo.size() > 100) undo.erase(undo.begin()); redo.clear(); }
  void keep3d(const CharLook& o) {   // a new face, still the same 3D people
    if (!o.model3d) { look.model3d = false; return; }
    look.model3d = true; look.own = o.own; ccSetSpecies(look, o.species);
  }
  void apply(bool mark = true) {
    if (look.model3d && look.own) ccPaintOverOwn(look, own, skin);
    else ccPaintSkin(look, skin);
    if (look.own && look.model3d) skin.slim = own.slim;
    spec = std::make_shared<CharacterSpec>();
    spec->skin = skin; spec->body = look.shape; spec->cape = look.cape == "none" ? "" : look.cape;
    spec->hash = skin.hash() ^ (look.shape.hash() * 31) ^ std::hash<std::string>()(spec->cape);
    if (mark) dirty = true;
  }
  void save(App& a, bool wear) {
    name = sanitizeName(name);
    if (saveCharacter(name, look, skin)) {
      dirty = false;
      status = "Saved " + name;
      if (wear) { wearSkin(a, "creator:" + name); status = "You are now " + name; }
    } else status = "Could not save";
  }
  // ---- rows ----
  void row(const char* lab, const std::string& val, const std::string& key) {
    ImGui::PushID(key.c_str());
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(lab);
    ImGui::SameLine(130);
    if (ImGui::ArrowButton("<", ImGuiDir_Left)) { push(); ccCycle(look, key, -1); apply(); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(180);
    ImGui::TextUnformatted(val.c_str());
    ImGui::SameLine(330);
    if (ImGui::ArrowButton(">", ImGuiDir_Right)) { push(); ccCycle(look, key, 1); apply(); }
    ImGui::PopID();
  }
  void listRow(const char* lab, const std::string& key, const std::string& cur) { row(lab, label(ccList(key), cur), key); }
  void swatches(const char* lab, const std::vector<std::string>& pal, std::string& cur, const char* key, std::function<void()> after = nullptr) {
    ImGui::PushID(key);
    ImGui::TextDisabled("%s", lab);
    int n = 0;
    for (auto& c : pal) {
      if (n++ % 12) ImGui::SameLine();
      ImGui::PushID(n);
      bool on = toLower(c) == toLower(cur);
      if (on) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 1));
      if (ImGui::ColorButton(c.c_str(), hexCol(c), ImGuiColorEditFlags_NoTooltip | (on ? 0 : ImGuiColorEditFlags_NoBorder), ImVec2(20, 20))) { push(); cur = c; if (after) after(); apply(); }
      if (on) ImGui::PopStyleColor();
      ImGui::PopID();
    }
    ImGui::SameLine();
    ImVec4 v = hexCol(cur); float f3[3] = {v.x, v.y, v.z};
    if (ImGui::ColorEdit3("##custom", f3, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
      if (!ImGui::IsItemDeactivated() && !colourEditing) { push(); colourEditing = true; }
      cur = colHex(f3); if (after) after(); apply();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) colourEditing = false;
    ImGui::PopID();
  }
  bool colourEditing = false;
  void slider(const char* lab, float& v, float lo, float hi) {
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(lab); ImGui::SameLine(130);
    ImGui::SetNextItemWidth(220);
    float before = v;
    if (ImGui::SliderFloat((std::string("##") + lab).c_str(), &v, lo, hi, "%.2f")) { if (!sliding) { CharLook o = look; o.shape = look.shape; undo.push_back(o); sliding = true; } apply(); }
    if (ImGui::IsItemDeactivated()) sliding = false;
    (void)before;
  }
  bool sliding = false;

  void options() {
    CharLook& L = look;
    auto sep = [](const char* t) { ImGui::Spacing(); ImGui::SeparatorText(t); };
    sep("Model");
    row("Model", L.model3d ? "3D character" : "Classic skin", "model");
    if (L.model3d) {
      listRow("People", "species", L.species);
      row("Skin", L.own ? "Your own skin" : "Creator's look", "own");
      ImGui::TextWrapped("A 3D character wears real hair, beards, ears, horns, hats, robes and cloaks over the skin, and stands at its people's height. It still works as a normal skin anywhere else.");
    } else ImGui::TextWrapped("A classic flat skin, like Minecraft's: everything is painted on the boxes.");
    sep("Body");
    row("Sex", L.sex == "f" ? "Female" : "Male", "sex");
    row("Arms", L.body == "slim" ? "Slender (slim arms)" : "Broad (classic arms)", "body");
    slider("Height", L.shape.height, 0.8f, 1.2f);
    slider("Build", L.shape.build, 0.8f, 1.25f);
    slider("Head size", L.shape.head, 0.85f, 1.25f);
    slider("Leg length", L.shape.legs, 0.8f, 1.2f);
    listRow("Head shape", "headShape", L.shape.headShape);
    swatches("Skin tone", ccPalette("skin"), L.skin, "skin");
    row("Features", ccFeatName(ccFeatIndex(L)), "feat");
    sep("Face");
    listRow("Eyes", "eyes", L.eyes);
    swatches("Eye colour", ccPalette("eye"), L.eyeCol, "eyeCol");
    listRow("Brows", "brows", L.brows);
    row("Nose", L.nose ? "Big" : "Small", "nose");
    listRow("Mouth", "mouth", L.mouth);
    listRow("Markings", "marks", L.marks);
    listRow("Ears", "ears", L.ears);
    listRow("Horns", "horns", L.horns);
    if (L.horns != "none") swatches("Horn colour", ccPalette("horn"), L.hornCol, "hornCol");
    sep("Hair");
    listRow("Hair", "hair", L.hair);
    listRow("Beard", "beard", L.beard);
    swatches("Hair colour", ccPalette("hair"), L.hairCol, "hairCol", [&] { look.beardCol = look.hairCol; });
    sep("Clothes");
    {
      const CCOutfit* o = ccOutfit(L.outfit);
      row("Outfit", o ? o->name : L.outfit, "outfit");
    }
    static const char* PARTS[4] = {"Main", "Trim", "Legs", "Boots"};
    for (int i = 0; i < 4; i++) swatches(PARTS[i], ccPalette("outfit"), L.cols[i], PARTS[i]);
    listRow("Shoes", "shoes", L.shoes);
    listRow("Belt", "belt", L.belt);
    listRow("Gloves", "gloves", L.gloves);
    listRow("Headwear", "hat", L.hat);
    row("Cloak", L.cloak ? "Yes" : "No", "cloak");
    listRow("Cape", "cape", L.cape);
    if (L.cape == "none" && !wornCapeName.empty()) { ImGui::SameLine(); ImGui::TextDisabled("(showing your %s)", wornCapeName.c_str()); }
    sep("Details");
    listRow("Neck", "neck", L.neck);
    listRow("Eyewear", "eyewear", L.eyewear);
    row("Helmet", L.helm == "hide" ? "Hidden" : "Shown", "helm");
    ImGui::Spacing();
    ImGui::TextDisabled("Every change shows on the 3D model straight away. Save keeps the look; Paint by hand opens the skin editor.");
  }
};
}

std::unique_ptr<Screen> makeCreatorScreen(const std::string& character) { return std::make_unique<CreatorScreen>(character); }
std::unique_ptr<Screen> makeCreatorScreen() { return std::make_unique<CreatorScreen>(""); }

} // namespace dq
