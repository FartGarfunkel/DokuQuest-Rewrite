// launcher/options.cpp — launcher.html's options dialog grown into a tab: renderer, graphics presets with a
// suggestion from the GPU (GL_RENDERER of the launcher's own GL context), view distances, micro-voxel detail,
// resource/shader packs, frame rate, launcher behaviour and updates. These are the game's own settings
// (common/settings.h, userDir()/settings.json), so the game starts with what is chosen here.
#include "launcher/launcher.h"
#include <cstring>
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

// ---------------------------------------------------------------- option rows (.opt)
namespace {
struct Opt {
  ImVec2 start; float w, ctrlW, textH;
  Opt(Launcher& L, const char* label, const char* desc, float ctrl = 220) {
    w = ImGui::GetContentRegionAvail().x; ctrlW = std::min(ctrl, w * 0.55f);
    start = ImGui::GetCursorPos();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(start.x + w - ctrlW - 12);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(232, 225, 214, 255)); ImGui::TextUnformatted(label); ImGui::PopStyleColor();
    if (desc && *desc) { ImGui::PushFont(L.fSmall); ImGui::PushStyleColor(ImGuiCol_Text, C_DIM); ImGui::TextUnformatted(desc); ImGui::PopStyleColor(); ImGui::PopFont(); }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    textH = ImGui::GetItemRectSize().y;
    float fh = ImGui::GetFrameHeight();
    ImGui::SetCursorPos(ImVec2(start.x + w - ctrlW, start.y + std::max(0.f, (textH - fh) * 0.5f)));
    ImGui::SetNextItemWidth(ctrlW);
  }
  ~Opt() {
    float h = std::max(textH, ImGui::GetCursorPosY() - start.y - ImGui::GetStyle().ItemSpacing.y);
    float y = start.y + h + 7;
    ImVec2 sp = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(sp.x + start.x, sp.y + y - ImGui::GetScrollY()), ImVec2(sp.x + start.x + w, sp.y + y - ImGui::GetScrollY()), IM_COL32(255, 255, 255, 18));
    ImGui::SetCursorPos(ImVec2(start.x, y + 7));
  }
};

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

void heading(Launcher& L, const char* t) {
  ImGui::PushFont(L.fMid);
  ImVec2 p = ImGui::GetCursorScreenPos();
  shadowText(ImGui::GetWindowDrawList(), L.fMid, p, C_WHITE, IM_COL32(63, 63, 63, 255), 2, t);
  ImGui::Dummy(ImVec2(0, L.fMid->FontSize + 4));
  ImGui::PopFont();
}

const char* presetNames[] = {"potato", "low", "medium", "high", "ultra", "custom"};
const char* presetLabels[] = {"Potato", "Low", "Medium", "High", "Ultra", "Custom"};
}

void drawOptions(Launcher& L) {
  Settings& s = L.settings;
  bool changed = false;
  auto custom = [&] { s.preset = "custom"; changed = true; };
  float colW = (ImGui::GetContentRegionAvail().x - 16) * 0.5f, colH = ImGui::GetContentRegionAvail().y;
  // the game saves settings.json when it quits, so the game's options are locked while it runs
  bool locked = L.gameRunning;
  if (locked) ImGui::BeginDisabled();

  // ---------------- left: graphics
  beginDokuPanel("##optgfx", ImVec2(colW, colH), true, ImGuiWindowFlags_NavFlattened);
  heading(L, "Graphics");
  if (locked) { ImGui::EndDisabled(); ImGui::PushFont(L.fSmall); dimText("The game is running: these options can be changed once it has closed."); ImGui::PopFont(); ImGui::BeginDisabled(); }
  {
    Opt o(L, "Renderer", "Modern needs OpenGL 4.5+. Legacy runs on almost any PC since 2010.", 260);
    const char* labels[] = {"Legacy (OpenGL 3.3 — older PCs)", "Modern (OpenGL 4.6)", "Vulkan (coming later)"};
    int cur = (int)s.renderer;
    if (ImGui::BeginCombo("##renderer", labels[clampv(cur, 0, 2)])) {
      for (int k : {1, 0, 2}) {
        bool modernOk = k != 1 || L.gpu.major * 10 + L.gpu.minor >= 43;
        if (ImGui::Selectable(labels[k], cur == k, k == 2 ? ImGuiSelectableFlags_Disabled : 0)) { s.renderer = (RendererKind)k; changed = true; }
        if (k == 1 && !modernOk && ImGui::IsItemHovered()) ImGui::SetTooltip("This driver offers OpenGL %d.%d; the game will fall back to Legacy.", L.gpu.major, L.gpu.minor);
      }
      ImGui::EndCombo();
    }
  }
  {
    Opt o(L, "Graphics preset", "Sets the options below at once. Changing one makes it Custom.", 260);
    int cur = 5;
    for (int i = 0; i < 6; i++) if (s.preset == presetNames[i]) cur = i;
    if (ImGui::BeginCombo("##preset", presetLabels[cur])) {
      for (int i = 0; i < 5; i++) if (ImGui::Selectable(presetLabels[i], cur == i)) { RendererKind keep = s.renderer; s.applyPreset(presetNames[i]); if (i != 0) s.renderer = keep; changed = true; }
      ImGui::Selectable("Custom", cur == 5, ImGuiSelectableFlags_Disabled);
      ImGui::EndCombo();
    }
  }
  // the suggestion from this computer's graphics
  {
    ImGui::PushFont(L.fSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
    ImGui::PushTextWrapPos(0);
    ImGui::Text("Your graphics: %s (OpenGL %d.%d)", L.gpu.renderer.empty() ? "unknown" : L.gpu.renderer.c_str(), L.gpu.major, L.gpu.minor);
    ImGui::PopStyleColor();
    std::string sug = std::string("Suggested: ") + presetLabels[std::max(0, (int)(std::find(std::begin(presetNames), std::end(presetNames), L.gpu.preset) - std::begin(presetNames)))] +
                      (L.gpu.kind == RendererKind::Legacy ? " with the Legacy renderer. " : " with the Modern renderer. ") + L.gpu.why;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 160, 255)); ImGui::TextUnformatted(sug.c_str()); ImGui::PopStyleColor();
    ImGui::PopTextWrapPos(); ImGui::PopFont();
    bool same = s.preset == L.gpu.preset && s.renderer == L.gpu.kind;
    if (mcButton(L, same ? "Using the suggestion" : "Use the suggestion", ImVec2(0, 32), BtnKind::Grey, !same, L.fUi)) { s.applyPreset(L.gpu.preset); s.renderer = L.gpu.kind; changed = true; }
    ImGui::Dummy(ImVec2(0, 6));
  }
  {
    Opt o(L, "Render distance", strFormat("%d chunks of full detail around you", s.renderDist).c_str());
    if (ImGui::SliderInt("##rd", &s.renderDist, 2, 32, "%d chunks")) custom();
  }
  {
    static const int steps[] = {0, 512, 1024, 2048, 4096, 8192, 16384};
    int idx = 0; for (int i = 0; i < 7; i++) if (s.farTerrain >= steps[i]) idx = i;
    Opt o(L, "Distant terrain", "Simplified land drawn past the render distance (see forever)");
    if (ImGui::SliderInt("##far", &idx, 0, 6, idx == 0 ? "Off" : strFormat("%d blocks", steps[idx]).c_str())) { s.farTerrain = steps[idx]; custom(); }
  }
  {
    Opt o(L, "Micro-voxel detail", "Blocks are 32×32×32 little voxels: how much of that depth is drawn");
    const char* mv[] = {"Flat faces", "Shaded micro-voxels", "Relief micro-voxels"};
    if (ImGui::Combo("##mv", &s.microVoxels, mv, 3)) custom();
  }
  if (s.microVoxels == 2) {
    Opt o(L, "Relief distance", "Past this the relief fades to shaded faces");
    if (ImGui::SliderInt("##mvd", &s.microVoxelDist, 16, 128, "%d blocks")) custom();
  }
  {
    Opt o(L, "Water", nullptr);
    const char* wq[] = {"Flat", "Waves", "Waves + reflections"};
    if (ImGui::Combo("##water", &s.waterQuality, wq, 3)) custom();
  }
  {
    Opt o(L, "Shadows", "Soft sun shadows (Modern renderer)");
    const char* sh[] = {"Off", "Soft"};
    if (ImGui::Combo("##shadows", &s.shadows, sh, 2)) custom();
  }
  {
    Opt o(L, "Particles", nullptr);
    const char* pl[] = {"Minimal", "Decreased", "All"};
    int v = clampv(s.particles, 0, 2);
    if (ImGui::BeginCombo("##part", pl[v])) { for (int i = 2; i >= 0; i--) if (ImGui::Selectable(pl[i], v == i)) { s.particles = i; custom(); } ImGui::EndCombo(); }
  }
  {
    Opt o(L, "Fancy leaves", nullptr, 40);
    if (ImGui::Checkbox("##leaves", &s.fancyLeaves)) custom();
  }
  {
    Opt o(L, "Clouds", nullptr, 40);
    if (ImGui::Checkbox("##clouds", &s.clouds)) custom();
  }
  {
    Opt o(L, "Frame rate limit", nullptr);
    struct F { const char* n; int fps; bool vs; } fr[] = {{"Match display (VSync)", 0, true}, {"30 fps", 30, false}, {"60 fps", 60, false}, {"120 fps", 120, false}, {"144 fps", 144, false}, {"240 fps", 240, false}, {"Unlimited (VSync off)", 0, false}};
    int cur = 0; for (int i = 0; i < 7; i++) if (fr[i].vs == s.vsync && (s.vsync || fr[i].fps == s.fpsLimit)) cur = i;
    if (ImGui::BeginCombo("##fps", fr[cur].n)) { for (int i = 0; i < 7; i++) if (ImGui::Selectable(fr[i].n, cur == i)) { s.vsync = fr[i].vs; s.fpsLimit = fr[i].fps; changed = true; } ImGui::EndCombo(); }
  }
  {
    Opt o(L, "Start in full screen", "F11 switches at any time", 40);
    if (ImGui::Checkbox("##full", &s.fullscreen)) changed = true;
  }
  endDokuPanel();

  if (locked) ImGui::EndDisabled();
  ImGui::SameLine(0, 16);
  // ---------------- right: packs, launcher, updates
  beginDokuPanel("##optmore", ImVec2(colW, colH), true, ImGuiWindowFlags_NavFlattened);
  heading(L, "Packs");
  if (locked) ImGui::BeginDisabled();
  static std::vector<std::string> rps, sps; static double listedAt = -100;
  if (L.time - listedAt > 3) { rps = listPacks("resourcepacks"); sps = listPacks("shaderpacks"); listedAt = L.time; }
  {
    Opt o(L, "Resource pack", "Tiles, items and sound profiles. Put packs (folders or .zip) in resourcepacks/.", 240);
    std::string cur = s.resourcePack == "auto" ? "Automatic (GeruDoku when installed)" : s.resourcePack == "none" || s.resourcePack.empty() ? "None (built-in Doku tiles)" : s.resourcePack;
    if (ImGui::BeginCombo("##rp", cur.c_str())) {
      if (ImGui::Selectable("Automatic (GeruDoku when installed)", s.resourcePack == "auto")) { s.resourcePack = "auto"; changed = true; }
      if (ImGui::Selectable("None (built-in Doku tiles)", s.resourcePack == "none" || s.resourcePack.empty())) { s.resourcePack = "none"; changed = true; }
      for (auto& p : rps) if (ImGui::Selectable(p.c_str(), s.resourcePack == p)) { s.resourcePack = p; changed = true; }
      ImGui::EndCombo();
    }
  }
  {
    Opt o(L, "Shader pack", "Replaces the game's shaders (OptiFine style). Folders in shaderpacks/.", 240);
    if (ImGui::BeginCombo("##sp", s.shaderPack.empty() ? "None" : s.shaderPack.c_str())) {
      if (ImGui::Selectable("None", s.shaderPack.empty())) { s.shaderPack.clear(); changed = true; }
      for (auto& p : sps) if (ImGui::Selectable(p.c_str(), s.shaderPack == p)) { s.shaderPack = p; changed = true; }
      ImGui::EndCombo();
    }
  }
  if (mcButton(L, "Resource packs folder", ImVec2(0, 32), BtnKind::Grey, true, L.fUi)) openPath(userSub("resourcepacks"));
  ImGui::SameLine();
  if (mcButton(L, "Shader packs folder", ImVec2(0, 32), BtnKind::Grey, true, L.fUi)) openPath(userSub("shaderpacks"));
  if (locked) ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, 8));

  heading(L, "Launcher");
  {
    Opt o(L, "Keep the launcher open", "Come back here when you quit the game", 40);
    bool k = L.keepOpen();
    if (ImGui::Checkbox("##keep", &k)) { s.extra["launcherKeepOpen"] = k; changed = true; }
  }
  ImGui::Dummy(ImVec2(0, 4));
  heading(L, "Updates");
  {
    std::string v = L.updater.currentVersion();
    Opt o(L, ("Game version " + niceVersion(v)).c_str(), ("Launcher " + niceVersion(DQ_VERSION) + ". New versions come from GitHub Releases; your worlds stay as they are.").c_str(), 200);
    if (mcButton(L, L.updater.busy() ? "Checking…" : "Check for updates", ImVec2(200, 34), BtnKind::Grey, !L.updater.busy(), L.fUi)) L.updater.check(false);
  }
  if (L.updater.busy() && (L.updater.state == Updater::State::Downloading || L.updater.state == Updater::State::Extracting)) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(79, 138, 47, 255));
    ImGui::ProgressBar(L.updater.progress, ImVec2(-1, 0));
    ImGui::PopStyleColor();
  }
  if (!L.updater.message.empty()) {
    ImGui::PushFont(L.fSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, L.updater.state == Updater::State::Failed ? C_BAD : C_DIM);
    ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(L.updater.message.c_str()); ImGui::PopTextWrapPos();
    ImGui::PopStyleColor(); ImGui::PopFont();
  }
  {
    Opt o(L, "Check when the launcher starts", "New game versions install by themselves", 40);
    if (ImGui::Checkbox("##auto", &s.autoUpdate)) changed = true;
  }
  {
    Opt o(L, "Updates from GitHub", "The repository with DokuQuest releases (owner/name)", 260);
    static std::string src; static bool init = false;
    if (!init) { src = L.updater.source; init = true; }
    char buf[160]; std::snprintf(buf, sizeof buf, "%s", src.c_str());
    if (ImGui::InputText("##src", buf, sizeof buf)) src = buf;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      std::string t = src;
      for (const char* pre : {"https://github.com/", "http://github.com/", "github.com/"}) if (startsWith(toLower(t), pre)) t = t.substr(std::strlen(pre));
      if (endsWith(t, ".git")) t.resize(t.size() - 4);
      while (!t.empty() && t.back() == '/') t.pop_back();
      if (t.empty()) t = "FartGarfunkel/DokuQuest-Rewrite";
      if (validRepoSource(t)) { L.updater.source = src = t; s.extra["updateSource"] = t; changed = true; }
      else { L.updater.message = "Write the repository as owner/name, like FartGarfunkel/DokuQuest-Rewrite."; L.updater.state = Updater::State::Failed; }
    }
  }
  endDokuPanel();
  if (changed) L.saveSettings();
}

} // namespace dq
