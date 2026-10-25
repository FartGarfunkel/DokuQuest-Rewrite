// ui/layout_flow.h — layout helpers so the character screens (creator, skin editor, cape editor) fit any window
// shape (16:9, 21:9 ultrawide, 32:9) and any interface zoom: window sizes come from the zoom (Z()), rows of buttons
// wrap onto a new line when the row is full instead of running off the right edge.
#pragma once
#include "ui/theme.h"
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <utility>

namespace dq { namespace flow {

// a window of baseW x baseH "CSS pixels" times the zoom, never bigger than the screen minus a margin
inline ImVec2 windowSize(float screenW, float screenH, float baseW, float baseH, float margin = 8) {
  float z = ui::Z();
  return ImVec2(std::min(screenW - margin * 2, baseW * z), std::min(screenH - margin * 2, baseH * z));
}

// one row of buttons that wraps; call row.button() for each, in order
struct Row {
  bool first = true;
  bool fits(float w) const {
    ImGuiStyle& st = ImGui::GetStyle();
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    return ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= right;
  }
  // place the next item of width w on this line when it fits, else on a new one
  void next(float w) { if (!first && fits(w)) ImGui::SameLine(); first = false; }
  // wd: width in CSS pixels (scaled by the zoom), 0 = as wide as the label
  bool button(const char* label, float wd = 0) {
    float w = wd > 0 ? wd * ui::Z() : ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
    next(w);
    return ImGui::Button(label, ImVec2(wd > 0 ? w : 0, 0));
  }
};

// how many lines a set of buttons (widths in CSS px, 0 = label width) needs in `avail` pixels
inline int rowsNeeded(const std::vector<std::pair<const char*, float>>& b, float avail) {
  ImGuiStyle& st = ImGui::GetStyle(); int rows = 1; float x = 0;
  for (auto& [l, wd] : b) {
    float w = wd > 0 ? wd * ui::Z() : ImGui::CalcTextSize(l, nullptr, true).x + st.FramePadding.x * 2;
    if (x > 0 && x + st.ItemSpacing.x + w > avail) { rows++; x = 0; }
    x += (x > 0 ? st.ItemSpacing.x : 0) + w;
  }
  return rows;
}

}}  // namespace dq::flow
