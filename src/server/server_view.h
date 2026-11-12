// server/server_view.h — what the launcher's server dashboard draws (game/server.js serverDrawMap, serverDrawIso,
// serverRenderPlayers): the loaded world seen from above with everyone on it, and the columns round one player for
// the isometric view. The world belongs to the server thread, so DedicatedServer samples it there (sampleServerView,
// called from its loop while someone is looking) and hands the UI thread a finished copy.
#pragma once
#include "sim/level.h"
#include <array>

namespace dq {

// one column seen from above (the original's map pixel): the block on top, its global y, the climate tint
struct ViewCol { u16 id = 0; i16 y = -1; u8 cl = 0x88; };   // y < 0: nothing there (void / not loaded)
struct ViewChunk { int cx = 0, cz = 0; std::array<ViewCol, 256> col; };   // col[(lz << 4) | lx]
struct ViewPlayer { std::string name, address; int realm = 0, dim = DIM_OVERWORLD; vec3 pos{0}; float yaw = 0, hp = 20, maxHp = 20; bool dead = false, op = false; u32 ping = 0; };

// what the dashboard is looking at (set from the UI thread)
struct ServerViewReq {
  bool map = false, iso = false;   // tabs open
  std::string follow;              // player to centre on ("" = spawn)
  int isoR = 26;                   // isometric half size in blocks (16, 26, 36)
};

struct ServerView {
  u64 seq = 0;                     // bumps with each new copy
  double at = 0;                   // nowSeconds() when sampled
  std::vector<ViewPlayer> players;
  vec3 spawn{0};
  std::string follow;              // the player actually followed ("" = spawn)
  int realm = REALM_MAIN, dim = DIM_OVERWORLD;
  vec3 centre{0};
  bool ceiling = false;            // a roofed world (Ember Depths, pocket): the floor round the centre is shown
  // the map: every loaded chunk of that world (shared between copies until it is sampled again)
  std::shared_ptr<const std::vector<ViewChunk>> chunks;
  u64 chunksSeq = 0;
  int chunksTotal = 0;             // all chunks loaded in every world
  // the isometric view: (2R+1)^2 columns, row z then x, from (isoX0, isoZ0)
  int isoR = 0, isoX0 = 0, isoZ0 = 0, isoBase = 0;
  std::vector<ViewCol> iso;
  u64 isoSeq = 0;
  double isoAt = -100;
  std::string timeOfDay, weather;  // the Settings tab's "now" line
  i64 day = 0;
  json rules = json::object();     // the world's game rules
};

// On the server thread: refreshes `v` for `req` (players every call, the map and isometric columns when due).
void sampleServerView(Level& lv, const std::vector<ViewPlayer>& players, const ServerViewReq& req, ServerView& v, double now);

} // namespace dq
