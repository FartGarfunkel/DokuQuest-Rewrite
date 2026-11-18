// ui/guide_screen.cpp — the crafting guide (a guidebook): a book of the things you can make.
// Its index lists, by kind, what you could make from the things you've had (all a recipe takes, or the thing itself:
// discovery, stats.known, sim/rpg.h knowsItem); a thing's
// page shows every way it's made (the crafting grid drawn out, the furnace, the stonecutter, the Tool Forge, an alloy's
// melt) and on the facing page what it's used in. Click any picture to go to its page, Back (B / Esc) to return.
// Controller: the d-pad walks the pictures on the open pages, A opens one, X / Y turn the pages, B goes back.
#include "ui/screen.h"
#include "ui/ui.h"
#include "ui/book.h"
#include "ui/book_ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "core/registry.h"
#include "core/metals.h"
#include "sim/crafting.h"
#include "sim/stations.h"
#include "sim/player.h"
#include "sim/rpg.h"
#include "sim/machines.h"
#include "sim/worktable.h"
#include "sim/inv_ops.h"
#include "core/registry.h"
#include "ui/book_ui.h"
#include <misc/cpp/imgui_stdlib.h>
#include <imgui.h>
#include <algorithm>
#include <map>
#include <set>

namespace dq {
std::unique_ptr<Screen> makeGuideScreen(const std::string& item = "");

namespace {
bool ingMatches(const std::string& ing, const std::string& id) {
  if (ing == id) return true;
  if (!ing.empty() && ing[0] == '#') { auto it = reg().tags.find(ing); return it != reg().tags.end() && std::find(it->second.begin(), it->second.end(), id) != it->second.end(); }
  return false;
}
// the reverse of the stonecutter's and the Tool Forge's lists: what's cut / forged into each thing (made once)
const std::map<std::string, std::vector<std::pair<std::string, int>>>& cutFrom() {
  static std::map<std::string, std::vector<std::pair<std::string, int>>> m = [] {
    std::map<std::string, std::vector<std::pair<std::string, int>>> o;
    for (auto& d : reg().items) {
      for (auto& [out, n] : stonecutterOptions(d.key)) o[out].push_back({d.key, n});
    }
    return o;
  }();
  return m;
}

class GuideScreen : public Screen {
public:
  explicit GuideScreen(std::string item) { if (!item.empty()) { hist.push_back(item); goPage = true; } }
  const char* id() const override { return "guide"; }
  bool pausesGame() const override { return false; }
  bool onBack(App&) override { if (!hist.empty()) { hist.pop_back(); goPage = true; return true; } return false; }
  void update(App& a, double) override {
    frames++;
    if (!a.session || !a.session->player || a.session->player->dead) { a.popScreen(); return; }
    if (frames > 1 && a.input.pressed(Act::Inventory)) a.popScreen();
  }

  void draw(App& a) override {
    Player* p = a.session && a.session->player ? a.session->player : nullptr;
    if (!p) return;
    const float z = ui::Z();
    book.keysTurn = false; book.keysTab = false;   // (the d-pad walks the pictures; X / Y turn the pages)
    Input& in = a.input;
    if (in.pressed(Act::MenuX)) book.spread = std::max(0, book.spread - 1);
    if (in.pressed(Act::MenuY)) book.spread++;
    const std::string cur = hist.empty() ? std::string() : hist.back();
    book.begin(a, "##guide", {{"Index", "crafting_table"}, {"The page", cur.empty() ? std::string("book") : cur}});
    // ---- what you could make: the recipes that use something you've had (or make something you've had)
    auto known = [&](const std::string& k) {
      if (k.empty()) return false;
      if (k[0] == '#') { auto it = reg().tags.find(k); if (it != reg().tags.end()) for (auto& m : it->second) if (knowsItem(*p, m)) return true; return false; }
      return knowsItem(*p, k);
    };
    std::map<std::string, std::vector<std::string>> byCat;   // category -> outputs
    std::set<std::string> seen; int hidden = 0;
    const bool imm = a.session->world().immersive;
    for (const Recipe& r : reg().recipes) {
      if (r.props.is_object() && r.props.value(imm ? "classicOnly" : "immersiveOnly", false)) continue;
      if (!reg().item(r.out)) continue;
      // (found: you've had all it takes, or the thing itself: a log shows you planks, planks then the table and sticks...)
      bool vis = knowsItem(*p, r.out);
      if (!vis) { vis = true; for (auto& [k, n] : recipeIngredients(r)) if (!known(k)) { vis = false; break; } }
      if (!vis) { if (!seen.count(r.out)) hidden++; continue; }
      if (seen.insert(r.out).second) byCat[recipeCategory(r)].push_back(r.out);
    }
    for (auto& [in2, out] : reg().smelting) if (reg().item(out) && (knowsItem(*p, in2) || knowsItem(*p, out)) && seen.insert(out).second) byCat["smelting"].push_back(out);
    for (const char* mk : {"millstone", "sawmill", "crusher"}) if (const json* T = machineTable(mk)) for (auto& [in2, o] : T->items())   // (the machines' work)
      if (o.is_array() && !o.empty() && o[0].is_string() && reg().item(o[0].get<std::string>()) && (knowsItem(*p, in2) || knowsItem(*p, o[0].get<std::string>())) && seen.insert(o[0].get<std::string>()).second) byCat["machines"].push_back(o[0].get<std::string>());
    // the search: only what's named like it (the index's pictures; a thing's page stays as it is)
    if (!query.empty()) {
      std::string q = query; for (char& c : q) c = (char)std::tolower((unsigned char)c);
      for (auto& [k, v] : byCat) v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& id) { std::string n = reg().item(id)->name; for (char& c : n) c = (char)std::tolower((unsigned char)c); return n.find(q) == std::string::npos; }), v.end());
    }
    clicks.clear();
    // ---- the index
    ui::BookRows idx;
    book.heading(idx, "Crafting Guide");
    book.para(idx, std::to_string(seen.size()) + " things you could make" + (hidden ? " \xC2\xB7 more to find as you gather" : ""), ui::ink::muted, ui::F_SMALL);
    static const std::pair<const char*, const char*> CATS[] = {{"blocks", "Blocks"}, {"tools", "Tools and weapons"}, {"armor", "Armour"}, {"food", "Food"}, {"smelting", "From the furnace"}, {"machines", "From the mill, the saw and the crusher"}, {"redstone", "Redstone"}, {"misc", "Other things"}};
    const float ic = std::round(ui::textSize(ui::F_BODY, "Ag").y * 1.9f), gap = 3 * z;
    for (auto& [ck, cn] : CATS) {
      auto it = byCat.find(ck);
      if (it == byCat.end() || it->second.empty()) continue;
      std::vector<std::string> v = it->second;
      std::sort(v.begin(), v.end(), [](const std::string& x, const std::string& y) { return reg().item(x)->name < reg().item(y)->name; });
      book.gap(idx, 4 * z);
      book.sub(idx, cn);
      iconRows(idx, v, ic, gap, *p);
    }
    if (seen.empty()) book.para(idx, "Gather something: wood, stone, anything. What it makes will be written here.", ui::ink::muted);
    // ---- the page of the thing chosen
    ui::BookRows pg;
    if (cur.empty()) { book.heading(pg, "The page"); book.para(pg, "Choose something in the index.", ui::ink::muted); }
    else itemPage(pg, cur, *p, ic, gap, known);
    book.draw({idx, pg});
    if (goPage) { goPage = false; book.goTab(hist.empty() ? 0 : 1); }
    // the controller: the d-pad goes to the nearest picture that way, A opens it
    if (!clicks.empty()) {
      const ivec2 nav = in.navStep();
      if (padCur < 0 || padCur >= (int)clicks.size()) padCur = 0;
      if (nav != ivec2(0)) {
        padOn = true;
        const ImVec2 c0 = clicks[padCur].c;
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < (int)clicks.size(); i++) {
          if (i == padCur) continue;
          const float dx = clicks[i].c.x - c0.x, dy = clicks[i].c.y - c0.y;
          if ((nav.x > 0 && dx <= 2) || (nav.x < 0 && dx >= -2) || (nav.y > 0 && dy <= 2) || (nav.y < 0 && dy >= -2)) continue;
          const float d = nav.x ? std::fabs(dx) + std::fabs(dy) * 3 : std::fabs(dy) + std::fabs(dx) * 3;
          if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) padCur = best;
      }
      if (padOn && in.pressed(Act::MenuOk)) open(clicks[padCur].item);
    }
    if (std::hypot(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y) > 2) padOn = false;
    {   // the search, over the book (typing there: the book's keys wait)
      const float z2 = ui::Z();
      ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, 38 * z2), ImGuiCond_Always, ImVec2(0.5f, 0));   // (under the tab strip, which covered it)
      ImGui::SetNextWindowBgAlpha(0.8f);
      ImGui::Begin("##guide_search", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
      ImGui::SetNextItemWidth(260 * z2);
      if (ImGui::InputTextWithHint("##q", "Search the guide", &query)) book.goTab(0);
      book.active = !ImGui::IsItemActive();
      ImGui::End();
    }
    book.hint("Click a picture: its page \xC2\xB7 B / Esc: back \xC2\xB7 X / Y or the arrows: turn the pages \xC2\xB7 D-pad + A on a controller");
    book.end();
  }

private:
  struct Click { ImVec2 c; std::string item; };
  ui::Book book;
  std::vector<std::string> hist;
  std::vector<Click> clicks;
  int frames = 0, padCur = -1; bool padOn = false, goPage = false;
  std::map<std::string, int> tipFrame;
  std::string query;

  void layOut(int ridx, bool small) {
    App& a = app();
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    // the nearest crafting table within reach
    ivec3 best(0); float bd = 1e9f; bool found = false;
    const ivec3 c(ifloor(p.pos.x), ifloor(p.pos.y), ifloor(p.pos.z));
    for (int dy = -2; dy <= 2; dy++) for (int dz = -5; dz <= 5; dz++) for (int dx = -5; dx <= 5; dx++) {
      const ivec3 b = c + ivec3(dx, dy, dz);
      if (!reg().block(w.getBlock(b.x, b.y, b.z)).has("table")) continue;
      const float d = glm::length(vec3(b) + vec3(0.5f) - p.pos);
      if (d < bd && d <= 5.f) { bd = d; best = b; found = true; }
    }
    if (found) {
      if (!worktableLayout(*s->level, p, w, best, ridx).empty()) return;
      const ivec3 at = best; const std::string name = p.name;
      a.popScreen();
      uiDefer([at, name](App& a2) { a2.events.emit("screenOpen", json::array({name, "table", json{{"pos", {at.x, at.y, at.z}}}})); });
      return;
    }
    if (!small) { a.toast("Lay it out on a crafting table: stand by one", "warn"); return; }
    a.popScreen();
    uiDefer([ridx](App& a2) {   // (your own 2 x 2: the inventory opened with the recipe set out in it)
      InvOpen o; o.mode = "inventory";
      a2.pushScreen(makeInventoryScreen(o));
      inventoryBookPick(a2, ridx);
    });
  }
  void open(const std::string& item) {
    if (item.rfind("#lay:", 0) == 0) { layOut(std::atoi(item.c_str() + 5), item.size() > 2 && item.compare(item.size() - 2, 2, ":s") == 0); return; }
    if (item.empty() || !reg().item(item)) return; if (hist.empty() || hist.back() != item) hist.push_back(item); goPage = true; ui::playClick(); }
  // one picture that takes a click (its page) and shows its name
  void pic(ImDrawList* dl, ImVec2 p, float s, const std::string& item, float alpha = 1, const std::string& label = "") {
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const bool hot = mp.x >= p.x && mp.x < p.x + s && mp.y >= p.y && mp.y < p.y + s;
    const int me = (int)clicks.size();
    clicks.push_back({ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), item});
    const bool padHere = padOn && padCur == me;
    if (hot || padHere) dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), ui::ink::hilite, 3 * ui::Z());
    if (!item.empty()) ui::drawIcon(dl, item, ImVec2(p.x + s * 0.08f, p.y + s * 0.08f), s * 0.84f, alpha);
    if ((hot || padHere) && !item.empty()) {
      const ItemDef* d = reg().item(item);
      const std::string n = label.empty() ? (d ? d->name : item) : label;
      if (!bookUiScreenTip(n)) ui::setTooltip("%s", n.c_str());
      if (hot && ImGui::IsMouseClicked(0)) open(item);
    }
  }
  void iconRows(ui::BookRows& R, const std::vector<std::string>& items, float ic, float gap, Player&) {
    const int per = std::max(1, (int)((book.pageW() + gap) / (ic + gap)));
    for (size_t i = 0; i < items.size(); i += per) {
      std::vector<std::string> row(items.begin() + i, items.begin() + std::min(items.size(), i + per));
      ui::BookRow r; r.h = ic + gap;
      r.draw = [this, row, ic, gap](ImDrawList* dl, ImVec2 q, float) { for (size_t k = 0; k < row.size(); k++) pic(dl, ImVec2(q.x + k * (ic + gap), q.y), ic, row[k]); };
      R.push_back(std::move(r));
    }
  }
  // a crafting recipe drawn out: its grid, the arrow, what it makes
  void gridRow(ui::BookRows& R, const Recipe& r, float ic, const std::function<bool(const std::string&)>& known) {
    int w = 3, h = 3; recipeDims(r, w, h);
    std::vector<std::string> cells(9);
    if (r.shaped()) {
      for (int y = 0; y < (int)r.shape.size() && y < 3; y++) for (int x = 0; x < (int)r.shape[y].size() && x < 3; x++) {
        const char c = r.shape[y][x]; auto it = r.key.find(c); if (c != ' ' && it != r.key.end()) cells[y * 3 + x] = it->second;
      }
    } else for (size_t i = 0; i < r.items.size() && i < 9; i++) cells[i] = r.items[i];
    const float cs = ic * 0.9f, z = ui::Z();
    ui::BookRow row; row.h = cs * 3 + 8 * z;
    const std::string out = r.out; const int n = r.n; const bool shapeless = !r.shaped();
    const int ridx = (int)(&r - &reg().recipes[0]);
    int rw = 3, rh = 3; recipeDims(r, rw, rh); const bool small = rw <= 2 && rh <= 2;
    row.draw = [this, cells, cs, out, n, shapeless, known, z, ridx, small](ImDrawList* dl, ImVec2 q, float) {
      for (int k = 0; k < 9; k++) {
        const ImVec2 c(q.x + (k % 3) * cs, q.y + (k / 3) * cs);
        dl->AddRect(c, ImVec2(c.x + cs, c.y + cs), ui::ink::faint, 0, 0, std::max(1.f, z));
        if (cells[k].empty()) continue;
        const std::string icon = ingredientIcon(cells[k]);
        const bool kn = known(cells[k]);
        pic(dl, c, cs, icon, kn ? 1.f : 0.35f, kn ? ingredientLabel(cells[k]) : "?");
      }
      const float ax = q.x + cs * 3 + 8 * z, ay = q.y + cs * 1.5f;
      dl->AddTriangleFilled(ImVec2(ax, ay - 6 * z), ImVec2(ax + 12 * z, ay), ImVec2(ax, ay + 6 * z), ui::ink::muted);
      pic(dl, ImVec2(ax + 18 * z, q.y + cs), cs * 1.1f, out);
      if (n > 1) ui::text(dl, ui::F_SMALL, ImVec2(ax + 18 * z + cs * 1.1f, q.y + cs * 1.6f), ui::ink::text, ("\xC3\x97" + std::to_string(n)).c_str());
      if (shapeless) ui::text(dl, ui::F_SMALL, ImVec2(ax, q.y + cs * 2.4f), ui::ink::muted, "any order");
      {   // "Lay it out": onto the crafting table you're by, piece by piece from your bag (or your own 2 x 2 for a small one)
        const char* t = "Lay it out";
        const ImVec2 ts = ui::textSize(ui::F_SMALL, t), b0(ax + 18 * z + cs * 1.4f + 10 * z, q.y + cs * 1.05f), b1(b0.x + ts.x + 12 * z, b0.y + ts.y + 6 * z);
        const ImVec2 mp = ImGui::GetIO().MousePos; const bool hot = mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y;
        const int me = (int)clicks.size(); clicks.push_back({ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f), "#lay:" + std::to_string(ridx) + (small ? ":s" : "")});
        const bool padHere = padOn && padCur == me;
        dl->AddRect(b0, b1, hot || padHere ? ui::ink::head : ui::ink::muted, 3 * z, 0, 1.5f * z);
        ui::text(dl, ui::F_SMALL, ImVec2(b0.x + 6 * z, b0.y + 3 * z), hot || padHere ? ui::ink::head : ui::ink::text, t);
        if (hot && ImGui::IsMouseClicked(0)) layOut(ridx, small);
      }
    };
    R.push_back(std::move(row));
  }
  void itemPage(ui::BookRows& R, const std::string& id, Player& p, float ic, float gap, const std::function<bool(const std::string&)>& known) {
    const ItemDef* d = reg().item(id);
    const float z = ui::Z();
    book.heading(R, d ? d->name : id);
    { ui::BookRow big; big.h = ic * 1.6f + 4 * z; big.draw = [this, id, ic](ImDrawList* dl, ImVec2 q, float) { pic(dl, q, ic * 1.6f, id); }; R.push_back(std::move(big)); }
    if (d) { const std::string ds = d->str("desc"); if (!ds.empty()) book.para(R, ds, ui::ink::muted, ui::F_SMALL); }
    book.gap(R, 4 * z);
    book.sub(R, "How it's made");
    int ways = 0;
    const bool imm = a_immersive();
    for (const Recipe& r : reg().recipes) {
      if (r.out != id) continue;
      if (r.props.is_object() && r.props.value(imm ? "classicOnly" : "immersiveOnly", false)) continue;
      ways++;
      gridRow(R, r, ic, known);
    }
    for (auto& [in2, out] : reg().smelting) if (out == id) {
      ways++;
      ui::BookRow row; row.h = ic + 6 * z;
      const std::string src = in2;
      row.draw = [this, src, ic, z](ImDrawList* dl, ImVec2 q, float) {
        pic(dl, q, ic, src);
        ui::text(dl, ui::F_SMALL, ImVec2(q.x + ic + 6 * z, q.y + ic * 0.3f), ui::ink::text, "smelted in a furnace");
      };
      R.push_back(std::move(row));
    }
    if (auto it = cutFrom().find(id); it != cutFrom().end()) for (auto& [src, n] : it->second) {
      ways++;
      ui::BookRow row; row.h = ic + 6 * z;
      row.draw = [this, src = src, n = n, ic, z](ImDrawList* dl, ImVec2 q, float) {
        pic(dl, q, ic, src);
        ui::text(dl, ui::F_SMALL, ImVec2(q.x + ic + 6 * z, q.y + ic * 0.3f), ui::ink::text, ("cut on a stonecutter" + (n > 1 ? " (" + std::to_string(n) + " from one)" : std::string())).c_str());
      };
      R.push_back(std::move(row));
    }
    if (d && d->props.contains("forgeHead") && d->props["forgeHead"].is_object()) {
      ways++;
      const std::string mat = d->props["forgeHead"].value("mat", std::string());
      const int cost = d->props["forgeHead"].value("cost", 1);
      ui::BookRow row; row.h = ic + 6 * z;
      row.draw = [this, mat, cost, ic, z](ImDrawList* dl, ImVec2 q, float) {
        pic(dl, q, ic, mat);
        ui::text(dl, ui::F_SMALL, ImVec2(q.x + ic + 6 * z, q.y + ic * 0.3f), ui::ink::text, ("hammered out at the Tool Forge (" + std::to_string(cost) + " of it)").c_str());
      };
      R.push_back(std::move(row));
    }
    for (const Metal* m : namedAlloys()) if (m && m->ingot == id && !m->ranges.empty()) {
      ways++;
      std::string t = "Melted together in a crucible: ";
      bool first = true;
      for (auto& [k, rg] : m->ranges) { const Metal* md = metalDef(k); t += (first ? "" : ", ") + std::to_string((int)std::round(rg.first * 100)) + "-" + std::to_string((int)std::round(rg.second * 100)) + "% " + (md ? md->name : k); first = false; }
      book.para(R, t, ui::ink::text, ui::F_SMALL);
    }
    for (const char* mk : {"millstone", "sawmill", "crusher"}) if (const json* T = machineTable(mk)) for (auto& [src, o] : T->items())
      if (o.is_array() && !o.empty() && o[0].is_string() && o[0].get<std::string>() == id && reg().item(src)) {
        ways++;
        const int n = o.size() > 1 && o[1].is_number() ? o[1].get<int>() : 1;
        const std::string what = std::string("put through a ") + mk + (n > 1 ? " (" + std::to_string(n) + " from one)" : std::string());
        ui::BookRow row; row.h = ic + 6 * z;
        row.draw = [this, src = src, what, ic, z](ImDrawList* dl, ImVec2 q, float) { pic(dl, q, ic, src); ui::text(dl, ui::F_SMALL, ImVec2(q.x + ic + 6 * z, q.y + ic * 0.3f), ui::ink::text, what.c_str()); };
        R.push_back(std::move(row));
      }
    if (!ways) book.para(R, "Not made: found, mined or gathered.", ui::ink::muted, ui::F_SMALL);
    // ---- the facing page: what it goes into
    ui::BookRow brk; brk.breakBefore = true; brk.h = 1; brk.draw = [](ImDrawList*, ImVec2, float) {}; R.push_back(brk);
    book.sub(R, "What it's used for");
    std::vector<std::string> into; std::set<std::string> dup;
    int hiddenUses = 0;
    for (const Recipe& r : reg().recipes) {
      if (r.props.is_object() && r.props.value(imm ? "classicOnly" : "immersiveOnly", false)) continue;
      bool uses = false; for (auto& [k, n] : recipeIngredients(r)) if (ingMatches(k, id)) { uses = true; break; }
      if (!uses || !reg().item(r.out) || !dup.insert(r.out).second) continue;
      into.push_back(r.out);
    }
    (void)p; (void)hiddenUses;
    if (into.empty()) book.para(R, "Nothing at the crafting table.", ui::ink::muted, ui::F_SMALL);
    else iconRows(R, into, ic, gap, p);
    if (auto it = reg().smelting.find(id); it != reg().smelting.end()) {
      book.gap(R, 4 * z);
      ui::BookRow row; row.h = ic + 6 * z;
      const std::string out = it->second;
      row.draw = [this, out, ic, z](ImDrawList* dl, ImVec2 q, float) {
        ui::text(dl, ui::F_SMALL, ImVec2(q.x, q.y + ic * 0.3f), ui::ink::text, "In a furnace it becomes");
        pic(dl, ImVec2(q.x + ui::textSize(ui::F_SMALL, "In a furnace it becomes").x + 6 * z, q.y), ic, out);
      };
      R.push_back(std::move(row));
    }
    const auto cuts = stonecutterOptions(id);
    if (!cuts.empty()) {
      book.gap(R, 4 * z); book.para(R, "On a stonecutter it's cut into:", ui::ink::text, ui::F_SMALL);
      std::vector<std::string> v; for (auto& [k, n] : cuts) v.push_back(k);
      iconRows(R, v, ic, gap, p);
    }
    const auto heads = forgeOptions(id);
    if (!heads.empty()) {
      book.gap(R, 4 * z); book.para(R, "At the Tool Forge it's hammered into:", ui::ink::text, ui::F_SMALL);
      std::vector<std::string> v; for (auto& [k, n] : heads) v.push_back(k);
      iconRows(R, v, ic, gap, p);
    }
    if (hist.size() > 1) {
      book.gap(R, 8 * z);
      const std::string back = hist[hist.size() - 2];
      ui::BookRow row; row.h = ic * 0.8f;
      row.draw = [this, back, ic, z](ImDrawList* dl, ImVec2 q, float) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const std::string t = "< Back to " + (reg().item(back) ? reg().item(back)->name : back);
        const ImVec2 ts = ui::textSize(ui::F_SMALL, t.c_str());
        const bool hot = mp.x >= q.x && mp.x < q.x + ts.x && mp.y >= q.y && mp.y < q.y + ts.y + 4 * z;
        ui::text(dl, ui::F_SMALL, q, hot ? ui::ink::head : ui::ink::key, t.c_str());
        if (hot && ImGui::IsMouseClicked(0)) { hist.pop_back(); goPage = true; }
      };
      R.push_back(std::move(row));
    }
  }
  bool a_immersive() const { return app().session && app().session->world().immersive; }
};
}  // namespace

std::unique_ptr<Screen> makeGuideScreen(const std::string& item) { return std::make_unique<GuideScreen>(item); }

}  // namespace dq
