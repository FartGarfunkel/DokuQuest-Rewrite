// ui/skineditor_screen.cpp — the skin editor (ui/skineditor.js): a flat 64x64 pixel grid and the turnable 3D
// voxel character, both paintable (the 3D view finds the pixel under the cursor by casting a ray against the
// body's boxes), with pencil, eraser, fill, eyedropper, palette, brush sizes, mirror, undo/redo, part isolation,
// slim/classic arms, start from current/default/blank, import, export and save. Mouse, keyboard and controller
// (View button: paint with the stick). Plain ImGui, self-contained, so the UI theme can restyle it.
#include "ui/char_screens.h"
#include "ui/char_preview.h"
#include "ui/file_pick.h"
#include "client/app.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {

namespace {
const char* PALETTE[] = {"#000000", "#3b3b3b", "#7a7a7a", "#b5b5b5", "#ffffff", "#5a3a22", "#8a5a34", "#c68e5a", "#e8bf96", "#f5d9bd",
                         "#7a1f1f", "#c0392b", "#ff6b4a", "#ffb07a", "#f2c14e", "#ffe680", "#2e6b2e", "#4caf50", "#a5d66b", "#1f5f6b",
                         "#2f8f86", "#7fd6cf", "#1f3f8a", "#3c6fd6", "#8fb5ff", "#4a2a7a", "#8e5ad6", "#d6a5ff", "#a03a6b", "#ff8fc0"};
const char* TOOLS[4] = {"Pencil", "Eraser", "Fill", "Eyedropper"};
const char* FACE_NAMES[6] = {"right", "left", "top", "bottom", "back", "front"};
std::array<u8, 4> hexRGBA(const char* h) { unsigned v = (unsigned)std::strtoul(h + 1, nullptr, 16); return {(u8)(v >> 16), (u8)(v >> 8), (u8)v, 255}; }
ImVec4 toV(const std::array<u8, 4>& c) { return ImVec4(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, 1); }

class SkinEditorScreen : public Screen {
public:
  SkinEditorScreen(const SkinImage* start, std::string nm, std::string ch) : name(std::move(nm)), character(std::move(ch)) { if (start) { img = *start; haveStart = true; } }
  ~SkinEditorScreen() override { for (GLuint* t : {&skinTex, &checkTex, &maskTex}) if (*t && glDeleteTextures) glDeleteTextures(1, t); }
  const char* id() const override { return "skineditor"; }

  void onOpen(App& a) override {
    if (!haveStart) {
      startFrom(a, "current", false);
      std::string ref = skinArms(a.settings.skin).first;
      name = startsWith(ref, "creator:") ? ref.substr(8) : ref.empty() ? sanitizeName(a.settings.playerName) : fs::path(ref).stem().string();
      if (startsWith(ref, "creator:")) character = ref.substr(8);
    }
    if (name.empty()) name = "skin";
    watchDroppedFiles();
    changed(); rebuild(); dirty = false;
  }
  void onClose(App&) override { if (padMode) setPad(false); }
  bool onBack(App&) override {
    if (padMode) { setPad(false); return true; }
    if (confirm) { confirm = false; return true; }
    if (dirty) { confirm = true; return true; }
    return false;
  }
  void update(App&, double dt) override {
    std::string path;   // the system file chooser, or a skin dropped on the window
    if (picker.take(path)) { if (!path.empty()) importFile(path); else if (!picker.error().empty()) status(picker.error()); }
    if (takeDroppedFile(path, "png")) importFile(path);
    rebuildT += (float)dt; if (specDirty && (!stroking || rebuildT > 0.12f)) rebuild(); if (msgT > 0) msgT -= (float)dt; }

  void draw(App& a) override {
    ImGuiIO& io = ImGui::GetIO();
    float W = std::min((float)a.width - 16, 1260.f), H = std::min((float)a.height - 16, 780.f);
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin(character.empty() ? "Skin editor##ske" : "Skin editor - character##ske", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    keys(a);
    hoverInfo.clear();
    float side = 230, body = H - 110;
    // ---- tools ----
    ImGui::BeginChild("tools##ske", ImVec2(150, body), ImGuiChildFlags_Borders);
    for (int i = 0; i < 4; i++) if (ImGui::Selectable(TOOLS[i], tool == i)) setTool(i);
    ImGui::SeparatorText("Brush");
    for (int s = 1; s <= 3; s++) { if (s > 1) ImGui::SameLine(); if (ImGui::RadioButton(std::to_string(s).c_str(), size == s)) size = s; }
    ImGui::SeparatorText("Options");
    ImGui::Checkbox("Mirror (M)", &mirror);
    ImGui::Checkbox("Outer layer (O)", &outer);
    ImGui::Checkbox("Grid (H)", &grid);
    if (ImGui::Button(layer == 0 ? "Paint: auto" : layer == 1 ? "Paint: base" : "Paint: outer")) layer = (layer + 1) % 3;
    ImGui::BeginDisabled(undoS.empty()); if (ImGui::Button("Undo")) doUndo(); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(redoS.empty()); if (ImGui::Button("Redo")) doRedo(); ImGui::EndDisabled();
    if (ImGui::Button("Reset views (R)")) resetViews();
    ImGui::SeparatorText("Controller");
    if (ImGui::Button(padMode ? "Stop stick painting" : "Paint with stick")) setPad(!padMode);
    ImGui::TextDisabled("View: stick cursor\nA paint, B stop\nLB/RB tool, X/Y undo/redo\nLT/RT zoom, R-stick turn");
    ImGui::EndChild();
    ImGui::SameLine();
    // ---- the two views ----
    float vw = (W - 150 - side - 40) * 0.5f;
    ImGui::BeginChild("flat##ske", ImVec2(vw, body), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawFlat(io);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("3d##ske", ImVec2(vw, body), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw3D(io);
    ImGui::EndChild();
    ImGui::SameLine();
    // ---- colour, parts, arms, start, files ----
    ImGui::BeginChild("side##ske", ImVec2(0, body), ImGuiChildFlags_Borders);
    sidePanel(a);
    ImGui::EndChild();
    // ---- foot ----
    std::string st = msgT > 0 ? msg : !hoverInfo.empty() ? hoverInfo : dirty ? "Unsaved changes" : "";
    ImGui::TextDisabled("%s", st.c_str());
    ImGui::SetNextItemWidth(220); ImGui::InputText("Name##ske", &name);
    ImGui::SameLine();
    if (ImGui::Button(character.empty() ? "Save skin" : "Save character", ImVec2(150, 0))) save(a);
    ImGui::SameLine();
    if (ImGui::Button("Character creator...", ImVec2(170, 0))) {
      if (dirty) save(a);
      std::string c = character;
      ImGui::End();
      a.popScreen();
      a.pushScreen(makeCreatorScreen(c));
      return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(100, 0))) { if (dirty) confirm = true; else { ImGui::End(); a.popScreen(); return; } }
    if (confirm) ImGui::OpenPopup("Unsaved changes##ske");
    if (ImGui::BeginPopupModal("Unsaved changes##ske", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Save this skin before leaving?");
      bool close = false;
      if (ImGui::Button("Save & close")) { save(a); close = true; }
      ImGui::SameLine(); if (ImGui::Button("Discard")) close = true;
      ImGui::SameLine(); if (ImGui::Button("Keep editing")) { confirm = false; ImGui::CloseCurrentPopup(); }
      if (close) { confirm = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::End(); a.popScreen(); return; }
      ImGui::EndPopup();
    }
    ImGui::End();
  }

private:
  SkinImage img;
  bool haveStart = false;
  std::string name, character, msg, hoverInfo, importPath;
  FilePicker picker;
  float msgT = 0;
  int tool = 0, size = 1, layer = 0, iso = -1;   // layer: 0 auto, 1 base, 2 outer
  bool mirror = false, outer = true, grid = true, dirty = false, confirm = false, stroking = false, padMode = false;
  std::array<u8, 4> color{60, 111, 214, 255};
  std::vector<std::string> recent;
  std::vector<SkinImage> undoS, redoS;
  bool pendingUndo = false;
  // flat view
  float fz = 8, fox = 0, foy = 0; bool fit = true; int hov = -1, lastFlat = -1;
  // 3D view
  float yaw = PI + 0.45f, elev = 0.22f, dist = 64; int hov3 = -1, last3 = -1; vec2 last3Pos{0}; bool rotating = false;
  vec3 hitPoint{0}; bool hitOk = false;
  CharPreview preview;
  std::shared_ptr<CharacterSpec> spec;
  bool specDirty = true; float rebuildT = 0;
  GLuint skinTex = 0, checkTex = 0, maskTex = 0; bool texDirty = true; int maskKey = -99;
  vec2 pad{32, 32};

  void status(const std::string& s) { msg = s; msgT = 2.5f; }
  void setTool(int t) { tool = t; status(TOOLS[t]); }
  void resetViews() { fit = true; yaw = PI + 0.45f; elev = 0.22f; dist = 64; }
  void setPad(bool on) {
    padMode = on;
    ImGuiIO& io = ImGui::GetIO();
    if (on) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad; else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  }
  // ---- editing (skineditor.js) ----
  const SkinMap& map() const { return skinMap(img.slim); }
  bool valid(int j, bool mir = false) const { int e = map().map[j]; return e >= 0 && (mir || iso < 0 || map().entries[e].part == iso); }
  void pushUndo() { undoS.push_back(img); if (undoS.size() > 100) undoS.erase(undoS.begin()); redoS.clear(); dirty = true; }
  void changed() { texDirty = true; specDirty = true; }
  void put(int j, bool mir) {
    if (!valid(j, mir)) return;
    if (pendingUndo) { pushUndo(); pendingUndo = false; }
    u8* p = img.px.data() + j * 4;
    if (tool == 1) std::memset(p, 0, 4); else std::memcpy(p, color.data(), 4);
    dirty = true;
  }
  void apply(int i) {
    int x0 = i % 64, y0 = i / 64, o = (size - 1) / 2;
    for (int dy = 0; dy < size; dy++) for (int dx = 0; dx < size; dx++) {
      int x = x0 + dx - o, y = y0 + dy - o;
      if (x < 0 || y < 0 || x > 63 || y > 63) continue;
      put(y * 64 + x, false);
      if (mirror) { int m = skinMirrorPixel(map(), x, y); if (m >= 0) put(m, true); }
    }
    changed();
  }
  void lineTo(int a, int b) {
    int x0 = a % 64, y0 = a / 64, x1 = b % 64, y1 = b / 64, dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while (x0 != x1 || y0 != y1) { int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } apply(y0 * 64 + x0); }
  }
  void addRecent() {
    std::string h = strFormat("#%02x%02x%02x", color[0], color[1], color[2]);
    recent.erase(std::remove(recent.begin(), recent.end(), h), recent.end());
    recent.insert(recent.begin(), h);
    if (recent.size() > 10) recent.resize(10);
  }
  void strokeStart(int i, bool alt) {
    if (i < 0) return;
    if (alt || tool == 3) { const u8* p = img.px.data() + i * 4; if (p[3]) { color = {p[0], p[1], p[2], 255}; addRecent(); } return; }
    if (tool == 2) { fill(i); return; }
    pendingUndo = true; stroking = true; apply(i);
  }
  void strokeEnd() { if (!stroking) return; stroking = false; pendingUndo = false; if (tool != 1) addRecent(); }
  void fill(int i) {
    if (!valid(i)) return;
    const SkinMap& M = map();
    const SkinMapEntry& E = M.entries[M.map[i]];
    std::array<u8, 4> t; std::memcpy(t.data(), img.px.data() + i * 4, 4);
    std::array<u8, 4> c = tool == 1 ? std::array<u8, 4>{0, 0, 0, 0} : color;
    if (t == c) return;
    pushUndo();
    std::vector<u8> seen(4096, 0); std::vector<int> st{i}; seen[i] = 1;
    auto same = [&](int j) { int e = M.map[j]; if (e < 0 || M.entries[e].part != E.part || M.entries[e].layer != E.layer) return false; return std::memcmp(img.px.data() + j * 4, t.data(), 4) == 0; };
    while (!st.empty()) {
      int j = st.back(); st.pop_back();
      int x = j % 64, y = j / 64;
      std::memcpy(img.px.data() + j * 4, c.data(), 4);
      if (mirror) { int m = skinMirrorPixel(M, x, y); if (m >= 0) std::memcpy(img.px.data() + m * 4, c.data(), 4); }
      for (auto [nx, ny] : {std::pair<int, int>{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}}) {
        if (nx < 0 || ny < 0 || nx > 63 || ny > 63) continue;
        int n = ny * 64 + nx;
        if (!seen[n] && same(n)) { seen[n] = 1; st.push_back(n); }
      }
    }
    addRecent();
    changed();
  }
  void doUndo() { if (undoS.empty()) { status("Nothing to undo"); return; } redoS.push_back(img); img = undoS.back(); undoS.pop_back(); changed(); dirty = true; status("Undo"); }
  void doRedo() { if (redoS.empty()) { status("Nothing to redo"); return; } undoS.push_back(img); img = redoS.back(); redoS.pop_back(); changed(); dirty = true; status("Redo"); }
  void setSlim(bool s, bool convert) {
    if (convert && s != img.slim) { pushUndo(); skinConvertArms(img, s); changed(); }
    img.slim = s; changed();
  }
  void startFrom(App& a, const std::string& kind, bool undoable) {
    if (undoable) pushUndo();
    bool slim = img.slim;
    if (kind == "blank") {
      const SkinMap& M = map();
      for (int j = 0; j < 4096; j++) { int e = M.map[j]; u8* p = img.px.data() + j * 4; if (e >= 0 && !M.entries[e].layer) { p[0] = p[1] = p[2] = 154; p[3] = 255; } else std::memset(p, 0, 4); }
    } else if (kind == "current") img = resolveCharacter(a.settings.skin)->skin;
    else { img = paintDefaultSkin(); if (slim) skinConvertArms(img, true); }
    changed();
  }
  void rebuild() {
    spec = std::make_shared<CharacterSpec>();
    spec->skin = img;
    spec->hash = img.hash();
    specDirty = false; rebuildT = 0;
  }
  void save(App& a) {
    name = sanitizeName(name);
    if (!character.empty()) {
      CharLook L; SkinImage old;
      if (!loadCharacter(character, L, old)) L = ccDefaultLook(7u);
      if (saveCharacter(character, L, img)) { wearSkin(a, "creator:" + sanitizeName(character)); dirty = false; status("Character saved"); }
      else status("Could not save");
      return;
    }
    fs::path p = skinsDir() / (name + ".png");
    if (saveSkinPng(p, img)) { wearSkin(a, name + ".png" + (img.slim ? "#slim" : "#classic")); dirty = false; status("Skin saved"); }
    else status("Could not save the skin");
  }
  bool importFile(const fs::path& p) {
    SkinImage s; std::string err;
    fs::path q = p.is_absolute() || fs::exists(p) ? p : skinsDir() / p;
    if (!loadSkinFile(q, s, &err)) { status("That PNG is not a skin (64x64 or 64x32)"); return false; }
    pushUndo(); img = s; changed(); status("Imported " + q.filename().string());
    return true;
  }
  // ---- textures for the flat view ----
  void uploadTextures() {
    if (!checkTex) {
      std::vector<u8> c(64 * 64 * 4);
      for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { u8 v = (x + y) & 1 ? 0x3a : 0x2a; u8* p = &c[(y * 64 + x) * 4]; p[0] = v; p[1] = (u8)(v - 5); p[2] = (u8)(v - 10); p[3] = 255; }
      checkTex = makeTexture2D(64, 64, c.data(), true, false, false);
    }
    if (texDirty) {
      if (!skinTex) skinTex = makeTexture2D(64, 64, img.px.data(), true, false, false);
      else { glBindTexture(GL_TEXTURE_2D, skinTex); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data()); }
      texDirty = false;
    }
    int mk = iso * 2 + (img.slim ? 1 : 0);
    if (mk != maskKey) {
      std::vector<u8> m(64 * 64 * 4, 0);
      for (int j = 0; j < 4096; j++) if (!valid(j)) { m[j * 4] = m[j * 4 + 1] = m[j * 4 + 2] = 12; m[j * 4 + 3] = 175; }
      if (maskTex) glDeleteTextures(1, &maskTex);
      maskTex = makeTexture2D(64, 64, m.data(), true, false, false);
      maskKey = mk;
    }
  }
  static ImTextureID tid(GLuint t) { return (ImTextureID)(intptr_t)t; }
  void drawFlat(ImGuiIO& io) {
    uploadTextures();
    ImVec2 o = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail();
    av.y -= 20;
    if (fit) { fz = std::max(2.f, std::min(av.x, av.y) / 66); fox = (av.x - 64 * fz) / 2; foy = (av.y - 64 * fz) / 2; }
    ImGui::InvisibleButton("flatcv", av, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, ImVec2(o.x + av.x, o.y + av.y), true);
    ImVec2 a(o.x + fox, o.y + foy), b(a.x + 64 * fz, a.y + 64 * fz);
    dl->AddImage(tid(checkTex), a, b); dl->AddImage(tid(skinTex), a, b); dl->AddImage(tid(maskTex), a, b);
    if (grid && fz >= 5) for (int k = 0; k <= 64; k++) {
      dl->AddLine(ImVec2(a.x + k * fz, a.y), ImVec2(a.x + k * fz, b.y), IM_COL32(0, 0, 0, 77));
      dl->AddLine(ImVec2(a.x, a.y + k * fz), ImVec2(b.x, a.y + k * fz), IM_COL32(0, 0, 0, 77));
    }
    const SkinMap& M = map();
    for (auto& E : M.entries) {
      if (iso >= 0 && E.part != iso) continue;
      dl->AddRect(ImVec2(a.x + E.rect.x * fz, a.y + E.rect.y * fz), ImVec2(a.x + (E.rect.x + E.rect.w) * fz, a.y + (E.rect.y + E.rect.h) * fz), E.layer ? IM_COL32(0x6f, 0xc3, 0xe8, 255) : IM_COL32(0xe8, 0xb2, 0x5a, 255), 0, 0, std::max(1.f, fz / 8));
    }
    // the cursor: mouse, or the controller's stick
    vec2 mp(io.MousePos.x - a.x, io.MousePos.y - a.y);
    int tx = (int)std::floor(mp.x / fz), ty = (int)std::floor(mp.y / fz);
    hov = hovered && tx >= 0 && ty >= 0 && tx < 64 && ty < 64 ? ty * 64 + tx : -1;
    if (padMode) {
      vec2 st(padAxis(ImGuiKey_GamepadLStickRight) - padAxis(ImGuiKey_GamepadLStickLeft), padAxis(ImGuiKey_GamepadLStickDown) - padAxis(ImGuiKey_GamepadLStickUp));
      pad = glm::clamp(pad + st * io.DeltaTime * 24.f, vec2(0), vec2(63.99f));
      hov = (int)pad.y * 64 + (int)pad.x;
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) { strokeStart(hov, false); lastFlat = hov; }
      else if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) && stroking && hov != lastFlat) { if (lastFlat >= 0) lineTo(lastFlat, hov); lastFlat = hov; }
      if (ImGui::IsKeyReleased(ImGuiKey_GamepadFaceDown)) strokeEnd();
      float zt = padAxis(ImGuiKey_GamepadR2) - padAxis(ImGuiKey_GamepadL2);
      if (std::fabs(zt) > 0.1f) zoomFlat(std::exp(zt * io.DeltaTime * 2), a.x - o.x + pad.x * fz, a.y - o.y + pad.y * fz);
    }
    if (hov >= 0) {
      int e = M.map[hov];
      hoverInfo.clear();
      if (e >= 0) {
        const SkinMapEntry& E = M.entries[e];
        dl->AddRectFilled(ImVec2(a.x + E.rect.x * fz, a.y + E.rect.y * fz), ImVec2(a.x + (E.rect.x + E.rect.w) * fz, a.y + (E.rect.y + E.rect.h) * fz), IM_COL32(255, 255, 255, 33));
        const SkinPartDef& P = skinParts(img.slim)[E.part];
        hoverInfo = strFormat("%s - %s - %d, %d", E.layer ? P.oname : P.name, FACE_NAMES[E.face], hov % 64, hov / 64);
      }
      int off = (size - 1) / 2;
      dl->AddRect(ImVec2(a.x + (hov % 64 - off) * fz, a.y + (hov / 64 - off) * fz), ImVec2(a.x + (hov % 64 - off + size) * fz, a.y + (hov / 64 - off + size) * fz), IM_COL32(255, 255, 255, 255), 0, 0, std::max(1.5f, fz / 6));
    } else if (hov3 >= 0) {
      dl->AddRect(ImVec2(a.x + (hov3 % 64) * fz, a.y + (hov3 / 64) * fz), ImVec2(a.x + (hov3 % 64 + 1) * fz, a.y + (hov3 / 64 + 1) * fz), IM_COL32(255, 255, 255, 255), 0, 0, std::max(1.5f, fz / 6));
    }
    dl->PopClipRect();
    // mouse: paint (left, Alt = eyedropper), pan (right/middle), zoom (wheel)
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { strokeStart(hov, io.KeyAlt); lastFlat = hov; }
    else if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && stroking && hov >= 0 && hov != lastFlat) { if (lastFlat >= 0) lineTo(lastFlat, hov); else apply(hov); lastFlat = hov; }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !padMode) strokeEnd();
    if (ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) { fox += io.MouseDelta.x; foy += io.MouseDelta.y; fit = false; }
    if (hovered && io.MouseWheel != 0) zoomFlat(io.MouseWheel > 0 ? 1.2f : 1 / 1.2f, io.MousePos.x - o.x, io.MousePos.y - o.y);
    ImGui::TextDisabled("Texture - wheel zoom - right-drag pan");
  }
  void zoomFlat(float f, float x, float y) { float z = clampv(fz * f, 2.f, 60.f), k = z / fz; fox = x - (x - fox) * k; foy = y - (y - foy) * k; fz = z; fit = false; }

  // a ray against the body's boxes -> the nearest paintable pixel (skineditor.js pick3D)
  int pick3D(vec2 ndc) {
    mat4 inv = glm::inverse(preview.viewProj);
    vec4 A = inv * vec4(ndc, -1, 1), B = inv * vec4(ndc, 1, 1);
    vec3 o = vec3(A) / A.w, d = vec3(B) / B.w - o;
    struct Hit { float t; int i; int L; vec3 p; };
    std::vector<Hit> hits;
    auto& parts = skinParts(img.slim);
    for (int pi = 0; pi < 6; pi++) for (int L = 0; L < 2; L++) {
      if (iso >= 0 && pi != iso) continue;
      if (L && !outer && layer != 2) continue;
      const SkinPartDef& P = parts[pi];
      float g = L ? (pi == 0 ? 1.f : 0.5f) : 0;
      vec3 mn = P.mn - vec3(g), mx = P.mx + vec3(g);
      float tn = -1e30f, tf = 1e30f; int ax = -1; bool ok = true;
      for (int k = 0; k < 3 && ok; k++) {
        if (std::fabs(d[k]) < 1e-9f) { if (o[k] < mn[k] || o[k] > mx[k]) ok = false; continue; }
        float t1 = (mn[k] - o[k]) / d[k], t2 = (mx[k] - o[k]) / d[k]; if (t1 > t2) std::swap(t1, t2);
        if (t1 > tn) { tn = t1; ax = k; }
        tf = std::min(tf, t2);
      }
      if (!ok || ax < 0 || tn > tf || tn < 0) continue;
      int fi = ax * 2 + (d[ax] < 0 ? 0 : 1);
      vec3 p = o + d * tn, q = glm::clamp((p - mn) / (mx - mn), vec3(0), vec3(0.9999f));
      ivec2 uv = L ? P.ov : P.uv;
      ivec2 px = skinFacePixel(skinRects(uv.x, uv.y, P.sz.x, P.sz.y, P.sz.z), fi, q);
      hits.push_back({tn, px.y * 64 + px.x, L, p});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
    for (auto& h : hits) if (layer == 1 ? h.L == 0 : layer == 2 ? h.L == 1 : (h.L == 0 || img.px[h.i * 4 + 3] > 0)) { hitPoint = h.p; hitOk = true; return h.i; }
    hitOk = false;
    return -1;
  }
  void draw3D(ImGuiIO& io) {
    if (!spec) rebuild();
    ImVec2 o = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail();
    av.y -= 20;
    CharPreviewOpts op; op.yaw = yaw; op.elev = elev; op.dist = dist; op.still = true; op.overlay = outer; op.isolate = iso; op.gear = iso < 0;
    GLuint tex = preview.render(*spec, (int)(av.x * io.DisplayFramebufferScale.x), (int)(av.y * io.DisplayFramebufferScale.y), op);
    previewImage(tex, av.x, av.y);
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("cv3d", av, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    vec2 mp(io.MousePos.x - o.x, io.MousePos.y - o.y);
    auto ndcOf = [&](vec2 m) { return vec2(m.x / av.x * 2 - 1, 1 - m.y / av.y * 2); };
    hov3 = hovered ? pick3D(ndcOf(mp)) : -1;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (hov3 >= 0) { strokeStart(hov3, io.KeyAlt); last3 = hov3; last3Pos = mp; rotating = false; }
      else rotating = true;
    }
    if (ImGui::IsItemActive()) {
      if (rotating || ImGui::IsMouseDown(ImGuiMouseButton_Right)) { yaw += io.MouseDelta.x * 0.012f; elev = clampv(elev + io.MouseDelta.y * 0.01f, -1.3f, 1.3f); }
      else if (stroking) {   // paint along the drag, a few pixels at a time
        int n = std::max(1, (int)std::ceil(glm::length(mp - last3Pos) / 3));
        for (int s = 1; s <= n; s++) { int p = pick3D(ndcOf(last3Pos + (mp - last3Pos) * ((float)s / n))); if (p >= 0 && p != last3) { apply(p); last3 = p; } }
        last3Pos = mp;
      }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { rotating = false; if (!padMode) strokeEnd(); }
    if (hovered && io.MouseWheel != 0) dist = clampv(dist * (io.MouseWheel < 0 ? 1.1f : 1 / 1.1f), 28.f, 140.f);
    yaw += (padAxis(ImGuiKey_GamepadRStickRight) - padAxis(ImGuiKey_GamepadRStickLeft)) * io.DeltaTime * 2.6f;
    elev = clampv(elev + (padAxis(ImGuiKey_GamepadRStickDown) - padAxis(ImGuiKey_GamepadRStickUp)) * io.DeltaTime * 1.8f, -1.3f, 1.3f);
    if (hov3 >= 0 && hitOk) {   // where the brush lands
      vec4 c = preview.viewProj * vec4(hitPoint, 1);
      if (c.w > 0) ImGui::GetWindowDrawList()->AddCircle(ImVec2(o.x + (c.x / c.w + 1) / 2 * av.x, o.y + (1 - c.y / c.w) / 2 * av.y), 4, IM_COL32(255, 255, 255, 230), 12, 2);
      const SkinMap& M = map(); int e = M.map[hov3];
      if (e >= 0) { const SkinPartDef& P = skinParts(img.slim)[M.entries[e].part]; hoverInfo = strFormat("%s - %s - %d, %d", M.entries[e].layer ? P.oname : P.name, FACE_NAMES[M.entries[e].face], hov3 % 64, hov3 / 64); }
    }
    ImGui::TextDisabled("3D - paint on the model - drag elsewhere to turn");
  }
  void sidePanel(App& a) {
    ImGui::SeparatorText("Colour");
    float c4[4] = {color[0] / 255.f, color[1] / 255.f, color[2] / 255.f, 1};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorEdit3("##col", c4, ImGuiColorEditFlags_DisplayHex)) color = {(u8)std::lround(c4[0] * 255), (u8)std::lround(c4[1] * 255), (u8)std::lround(c4[2] * 255), 255};
    int n = 0;
    for (const char* h : PALETTE) {
      if (n++ % 10) ImGui::SameLine();
      ImGui::PushID(n);
      if (ImGui::ColorButton(h, toV(hexRGBA(h)), ImGuiColorEditFlags_NoTooltip, ImVec2(17, 17))) color = hexRGBA(h);
      ImGui::PopID();
    }
    ImGui::SeparatorText("Recent");
    n = 0;
    for (auto& h : recent) {
      if (n++ % 10) ImGui::SameLine();
      ImGui::PushID(100 + n);
      if (ImGui::ColorButton(h.c_str(), toV(hexRGBA(h.c_str())), ImGuiColorEditFlags_NoTooltip, ImVec2(17, 17))) color = hexRGBA(h.c_str());
      ImGui::PopID();
    }
    if (recent.empty()) ImGui::TextDisabled("(none yet)");
    ImGui::SeparatorText("Parts");
    if (ImGui::Selectable("All", iso < 0)) iso = -1;
    auto& P = skinParts(img.slim);
    for (int i = 0; i < 6; i++) if (ImGui::Selectable(P[i].name, iso == i)) iso = i;
    ImGui::SeparatorText("Arms");
    if (ImGui::RadioButton("Classic", !img.slim)) setSlim(false, true);
    ImGui::SameLine();
    if (ImGui::RadioButton("Slim", img.slim)) setSlim(true, true);
    ImGui::SeparatorText("Start from");
    if (ImGui::Button("Current")) startFrom(a, "current", true);
    ImGui::SameLine(); if (ImGui::Button("Default")) startFrom(a, "default", true);
    ImGui::SameLine(); if (ImGui::Button("Blank")) startFrom(a, "blank", true);
    ImGui::SeparatorText("Files");
    if (ImGui::Button("Import PNG...")) ImGui::OpenPopup("import##ske");
    ImGui::SameLine();
    if (ImGui::Button("Export PNG")) {
      fs::path p = userSub("exports") / (sanitizeName(name) + "_skin.png");
      status(saveSkinPng(p, img) ? "Exported " + p.string() : "Could not export");
    }
    if (ImGui::BeginPopup("import##ske")) {
      if (ImGui::Button(picker.busy() ? "Choosing..." : "Choose a PNG file...")) { picker.open("Import a skin (64x64 or 64x32 PNG)", "png"); ImGui::CloseCurrentPopup(); }
      ImGui::TextDisabled("or drop a skin .png on the window");
      ImGui::Separator();
      ImGui::TextDisabled("Your skins folder:");
      auto list = listSkins();
      if (list.empty()) ImGui::TextDisabled("(empty) %s", skinsDir().string().c_str());
      for (auto& s : list) if (ImGui::Selectable(s.c_str())) importFile(s);
      ImGui::Separator();
      ImGui::SetNextItemWidth(260); ImGui::InputText("##path", &importPath);
      ImGui::SameLine();
      if (ImGui::Button("Import file") && !importPath.empty()) if (importFile(importPath)) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
  void keys(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    // controller
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack)) setPad(!padMode);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) setTool((tool + 3) % 4);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) setTool((tool + 1) % 4);
    if (padMode) {
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft)) doUndo();
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp)) doRedo();
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart)) save(a);
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadL3)) outer = !outer;
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadR3)) mirror = !mirror;
    }
    if (io.WantTextInput) return;
    bool ctrl = io.KeyCtrl || io.KeySuper;
    auto P = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (ctrl && P(ImGuiKey_Z)) { if (io.KeyShift) doRedo(); else doUndo(); return; }
    if (ctrl && P(ImGuiKey_Y)) { doRedo(); return; }
    if (ctrl && P(ImGuiKey_S)) { save(a); return; }
    if (ctrl || io.KeyAlt) return;
    if (P(ImGuiKey_P) || P(ImGuiKey_B)) setTool(0);
    if (P(ImGuiKey_E)) setTool(1);
    if (P(ImGuiKey_F) || P(ImGuiKey_G)) setTool(2);
    if (P(ImGuiKey_I) || P(ImGuiKey_K)) setTool(3);
    for (int s = 1; s <= 3; s++) if (P((ImGuiKey)(ImGuiKey_0 + s))) size = s;
    if (P(ImGuiKey_LeftBracket)) size = std::max(1, size - 1);
    if (P(ImGuiKey_RightBracket)) size = std::min(3, size + 1);
    if (P(ImGuiKey_M)) mirror = !mirror;
    if (P(ImGuiKey_O)) outer = !outer;
    if (P(ImGuiKey_H)) grid = !grid;
    if (P(ImGuiKey_L)) layer = (layer + 1) % 3;
    if (P(ImGuiKey_R)) resetViews();
    if (P(ImGuiKey_Equal) || P(ImGuiKey_KeypadAdd)) { dist = clampv(dist / 1.2f, 28.f, 140.f); zoomFlat(1.2f, 200, 200); }
    if (P(ImGuiKey_Minus) || P(ImGuiKey_KeypadSubtract)) { dist = clampv(dist * 1.2f, 28.f, 140.f); zoomFlat(1 / 1.2f, 200, 200); }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) yaw -= 0.2f;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) yaw += 0.2f;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) elev = clampv(elev + 0.15f, -1.3f, 1.3f);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) elev = clampv(elev - 0.15f, -1.3f, 1.3f);
  }
};
}

std::unique_ptr<Screen> makeSkinEditorScreen(const SkinImage& start, const std::string& name, const std::string& character) {
  return std::make_unique<SkinEditorScreen>(&start, name, character);
}
std::unique_ptr<Screen> makeSkinEditorScreen() { return std::make_unique<SkinEditorScreen>(nullptr, "", ""); }

} // namespace dq
