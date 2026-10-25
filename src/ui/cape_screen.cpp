// ui/cape_screen.cpp — the cape editor: paint a 64x32 cape in the Minecraft layout (assets/cape_tex.h) on a flat
// canvas, with a turning 3D preview on your character, save it as the cape you wear (Settings::extra["cape"], drawn
// by the character layer), export / import it as a PNG (system file chooser or a file dropped on the window).
// The painting follows the skin editor (pencil, eraser, fill, eyedropper, brush sizes, mirror, undo / redo, palette,
// a stick cursor for controllers); the file handling follows the skin import. Plain ImGui, so the theme can restyle it.
#include "ui/char_screens.h"
#include "ui/char_preview.h"
#include "ui/file_pick.h"
#include "ui/layout_flow.h"
#include "assets/cape_tex.h"
#include "assets/char_model.h"
#include "client/app.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {

namespace {
const char* PALETTE[] = {"#000000", "#3b3b3b", "#7a7a7a", "#b5b5b5", "#ffffff", "#5a3a22", "#8a5a34", "#c68e5a", "#e8bf96", "#f5d9bd",
                         "#7a1f1f", "#c0392b", "#ff6b4a", "#ffb07a", "#f2c14e", "#ffe680", "#2e6b2e", "#4caf50", "#a5d66b", "#1f5f6b",
                         "#2f8f86", "#7fd6cf", "#1f3f8a", "#3c6fd6", "#8fb5ff", "#4a2a7a", "#8e5ad6", "#d6a5ff", "#a03a6b", "#ff8fc0"};
const char* TOOLS[4] = {"Pencil", "Eraser (restore)", "Fill", "Eyedropper"};
const char* LIVE = "~edit";   // the editor's unsaved cape, as the renderer knows it (cape@~edit)
std::array<u8, 4> hexRGBA(const char* h) { unsigned v = (unsigned)std::strtoul(h + 1, nullptr, 16); return {(u8)(v >> 16), (u8)(v >> 8), (u8)v, 255}; }
ImVec4 toV(const std::array<u8, 4>& c) { return ImVec4(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, 1); }
int rectAt(int x, int y) { auto& R = capeRects(); for (int i = 0; i < (int)R.size(); i++) if (x >= R[i].x && y >= R[i].y && x < R[i].x + R[i].w && y < R[i].y + R[i].h) return i; return -1; }

class CapeEditorScreen : public Screen {
public:
  ~CapeEditorScreen() override { for (GLuint* t : {&capeTex, &checkTex, &maskTex}) if (*t && glDeleteTextures) glDeleteTextures(1, t); }
  const char* id() const override { return "capeeditor"; }
  bool wantsMouse() const override { return true; }

  void onOpen(App& a) override {
    base = defaultCapePixels();
    std::string worn = wornCapeName(a.settings);
    if (!worn.empty() && loadCapeNamed(worn, px)) name = worn; else { px = base; name = "cape"; }
    spec = resolveCharacter(a.settings.skin);
    watchDroppedFiles();
    texDirty = true; liveDirty = true; dirty = false;
  }
  void onClose(App&) override { if (padMode) setPad(false); dropLiveCape(LIVE); }
  bool onBack(App&) override {
    if (padMode) { setPad(false); return true; }
    if (confirm) { confirm = false; return true; }
    if (dirty) { confirm = true; return true; }
    return false;
  }
  void update(App&, double dt) override {
    std::string path;   // the system file chooser, or a cape dropped on the window
    if (picker.take(path)) { if (!path.empty()) importFile(path); else if (!picker.error().empty()) status(picker.error()); }
    if (takeDroppedFile(path, "png")) importFile(path);
    liveT += (float)dt;
    if (liveDirty && liveT > 0.08f) { setLiveCape(LIVE, px); liveDirty = false; liveT = 0; }
    if (msgT > 0) msgT -= (float)dt;
    t += (float)dt;
    if (spin && !rotating) yaw += (float)dt * 0.5f;
  }

  void draw(App& a) override {
    ImGuiIO& io = ImGui::GetIO();
    const float z = ui::Z();
    ImVec2 WS = flow::windowSize((float)a.width, (float)a.height, 1180, 740, 8);
    float W = WS.x, H = WS.y;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::Begin("Cape editor##cpe", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    keys(a);
    hoverInfo.clear();
    // room for the foot (status line, name, a row or two of buttons)
    ImGuiStyle& sty = ImGui::GetStyle();
    int botRows = flow::rowsNeeded({{"Save & wear", 140}, {"Save", 90}, {"Take cape off", 140}, {"Export PNG", 120}, {"Import PNG...", 140}, {"Back", 90}}, ImGui::GetContentRegionAvail().x);
    float botH = ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetFrameHeightWithSpacing() * (botRows + 1) + sty.WindowPadding.y;
    float body = std::max(160.f, ImGui::GetContentRegionAvail().y - botH);
    float toolsW = 150 * z, sideW = 240 * z;
    float rest = std::max(200.f, W - toolsW - sideW - 6 * sty.ItemSpacing.x - 2 * sty.WindowPadding.x);
    float flatW = rest * 0.56f, viewW = rest - flatW;
    // ---- tools ----
    ImGui::BeginChild("tools##cpe", ImVec2(toolsW, body), ImGuiChildFlags_Borders);
    for (int i = 0; i < 4; i++) if (ImGui::Selectable(TOOLS[i], tool == i)) setTool(i);
    ImGui::SeparatorText("Brush");
    for (int s = 1; s <= 3; s++) { if (s > 1) ImGui::SameLine(); if (ImGui::RadioButton(std::to_string(s).c_str(), size == s)) size = s; }
    ImGui::SeparatorText("Options");
    ImGui::Checkbox("Mirror (M)", &mirror);
    ImGui::Checkbox("Grid (H)", &grid);
    ImGui::BeginDisabled(undoS.empty()); if (ImGui::Button("Undo")) doUndo(); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(redoS.empty()); if (ImGui::Button("Redo")) doRedo(); ImGui::EndDisabled();
    if (ImGui::Button("Fit view (R)")) { fit = true; resetView(); }
    ImGui::SeparatorText("Controller");
    if (ImGui::Button(padMode ? "Stop stick painting" : "Paint with stick")) setPad(!padMode);
    ImGui::TextWrapped("Back: stick cursor\nA paint, B stop\nLB/RB tool, X/Y undo/redo\nLT/RT zoom, R-stick turn");
    ImGui::EndChild();
    ImGui::SameLine();
    // ---- the flat canvas and the 3D preview ----
    ImGui::BeginChild("flat##cpe", ImVec2(flatW, body), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawFlat(io);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("3d##cpe", ImVec2(viewW, body), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw3D(io, a);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("side##cpe", ImVec2(0, body), ImGuiChildFlags_Borders);
    sidePanel(a);
    ImGui::EndChild();
    // ---- foot ----
    std::string st = msgT > 0 ? msg : !hoverInfo.empty() ? hoverInfo : dirty ? "Unsaved changes" : "";
    ImGui::TextDisabled("%s", st.c_str());
    ImGui::SetNextItemWidth(std::min(220.f * z, W * 0.4f)); ImGui::InputText("Name##cpe", &name);
    flow::Row row;
    if (row.button("Save & wear", 140)) saveCape(a, true);
    if (row.button("Save", 90)) saveCape(a, false);
    if (row.button("Take cape off", 140)) { setWornCapeName(a.settings, ""); if (a.session && a.session->player) a.session->player->data.erase("customCape"); status("No painted cape worn"); }
    if (row.button("Export PNG", 120)) {
      fs::path p = userSub("exports") / (sanitizeName(name) + "_cape.png");
      status(saveCapePng(p, px) ? "Exported " + p.string() : "Could not export");
    }
    if (row.button(picker.busy() ? "Choosing..." : "Import PNG...", 140)) picker.open("Import a cape (64x32 PNG)", "png");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Or drop a cape .png on the window");
    if (row.button("Back", 90)) { if (dirty) confirm = true; else { ImGui::End(); a.popScreen(); return; } }
    if (confirm) ImGui::OpenPopup("Unsaved changes##cpe");
    if (ImGui::BeginPopupModal("Unsaved changes##cpe", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Save this cape before leaving?");
      bool close = false;
      if (ImGui::Button("Save & close")) { saveCape(a, true); close = true; }
      ImGui::SameLine(); if (ImGui::Button("Discard")) close = true;
      ImGui::SameLine(); if (ImGui::Button("Keep editing")) { confirm = false; ImGui::CloseCurrentPopup(); }
      if (close) { confirm = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::End(); a.popScreen(); return; }
      ImGui::EndPopup();
    }
    ImGui::End();
  }

private:
  CapePixels px, base;
  std::shared_ptr<const CharacterSpec> spec;
  std::string name, msg, hoverInfo;
  FilePicker picker;
  float msgT = 0, liveT = 0, t = 0;
  int tool = 0, size = 1;
  bool mirror = false, grid = true, dirty = false, confirm = false, stroking = false, padMode = false, pendingUndo = false, liveDirty = true, texDirty = true;
  std::array<u8, 4> color{200, 40, 40, 255};
  std::vector<std::string> recent;
  std::vector<CapePixels> undoS, redoS;
  // flat view
  float fz = 10, fox = 0, foy = 0; bool fit = true; int lastPx = -1; vec2 pad{12, 9};
  // 3D view
  float yaw = 0.5f, elev = 0.12f, dist = 62; bool spin = true, walk = false, rotating = false;
  CharPreview preview;
  GLuint capeTex = 0, checkTex = 0, maskTex = 0;

  void status(const std::string& s) { msg = s; msgT = 2.5f; }
  void setTool(int k) { tool = k; status(TOOLS[k]); }
  void resetView() { yaw = 0.5f; elev = 0.12f; dist = 62; }
  void setPad(bool on) {
    padMode = on;
    ImGuiIO& io = ImGui::GetIO();
    if (on) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad; else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  }
  // ---- editing ----
  void pushUndo() { undoS.push_back(px); if (undoS.size() > 100) undoS.erase(undoS.begin()); redoS.clear(); dirty = true; }
  void changed() { texDirty = true; liveDirty = true; dirty = true; }
  void put(int x, int y) {
    if (x < 0 || y < 0 || x > 63 || y > 31 || !capePixelUsed(x, y)) return;
    if (pendingUndo) { pushUndo(); pendingUndo = false; }
    u8* p = px.at(x, y);
    if (tool == 1) std::copy_n(base.at(x, y), 4, p);
    else { p[0] = color[0]; p[1] = color[1]; p[2] = color[2]; p[3] = 255; }
  }
  void apply(int x0, int y0) {
    int o = (size - 1) / 2;
    for (int dy = 0; dy < size; dy++) for (int dx = 0; dx < size; dx++) {
      int x = x0 + dx - o, y = y0 + dy - o;
      put(x, y);
      if (mirror) { int r = rectAt(x, y); if (r == 2 || r == 3) { const CapeRect& R = capeRects()[r]; put(R.x + (R.w - 1 - (x - R.x)), y); } }
    }
    changed();
  }
  void lineTo(int a, int b) {
    int x0 = a % 64, y0 = a / 64, x1 = b % 64, y1 = b / 64, dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while (x0 != x1 || y0 != y1) { int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } apply(x0, y0); }
  }
  void addRecent() {
    std::string h = strFormat("#%02x%02x%02x", color[0], color[1], color[2]);
    recent.erase(std::remove(recent.begin(), recent.end(), h), recent.end());
    recent.insert(recent.begin(), h);
    if (recent.size() > 10) recent.resize(10);
  }
  void strokeStart(int i, bool alt) {
    if (i < 0) return;
    int x = i % 64, y = i / 64;
    if (!capePixelUsed(x, y)) return;
    if (alt || tool == 3) { const u8* p = px.at(x, y); color = {p[0], p[1], p[2], 255}; addRecent(); return; }
    if (tool == 2) { fill(x, y); return; }
    pendingUndo = true; stroking = true; apply(x, y);
  }
  void strokeEnd() { if (!stroking) return; stroking = false; pendingUndo = false; if (tool != 1) addRecent(); }
  void fill(int sx, int sy) {
    int r0 = rectAt(sx, sy); if (r0 < 0) return;
    std::array<u8, 4> t0; std::copy_n(px.at(sx, sy), 4, t0.begin());
    std::array<u8, 4> c = color;
    if (tool == 1) std::copy_n(base.at(sx, sy), 4, c.begin());
    if (t0 == c) return;
    pushUndo();
    std::vector<u8> seen(64 * 32, 0); std::vector<int> st{sy * 64 + sx}; seen[sy * 64 + sx] = 1;
    while (!st.empty()) {
      int j = st.back(); st.pop_back(); int x = j % 64, y = j / 64;
      std::copy_n(c.begin(), 4, px.at(x, y));
      for (auto [nx, ny] : {std::pair<int, int>{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}}) {
        if (nx < 0 || ny < 0 || nx > 63 || ny > 31 || rectAt(nx, ny) != r0) continue;
        int n = ny * 64 + nx;
        if (!seen[n] && std::equal(t0.begin(), t0.end(), px.at(nx, ny))) { seen[n] = 1; st.push_back(n); }
      }
    }
    addRecent(); changed();
  }
  void doUndo() { if (undoS.empty()) { status("Nothing to undo"); return; } redoS.push_back(px); px = undoS.back(); undoS.pop_back(); changed(); status("Undo"); }
  void doRedo() { if (redoS.empty()) { status("Nothing to redo"); return; } undoS.push_back(px); px = redoS.back(); redoS.pop_back(); changed(); status("Redo"); }
  void startFrom(App& a, int kind) {
    pushUndo();
    if (kind == 0) px = base;
    else if (kind == 1) px = blankCapePixels();
    else { std::string w = wornCapeName(a.settings); CapePixels c; if (!w.empty() && loadCapeNamed(w, c)) px = c; else { status("You are not wearing a painted cape"); return; } }
    changed();
  }
  void saveCape(App& a, bool wear) {
    name = sanitizeName(name);
    if (name.empty()) name = "cape";
    if (!saveCapeNamed(name, px)) { status("Could not save the cape"); return; }
    capeMeshInvalidate(customCapeKey(name));   // the picture changed: build the 3D cape again
    dirty = false;
    if (wear) { setWornCapeName(a.settings, name); if (a.session && a.session->player) a.session->player->data["customCape"] = name; status("Saved and worn: " + name); }
    else status("Saved " + name + " (Save & wear puts it on)");
  }
  bool importFile(const fs::path& p) {
    CapePixels c; std::string err;
    fs::path q = p.is_absolute() || fs::exists(p) ? p : capesDir() / p;
    if (!loadCapePng(q, c, &err)) { status(err.empty() ? "That PNG is not a cape (64x32)" : err); return false; }
    pushUndo(); px = c; changed(); status("Imported " + q.filename().string());
    if (name.empty() || name == "cape") name = sanitizeName(q.stem().string());
    return true;
  }
  // ---- the flat canvas ----
  static ImTextureID tid(GLuint k) { return (ImTextureID)(intptr_t)k; }
  void uploadTextures() {
    if (!checkTex) {
      std::vector<u8> c(64 * 32 * 4);
      for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) { u8 v = (x + y) & 1 ? 0x3a : 0x2a; u8* p = &c[(y * 64 + x) * 4]; p[0] = v; p[1] = (u8)(v - 5); p[2] = (u8)(v - 10); p[3] = 255; }
      checkTex = makeTexture2D(64, 32, c.data(), true, false, false);
      std::vector<u8> m(64 * 32 * 4, 0);
      for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) if (!capePixelUsed(x, y)) { u8* p = &m[(y * 64 + x) * 4]; p[0] = p[1] = p[2] = 12; p[3] = 190; }
      maskTex = makeTexture2D(64, 32, m.data(), true, false, false);
    }
    if (texDirty) {
      if (!capeTex) capeTex = makeTexture2D(64, 32, px.rgba.data(), true, false, false);
      else { glBindTexture(GL_TEXTURE_2D, capeTex); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 32, GL_RGBA, GL_UNSIGNED_BYTE, px.rgba.data()); }
      texDirty = false;
    }
  }
  void zoomAt(float f, float mx, float my) {   // mx, my: position in the child's space
    float nf = clampv(fz * f, 3.f, 80.f);
    fox = mx - (mx - fox) * nf / fz; foy = my - (my - foy) * nf / fz; fz = nf; fit = false;
  }
  void drawFlat(ImGuiIO& io) {
    uploadTextures();
    ImVec2 o = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail();
    av.y -= ImGui::GetTextLineHeightWithSpacing();
    if (fit) { fz = std::max(3.f, std::min(av.x / 66.f, av.y / 34.f)); fox = (av.x - 64 * fz) / 2; foy = (av.y - 32 * fz) / 2; }
    ImGui::InvisibleButton("capecv", av, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, ImVec2(o.x + av.x, o.y + av.y), true);
    ImVec2 a(o.x + fox, o.y + foy), b(a.x + 64 * fz, a.y + 32 * fz);
    dl->AddImage(tid(checkTex), a, b); dl->AddImage(tid(capeTex), a, b); dl->AddImage(tid(maskTex), a, b);
    if (grid && fz >= 6) for (int k = 0; k <= 64; k++) {
      dl->AddLine(ImVec2(a.x + k * fz, a.y), ImVec2(a.x + k * fz, b.y), IM_COL32(0, 0, 0, 60));
      if (k <= 32) dl->AddLine(ImVec2(a.x, a.y + k * fz), ImVec2(b.x, a.y + k * fz), IM_COL32(0, 0, 0, 60));
    }
    for (auto& R : capeRects())
      dl->AddRect(ImVec2(a.x + R.x * fz, a.y + R.y * fz), ImVec2(a.x + (R.x + R.w) * fz, a.y + (R.y + R.h) * fz), R.inner ? IM_COL32(0x6f, 0xc3, 0xe8, 255) : IM_COL32(0xe8, 0xb2, 0x5a, 255), 0, 0, std::max(1.f, fz / 8));
    // wheel zoom, middle-drag pan
    if (hovered && io.MouseWheel != 0) zoomAt(io.MouseWheel > 0 ? 1.2f : 1 / 1.2f, io.MousePos.x - o.x, io.MousePos.y - o.y);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0)) { fox += io.MouseDelta.x; foy += io.MouseDelta.y; fit = false; }
    vec2 mp(io.MousePos.x - a.x, io.MousePos.y - a.y);
    int tx = (int)std::floor(mp.x / fz), ty = (int)std::floor(mp.y / fz);
    int hov = hovered && tx >= 0 && ty >= 0 && tx < 64 && ty < 32 ? ty * 64 + tx : -1;
    if (padMode) {   // the stick is the cursor, A paints, the triggers zoom
      vec2 sk(padAxis(ImGuiKey_GamepadLStickRight) - padAxis(ImGuiKey_GamepadLStickLeft), padAxis(ImGuiKey_GamepadLStickDown) - padAxis(ImGuiKey_GamepadLStickUp));
      pad = glm::clamp(pad + sk * io.DeltaTime * 22.f, vec2(0), vec2(63.99f, 31.99f));
      hov = (int)pad.y * 64 + (int)pad.x;
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) { strokeStart(hov, false); lastPx = hov; }
      else if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) && stroking && hov != lastPx) { if (lastPx >= 0) lineTo(lastPx, hov); lastPx = hov; }
      if (ImGui::IsKeyReleased(ImGuiKey_GamepadFaceDown)) strokeEnd();
      float zt = padAxis(ImGuiKey_GamepadR2) - padAxis(ImGuiKey_GamepadL2);
      if (std::fabs(zt) > 0.1f) zoomAt(std::exp(zt * io.DeltaTime * 2), fox + pad.x * fz, foy + pad.y * fz);
    } else {
      if (hovered && hov >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { strokeStart(hov, io.KeyAlt); lastPx = hov; }
      else if (hovered && hov >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) strokeStart(hov, true);
      else if (stroking && ImGui::IsMouseDown(ImGuiMouseButton_Left) && hov >= 0 && hov != lastPx) { if (lastPx >= 0) lineTo(lastPx, hov); lastPx = hov; }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) strokeEnd();
    }
    if (hov >= 0) {
      int r = rectAt(hov % 64, hov / 64);
      if (r >= 0) {
        const CapeRect& R = capeRects()[r];
        dl->AddRectFilled(ImVec2(a.x + R.x * fz, a.y + R.y * fz), ImVec2(a.x + (R.x + R.w) * fz, a.y + (R.y + R.h) * fz), IM_COL32(255, 255, 255, 26));
        hoverInfo = strFormat("%s - %d, %d", R.name, hov % 64, hov / 64);
      } else hoverInfo = "(not part of the cape)";
      int off = (size - 1) / 2;
      dl->AddRect(ImVec2(a.x + (hov % 64 - off) * fz, a.y + (hov / 64 - off) * fz), ImVec2(a.x + (hov % 64 - off + size) * fz, a.y + (hov / 64 - off + size) * fz), IM_COL32(255, 255, 255, 255), 0, 0, std::max(1.5f, fz / 6));
    }
    dl->PopClipRect();
    ImGui::TextDisabled("Left paint - right pick - wheel zoom - middle drag moves");
  }
  // ---- the 3D preview: your character with the cape on ----
  void draw3D(ImGuiIO& io, App&) {
    ImVec2 av = ImGui::GetContentRegionAvail();
    float ph = std::max(40.f, av.y - ImGui::GetFrameHeightWithSpacing() * 2);
    CharPreviewOpts o; o.yaw = yaw; o.elev = elev; o.dist = dist; o.time = t; o.walk = walk;
    o.cape = customCapeKey(LIVE);
    ImVec2 at = ImGui::GetCursorScreenPos();
    GLuint tex = preview.render(*spec, (int)(av.x * io.DisplayFramebufferScale.x), (int)(ph * io.DisplayFramebufferScale.y), o);
    previewImage(tex, av.x, ph);
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("turn##cpe", ImVec2(av.x, ph));
    rotating = ImGui::IsItemActive();
    if (rotating) { yaw += io.MouseDelta.x * 0.012f; elev = clampv(elev + io.MouseDelta.y * 0.01f, -1.3f, 1.3f); }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0) dist = clampv(dist * (io.MouseWheel < 0 ? 1.1f : 1 / 1.1f), 28.f, 140.f);
    yaw += (padAxis(ImGuiKey_GamepadRStickRight) - padAxis(ImGuiKey_GamepadRStickLeft)) * io.DeltaTime * 2.6f;
    elev = clampv(elev + (padAxis(ImGuiKey_GamepadRStickDown) - padAxis(ImGuiKey_GamepadRStickUp)) * io.DeltaTime * 1.8f, -1.3f, 1.3f);
    ImGui::Checkbox("Turn", &spin); ImGui::SameLine(); ImGui::Checkbox("Walk", &walk);
    ImGui::SameLine(); if (ImGui::Button("Back view")) { yaw = 0.5f; elev = 0.12f; dist = 62; }
  }
  void sidePanel(App& a) {
    ImGui::SeparatorText("Colour");
    float c4[4] = {color[0] / 255.f, color[1] / 255.f, color[2] / 255.f, 1};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorEdit3("##col", c4, ImGuiColorEditFlags_DisplayHex)) color = {(u8)std::lround(c4[0] * 255), (u8)std::lround(c4[1] * 255), (u8)std::lround(c4[2] * 255), 255};
    int n = 0; const float sw = 17 * ui::Z();
    for (const char* h : PALETTE) {
      if (n++ % 10) ImGui::SameLine();
      ImGui::PushID(n);
      if (ImGui::ColorButton(h, toV(hexRGBA(h)), ImGuiColorEditFlags_NoTooltip, ImVec2(sw, sw))) color = hexRGBA(h);
      ImGui::PopID();
    }
    ImGui::SeparatorText("Recent");
    n = 0;
    for (auto& h : recent) {
      if (n++ % 10) ImGui::SameLine();
      ImGui::PushID(100 + n);
      if (ImGui::ColorButton(h.c_str(), toV(hexRGBA(h.c_str())), ImGuiColorEditFlags_NoTooltip, ImVec2(sw, sw))) color = hexRGBA(h.c_str());
      ImGui::PopID();
    }
    if (recent.empty()) ImGui::TextDisabled("(none yet)");
    ImGui::SeparatorText("Start from");
    if (ImGui::Button("Default")) startFrom(a, 0);
    ImGui::SameLine(); if (ImGui::Button("Blank")) startFrom(a, 1);
    ImGui::SameLine(); if (ImGui::Button("Worn")) startFrom(a, 2);
    ImGui::SeparatorText("Your capes");
    auto list = listCapes();
    if (list.empty()) ImGui::TextDisabled("(none saved yet)");
    std::string worn = wornCapeName(a.settings);
    for (auto& c : list) {
      ImGui::PushID(c.c_str());
      if (ImGui::Selectable((c + (c == worn ? "  (worn)" : "")).c_str(), c == name)) { CapePixels cp; if (loadCapeNamed(c, cp)) { pushUndo(); px = cp; name = c; changed(); dirty = false; status("Opened " + c); } }
      ImGui::PopID();
    }
    ImGui::TextDisabled("Folder: %s", capesDir().string().c_str());
  }
  void keys(App&) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack)) setPad(!padMode);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) setTool((tool + 3) % 4);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) setTool((tool + 1) % 4);
    if (padMode) {
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft)) doUndo();
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp)) doRedo();
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadR3)) mirror = !mirror;
    }
    if (io.WantTextInput) return;
    bool ctrl = io.KeyCtrl || io.KeySuper;
    auto P = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (ctrl && P(ImGuiKey_Z)) { if (io.KeyShift) doRedo(); else doUndo(); return; }
    if (ctrl && P(ImGuiKey_Y)) { doRedo(); return; }
    if (ctrl || io.KeyAlt) return;
    if (P(ImGuiKey_P) || P(ImGuiKey_B)) setTool(0);
    if (P(ImGuiKey_E)) setTool(1);
    if (P(ImGuiKey_F) || P(ImGuiKey_G)) setTool(2);
    if (P(ImGuiKey_I) || P(ImGuiKey_K)) setTool(3);
    for (int s = 1; s <= 3; s++) if (P((ImGuiKey)(ImGuiKey_0 + s))) size = s;
    if (P(ImGuiKey_M)) mirror = !mirror;
    if (P(ImGuiKey_H)) grid = !grid;
    if (P(ImGuiKey_R)) { fit = true; resetView(); }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) yaw -= 0.2f;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) yaw += 0.2f;
  }
};
}

std::unique_ptr<Screen> makeCapeEditorScreen() { return std::make_unique<CapeEditorScreen>(); }

} // namespace dq
