// launcher/options.cpp — the Options tab, laid out like Better than Adventure 8.0's options screen (the same pages,
// categories and rows as the game's Options, ui/options.cpp): renderer, graphics presets with a suggestion from the
// GPU (GL_RENDERER of the launcher's own GL context), view distances, micro-voxel detail, resource/shader packs,
// frame rate, launcher behaviour and updates. These are the game's own settings (common/settings.h,
// userDir()/settings.json), so the game starts with what is chosen here.
#include "launcher/launcher.h"
#include "common/ui_scale.h"
#include "assets/tileset.h"
#include "render/gl.h"
#include <imgui_internal.h>
#include <map>
#include <set>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace dq {

// ---------------------------------------------------------------- GPU → suggested preset
GpuInfo suggestForGpu(const std::string& vendor, const std::string& renderer, int major, int minor) {
  GpuInfo g; g.vendor = vendor; g.renderer = renderer; g.major = major; g.minor = minor;
  std::string r = toLower(renderer + " " + vendor);
  auto has = [&](const char* s) { return r.find(s) != std::string::npos; };
  auto numAfter = [&](const char* key) -> int {   // first number after key, e.g. "gtx 1060" → 1060
    size_t p = r.find(key); if (p == std::string::npos) return -1;
    p += std::strlen(key);
    while (p < r.size() && !std::isdigit((unsigned char)r[p])) { if (std::isalpha((unsigned char)r[p])) return -1; p++; }
    return p < r.size() ? std::atoi(r.c_str() + p) : -1;
  };
  int gl = major * 10 + minor;
  g.kind = gl >= 43 ? RendererKind::Modern : RendererKind::Legacy;
  if (has("llvmpipe") || has("softpipe") || has("swiftshader") || has("software") || has("swrast")) { g.preset = "potato"; g.kind = RendererKind::Legacy; g.why = "No graphics card was found (software drawing): the lightest settings."; }
  else if (has("mali") || has("adreno") || has("powervr") || has("videocore") || has("v3d") || has("panfrost") || has("lima")) { g.preset = "potato"; g.kind = RendererKind::Legacy; g.why = "A small ARM graphics chip: the lightest settings."; }
  else if (has("intel")) {
    if (has("arc")) { g.preset = "high"; g.why = "Intel Arc graphics card."; }
    else if (has("iris xe") || has("xe graphics")) { g.preset = "medium"; g.why = "Intel Iris Xe graphics."; }
    else if (has("uhd") || has("iris")) { g.preset = "low"; g.why = "Intel UHD/Iris graphics built into the processor."; }
    else { g.preset = "potato"; g.kind = RendererKind::Legacy; g.why = "Older Intel HD graphics built into the processor."; }
  } else if (has("nvidia") || has("geforce") || has("quadro")) {
    int rtx = numAfter("rtx"), gtx = numAfter("gtx"), gt = numAfter("gt ");
    if (rtx >= 3000) { g.preset = "ultra"; g.why = "A recent NVIDIA RTX graphics card."; }
    else if (rtx > 0) { g.preset = "high"; g.why = "An NVIDIA RTX graphics card."; }
    else if (gtx >= 1000) { g.preset = gtx >= 1060 && gtx < 1600 ? "high" : "medium"; g.why = "An NVIDIA GTX graphics card."; }
    else if (gtx >= 900) { g.preset = "medium"; g.why = "An NVIDIA GTX 900 graphics card."; }
    else if (gtx > 0 || gt > 0) { g.preset = "low"; g.why = "An older NVIDIA graphics card."; }
    else { g.preset = "medium"; g.why = "An NVIDIA graphics card."; }
  } else if (has("amd") || has("radeon") || has("ati ")) {
    int rx = numAfter("rx");
    if (rx >= 6000) { g.preset = rx >= 6700 ? "ultra" : "high"; g.why = "A recent AMD Radeon RX graphics card."; }
    else if (rx >= 5000 || has("vega 56") || has("vega 64")) { g.preset = "high"; g.why = "An AMD Radeon RX graphics card."; }
    else if (rx >= 400) { g.preset = "medium"; g.why = "An AMD Radeon RX 400/500 graphics card."; }
    else if (has("radeon(tm) graphics") || has("radeon graphics") || has("vega") || has("780m") || has("680m")) { g.preset = "low"; g.why = "AMD graphics built into the processor."; }
    else { g.preset = "low"; g.why = "An older AMD graphics card."; }
  } else if (has("apple")) { g.preset = "medium"; g.why = "Apple graphics."; }
  else { g.preset = "medium"; g.why = "Unknown graphics: middle settings."; }
  if (gl < 43 && g.preset != "potato") {
    g.kind = RendererKind::Legacy;
    if (g.preset == "ultra" || g.preset == "high" || g.preset == "medium") g.preset = "low";
    g.why += strFormat(" Its driver offers OpenGL %d.%d, so the Legacy renderer.", major, minor);
  }
  return g;
}


// ---------------------------------------------------------------- BTA 8.0's options layout, in the launcher
// The same pages, categories and rows as the game's Options (ui/options.cpp, ui/paged.h): the page list on the left
// with the pack's pictures, folding categories, 24-unit rows (name, a faint line, a 100-unit control, a reset
// button once it isn't the default) and the description when the name is hovered. One unit is 2 design pixels.
namespace {
std::vector<std::string> listPacks(const char* sub) {
  std::vector<std::string> out; std::error_code ec;
  for (auto& e : fs::directory_iterator(userSub(sub), ec)) {
    std::string n = e.path().filename().string();
    if (n.empty() || n[0] == '.') continue;
    if (e.is_directory() || e.path().extension() == ".zip") out.push_back(n);
  }
  std::sort(out.begin(), out.end());
  return out;
}
const char* presetNames[] = {"potato", "low", "medium", "high", "ultra", "custom"};
const char* presetLabels[] = {"Potato", "Low", "Medium", "High", "Ultra", "Custom"};
constexpr ImU32 K_WHITE = IM_COL32(255, 255, 255, 255), K_HOT = IM_COL32(255, 255, 160, 255), K_GREY = IM_COL32(127, 127, 127, 255), K_LINE = IM_COL32(127, 127, 127, 95);
constexpr float KU = 2;   // pixels per BTA unit

std::string lowerS(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// a page icon: a picture of the DokuQuest Base pack (BTA draws an item beside each page)
unsigned packIcon(const char* rel) {
  static std::map<std::string, unsigned> cache;
  auto it = cache.find(rel);
  if (it != cache.end()) return it->second;
  unsigned t = 0;
  if (auto b = readBinary(dataDir() / "resourcepacks" / "DokuQuest Base" / "assets" / "minecraft" / "textures" / rel)) {
    int w, h; std::vector<u8> px;
    if (loadPngMem(b->data(), b->size(), w, h, px)) {
      if (h > w) { px.resize((size_t)w * w * 4); h = w; }   // an animation strip: its first frame
      t = makeTexture2D(w, h, px.data(), true, false, false);
    }
  }
  return cache[rel] = t;
}

struct LKit {
  Launcher* L = nullptr;
  int page = 0; bool pageChanged = false;
  std::string filter;
  std::set<std::string> folded;
  float cw = 0; bool locked = false;
  std::string curCat, pendCat, tip; bool catOpen = true, catShown = false;
  float rowX = 0, rowNext = 0;

  bool searching() const { return !filter.empty(); }
  bool matches(const char* s) const { return filter.empty() || lowerS(s).find(lowerS(filter)) != std::string::npos; }
  void begin() { curCat.clear(); pendCat.clear(); catOpen = true; catShown = false; tip.clear(); }
  void flushCat() {
    if (catShown || pendCat.empty()) return;
    catShown = true;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(("cat/" + pendCat).c_str());
    bool clicked = ImGui::InvisibleButton("##cat", ImVec2(cw, 24 * KU));
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImGui::PopID();
    std::string key = std::to_string(page) + "/" + pendCat;
    if (clicked && !searching()) { if (folded.count(key)) folded.erase(key); else folded.insert(key); }
    bool open = searching() || !folded.count(key);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 c = hot ? K_HOT : K_GREY;
    float cy = p.y + 12 * KU, s = 3 * KU; ImVec2 tc(p.x + 3 * KU, cy);
    if (open) dl->AddTriangle(ImVec2(tc.x - s, tc.y - s * 0.6f), ImVec2(tc.x + s, tc.y - s * 0.6f), ImVec2(tc.x, tc.y + s * 0.8f), c, 1.2f);
    else dl->AddTriangle(ImVec2(tc.x - s * 0.6f, tc.y - s), ImVec2(tc.x - s * 0.6f, tc.y + s), ImVec2(tc.x + s * 0.8f, tc.y), c, 1.2f);
    shadowText(dl, L->fUi, ImVec2(p.x + 10 * KU, std::round(cy - L->fUi->FontSize * 0.5f)), c, IM_COL32(31, 31, 31, 255), 2, pendCat.c_str());
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 24 * KU));
    ImGui::Dummy(ImVec2(cw, 0));
  }
  bool category(const char* name) {
    curCat = pendCat = name; catShown = false;
    catOpen = searching() || !folded.count(std::to_string(page) + "/" + name);
    if (!searching()) flushCat();
    return catOpen;
  }
  // a row: true when shown; the control goes at *ctrl
  bool rowBegin(const char* label, const char* desc, float ctrlU, bool isDef, ImVec2* ctrl, bool* reset) {
    *reset = false;
    if (!catOpen || !matches(label)) return false;
    flushCat();
    const float indent = curCat.empty() ? 0 : 16 * KU;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x + indent, right = p.x + cw, w = std::min(ctrlU * KU, (cw - indent) * 0.6f);
    *ctrl = ImVec2(std::round(right - w), p.y + 2 * KU);
    bool hov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(x, p.y + 2 * KU), ImVec2(right, p.y + 22 * KU));
    bool labelHov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(x, p.y + 2 * KU), ImVec2(ctrl->x - 20 * KU, p.y + 22 * KU));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* f = L->fUi;
    float lw = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label).x;
    shadowText(dl, f, ImVec2(x, std::round(p.y + 12 * KU - f->FontSize * 0.5f)), hov ? K_HOT : (locked ? IM_COL32(160, 160, 160, 255) : K_WHITE), IM_COL32(56, 56, 56, 255), 2, label);
    float lineEnd = ctrl->x - 8 * KU - (isDef ? 0 : 20 * KU);
    if (lineEnd > x + lw + 8 * KU) dl->AddRectFilled(ImVec2(x + lw + 8 * KU, p.y + 12 * KU), ImVec2(lineEnd, p.y + 12 * KU + 1), K_LINE);
    if (labelHov && desc && *desc) tip = desc;
    ImGui::PushID(curCat.c_str()); ImGui::PushID(label);
    if (!isDef && !locked) {
      ImGui::SetCursorScreenPos(ImVec2(ctrl->x - 20 * KU, ctrl->y));
      if (mcButton(*L, "R##reset", ImVec2(20 * KU, 20 * KU), BtnKind::Grey, true, L->fSmall)) *reset = true;
      if (ImGui::IsItemHovered()) tip = "Reset to default";
    }
    ImGui::SetCursorScreenPos(*ctrl);
    rowX = p.x; rowNext = p.y + 24 * KU;
    return true;
  }
  void rowEnd() {
    ImGui::PopID(); ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(rowX, rowNext));
    ImGui::Dummy(ImVec2(cw, 0));
  }
  float ctrlW(float u) const { return std::min(u * KU, (cw - (curCat.empty() ? 0 : 16 * KU)) * 0.6f); }
  bool toggle(const char* label, bool* v, bool def, const char* desc = nullptr) {
    ImVec2 c; bool r;
    if (!rowBegin(label, desc, 100, *v == def, &c, &r)) return false;
    bool ch = mcButton(*L, *v ? "ON###t" : "OFF###t", ImVec2(ctrlW(100), 20 * KU), BtnKind::Grey, !locked, L->fUi);
    if (ch) *v = !*v;
    rowEnd();
    if (r) { *v = def; ch = true; }
    return ch;
  }
  bool cycle(const char* label, int* cur, const std::vector<std::string>& names, int def, const char* desc = nullptr, float u = 100) {
    if (names.empty()) return false;
    int n = (int)names.size(); *cur = clampv(*cur, 0, n - 1);
    ImVec2 c; bool r;
    if (!rowBegin(label, desc, u, *cur == def, &c, &r)) return false;
    bool ch = false;
    std::string t = names[*cur] + "###c";
    if (mcButton(*L, t.c_str(), ImVec2(ctrlW(u), 20 * KU), BtnKind::Grey, !locked, L->fUi)) { *cur = (*cur + 1) % n; ch = true; }
    if (!locked && ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *cur = (*cur + n - 1) % n; ch = true; }
    rowEnd();
    if (r && *cur != def) { *cur = def; ch = true; }
    return ch;
  }
  bool range(const char* label, float* v, float mn, float mx, float step, const std::string& shown, float def, const char* desc = nullptr, float u = 100) {
    ImVec2 c; bool r;
    bool isDef = std::fabs(*v - def) < std::max(1e-4f, step * 0.5f);
    if (!rowBegin(label, desc, u, isDef, &c, &r)) return false;
    float w = ctrlW(u), h = 20 * KU, hw = 8 * KU;
    if (locked) ImGui::BeginDisabled();
    ImGui::InvisibleButton("##slider", ImVec2(w, h));
    if (locked) ImGui::EndDisabled();
    bool hot = !locked && (ImGui::IsItemHovered() || ImGui::IsItemFocused()), ch = false;
    if (!locked && ImGui::IsItemActive()) {
      float t = clampv((ImGui::GetIO().MousePos.x - c.x - hw * 0.5f) / std::max(1.f, w - hw), 0.f, 1.f);
      float nv = mn + t * (mx - mn); if (step > 0) nv = clampv(std::round((nv - mn) / step) * step + mn, mn, mx);
      if (nv != *v) { *v = nv; ch = true; }
    }
    if (!locked && ImGui::IsItemFocused()) {
      int dx = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ? 1 : ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ? -1 : 0;
      if (dx) { *v = clampv(*v + step * dx, mn, mx); ch = true; }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(c.x + w, c.y + h);
    dl->AddImage((ImTextureID)(intptr_t)L->texBtn, c, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(110, 110, 110, 255));
    dl->AddRectFilled(c, b, IM_COL32(0, 0, 0, 90));
    dl->AddRect(c, b, IM_COL32(0, 0, 0, 255), 0, 0, 2);
    float t = mx > mn ? clampv((*v - mn) / (mx - mn), 0.f, 1.f) : 0, hx = std::round(c.x + (w - hw) * t);
    dl->AddImage((ImTextureID)(intptr_t)(hot ? L->texBtnHi : L->texBtn), ImVec2(hx, c.y), ImVec2(hx + hw, b.y));
    dl->AddRectFilled(ImVec2(hx + 2, c.y + 2), ImVec2(hx + hw - 2, c.y + 4), IM_COL32(255, 255, 255, 82));
    dl->AddRectFilled(ImVec2(hx + 2, b.y - 6), ImVec2(hx + hw - 2, b.y - 2), IM_COL32(0, 0, 0, 82));
    dl->AddRect(ImVec2(hx, c.y), ImVec2(hx + hw, b.y), hot ? K_WHITE : IM_COL32(0, 0, 0, 255), 0, 0, 2);
    ImFont* f = L->fUi; ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, shown.c_str());
    shadowText(dl, f, ImVec2(std::floor((c.x + b.x - ts.x) * 0.5f), std::floor((c.y + b.y - ts.y) * 0.5f) - 1), locked ? IM_COL32(160, 160, 160, 255) : hot ? K_HOT : IM_COL32(224, 224, 224, 255), IM_COL32(56, 56, 56, 255), 2, shown.c_str());
    rowEnd();
    if (r && !isDef) { *v = def; ch = true; }
    return ch;
  }
  bool action(const char* label, const char* btn, const char* desc = nullptr, bool disabled = false, float u = 100, bool always = false) {
    ImVec2 c; bool r;
    if (!rowBegin(label, desc, u, true, &c, &r)) return false;
    std::string t = std::string(btn) + "###a";
    bool hit = mcButton(*L, t.c_str(), ImVec2(ctrlW(u), 20 * KU), BtnKind::Grey, !disabled && (always || !locked), L->fUi);
    rowEnd();
    return hit;
  }
  void note(const std::string& t, ImU32 col = IM_COL32(160, 160, 160, 255)) {
    if (searching() || !catOpen || t.empty()) return;
    flushCat();
    float indent = curCat.empty() ? 0 : 16 * KU;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x + indent, p.y + 2 * KU));
    ImGui::PushFont(L->fSmall); ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(p.x - ImGui::GetWindowPos().x + cw);
    ImGui::TextUnformatted(t.c_str());
    ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
    ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, q.y + 4 * KU));
    ImGui::Dummy(ImVec2(cw, 0));
  }
};
LKit& kit() { static LKit k; return k; }

void tooltipAt(Launcher& L, ImVec2 p, const std::string& t) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* f = L.fSmall;
  float wrap = 380;
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, wrap, t.c_str());
  ImVec2 ds = ImGui::GetIO().DisplaySize;
  p.x = std::min(p.x, ds.x - ts.x - 14); p.y = std::min(p.y, ds.y - ts.y - 14);
  ImVec2 b(p.x + ts.x + 10, p.y + ts.y + 10);
  dl->AddRectFilled(p, b, IM_COL32(16, 0, 16, 240));
  dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(b.x - 1, b.y - 1), IM_COL32(80, 0, 255, 90), 0, 0, 2);
  dl->AddText(f, f->FontSize, ImVec2(p.x + 5, p.y + 5), K_WHITE, t.c_str(), nullptr, wrap);
}
}

void drawOptions(Launcher& L) {
  Settings& s = L.settings;
  static const Settings D{};
  bool changed = false;
  LKit& k = kit();
  k.L = &L;
  k.locked = L.gameRunning;
  auto custom = [&] { s.preset = "custom"; changed = true; };
  struct PageDef { const char* name; const char* icon; };
  static const PageDef pages[] = {{"General", "block/grass_block_side.png"}, {"Player", "item/armor_stand.png"}, {"Audio", "item/music_disc_cat.png"},
    {"Video", "item/painting.png"}, {"Controls", "item/oak_boat.png"}, {"Accessibility", "item/spyglass.png"}, {"Asset Packs", "item/flower_banner_pattern.png"},
    {"Language Packs", "item/book.png"}, {"Launcher", "item/compass_00.png"}, {"Search", "block/glass.png"}};
  const int NP = 10, SEARCH = 9;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  float fullW = ImGui::GetContentRegionAvail().x, fullH = ImGui::GetContentRegionAvail().y;
  float leftW = std::floor(fullW * 0.25f);
  ImDrawList* bg = ImGui::GetWindowDrawList();
  // the bands: BTA's darker page dirt, a lighter 8-unit strip between the list and the page
  drawDirt(bg, origin, ImVec2(origin.x + fullW, origin.y + fullH), L, 0.62f);
  drawDirt(bg, ImVec2(origin.x + leftW, origin.y), ImVec2(origin.x + leftW + 8 * KU, origin.y + fullH), L, 0.25f);
  bg->AddRect(origin, ImVec2(origin.x + fullW, origin.y + fullH), IM_COL32(0, 0, 0, 255), 0, 0, 2);
  // the page list
  k.pageChanged = false;
  ImGui::SetCursorScreenPos(origin);
  ImGui::BeginChild("##optpages", ImVec2(leftW, fullH), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
  {
    float rowH = 20 * KU, listH = rowH * NP, y0 = listH < fullH ? std::floor((fullH - listH) * 0.5f) : 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < NP; i++) {
      ImGui::SetCursorPos(ImVec2(4 * KU, y0 + rowH * i));
      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(i);
      bool hit = ImGui::InvisibleButton("##pg", ImVec2(std::max(8.f, leftW - 8 * KU), rowH));
      bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
      ImGui::PopID();
      if (hit && k.page != i) { k.page = i; k.pageChanged = true; }
      if (unsigned t = packIcon(pages[i].icon)) dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(p.x, p.y + 2 * KU), ImVec2(p.x + 16 * KU, p.y + 18 * KU));
      ImU32 c = hot ? K_HOT : i == k.page ? K_WHITE : K_GREY;
      shadowText(dl, L.fUi, ImVec2(p.x + 19 * KU, std::round(p.y + (rowH - L.fUi->FontSize) * 0.5f)), c, IM_COL32(31, 31, 31, 255), 2, pages[i].name);
    }
    ImGui::SetCursorPos(ImVec2(0, y0 + listH)); ImGui::Dummy(ImVec2(1, 1));
  }
  ImGui::EndChild();
  // the page
  float pageX = origin.x + leftW + 8 * KU;
  ImGui::SetCursorScreenPos(ImVec2(pageX, origin.y));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * KU, 2 * KU));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6 * KU);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(192, 192, 192, 255));
  ImGui::BeginChild("##optpage", ImVec2(origin.x + fullW - pageX, fullH), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
  ImGui::PopStyleVar();
  if (k.pageChanged) ImGui::SetScrollY(0);
  k.cw = ImGui::GetContentRegionAvail().x - 4 * KU;
  k.begin();
  if (k.locked && k.page != 8) k.note("The game is running: these options can be changed in the game's own Options, or here once it has closed. Folders can still be opened.", IM_COL32(255, 255, 160, 255));

  auto pct = [](float v) { return std::to_string((int)std::lround(v * 100)) + "%"; };
  auto idx = [](const char* const* v, int n, const std::string& x, int d) { for (int i = 0; i < n; i++) if (x == v[i]) return i; return d; };
  auto drawPage = [&](int pg) {
    switch (pg) {
    case 0:
      if (k.category("Gameplay")) {
        k.range("Field of View", &s.fov, 55, 100, 1, std::to_string((int)std::lround(s.fov)) + "\xC2\xB0", D.fov, "How wide you see.") && (custom(), true);
        if (k.toggle("View Bobbing", &s.viewBob, D.viewBob, "The view sways as you walk.")) changed = true;
        if (k.toggle("Pause on Lost Focus", &s.pauseOnLostFocus, D.pauseOnLostFocus, "Opens the pause menu a moment after the game window is no longer the one in front. A world shared with others keeps going.")) changed = true;
        { static const char* sp[] = {"auto", "vertical", "horizontal"}; int cur = idx(sp, 3, s.split, 0);
          if (k.cycle("Two-Player Split", &cur, {"Automatic", "Side by Side", "Top and Bottom"}, 0, "How the screen splits when a second player joins with a controller.", 120)) { s.split = sp[cur]; changed = true; } }
      }
      if (k.category("User Interface")) {
        if (k.toggle("FPS in Overlay", &s.showFps, D.showFps, "The frame rate in the corner.")) changed = true;
      }
      if (k.category("Miscellaneous")) {
        float f = (float)(clampv(s.autosaveSec, 0, 115) / 5); int st = (int)f;
        if (k.range("Autosave", &f, 0, 23, 1, st == 0 ? "Off" : std::to_string(st * 5) + " seconds", (float)(D.autosaveSec / 5), "How often a world saves itself, in steps of 5 seconds, up to 115. Off still saves when you leave.")) { s.autosaveSec = (int)std::lround(f) * 5; changed = true; }
        { bool he = s.extra.value("holidayEvents", true); if (k.toggle("Holiday Events", &he, true, "Seasonal surprises from your calendar, like Halloween in October.")) { s.extra["holidayEvents"] = he; changed = true; } }
        { bool eb = s.extra.value("engineBridge", false); if (k.toggle("Engine Bridge", &eb, false, "Lets the launcher's Engine page talk to the game while it runs. Port 25590, this computer only.")) { s.extra["engineBridge"] = eb; changed = true; } }
        if (k.action("Worlds Folder", "Open", "The folder your saved worlds live in.", false, 100, true)) openPath(userSub("saves"));
        if (k.action("Screenshots Folder", "Open", "Pictures taken in the game.", false, 100, true)) openPath(userSub("screenshots"));
      }
      break;
    case 1:
      if (k.category("Player")) {
        if (k.toggle("Body in First Person", &s.fpBody, D.fpBody, "Look down to see your chest, legs and armour.")) changed = true;
        if (k.toggle("Held Item Sway", &s.heldSway, D.heldSway, "What you hold lags a touch behind when you turn.")) changed = true;
      }
      break;
    case 2:
      if (k.category("Audio")) {
        if (k.toggle("Subtitles", &s.captions, D.captions, "Words at the side of the screen for the sounds around you.")) changed = true;
        if (k.toggle("Mute on Lost Focus", &s.muteOnLostFocus, D.muteOnLostFocus, "Silence while another window is in front. The volume sliders stay as they are.")) changed = true;
        if (k.range("Master Volume", &s.volume, 0, 1, 0.05f, s.volume > 0 ? pct(s.volume) : "Off", D.volume)) changed = true;
        if (k.range("Music", &s.music, 0, 1, 0.05f, s.music > 0 ? pct(s.music) : "Off", D.music, "Pieces of the place and the hour.")) changed = true;
        { int cur = clampv(s.musicFreq, 0, 3); if (k.cycle("Music Frequency", &cur, {"Rarely", "Now and Then", "Often", "Nearly Always"}, D.musicFreq, "How long the quiet lasts between pieces.", 120)) { s.musicFreq = cur; changed = true; } }
        if (k.range("World Sounds", &s.ambience, 0, 1, 0.05f, s.ambience > 0 ? pct(s.ambience) : "Off", D.ambience, "Wind, birds, crickets, rain, and the sounds of caves.")) changed = true;
        if (k.range("Entity Sounds", &s.mobVol, 0, 1, 0.05f, s.mobVol > 0 ? pct(s.mobVol) : "Off", D.mobVol, "Voices and footsteps of animals and monsters.")) changed = true;
      }
      break;
    case 3:
      if (k.category("Display")) {
        { static const char* rl[] = {"Legacy", "Modern", "Vulkan"}; int cur = clampv((int)s.renderer, 0, 2);
          std::string why = strFormat("Modern needs OpenGL 4.5+ (this computer: OpenGL %d.%d). Legacy (OpenGL 3.3) runs on almost any PC since 2010. Vulkan uses the Vulkan driver.", L.gpu.major, L.gpu.minor);
          if (k.cycle("Renderer", &cur, {rl[0], rl[1], rl[2]}, (int)D.renderer, why.c_str())) { s.renderer = (RendererKind)cur; changed = true; } }
        { int cur = 5; for (int i = 0; i < 6; i++) if (s.preset == presetNames[i]) cur = i;
          int pick = cur;
          std::string sug = std::string("Sets the options below at once; changing one makes it Custom. Your graphics: ") + (L.gpu.renderer.empty() ? "unknown" : L.gpu.renderer) + ". Suggested: " +
            presetLabels[std::max(0, (int)(std::find(std::begin(presetNames), std::end(presetNames), L.gpu.preset) - std::begin(presetNames)))] + ". " + L.gpu.why;
          if (k.cycle("Graphics Preset", &pick, {"Potato", "Low", "Medium", "High", "Ultra", "Custom"}, 5, sug.c_str())) {
            if (pick == 5) pick = 0;   // Custom isn't picked; it is what changing an option makes
            RendererKind keep = s.renderer; s.applyPreset(presetNames[pick]); if (pick != 0) s.renderer = keep; changed = true;
          }
          bool same = s.preset == L.gpu.preset && s.renderer == L.gpu.kind;
          if (k.action("Suggested Settings", same ? "In Use" : "Use", sug.c_str(), same)) { s.applyPreset(L.gpu.preset); s.renderer = L.gpu.kind; changed = true; }
        }
        if (k.toggle("Full Screen", &s.fullscreen, D.fullscreen, "Start in full screen. F11 switches at any time.")) changed = true;
        { static const int v[] = {0, 30, 60, 120, 144, 240, -1}; int cur = s.vsync ? 0 : 6; if (!s.vsync) for (int i = 1; i < 6; i++) if (s.fpsLimit == v[i]) cur = i;
          if (k.cycle("FPS Limit", &cur, {"VSync", "30 FPS", "60 FPS", "120 FPS", "144 FPS", "240 FPS", "Unlimited"}, 0, "VSync matches the display. 30 saves battery.")) { s.vsync = cur == 0; s.fpsLimit = v[cur]; changed = true; } }
        if (k.range("Render Scale", &s.scale, 0.4f, 1, 0.05f, pct(s.scale), D.scale, "Render fewer pixels for smoother play.")) custom();
      }
      if (k.category("Visuals")) {
        { int cur = clampv(s.extra.value("dreamLight", 1), 0, 2); if (k.cycle("Lighting", &cur, {"Classic", "Dreamlike", "Dreamlike (Strong)"}, 1, "Dreamlike: a soft glow round bright things, golden light when the sun is low.", 120)) { s.extra["dreamLight"] = cur; changed = true; } }
        if (k.range("Brightness", &s.brightness, 0, 1, 0.05f, s.brightness <= 0 ? "Moody" : s.brightness >= 1 ? "Bright" : pct(s.brightness), D.brightness, "How much you can see in the dark.")) custom();
        { bool cl = s.extra.value("colouredLight", true); if (k.toggle("Coloured Light", &cl, true, "Redstone glows red, soul fire blue, the coloured lamps their colour.")) { s.extra["colouredLight"] = cl; changed = true; } }
        if (k.cycle("Block Detail", &s.microVoxels, {"Flat", "Shaded", "Raised"}, D.microVoxels, "Blocks are 32x32x32 little voxels: how much of that depth is drawn.")) custom();
        if (s.microVoxels == 2) { float f = (float)s.microVoxelDist; if (k.range("Relief Distance", &f, 16, 128, 8, std::to_string((int)f) + " blocks", (float)D.microVoxelDist, "Past this the relief fades to shaded faces.")) { s.microVoxelDist = (int)f; custom(); } }
        if (k.cycle("Water", &s.waterQuality, {"Flat", "Waves", "Reflections"}, D.waterQuality, "Reflections: waves and reflections.")) custom();
        { bool sh = s.shadows != 0; if (k.toggle("Sun Shadows", &sh, D.shadows != 0, "Soft sun shadows (Modern renderer).")) { s.shadows = sh; custom(); } }
        if (k.toggle("Clouds", &s.clouds, D.clouds)) custom();
        { bool vc = s.extra.value("volumetricClouds", true);
          if (k.toggle("Volumetric Clouds", &vc, true, "Clouds as real volumes that thicken and darken into the storms. Off: the voxel heaps.")) { s.extra["volumetricClouds"] = vc; custom(); } }
        if (k.toggle("Fancy Leaves", &s.fancyLeaves, D.fancyLeaves, "See through tree canopies.")) custom();
        { static const char* pv[] = {"near", "all", "flat"}; int cur = idx(pv, 3, s.plantDetail, 0);
          if (k.cycle("3D Plants", &cur, {"Near You", "Everywhere", "Flat"}, 0, "Solid grass, flowers and crops near you; flat further out. Everywhere is the slowest.")) { s.plantDetail = pv[cur]; custom(); } }
        { bool g = s.extra.value("grass3d", s.preset != "potato"); if (k.toggle("3D Grass", &g, s.preset != "potato", "Blades of grass waving near you.")) { s.extra["grass3d"] = g; custom(); } }
        if (k.toggle("Waving Plants", &s.wavingPlants, D.wavingPlants, "Grass, leaves and water move in the wind.")) custom();
        if (k.toggle("See Through Portals", &s.portalViews, D.portalViews, "Portals show the other side.")) custom();
      }
      if (k.category("Post-Processing")) {
        static std::vector<std::string> sps; static double at = -100;
        if (L.time - at > 3) { sps = listPacks("shaderpacks"); at = L.time; }
        std::vector<std::string> items = {"None"}; int cur = 0;
        for (size_t i = 0; i < sps.size(); i++) { items.push_back(sps[i]); if (sps[i] == s.shaderPack) cur = (int)i + 1; }
        int c = cur;
        if (k.cycle("Shader Pack", &c, items, 0, "Our own packs and Iris/OptiFine packs (folders or .zip) in shaderpacks/.", 140) && c != cur) { s.shaderPack = c == 0 ? "" : sps[c - 1]; changed = true; }
        { float cc = std::clamp(s.extra.value("colorCorrectionNether", 0.5f), 0.f, 1.f);
          if (k.range("Nether Color Correction", &cc, 0, 1, 0.05f, pct(cc), 0.5f, "Better than Adventure's warm grade over the Nether.")) { s.extra["colorCorrectionNether"] = cc; changed = true; } }
        { float hh = std::clamp(s.extra.value("heatHaze", 0.5f), 0.f, 1.f);
          if (k.range("Heat Haze", &hh, 0, 1, 0.05f, pct(hh), 0.5f, "Better than Adventure's shimmer over the hot Nether's distance.")) { s.extra["heatHaze"] = hh; changed = true; } }
        int lv = std::clamp(s.extra.value("shaderLevel", 0), 0, 4);
        if (k.cycle("Shaders", &lv, {"Off", "Low", "Medium", "High", "Ultra"}, 0, "The game's own shaders over the picture: a film grade and vignette, then bloom and eyes that adjust, then sun rays, a lens flare, clear water and sun shadows; Ultra adds reflections and shading in the corners.")) { s.extra["shaderLevel"] = lv; changed = true; }
        { static const int v[] = {1, 2, 4, 8, 16}; int cur = 2; for (int i = 0; i < 5; i++) if (s.extra.value("anisotropic", 4) == v[i]) cur = i;
          if (k.cycle("Texture Filtering", &cur, {"Off", "2x", "4x", "8x", "16x"}, 2, "Anisotropic filtering: ground seen at a slant stays sharp into the distance.")) { s.extra["anisotropic"] = v[cur]; changed = true; } }
        if (k.action("Shader Packs Folder", "Open", nullptr, false, 100, true)) openPath(userSub("shaderpacks"));
      }
      if (k.category("Performance")) {
        { float f = (float)s.renderDist; if (k.range("Render Distance", &f, 2, 32, 1, std::to_string(s.renderDist) + " chunks", (float)D.renderDist, "Chunks of full detail around you.")) { s.renderDist = (int)std::lround(f); custom(); } }
        { static const int steps[] = {0, 512, 1024, 2048, 4096, 8192, 16384}; int cur = 0; for (int i = 0; i < 7; i++) if (s.farTerrain >= steps[i]) cur = i;
          if (k.cycle("Distant Terrain", &cur, {"Off", "512 Blocks", "1,024 Blocks", "2,048 Blocks", "4,096 Blocks", "8,192 Blocks", "16,384 Blocks"}, 3, "Simplified land drawn past the render distance (see forever).", 120)) { s.farTerrain = steps[cur]; custom(); } }
        { static const int v[] = {0, 768, 1536, 3072}; int mb = s.extra.value("terrainMemoryMB", 0), cur = 0; for (int i = 0; i < 4; i++) if (mb == v[i]) cur = i;
          if (k.cycle("Terrain Memory", &cur, {"No Limit", "768 MB", "1.5 GB", "3 GB"}, 0, "The graphics memory the land may use.")) { s.extra["terrainMemoryMB"] = v[cur]; changed = true; } }
        { int cur = 2 - clampv(s.particles, 0, 2); if (k.cycle("Particles", &cur, {"All", "Decreased", "Minimal"}, 0)) { s.particles = 2 - cur; custom(); } }
      }
      break;
    case 4:
      if (k.category("Mouse")) {
        if (k.range("Sensitivity", &s.sens, 0.2f, 3, 0.05f, strFormat("%.2f\xC3\x97", s.sens), D.sens, "Mouse and touch look speed.")) changed = true;
        if (k.toggle("Invert Mouse", &s.invertY, D.invertY)) changed = true;
        if (k.toggle("Raw Mouse Input", &s.rawMouse, D.rawMouse, "The mouse reports its own movement. Off warps the pointer, which some remote desktops prefer.")) changed = true;
      }
      if (k.category("Keyboard")) {
        if (k.toggle("Toggle Sneaking", &s.toggleSneak, D.toggleSneak, "Press once to crouch, again to stand.")) changed = true;
        if (k.toggle("Toggle Sprinting", &s.toggleSprint, D.toggleSprint, "Sprint stays on while you move forward.")) changed = true;
        bool any = (s.keyBinds.is_object() && !s.keyBinds.empty()) || (s.padBinds.is_object() && !s.padBinds.empty());
        if (k.action("Reset All Keybinds", "Reset", "Every key and controller button back to how it came. Keys are changed one by one in the game's Options \xE2\x86\x92 Controls.", !any)) { s.keyBinds = json::object(); s.padBinds = json::object(); changed = true; }
      }
      if (k.category("Controller")) {
        if (k.range("Look Sensitivity", &s.padSens, 0.3f, 3, 0.05f, strFormat("%.2f\xC3\x97", s.padSens), D.padSens)) changed = true;
        { static const char* pv[] = {"xbox", "playstation", "nintendo"}; int cur = idx(pv, 3, s.padLayout, 0);
          if (k.cycle("Button Layout", &cur, {"Xbox", "PlayStation", "Nintendo"}, 0, "Xbox: A jumps. PlayStation: \xE2\x9C\x95 jumps. Nintendo: B jumps.")) { s.padLayout = pv[cur]; changed = true; } }
        if (k.toggle("Auto-Jump (Touch)", &s.autoJump, D.autoJump)) changed = true;
      }
      k.note("Every key and controller button is changed in the game: Options \xE2\x86\x92 Controls, where BTA keeps them.");
      break;
    case 5:
      if (k.category("Accessibility")) {
        if (k.toggle("Subtitles", &s.captions, D.captions, "Words at the side of the screen for the sounds around you.")) changed = true;
        if (k.toggle("View Bobbing", &s.viewBob, D.viewBob)) changed = true;
        if (k.toggle("Screen Motion Effects", &s.screenShake, D.screenShake, "Explosions and boss slams shake the view.")) changed = true;
        if (k.toggle("Damage Tilt", &s.hurtTilt, D.hurtTilt, "The view tilts a little when you are hurt.")) changed = true;
        { int cur = clampv(s.colorFilter, 0, 3); if (k.cycle("Colorblind Assist", &cur, {"None", "Protanopia", "Deuteranopia", "Tritanopia"}, 0, "Shifts the colours you may not tell apart into ones you can.")) { s.colorFilter = cur; changed = true; } }
      }
      break;
    case 6: {
      static std::vector<std::string> rps; static double at = -100;
      if (L.time - at > 3) { rps = listPacks("resourcepacks"); at = L.time; }
      auto base = [](const std::string& n) { return n.empty() || n == "auto" || n == "none" || n == "builtin" || n == "DokuQuest Base"; };
      std::string cur = base(s.resourcePack) ? "DokuQuest Base" : s.resourcePack;
      if (k.category("Textures")) {
        if (k.action("Asset Packs Folder", "Open", "Put packs (folders or .zip) here.", false, 100, true)) openPath(userSub("resourcepacks"));
        k.note("DokuQuest Base is the game's own art and always stays underneath. Pick one pack to lay over it; the game loads it when it starts.");
      }
      auto row = [&](const std::string& n, const char* sub, bool sel) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = k.cw - 16 * KU, h = 22 * KU;
        ImGui::SetCursorScreenPos(ImVec2(p.x + 16 * KU, p.y));
        ImGui::PushID(n.c_str());
        bool hit = !k.locked && ImGui::InvisibleButton("##pack", ImVec2(w, h));
        if (k.locked) ImGui::Dummy(ImVec2(w, h));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImVec2 a(p.x + 16 * KU, p.y), b(a.x + w, a.y + h);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (sel) { dl->AddRectFilled(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(128, 128, 128, 255)); dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255)); }
        else if (hov) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 20));
        if (unsigned t = packIcon("item/painting.png")) dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(a.x + 2 * KU, a.y + 3 * KU), ImVec2(a.x + 18 * KU, a.y + 19 * KU));
        shadowText(dl, L.fUi, ImVec2(a.x + 22 * KU, a.y + 2 * KU), K_WHITE, IM_COL32(56, 56, 56, 255), 2, n.c_str());
        dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(a.x + 22 * KU, a.y + 12 * KU), IM_COL32(128, 128, 128, 255), sub);
        ImGui::SetCursorScreenPos(ImVec2(p.x, b.y + 4 * KU)); ImGui::Dummy(ImVec2(k.cw, 0));
        return hit;
      };
      if (k.category("Selected Asset Packs") && !k.searching()) {
        if (cur != "DokuQuest Base" && row(cur, "Click to take it off", true)) { s.resourcePack = "DokuQuest Base"; changed = true; }
        row("DokuQuest Base", "Always on. The game's own art.", cur == "DokuQuest Base");
      }
      if (k.category("Available Asset Packs") && !k.searching()) {
        int shown = 0;
        for (auto& pk : rps) { if (pk == cur || pk == "DokuQuest Base") continue; shown++; if (row(pk, "Click to put it on", false)) { s.resourcePack = pk; changed = true; } }
        if (!shown) k.note("No asset packs found :(");
      }
      break;
    }
    case 7:
      if (k.category("Language Packs") && !k.searching()) {
        k.note("English (US) \xE2\x80\x94 the game's own words, in use.", K_WHITE);
        k.note("No other language packs found. A pack in the resource packs folder can add its own words later.");
      }
      break;
    case 8:
      if (k.category("Launcher")) {
        UiTier tier = uiTierForEdge(L.displayEdge);
        static const char* ids[] = {"auto", "100", "125", "150", "175", "200", "250", "300"};
        std::vector<std::string> labels = {std::string("Auto (") + tier.name + ")", "100%", "125%", "150%", "175%", "200%", "250%", "300%"};
        int cur = uiScaleIsAuto(s.guiScale) ? 0 : idx(ids, 8, s.guiScale, 0);
        bool was = k.locked; k.locked = L.gameRunning;
        if (k.cycle("GUI Scale", &cur, labels, 0, "Menus, buttons and text here and in the game. Auto is 100% on 1080p, 125% on 2K, 150% on 3K and 200% on 4K.", 120) && s.guiScale != ids[cur]) { s.guiScale = ids[cur]; changed = true; syncInterfaceScale(L, true); }
        k.locked = false;
        bool keep = L.keepOpen();
        if (k.toggle("Keep the Launcher Open", &keep, true, "Come back here when you quit the game.")) { s.extra["launcherKeepOpen"] = keep; changed = true; }
        k.locked = was;
      }
      if (k.category("Updates")) {
        bool was = k.locked; k.locked = false;
        std::string v = L.updater.currentVersion();
        if (k.action(("Game Version " + niceVersion(v)).c_str(), L.updater.busy() ? "Checking\xE2\x80\xA6" : "Check Now", ("Launcher " + niceVersion(DQ_VERSION) + ". New versions come from GitHub Releases; your worlds stay as they are.").c_str(), L.updater.busy(), 100, true)) L.updater.check(false);
        if (L.updater.busy() && (L.updater.state == Updater::State::Downloading || L.updater.state == Updater::State::Extracting)) {
          ImVec2 p = ImGui::GetCursorScreenPos();
          ImGui::SetCursorScreenPos(ImVec2(p.x + 16 * KU, p.y));
          ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(79, 138, 47, 255));
          ImGui::ProgressBar(L.updater.progress, ImVec2(k.cw - 16 * KU, 0));
          ImGui::PopStyleColor();
        }
        if (!L.updater.message.empty()) k.note(L.updater.message, L.updater.state == Updater::State::Failed ? C_BAD : IM_COL32(160, 160, 160, 255));
        {
          static std::vector<std::string> vers; static double at = -1;
          if (at < 0 || nowSeconds() - at > 3) { vers = L.updater.installedVersions(); at = nowSeconds(); }
          std::vector<std::string> items = {"The Newest"}, keys = {""};
          for (auto& ver : vers) { items.push_back(niceVersion(ver)); keys.push_back(ver); }
          if (std::find(keys.begin(), keys.end(), std::string(DQ_VERSION)) == keys.end()) { items.push_back(niceVersion(DQ_VERSION)); keys.push_back(DQ_VERSION); }
          const std::string pin = L.updater.pinnedVersion();
          int c = 0; for (size_t i = 0; i < keys.size(); i++) if (keys[i] == pin) c = (int)i;
          int before = c;
          if (k.cycle("Version to Play", &c, items, 0, "Go back to an earlier version you have (worlds made in a newer one may not open in an older one), or keep the newest.", 120) && c != before) {
            L.updater.pin(keys[c]); L.setNotice(c == 0 ? std::string("Playing the newest version") : "Playing " + items[c]);
          }
        }
        if (k.toggle("Check When Starting", &s.autoUpdate, D.autoUpdate, "New game versions install by themselves.")) changed = true;
        {
          ImVec2 cpos; bool r;
          if (k.rowBegin("Updates From GitHub", "The repository with DokuQuest releases (owner/name).", 150, true, &cpos, &r)) {
            static std::string src; static bool init = false;
            if (!init) { src = L.updater.source; init = true; }
            char buf[160]; std::snprintf(buf, sizeof buf, "%s", src.c_str());
            ImGui::SetNextItemWidth(k.ctrlW(150));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 255));
            if (ImGui::InputText("##src", buf, sizeof buf)) src = buf;
            ImGui::PopStyleColor();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
              std::string t = src;
              for (const char* pre : {"https://github.com/", "http://github.com/", "github.com/"}) if (startsWith(toLower(t), pre)) t = t.substr(std::strlen(pre));
              if (endsWith(t, ".git")) t.resize(t.size() - 4);
              while (!t.empty() && t.back() == '/') t.pop_back();
              if (t.empty()) t = "FartGarfunkel/DokuQuest-Rewrite";
              if (validRepoSource(t)) { L.updater.source = src = t; s.extra["updateSource"] = t; changed = true; }
              else { L.updater.message = "Write the repository as owner/name, like FartGarfunkel/DokuQuest-Rewrite."; L.updater.state = Updater::State::Failed; }
            }
            k.rowEnd();
          }
        }
        k.locked = was;
      }
      break;
    default: break;
    }
  };

  if (k.page == SEARCH) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 2 * KU));
    ImGui::SetNextItemWidth(k.cw);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(160, 160, 160, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
    if (k.pageChanged) ImGui::SetKeyboardFocusHere();
    char buf[96]; std::snprintf(buf, sizeof buf, "%s", k.filter.c_str());
    if (ImGui::InputTextWithHint("##search", "Search\xE2\x80\xA6", buf, sizeof buf)) k.filter = buf;
    ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 26 * KU)); ImGui::Dummy(ImVec2(k.cw, 0));
    if (k.filter.empty()) k.note("Type part of a setting's name: every page is searched.");
    else for (int pg = 0; pg < SEARCH; pg++) drawPage(pg);
  } else {
    std::string keep; keep.swap(k.filter);   // only the Search page filters
    drawPage(k.page);
    k.filter.swap(keep);
  }
  ImGui::Dummy(ImVec2(k.cw, 8 * KU));
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  if (!k.tip.empty()) tooltipAt(L, ImVec2(ImGui::GetIO().MousePos.x + 14, ImGui::GetIO().MousePos.y - 8), k.tip);
  if (changed) L.saveSettings();
}

} // namespace dq
