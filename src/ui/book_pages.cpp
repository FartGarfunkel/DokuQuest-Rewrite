// ui/book_pages.cpp — see book_pages.h
#include "ui/book_pages.h"
#include "ui/theme.h"
#include "assets/lore.h"
#include "core/registry.h"
#include <SDL.h>

namespace dq {

bool bookTextOf(const ItemStack& s, BookText& out) {
  out = BookText{};
  const ItemDef* d = s.empty() ? nullptr : s.def();
  if (!d) return false;
  if (d->has("lore")) {
    const LoreBook* lb = loreBook(d->str("lore"));
    if (!lb) return false;
    out.title = lb->title; out.author = "an unknown hand"; out.pages = lb->pages;
    return true;
  }
  if (s.id == "written_book" || s.id == "writable_book") {
    if (s.tag.is_object() && s.tag.contains("pages") && s.tag["pages"].is_array())
      for (auto& p : s.tag["pages"]) out.pages.push_back(p.is_string() ? p.get<std::string>() : std::string());
    if (s.id == "writable_book") { out.writable = true; out.titlePage = false; if (out.pages.empty()) out.pages.push_back(""); }
    else { out.title = s.tag.is_object() ? s.tag.value("title", std::string("Untitled")) : "Untitled"; out.author = s.tag.is_object() ? s.tag.value("author", std::string()) : ""; if (out.pages.empty()) out.pages.push_back(""); }
    return true;
  }
  return false;
}

bool bookTextOfLectern(const json& be, BookText& out) {
  if (!be.is_object() || !be.value("book", false)) return false;
  if (be.contains("lore") && be["lore"].is_string()) {
    const LoreBook* lb = loreBook(be["lore"].get<std::string>());
    if (!lb) return false;
    out = BookText{}; out.title = lb->title; out.author = "an unknown hand"; out.pages = lb->pages;
    return true;
  }
  if (be.contains("stack")) return bookTextOf(ItemStack::fromJson(be["stack"]), out);
  return false;
}

int bookPageCount(const BookText& b) { return (int)b.pages.size() + (b.titlePage ? 1 : 0); }
int bookSpreadCount(const BookText& b) { return std::max(1, (bookPageCount(b) + 1) / 2); }

CanvasPic paintBookSpread(const std::string& key, const BookText& b, int spread, int caretPage) {
  const int n = bookPageCount(b);
  // what's on these two pages (and the caret's blink) decides the version
  std::string sig = b.title + "\x1f" + b.author + "\x1f" + std::to_string(spread) + "\x1f" + std::to_string(caretPage);
  for (int k = 0; k < 2; k++) {
    const int pi = spread * 2 + k - (b.titlePage ? 1 : 0);
    if (pi >= 0 && pi < (int)b.pages.size()) sig += "\x1e" + b.pages[pi];
  }
  const bool blink = caretPage >= 0 && (SDL_GetTicks() / 500) % 2 == 0;
  const u64 ver = std::hash<std::string>()(sig) ^ (blink ? 0x9e3779b97f4a7c15ull : 0);
  return canvasPaint(key, ver, 1024, 640, [&](ImDrawList* dl, ImVec2 S) {
    const float hw = S.x * 0.5f;
    for (int k = 0; k < 2; k++) {   // the paper, a little darker toward the fold
      const float x0 = k ? hw : 0, x1 = k ? S.x : hw;
      dl->AddRectFilled(ImVec2(x0, 0), ImVec2(x1, S.y), IM_COL32(236, 224, 196, 255));
      const float f0 = k ? x0 : x1 - 46, f1 = k ? x0 + 46 : x1;
      dl->AddRectFilledMultiColor(ImVec2(f0, 0), ImVec2(f1, S.y), k ? IM_COL32(150, 125, 90, 140) : IM_COL32(0, 0, 0, 0), k ? IM_COL32(0, 0, 0, 0) : IM_COL32(150, 125, 90, 140),
                                  k ? IM_COL32(0, 0, 0, 0) : IM_COL32(150, 125, 90, 140), k ? IM_COL32(150, 125, 90, 140) : IM_COL32(0, 0, 0, 0));
    }
    const ImU32 ink = IM_COL32(52, 36, 20, 255), faint = IM_COL32(120, 96, 64, 255);
    ImFont* body = ui::font(ui::F_BODY); ImFont* head = ui::font(ui::F_H3);
    for (int k = 0; k < 2; k++) {
      const int di = spread * 2 + k;   // the page shown
      if (di >= n) continue;
      const float x0 = (k ? hw : 0) + 58, w = hw - 116;
      if (b.titlePage && di == 0) {   // the title page
        const float ts = 54;
        const ImVec2 sz = head->CalcTextSizeA(ts, 1e9f, w, b.title.c_str());
        dl->AddText(head, ts, ImVec2(x0 + (w - std::min(w, sz.x)) * 0.5f, S.y * 0.3f), ink, b.title.c_str(), nullptr, w);
        const float ly = S.y * 0.3f + sz.y + 26;
        dl->AddLine(ImVec2(x0 + w * 0.25f, ly), ImVec2(x0 + w * 0.75f, ly), faint, 2.5f);
        if (!b.author.empty()) {
          const std::string by = "by " + b.author;
          const ImVec2 as = body->CalcTextSizeA(32, 1e9f, 0, by.c_str());
          dl->AddText(body, 32, ImVec2(x0 + (w - as.x) * 0.5f, ly + 22), faint, by.c_str());
        }
        continue;
      }
      const int pi = di - (b.titlePage ? 1 : 0);
      const std::string& t = pi < (int)b.pages.size() ? b.pages[pi] : std::string();
      dl->AddText(body, 31, ImVec2(x0, 56), ink, t.c_str(), nullptr, w);
      if (caretPage == di && blink) {   // the pen's place: after the last word
        const ImVec2 ts = body->CalcTextSizeA(31, 1e9f, w, t.c_str());
        // (the last line's end: measure the text after the last wrap)
        float lastW = 0; size_t start = 0;
        const char* s0 = t.c_str(); const char* e = s0 + t.size(); const char* p = s0;
        while (p < e) { const char* nx = body->CalcWordWrapPositionA(31 / body->FontSize, p, e, w); if (nx == p) nx++; start = (size_t)(p - s0); p = nx; while (p < e && (*p == ' ')) p++; if (p < e && *p == '\n') p++; }
        lastW = body->CalcTextSizeA(31, 1e9f, 0, t.c_str() + start, e).x;
        const bool nl = !t.empty() && t.back() == '\n';
        const float cy = 56 + (t.empty() ? 0 : ts.y - 31) + (nl ? 31 : 0), cx = x0 + (nl ? 0 : lastW) + 2;
        dl->AddRectFilled(ImVec2(cx, cy + 2), ImVec2(cx + 3, cy + 31), ink);
      }
      const std::string num = std::to_string(di + 1);
      const ImVec2 ns = body->CalcTextSizeA(26, 1e9f, 0, num.c_str());
      dl->AddText(body, 26, ImVec2(k ? S.x - 58 - ns.x : 58, S.y - 54), faint, num.c_str());
    }
  });
}

} // namespace dq
