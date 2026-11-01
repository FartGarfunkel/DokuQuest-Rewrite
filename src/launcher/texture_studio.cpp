// launcher/texture_studio.cpp — the Texture Studio (Mod Studio → Textures): every block and item picture the game
// draws (the pack that's on, with your own over it), painted pixel by pixel like the skin editor: pencil, fill, picker,
// eraser, mirror, grid, undo/redo, the colours of the picture as a palette, a live block preview, PNG in and out.
// "Use in the game" saves userDir()/textures/<tile>.png, which the game lays over any pack (assets/packs.cpp
// applyUserTiles). Biome-tinted pictures (grass, leaves) keep their tint where the original had it.
// Controller: A on the canvas starts painting (d-pad moves, A paints, X picks the colour, Y changes the tool,
// L1/R1 step through the colours, B is done); everything else is ordinary menu items.
#include "launcher/launcher.h"
#include "assets/tileset.h"
#include "render/gl.h"
#include "ui/file_pick.h"
#include "common/fs.h"
#include <imgui_stdlib.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <unordered_set>

namespace dq {

namespace {
constexpr int N = 32;
using Px = std::array<u8, 4>;
const char* TOOLS[] = {"Pencil", "Fill", "Picker", "Eraser"};

struct TS {
  TileSet ts; std::vector<u8> base;   // base: the tiles before your own were laid over (to put one back)
  std::atomic<int> state{0}; std::thread th;
  GLuint atlas = 0, cur = 0; int cols = 64;
  std::vector<std::string> names; std::unordered_set<std::string> items, mine;
  int cat = 0; std::string filter, sel, status, path; int layer = -1;
  std::vector<int> shown; std::string shownKey;
  std::vector<u8> img, tintMask;   // the picture being painted (N*N*4) and where the game tints it by biome
  std::vector<std::vector<u8>> undo, redo;
  Px col{90, 160, 60, 255}; std::vector<Px> recent;
  int tool = 0; bool mirror = false, grid = true, texDirty = true, stroking = false, dirty = false;
  bool padEdit = false; int cx = 16, cy = 16, ri = 0;
  FilePicker pick;
};
TS& T() { static TS* t = new TS(); return *t; }   // never destroyed: a launcher closed mid-load just ends

// how a stored pixel looks: the tint marker shown as a meadow green, the marker alphas as solid
Px look(const u8* p, bool tint) {
  Px o{p[0], p[1], p[2], p[3]};
  if (o[3] >= 250) o[3] = 255;
  if (tint && o[3] >= 128) { o[0] = (u8)(o[0] * 0.55f); o[1] = (u8)(o[1] * 0.8f); o[2] = (u8)(o[2] * 0.36f); }
  return o;
}
ImU32 c32(const Px& p) { return IM_COL32(p[0], p[1], p[2], p[3]); }

bool ready(Launcher& L) {
  TS& t = T();
  if (t.state == 0) {
    t.state = 1;
    std::string pack = L.settings.resourcePack;
    t.th = std::thread([pack] {
      TS& s = T();
      loadBuiltinTiles(s.ts);
      s.ts.packName = applyResourcePack(s.ts, pack);
      s.base = s.ts.rgba;
      applyUserTiles(s.ts);
      s.state = 2;
    });
  }
  if (t.state != 2) return false;
  if (t.th.joinable()) {
    t.th.join();
    for (auto& [k, v] : t.ts.index) if (v >= 0 && v < t.ts.count && !startsWith(k, "crack_")) t.names.push_back(k);
    std::sort(t.names.begin(), t.names.end());
    if (auto is = readJson(dataDir() / "itemshapes.json")) if ((*is)["itemTiles"].is_array()) for (auto& v : (*is)["itemTiles"]) t.items.insert(v.get<std::string>());
    std::error_code ec;
    for (auto& e : fs::directory_iterator(userDir() / "textures", ec)) if (e.path().extension() == ".png") t.mine.insert(e.path().stem().string());
    // every picture in one atlas for the list
    const int rows = (t.ts.count + t.cols - 1) / t.cols, W = t.cols * N, H = rows * N;
    std::vector<u8> px((size_t)W * H * 4, 0);
    for (int i = 0; i < t.ts.count; i++) {
      const u8* src = t.ts.tile(i);
      bool tint = false; for (int k = 0; k < N * N && !tint; k++) tint = src[k * 4 + 3] == 253;
      for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        Px p = look(src + (y * N + x) * 4, tint);
        std::memcpy(&px[(((size_t)(i / t.cols) * N + y) * W + (i % t.cols) * N + x) * 4], p.data(), 4);
      }
    }
    t.atlas = makeTexture2D(W, H, px.data(), true, false, false);
    t.cur = makeTexture2D(N, N, nullptr, true, false, true);
  }
  return true;
}

void open(const std::string& name) {
  TS& t = T();
  auto it = t.ts.index.find(name);
  if (it == t.ts.index.end()) return;
  if (t.dirty && !t.sel.empty()) t.status = "Left " + t.sel + " without saving";
  t.sel = name; t.layer = it->second;
  const u8* src = t.ts.tile(t.layer);
  t.img.assign(src, src + N * N * 4);
  const u8* b = t.base.data() + (size_t)t.layer * N * N * 4;
  t.tintMask.assign(N * N, 0);
  for (int i = 0; i < N * N; i++) t.tintMask[i] = b[i * 4 + 3] == 253;
  for (int i = 0; i < N * N; i++) if (t.img[i * 4 + 3] >= 250) t.img[i * 4 + 3] = 255;
  t.undo.clear(); t.redo.clear(); t.dirty = false; t.texDirty = true;
  // the picture's own colours lead the palette
  t.recent.clear();
  for (int i = 0; i < N * N && t.recent.size() < 24; i++) {
    Px p{t.img[i * 4], t.img[i * 4 + 1], t.img[i * 4 + 2], 255};
    if (t.img[i * 4 + 3] < 128) continue;
    bool near = false; for (auto& q : t.recent) near |= std::abs(q[0] - p[0]) + std::abs(q[1] - p[1]) + std::abs(q[2] - p[2]) < 24;
    if (!near) t.recent.push_back(p);
  }
}
void pushUndo() { TS& t = T(); t.undo.push_back(t.img); if (t.undo.size() > 100) t.undo.erase(t.undo.begin()); t.redo.clear(); t.dirty = true; }
void setPx(int x, int y, const Px& p) {
  TS& t = T();
  auto put = [&](int xx) { if (xx < 0 || xx >= N || y < 0 || y >= N) return; std::memcpy(&t.img[(y * N + xx) * 4], p.data(), 4); };
  put(x); if (t.mirror) put(N - 1 - x);
  t.texDirty = true;
}
void fill(int x, int y, const Px& p) {
  TS& t = T();
  Px from; std::memcpy(from.data(), &t.img[(y * N + x) * 4], 4);
  if (from == p) return;
  std::vector<int> st{y * N + x}; std::vector<u8> seen(N * N, 0);
  while (!st.empty()) {
    int i = st.back(); st.pop_back();
    if (seen[i]) continue; seen[i] = 1;
    if (std::memcmp(&t.img[i * 4], from.data(), 4) != 0) continue;
    std::memcpy(&t.img[i * 4], p.data(), 4);
    int ix = i % N, iy = i / N;
    if (ix > 0) st.push_back(i - 1);
    if (ix < N - 1) st.push_back(i + 1);
    if (iy > 0) st.push_back(i - N);
    if (iy < N - 1) st.push_back(i + N);
  }
  t.texDirty = true;
}
void addRecent(const Px& p) {
  TS& t = T();
  auto it = std::find(t.recent.begin(), t.recent.end(), p);
  if (it != t.recent.end()) t.recent.erase(it);
  t.recent.insert(t.recent.begin(), p);
  if (t.recent.size() > 32) t.recent.pop_back();
}
// one press of the tool at (x, y); first = the start of a stroke (saves an undo step)
void use(int x, int y, bool first) {
  TS& t = T();
  if (t.layer < 0 || x < 0 || y < 0 || x >= N || y >= N) return;
  if (t.tool == 2) { std::memcpy(t.col.data(), &t.img[(y * N + x) * 4], 4); if (t.col[3] < 128) t.col[3] = 255; t.status = "Picked a colour"; return; }
  if (first) pushUndo();
  if (t.tool == 1) { fill(x, y, t.col); addRecent(t.col); }
  else setPx(x, y, t.tool == 3 ? Px{0, 0, 0, 0} : t.col);
}
bool saveMine() {
  TS& t = T();
  std::vector<u8> out = t.img;
  for (int i = 0; i < N * N; i++) if (out[i * 4 + 3] >= 250) out[i * 4 + 3] = 255;
  const fs::path p = userDir() / "textures" / (t.sel + ".png");
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  if (!savePng(p, N, N, out.data())) { t.status = "Couldn't write " + p.string(); return false; }
  // the list and the atlas show it at once
  u8* dst = t.ts.tile(t.layer);
  for (int i = 0; i < N * N; i++) { std::memcpy(dst + i * 4, &out[i * 4], 4); if (t.tintMask[i] && dst[i * 4 + 3] >= 128) dst[i * 4 + 3] = 253; }
  std::vector<u8> a((size_t)N * N * 4);
  for (int i = 0; i < N * N; i++) { Px q = look(dst + i * 4, t.tintMask[i]); std::memcpy(&a[i * 4], q.data(), 4); }
  glBindTexture(GL_TEXTURE_2D, t.atlas);
  glTexSubImage2D(GL_TEXTURE_2D, 0, (t.layer % t.cols) * N, (t.layer / t.cols) * N, N, N, GL_RGBA, GL_UNSIGNED_BYTE, a.data());
  t.mine.insert(t.sel); t.shownKey.clear(); t.dirty = false;
  t.status = "Saved " + p.string() + ": the game draws it from its next start";
  return true;
}
bool importPng(const std::string& path) {
  TS& t = T();
  int w = 0, h = 0; std::vector<u8> px;
  if (t.layer < 0) { t.status = "Pick a picture to paint over first"; return false; }
  if (!loadPng(path, w, h, px)) { t.status = "Couldn't read that PNG"; return false; }
  pushUndo(); t.img = fitTile(px, w, h, N); t.texDirty = true;
  t.status = "Brought in " + fs::path(path).filename().string() + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
  return true;
}
void doUndo() { TS& t = T(); if (t.undo.empty()) return; t.redo.push_back(t.img); t.img = t.undo.back(); t.undo.pop_back(); t.texDirty = true; t.dirty = true; }
void doRedo() { TS& t = T(); if (t.redo.empty()) return; t.undo.push_back(t.img); t.img = t.redo.back(); t.redo.pop_back(); t.texDirty = true; t.dirty = true; }

void drawList(Launcher& L) {
  TS& t = T();
  const char* cats[] = {"Blocks", "Items", "Yours"};
  for (int i = 0; i < 3; i++) {
    if (i) ImGui::SameLine();
    if (mcButton(L, cats[i], ImVec2(78, 30), t.cat == i ? BtnKind::Go : BtnKind::Grey, true, L.fUi)) { t.cat = i; t.shownKey.clear(); }
  }
  ImGui::SetNextItemWidth(250); ImGui::InputTextWithHint("##tfind", "Find a picture (stone, sword...)", &t.filter);
  std::string f = toLower(t.filter), key = f + "|" + std::to_string(t.cat) + "|" + std::to_string(t.mine.size());
  if (key != t.shownKey) {
    t.shownKey = key; t.shown.clear();
    for (int i = 0; i < (int)t.names.size(); i++) {
      const std::string& n = t.names[i];
      const bool it = t.items.count(n) > 0;
      if (t.cat == 0 && it) continue;
      if (t.cat == 1 && !it) continue;
      if (t.cat == 2 && !t.mine.count(n)) continue;
      if (!f.empty() && n.find(f) == std::string::npos) continue;
      t.shown.push_back(i);
    }
  }
  ImGui::BeginChild("##tiles", ImVec2(250, std::max(200.f, ImGui::GetContentRegionAvail().y - 8)), ImGuiChildFlags_Borders, ImGuiWindowFlags_NavFlattened);
  ImGuiListClipper clip; clip.Begin((int)t.shown.size(), 28);
  const float aw = (float)(t.cols * N), ah = (float)(((t.ts.count + t.cols - 1) / t.cols) * N);
  while (clip.Step()) for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
    const std::string& n = t.names[t.shown[r]];
    const int li = t.ts.index[n];
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable(("##" + n).c_str(), n == t.sel, 0, ImVec2(0, 24))) open(n);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = (li % t.cols) * N / aw, v = (li / t.cols) * N / ah;
    dl->AddImage((ImTextureID)(intptr_t)t.atlas, ImVec2(p.x + 2, p.y + 1), ImVec2(p.x + 24, p.y + 23), ImVec2(u, v), ImVec2(u + N / aw, v + N / ah));
    dl->AddText(ImVec2(p.x + 32, p.y + 3), t.mine.count(n) ? IM_COL32(255, 190, 90, 255) : IM_COL32(235, 225, 210, 255), n.c_str());
  }
  ImGui::EndChild();
}

void drawCanvas(Launcher& L, float S) {
  TS& t = T();
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float c = S / N;
  ImGui::InvisibleButton("##canvas", ImVec2(S, S));
  const bool hov = ImGui::IsItemHovered(), focused = ImGui::IsItemFocused();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    ImVec2 a(p.x + x * c, p.y + y * c), b(a.x + c, a.y + c);
    const int i = y * N + x;
    Px q = t.img.empty() ? Px{0, 0, 0, 0} : look(&t.img[i * 4], t.tintMask[i]);
    if (q[3] < 255) dl->AddRectFilled(a, b, ((x + y) & 1) ? IM_COL32(70, 66, 62, 255) : IM_COL32(52, 48, 46, 255));
    if (q[3]) dl->AddRectFilled(a, b, c32(q));
  }
  if (t.grid) {
    for (int k = 0; k <= N; k++) {
      const ImU32 g = k % 8 == 0 ? IM_COL32(0, 0, 0, 110) : IM_COL32(0, 0, 0, 45);
      dl->AddLine(ImVec2(p.x + k * c, p.y), ImVec2(p.x + k * c, p.y + S), g);
      dl->AddLine(ImVec2(p.x, p.y + k * c), ImVec2(p.x + S, p.y + k * c), g);
    }
  }
  if (t.mirror) dl->AddLine(ImVec2(p.x + S / 2, p.y), ImVec2(p.x + S / 2, p.y + S), IM_COL32(120, 200, 255, 160), 2);
  dl->AddRect(p, ImVec2(p.x + S, p.y + S), focused || t.padEdit ? IM_COL32(255, 170, 60, 255) : IM_COL32(0, 0, 0, 200), 0, 0, 2);
  if (t.layer < 0) { dl->AddText(L.fMid, L.fMid->FontSize, ImVec2(p.x + 20, p.y + S / 2 - 10), IM_COL32(255, 230, 200, 200), "Pick a picture on the left"); return; }
  // the mouse: left paints with the tool, right picks the colour
  if (hov) {
    const int x = (int)((io.MousePos.x - p.x) / c), y = (int)((io.MousePos.y - p.y) / c);
    dl->AddRect(ImVec2(p.x + x * c, p.y + y * c), ImVec2(p.x + (x + 1) * c, p.y + (y + 1) * c), IM_COL32(255, 255, 255, 200), 0, 0, 2);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { int k = t.tool; t.tool = 2; use(x, y, true); t.tool = k; }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { use(x, y, true); t.stroking = t.tool == 0 || t.tool == 3; }
    else if (t.stroking && ImGui::IsMouseDown(ImGuiMouseButton_Left)) use(x, y, false);
  }
  if (t.stroking && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { t.stroking = false; if (t.tool == 0) addRecent(t.col); }
  // the controller
  if (!t.padEdit && focused && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) { t.padEdit = true; io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad; t.status = "Painting: d-pad moves, A paints, X picks, Y tool, L1/R1 colour, B done"; return; }
  if (t.padEdit) {
    auto pr = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (pr(ImGuiKey_GamepadDpadLeft)) t.cx = std::max(0, t.cx - 1);
    if (pr(ImGuiKey_GamepadDpadRight)) t.cx = std::min(N - 1, t.cx + 1);
    if (pr(ImGuiKey_GamepadDpadUp)) t.cy = std::max(0, t.cy - 1);
    if (pr(ImGuiKey_GamepadDpadDown)) t.cy = std::min(N - 1, t.cy + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) use(t.cx, t.cy, true);
    else if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) && (t.tool == 0 || t.tool == 3) && (pr(ImGuiKey_GamepadDpadLeft) || pr(ImGuiKey_GamepadDpadRight) || pr(ImGuiKey_GamepadDpadUp) || pr(ImGuiKey_GamepadDpadDown))) use(t.cx, t.cy, false);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)) { int k = t.tool; t.tool = 2; use(t.cx, t.cy, true); t.tool = k; }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) { t.tool = (t.tool + 1) % 4; t.status = TOOLS[t.tool]; }
    if (!t.recent.empty() && ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) { t.ri = (t.ri + (int)t.recent.size() - 1) % (int)t.recent.size(); t.col = t.recent[t.ri]; }
    if (!t.recent.empty() && ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) { t.ri = (t.ri + 1) % (int)t.recent.size(); t.col = t.recent[t.ri]; }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL2)) doUndo();
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR2)) doRedo();
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) { t.padEdit = false; io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; t.status = ""; }
    dl->AddRect(ImVec2(p.x + t.cx * c - 1, p.y + t.cy * c - 1), ImVec2(p.x + (t.cx + 1) * c + 1, p.y + (t.cy + 1) * c + 1), IM_COL32(255, 170, 60, 255), 0, 0, 3);
  }
}

void drawSide(Launcher& L) {
  TS& t = T();
  if (t.texDirty && t.cur && !t.img.empty()) {
    std::vector<u8> a((size_t)N * N * 4);
    for (int i = 0; i < N * N; i++) { Px q = look(&t.img[i * 4], t.tintMask[i]); std::memcpy(&a[i * 4], q.data(), 4); }
    glBindTexture(GL_TEXTURE_2D, t.cur);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, N, N, GL_RGBA, GL_UNSIGNED_BYTE, a.data());
    t.texDirty = false;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImTextureID tex = (ImTextureID)(intptr_t)t.cur;
  // the preview: a block as it sits in the world and the picture tiled (to see the seams), or an item held up big
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + 250, p.y + 130), IM_COL32(20, 16, 14, 230), 3);
    if (t.layer >= 0) {
      if (t.items.count(t.sel)) dl->AddImage(tex, ImVec2(p.x + 61, p.y + 1), ImVec2(p.x + 189, p.y + 129));
      else {
        const float s = 50; const ImVec2 c(p.x + 65, p.y + 62);
        const float k = s * 0.866f;
        dl->AddImageQuad(tex, ImVec2(c.x, c.y - s), ImVec2(c.x + k, c.y - s / 2), ImVec2(c.x, c.y), ImVec2(c.x - k, c.y - s / 2));
        dl->AddImageQuad(tex, ImVec2(c.x - k, c.y - s / 2), ImVec2(c.x, c.y), ImVec2(c.x, c.y + s), ImVec2(c.x - k, c.y + s / 2), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(205, 205, 205, 255));
        dl->AddImageQuad(tex, ImVec2(c.x, c.y), ImVec2(c.x + k, c.y - s / 2), ImVec2(c.x + k, c.y + s / 2), ImVec2(c.x, c.y + s), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(160, 160, 160, 255));
        const ImVec2 g(p.x + 130, p.y + 7);
        for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) dl->AddImage(tex, ImVec2(g.x + x * 38, g.y + y * 38), ImVec2(g.x + (x + 1) * 38, g.y + (y + 1) * 38));
      }
    }
    ImGui::Dummy(ImVec2(250, 134));
  }
  // the tools
  for (int i = 0; i < 4; i++) {
    if (i) ImGui::SameLine();
    if (mcButton(L, TOOLS[i], ImVec2(59, 30), t.tool == i ? BtnKind::Go : BtnKind::Grey, true, L.fSmall)) t.tool = i;
  }
  if (mcButton(L, t.mirror ? "Mirror: on" : "Mirror: off", ImVec2(122, 30), BtnKind::Grey, true, L.fSmall)) t.mirror = !t.mirror;
  ImGui::SameLine();
  if (mcButton(L, t.grid ? "Grid: on" : "Grid: off", ImVec2(122, 30), BtnKind::Grey, true, L.fSmall)) t.grid = !t.grid;
  if (mcButton(L, "Undo", ImVec2(122, 30), BtnKind::Grey, !t.undo.empty(), L.fSmall)) doUndo();
  ImGui::SameLine();
  if (mcButton(L, "Redo", ImVec2(122, 30), BtnKind::Grey, !t.redo.empty(), L.fSmall)) doRedo();
  // the colour and the palette
  float cf[4] = {t.col[0] / 255.f, t.col[1] / 255.f, t.col[2] / 255.f, t.col[3] / 255.f};
  ImGui::SetNextItemWidth(250);
  if (ImGui::ColorEdit4("##col", cf, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex)) for (int k = 0; k < 4; k++) t.col[k] = (u8)std::lround(cf[k] * 255);
  for (size_t i = 0; i < t.recent.size(); i++) {
    if (i % 8) ImGui::SameLine(0, 3);
    const Px& q = t.recent[i];
    ImGui::PushID((int)i);
    if (ImGui::ColorButton("##sw", ImVec4(q[0] / 255.f, q[1] / 255.f, q[2] / 255.f, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(28, 22))) { t.col = q; t.ri = (int)i; if (t.tool == 2 || t.tool == 3) t.tool = 0; }
    ImGui::PopID();
  }
}
} // namespace

void drawTextureStudio(Launcher& L) {
  TS& t = T();
  if (!ready(L)) { ImGui::PushFont(L.fMid); ImGui::TextUnformatted("Loading the pictures..."); ImGui::PopFont(); return; }
  static bool once = false;
  if (!once) { once = true; if (const char* e = std::getenv("DQ_TEX_OPEN")) open(e); }   // tests: start on a picture
  ImGuiIO& io = ImGui::GetIO();
  if (!io.WantTextInput && t.layer >= 0) {
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) doRedo(); else doUndo(); }
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) doRedo();
    else if (!io.KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_P) || ImGui::IsKeyPressed(ImGuiKey_B)) t.tool = 0;
      if (ImGui::IsKeyPressed(ImGuiKey_F)) t.tool = 1;
      if (ImGui::IsKeyPressed(ImGuiKey_I)) t.tool = 2;
      if (ImGui::IsKeyPressed(ImGuiKey_E)) t.tool = 3;
      if (ImGui::IsKeyPressed(ImGuiKey_M)) t.mirror = !t.mirror;
      if (ImGui::IsKeyPressed(ImGuiKey_G)) t.grid = !t.grid;
    }
  }
  ImGui::BeginGroup(); drawList(L); ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::PushFont(L.fMid);
  ImGui::TextUnformatted(t.sel.empty() ? "Texture Studio" : (t.sel + (t.dirty ? " *" : "") + (t.mine.count(t.sel) ? "  (yours)" : "")).c_str());
  ImGui::PopFont();
  const ImVec2 av = ImGui::GetContentRegionAvail();
  const float S = std::floor(std::clamp(std::min(av.x - 270, av.y - 130), 192.f, 512.f) / N) * N;
  ImGui::BeginGroup(); drawCanvas(L, S); ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup(); drawSide(L); ImGui::EndGroup();
  // in and out
  const bool on = t.layer >= 0;
  ImGui::SetNextItemWidth(260); ImGui::InputTextWithHint("##png", "A PNG to bring in (any square size)", &t.path);
  ImGui::SameLine();
  if (mcButton(L, "Browse\xE2\x80\xA6", ImVec2(0, 0), BtnKind::Grey, !t.pick.busy())) t.pick.open("A picture", "png");
  std::string picked; if (t.pick.take(picked) && !picked.empty()) t.path = picked;
  ImGui::SameLine();
  if (mcButton(L, "Bring it in", ImVec2(0, 0), BtnKind::Grey, on && !t.path.empty())) importPng(t.path);
  if (mcButton(L, "Use in the game", ImVec2(0, 0), BtnKind::Go, on)) saveMine();
  ImGui::SameLine();
  if (mcButton(L, "Start over", ImVec2(0, 0), BtnKind::Grey, on && !t.undo.empty())) { pushUndo(); const u8* s = t.ts.tile(t.layer); t.img.assign(s, s + N * N * 4); for (int i = 0; i < N * N; i++) if (t.img[i * 4 + 3] >= 250) t.img[i * 4 + 3] = 255; t.texDirty = true; }
  ImGui::SameLine();
  if (on && t.mine.count(t.sel) && mcButton(L, "Put the original back", ImVec2(0, 0))) {
    std::error_code ec; fs::remove(userDir() / "textures" / (t.sel + ".png"), ec);
    std::memcpy(t.ts.tile(t.layer), t.base.data() + (size_t)t.layer * N * N * 4, N * N * 4);
    t.mine.erase(t.sel); t.shownKey.clear();
    std::vector<u8> a((size_t)N * N * 4);
    for (int i = 0; i < N * N; i++) { Px q = look(t.ts.tile(t.layer) + i * 4, t.tintMask[i]); std::memcpy(&a[i * 4], q.data(), 4); }
    glBindTexture(GL_TEXTURE_2D, t.atlas);
    glTexSubImage2D(GL_TEXTURE_2D, 0, (t.layer % t.cols) * N, (t.layer / t.cols) * N, N, N, GL_RGBA, GL_UNSIGNED_BYTE, a.data());
    open(t.sel); t.status = "The original is back";
  }
  ImGui::SameLine();
  if (mcButton(L, "Open folder", ImVec2(0, 0))) { std::error_code ec; fs::create_directories(userDir() / "textures", ec); openPath(userDir() / "textures"); }
  ImGui::PushFont(L.fSmall);
  dimText("%s", !t.status.empty() ? t.status.c_str() : t.ts.packName.empty() ? "Pictures from the Doku tiles. P pencil, F fill, I picker, E eraser, M mirror, G grid; right-click picks a colour."
                                                    : ("Pictures from " + t.ts.packName + ". P pencil, F fill, I picker, E eraser, M mirror, G grid; right-click picks a colour.").c_str());
  ImGui::PopFont();
  ImGui::EndGroup();
}

} // namespace dq
