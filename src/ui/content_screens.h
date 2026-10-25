// ui/content_screens.h — the content features' screens (plain ImGui; restyled with the UI theme at merge):
// the map view (maps.js MapView), waystones (travel.js WayMenu), the stable (stable.js StableUI) and the
// journal of places. Each closes on Esc / B / its own key, and works with a controller (d-pad to pick, A to
// press, X/Y to zoom the map).
#pragma once
#include "ui/screen.h"
#include "common/fs.h"

namespace dq {

// slot: the inventory slot of the map to look at (-2 = the off hand, -1 = the best one you carry)
std::unique_ptr<Screen> makeMapScreen(int slot = -1);
std::unique_ptr<Screen> makeWaystoneScreen(const std::string& hereId);
std::unique_ptr<Screen> makeStableScreen();
std::unique_ptr<Screen> makePlacesScreen();
bool canOpenMapScreen(class App& a);                 // the tab strip: a map with you (menutabs.js `can`)
void stableListArrived(const json& list);             // a client: the host's answer (message kind "stable-list")

} // namespace dq
