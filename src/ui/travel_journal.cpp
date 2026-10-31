// ui/travel_journal.cpp — the Travel Journal (Better than Adventure's guidebook): the things you've carried
// (sim/travel_journal.cpp keeps the list in the player's data["found"]), and how each one is made. Controller:
// up/down picks, LB/RB jumps ten, B/Esc or the inventory button closes; the mouse clicks.
#include "ui/content_screens.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "core/registry.h"
#include "sim/player.h"
#include <imgui.h>
#include <algorithm>

namespace dq {

namespace {
class TravelJournalScreen : public Screen {
public:
  const char* id() const override { return "travel_journal"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    Input& in = a.input;
    if (in.pressed(Act::Inventory) || in.pressed(Act::MenuBack)) { a.popScreen(); return; }
    int n = (int)keys.size();
    if (!n) return;
    int old = sel;
    if (in.pressed(Act::MenuDown)) sel++;
    if (in.pressed(Act::MenuUp)) sel--;
    if (in.pressed(Act::MenuTabRight) || in.pressed(Act::MenuRight)) sel += 10;
    if (in.pressed(Act::MenuTabLeft) || in.pressed(Act::MenuLeft)) sel -= 10;
    sel = clampv(sel, 0, n - 1);
    if (sel != old) follow = true;
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    const Registry& R = reg();
    Player& p = *s->player;
    const json* found = p.data.is_object() && p.data.contains("found") && p.data["found"].is_object() ? &p.data["found"] : nullptr;
    keys.clear();
    if (found) for (auto it = found->begin(); it != found->end(); ++it) if (R.item(it.key())) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end(), [&](const std::string& x, const std::string& y) { return R.item(x)->name < R.item(y)->name; });
    sel = clampv(sel, 0, std::max(0, (int)keys.size() - 1));
    auto known = [&](const std::string& k) { return found && found->contains(k); };

    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.f, a.width - 30.f), std::min(540.f, a.height - 30.f)));
    ImGui::Begin("##travel_journal", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::Text("Travel Journal");
    size_t total = 0; for (auto& d : R.items) if (!d.key.empty()) total++;
    ImGui::SameLine(ImGui::GetWindowWidth() - 200); ImGui::TextDisabled("%d of %d things found", (int)keys.size(), (int)total);
    ImGui::Separator();
    const float footH = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
    if (keys.empty()) {
      ImGui::TextWrapped("Nothing yet. Everything you pick up, make or find is written in here, with how it's made.");
    } else {
      // the list
      ImGui::BeginChild("##tj_list", ImVec2(ImGui::GetContentRegionAvail().x * 0.4f, -footH), ImGuiChildFlags_Borders);
      ui::padScrollHere();
      for (int i = 0; i < (int)keys.size(); i++) {
        const ItemDef* d = R.item(keys[i]);
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::Selectable("##it", i == sel, 0, ImVec2(0, 26))) sel = i;
        if (i == sel && follow) { ImGui::SetScrollHereY(0.5f); follow = false; }
        ui::drawItem(ImGui::GetWindowDrawList(), ItemStack(keys[i], 1), ImVec2(at.x + 2, at.y + 1), 24, 1, false);
        ImGui::SameLine(34); ImGui::TextUnformatted(d->name.c_str());
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::SameLine();
      // the page: what it is, and how it's made
      ImGui::BeginChild("##tj_page", ImVec2(0, -footH), ImGuiChildFlags_Borders);
      const std::string& key = keys[sel];
      const ItemDef* d = R.item(key);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 at = ImGui::GetCursorScreenPos();
      ui::drawItem(dl, ItemStack(key, 1), at, 56, 1, false);
      ImGui::SetCursorScreenPos(ImVec2(at.x + 66, at.y + 8));
      ImGui::TextUnformatted(d->name.c_str());
      ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + 64));
      std::string desc = d->str("desc");
      if (!desc.empty()) ImGui::TextWrapped("%s", desc.c_str());
      auto shown = [&](const std::string& ing) -> std::string {   // a tag shows its first member
        if (!ing.empty() && ing[0] == '#') { auto t = R.tags.find(ing); return t != R.tags.end() && !t->second.empty() ? t->second[0] : std::string(); }
        return ing;
      };
      int made = 0;
      for (const Recipe& r : R.recipes) {
        if (r.out != key || made >= 3) continue;
        made++;
        ImGui::Spacing(); ImGui::SeparatorText(made == 1 ? "Made from" : "Or from");
        ImVec2 g = ImGui::GetCursorScreenPos();
        const float S = 38;
        std::vector<std::string> cells;
        int w = 3, h = 3;
        if (r.shaped()) {
          w = r.w ? r.w : (int)r.shape[0].size(); h = r.h ? r.h : (int)r.shape.size();
          for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            char c = y < (int)r.shape.size() && x < (int)r.shape[y].size() ? r.shape[y][x] : ' ';
            auto k = r.key.find(c);
            cells.push_back(c == ' ' || k == r.key.end() ? std::string() : shown(k->second));
          }
        } else {
          w = std::min(3, (int)r.items.size()); h = ((int)r.items.size() + 2) / 3;
          for (auto& it : r.items) cells.push_back(shown(it));
        }
        for (int i = 0; i < (int)cells.size(); i++) {
          ImVec2 c = ImVec2(g.x + (i % std::max(1, w)) * (S + 4), g.y + (i / std::max(1, w)) * (S + 4));
          ui::SlotLook lk; ui::drawSlot(dl, c, S, lk);
          if (!cells[i].empty()) ui::drawItem(dl, ItemStack(cells[i], 1), ImVec2(c.x + 3, c.y + 3), S - 6, known(cells[i]) ? 1.f : 0.35f, false);
        }
        float gw = std::max(1, w) * (S + 4);
        ImVec2 out = ImVec2(g.x + gw + 34, g.y + (std::max(1, h) * (S + 4) - S) * 0.5f);
        dl->AddText(ImVec2(g.x + gw + 8, out.y + S * 0.3f), ImGui::GetColorU32(ImGuiCol_Text), "\xE2\x86\x92");
        ui::SlotLook lk; ui::drawSlot(dl, out, S, lk);
        ui::drawItem(dl, ItemStack(key, r.n), ImVec2(out.x + 3, out.y + 3), S - 6, 1, true);
        ImGui::Dummy(ImVec2(gw + 80, std::max(1, h) * (S + 4)));
      }
      for (auto& [in, outKey] : R.smelting) if (outKey == key) { ImGui::Spacing(); ImGui::SeparatorText("Smelted from"); ImGui::TextUnformatted(R.item(in) ? R.item(in)->name.c_str() : in.c_str()); break; }
      if (!made) { ImGui::Spacing(); ImGui::TextDisabled("Not made at a table: found, dropped, grown or traded."); }
      int uses = 0;
      for (const Recipe& r : R.recipes) {
        bool in = false;
        for (auto& [c, k] : r.key) if (k == key) in = true;
        for (auto& k : r.items) if (k == key) in = true;
        if (in) uses++;
      }
      if (uses) { ImGui::Spacing(); ImGui::TextDisabled("Goes into %d recipe%s.", uses, uses == 1 ? "" : "s"); }
      ImGui::EndChild();
    }
    ImGui::TextDisabled("Up/down to turn the pages \xC2\xB7 E or Esc to close \xC2\xB7 faded: not found yet");
    ImGui::End();
  }
private:
  std::vector<std::string> keys;
  int sel = 0;
  bool follow = true;
};
} // namespace

std::unique_ptr<Screen> makeTravelJournalScreen() { return std::make_unique<TravelJournalScreen>(); }

} // namespace dq
