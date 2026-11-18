// imgui_hooks/nav_hooks.h — the label of the item ImGui's navigation highlights, per context (nav_hooks.cpp).
#pragma once
#include <map>
#include <string>
struct ImGuiContext;
struct DqNavSeen { std::string label; long long frame = -1, rectFrame = -1; bool shown = false; float minX = 0, minY = 0, maxX = 0, maxY = 0; };
long long& dqNavFrame();   // the game's frame number, set by the game each frame (the hooks stamp what they see with it)
std::map<ImGuiContext*, DqNavSeen>& dqNavSeen();   // (frame: dqNavFrame() when it was seen; shown: the highlight was showing)
