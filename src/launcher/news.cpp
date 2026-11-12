// launcher/news.cpp — the news panel: patch notes from assets/news/*.dqn (news.dqn = the C++ versions,
// alpha.dqn = the HTML/JS alphas converted from launcher/news.html). Format:
//   ## Title | Date      an entry          - text   a bullet          text   a paragraph
//   inline **bold**, *italic*, `code`;  lines starting with "# " are comments.
// Patch notes never mention Herobrine and every entry ends with "Removed Herobrine." — enforced here too,
// so notes from any version follow the rule.
#include "launcher/launcher.h"
#include <imgui_internal.h>

namespace dq {

namespace {
enum : u8 { S_PLAIN = 0, S_BOLD = 1, S_ITALIC = 2, S_CODE = 4 };
struct Span { std::string text; u8 style = 0; };
struct Line { bool bullet = false; std::vector<Span> spans; };
struct Entry { std::string title, date; std::vector<Line> lines; float height = -1, heightW = 0; };
std::vector<Entry> g_news;
std::string g_newsFrom;

std::vector<Span> parseInline(const std::string& s) {
  std::vector<Span> out; u8 st = 0; std::string cur;
  auto flush = [&] { if (!cur.empty()) { out.push_back({cur, st}); cur.clear(); } };
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '`') { flush(); st ^= S_CODE; continue; }
    if (!(st & S_CODE) && s[i] == '*') {
      flush();
      if (i + 1 < s.size() && s[i + 1] == '*') { st ^= S_BOLD; i++; } else st ^= S_ITALIC;
      continue;
    }
    cur += s[i];
  }
  flush();
  return out;
}

bool mentionsHim(const std::string& s) { return toLower(s).find("herobrine") != std::string::npos; }

void parseFile(const std::string& text) {
  Entry* e = nullptr;
  for (auto raw : splitStr(text, '\n')) {
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    if (raw.empty() || startsWith(raw, "# ") || raw == "#") continue;
    if (startsWith(raw, "## ")) {
      g_news.emplace_back(); e = &g_news.back();
      std::string t = raw.substr(3); size_t bar = t.rfind(" | ");
      if (bar != std::string::npos) { e->date = t.substr(bar + 3); t = t.substr(0, bar); }
      e->title = t;
      continue;
    }
    if (!e) continue;
    Line ln; std::string body = raw;
    if (startsWith(raw, "- ")) { ln.bullet = true; body = raw.substr(2); }
    if (mentionsHim(body)) continue;   // the closing line is added below, nothing else may name him
    ln.spans = parseInline(body);
    e->lines.push_back(std::move(ln));
  }
}
}

void loadNews(const fs::path& assetsRoot) {
  g_news.clear();
  fs::path dir = assetsRoot / "news";
  std::vector<fs::path> files; std::error_code ec;
  for (auto& f : fs::directory_iterator(dir, ec)) if (f.path().extension() == ".dqn") files.push_back(f.path());
  std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
    bool na = a.filename() == "news.dqn", nb = b.filename() == "news.dqn";
    return na != nb ? na : a.filename() < b.filename();
  });
  for (auto& f : files) if (auto t = readFile(f)) parseFile(*t);
  for (auto& e : g_news) { Line l; l.bullet = true; l.spans.push_back({"Removed Herobrine.", 0}); e.lines.push_back(l); }
  g_newsFrom = dir.string();
  DQ_LOG("launcher: %zu news entries from %s", g_news.size(), g_newsFrom.c_str());
}

// Lays out (and, when draw is set, draws) one entry at the cursor; returns its height.
static float layoutEntry(Launcher& L, Entry& e, ImVec2 origin, float width, bool draw) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float y = origin.y + 14;
  // h2: gold, 2px shadow #3f2f00, date small and dim
  ImFont* hf = L.fMid;
  if (draw) {
    shadowText(dl, hf, ImVec2(origin.x, y), C_GOLD, IM_COL32(63, 47, 0, 255), 2, e.title.c_str());
    dl->AddText(hf, hf->FontSize, ImVec2(origin.x + 1, y), C_GOLD, e.title.c_str());   // font-weight 600
  }
  float tw = hf->CalcTextSizeA(hf->FontSize, FLT_MAX, 0, e.title.c_str()).x;
  if (!e.date.empty() && draw) dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(origin.x + tw + 10, y + hf->FontSize - L.fSmall->FontSize - 1), C_DIM, e.date.c_str());
  y += hf->FontSize + 8;
  ImFont* f = L.fUi;
  float lh = std::floor(f->FontSize * 1.5f);
  for (auto& ln : e.lines) {
    float x0 = origin.x + (ln.bullet ? 20 : 0), x = x0, right = origin.x + width;
    if (ln.bullet && draw) dl->AddCircleFilled(ImVec2(origin.x + 8, y + lh * 0.5f), 2.5f, IM_COL32(228, 221, 210, 255));
    for (auto& sp : ln.spans) {
      ImU32 col = (sp.style & S_BOLD) ? C_WHITE : (sp.style & S_CODE) ? IM_COL32(255, 222, 170, 255) : (sp.style & S_ITALIC) ? IM_COL32(214, 205, 192, 255)
                : ln.bullet ? IM_COL32(228, 221, 210, 255) : C_INK;
      const char* p = sp.text.c_str(); const char* end = p + sp.text.size();
      while (p < end) {
        const char* w = p; while (w < end && *w != ' ') w++;
        const char* we = w; while (we < end && *we == ' ') we++;   // word + its trailing spaces
        float ww = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, p, w).x, wsp = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, p, we).x;
        if (x + ww > right && x > x0) { x = x0; y += lh; }
        if (draw) {
          ImVec2 pos(x, y + (lh - f->FontSize) * 0.5f);
          dl->AddText(f, f->FontSize, pos, col, p, w);
          if (sp.style & S_BOLD) dl->AddText(f, f->FontSize, ImVec2(pos.x + 1, pos.y), col, p, w);
        }
        x += wsp; p = we;
      }
    }
    y += lh + (ln.bullet ? 1 : 6);
  }
  y += 12;
  return y - origin.y;
}

void drawNews(Launcher& L, ImVec2 size) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  beginDokuPanel("##news", size, false);
  ImGui::PopStyleVar();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
  float hh = L.fMid->FontSize + 22;
  dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + sz.x - 2, p.y + hh), IM_COL32(255, 255, 255, 10));
  dl->AddLine(ImVec2(p.x, p.y + hh), ImVec2(p.x + sz.x, p.y + hh), IM_COL32(0, 0, 0, 255), 2);
  shadowText(dl, L.fMid, ImVec2(p.x + 16, p.y + 11), C_WHITE, IM_COL32(63, 63, 63, 255), 2, "DokuQuest News");
  float tw = L.fMid->CalcTextSizeA(L.fMid->FontSize, FLT_MAX, 0, "DokuQuest News").x;
  dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(p.x + 16 + tw + 12, p.y + 11 + L.fMid->FontSize - L.fSmall->FontSize - 1), C_DIM, "What's new in each version");
  ImGui::SetCursorPos(ImVec2(2, hh + 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 4));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
  ImGui::BeginChild("##newsbody", ImVec2(sz.x - 4, sz.y - hh - 3), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
  ImGui::PopStyleColor(); ImGui::PopStyleVar();
  float width = ImGui::GetContentRegionAvail().x;
  float viewTop = ImGui::GetScrollY(), viewBot = viewTop + ImGui::GetWindowHeight();
  if (g_news.empty()) dimText("No patch notes were found in %s.", g_newsFrom.c_str());
  ImDrawList* bdl = ImGui::GetWindowDrawList();
  for (size_t i = 0; i < g_news.size(); i++) {
    Entry& e = g_news[i];
    ImVec2 org = ImGui::GetCursorScreenPos();
    float localY = ImGui::GetCursorPosY();
    if (e.height < 0 || e.heightW != width) { e.height = layoutEntry(L, e, org, width, false); e.heightW = width; }
    if (localY + e.height >= viewTop && localY <= viewBot) e.height = layoutEntry(L, e, org, width, true);
    ImGui::Dummy(ImVec2(width, e.height));
    if (i + 1 < g_news.size()) {   // 1px dashed rgba(255,255,255,.12)
      float y = org.y + e.height - 1;
      for (float x = org.x; x < org.x + width; x += 6) bdl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 3, org.x + width), y), IM_COL32(255, 255, 255, 31));
    }
  }
  // controllers / keyboard: scroll the notes with the right stick or PageUp/PageDown
  if (ImGui::IsWindowHovered() || ImGui::IsWindowFocused()) {
    float step = ImGui::GetWindowHeight() * 0.8f;
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) ImGui::SetScrollY(ImGui::GetScrollY() + step);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) ImGui::SetScrollY(ImGui::GetScrollY() - step);
  }
  float rs = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue - ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
  if (std::fabs(rs) > 0.15f) ImGui::SetScrollY(ImGui::GetScrollY() + rs * 900 * ImGui::GetIO().DeltaTime);
  ImGui::EndChild();
  endDokuPanel();
}

} // namespace dq
