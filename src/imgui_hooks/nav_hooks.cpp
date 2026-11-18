// imgui_hooks/nav_hooks.cpp — which item the controller's highlight is on, by its label, for playtests (expect "nav").
// ImGui calls these item hooks (IMGUI_ENABLE_TEST_ENGINE, set for the imgui target, which builds this file) for every
// widget it lays out, with its label; the label of the item its navigation highlights is kept per context. A context
// turns the hooks on with TestEngineHookItems (ui::feedNav). Built into the imgui library: no game code here.
#include "imgui_hooks/nav_hooks.h"
#include <imgui_internal.h>

long long& dqNavFrame() { static long long f = 0; return f; }
std::map<ImGuiContext*, DqNavSeen>& dqNavSeen() { static std::map<ImGuiContext*, DqNavSeen> m; return m; }

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
  if (id != 0 && id == ctx->NavId) { DqNavSeen& s = dqNavSeen()[ctx]; s.minX = bb.Min.x; s.minY = bb.Min.y; s.maxX = bb.Max.x; s.maxY = bb.Max.y; s.rectFrame = dqNavFrame(); }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
  if (id == 0 || id != ctx->NavId || !label) return;
  DqNavSeen& s = dqNavSeen()[ctx];
  s.label = label; s.frame = dqNavFrame(); s.shown = ctx->NavCursorVisible;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }
