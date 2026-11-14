// ui/station_bag.cpp — at a station (ui/cinematic.h), your journal held in your off hand open at your bag: the bag's
// three rows on the left page, the hotbar on the right with what's in your hand. Click a stack (or A on the pad's
// cursor) and it's in your hand, for the other hand to work the station with. Drawn into the journal's picture
// (ui/book_ui.cpp bookUiRender(a, true)); the hand view holds it (render/item_layers.cpp).
#include "ui/book.h"
#include "ui/book_ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/player.h"
#include <imgui.h>

namespace dq {

void drawStationBag(App& a, int padCur) {
  if (!a.session || !a.session->player) return;
  Player& p = *a.session->player;
  using namespace ui;
  const float z = Z();
  std::string hoverName;
  auto slotRow = [&](int first, int count, int padBase, float is) {
    for (int k = 0; k < count; k++) {
      const int slot = first + k;
      if (slot >= (int)p.inv.size()) break;
      if (k % 9) ImGui::SameLine(0, 3);
      ImGui::PushID(slot);
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      // (the mouse's click only: a pad's A reached ImGui's own nav focus here too and swapped what's in hand; the pad has
      // its own cursor, LB / RB then the d-pad and A, cinematic bagPadUpdate)
      if (ImGui::InvisibleButton("s", ImVec2(is, is), ImGuiButtonFlags_MouseButtonLeft) && ImGui::IsMouseReleased(0)) { if (slot < 9) p.selected = slot; else std::swap(p.inv[slot], p.inv[p.selected]); }
      const bool hot = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      SlotLook lk; lk.hover = hot; lk.sel = slot == p.selected; lk.focus = padBase + k == padCur;
      drawSlot(dl, pos, is, lk);
      if (padBase + k == padCur) dl->AddRect(ImVec2(pos.x - 2, pos.y - 2), ImVec2(pos.x + is + 2, pos.y + is + 2), IM_COL32(120, 220, 255, 255), 3, 0, 2.5f);
      if (!p.inv[slot].empty()) drawItem(dl, p.inv[slot], ImVec2(pos.x + 2, pos.y + 2), is - 4);
      if ((hot || padBase + k == padCur) && !p.inv[slot].empty()) hoverName = p.inv[slot].def() ? p.inv[slot].def()->name + (p.inv[slot].n > 1 ? " x" + std::to_string(p.inv[slot].n) : "") : p.inv[slot].id;
      ImGui::PopID();
    }
  };
  const float is = bookPagesBegin(a, "##stbag");
  bookPageTitle(a, "Your bag");
  for (int r = 0; r < 3; r++) slotRow(9 + r * 9, 9, r * 9, is);   // (the bag's rows: the pad's order 0..26)
  ImGui::Dummy(ImVec2(1, 6 * z));
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ink::muted));
  ImGui::TextWrapped("Click a stack: it goes into your hand. E puts the book away.");
  ImGui::PopStyleColor();
  bookPagesRight("##stbagR");
  bookPageTitle(a, "To hand");
  slotRow(0, 9, 27, is);   // (the hotbar: the pad's 27..35)
  ImGui::Dummy(ImVec2(1, 8 * z));
  const ItemStack& h = p.held();
  ImGui::TextWrapped("In hand: %s", h.empty() ? "nothing" : (h.def() ? h.def()->name : h.id).c_str());
  if (!hoverName.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ink::key));
    ImGui::TextWrapped("%s", hoverName.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Dummy(ImVec2(1, 6 * z));
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ink::muted));
  ImGui::TextWrapped("Wheel / 1-9: what's in hand. Pad: LB / RB to the book, D-pad, A.");
  ImGui::PopStyleColor();
  bookPagesEnd();
}

} // namespace dq
