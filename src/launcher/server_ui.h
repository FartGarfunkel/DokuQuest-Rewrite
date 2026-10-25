// launcher/server_ui.h — the running server's dashboard (game/server.js serverRenderRunning): a header with the
// world, Save now and Stop server, the addresses to join, and the original's tabs — Players, World map, Isometric,
// Chat & console, Settings, Plugins. Shared between server_tab.cpp (setup + console), server_dash.cpp (header, tabs,
// players, settings, plugins) and server_views.cpp (the live map and the isometric view).
#pragma once
#include "launcher/launcher.h"
#include "server/server.h"

namespace dq {

enum class SvTab { Players, Map, Iso, Chat, Settings, Plugins, Count };
const char* svTabId(SvTab t);    // players | map | iso | chat | settings | plugins (--tab server:map)

struct ServerDash {
  SvTab tab = SvTab::Players;
  std::string follow;            // centre of the map and the isometric view ("" = spawn), like SERVER.follow
  int mapZoom = 2, isoR = 26;    // SERVER.mapZoom, SERVER.isoR
  ImVec2 mapPan{0, 0};           // blocks the map has been dragged off its centre
};
ServerDash& serverDash();

// server_dash.cpp: everything right of the setup panel while the server runs; stop = the Stop server button
void drawServerDashboard(Launcher& L, DedicatedServer& srv, const ServerStatus& st, double upSeconds, bool& stop);
// server_tab.cpp: the log with the command line under it (the Chat & console tab, and the panel while stopped)
void drawServerConsole(Launcher& L, DedicatedServer* srv, ImVec2 size, bool withPlayers);
// server_views.cpp
void drawServerMap(Launcher& L, DedicatedServer& srv, ImVec2 size);
void drawServerIso(Launcher& L, DedicatedServer& srv, ImVec2 size);
bool followCombo(Launcher& L, const char* id, const char* label, const std::vector<std::string>& names, float width);

} // namespace dq
