// server/server_view.cpp — sampling the world for the launcher's server dashboard (game/server.js serverChunkImage,
// serverDrawMap, serverDrawIso): runs on the server thread, the UI only ever sees the copies.
#include "server/server_view.h"
#include "sim/maps.h"
#include "sim/rules.h"
#include "sim/weather.h"

namespace dq {

namespace {
// the block seen from above in one column (serverChunkImage's scan): from `top` (band-local) down past see-through blocks
ViewCol sampleColumn(const Chunk& ch, int lx, int lz, int top, int base, const std::vector<u8>& skip) {
  ViewCol c;
  int col = (lz << 4) | lx;
  if (ch.maxY <= 0) return c;
  int y = std::min({top, (int)ch.heightMap[col], ch.maxY - 1});
  BlockId id = AIR;
  for (; y >= 0; y--) { id = ch.get(lx, y, lz); if (id >= skip.size() || skip[id] != 1) break; }
  if (y < 0) return c;
  c.id = (u16)id; c.y = (i16)(base + y);
  c.cl = (u8)(((ch.climate[col * 2] >> 4) << 4) | (ch.climate[col * 2 + 1] >> 4));
  return c;
}

const char* timeName(double t) {   // t: 0..1 of the day (the first half is day)
  if (t < 0.04) return "Dawn";
  if (t < 0.18) return "Morning";
  if (t < 0.32) return "Noon";
  if (t < 0.46) return "Afternoon";
  if (t < 0.52) return "Evening";
  if (t < 0.96) return "Night";
  return "Dawn";
}
}

void sampleServerView(Level& lv, const std::vector<ViewPlayer>& players, const ServerViewReq& req, ServerView& v, double now) {
  v.players = players;
  v.spawn = lv.info.spawn.y >= 0 ? lv.info.spawn : vec3(0, dimBase(DIM_OVERWORLD) + 80, 0);
  // the centre: the player followed, else spawn (serverCentre)
  std::string follow; int realm = REALM_MAIN; vec3 c = v.spawn;
  for (auto& p : players) if (!req.follow.empty() && toLower(p.name) == toLower(req.follow)) { follow = p.name; realm = p.realm; c = p.pos; break; }
  int dim = realm == REALM_MAIN ? dimOfY(ifloor(c.y)) : DIM_OVERWORLD;
  bool ceiling = (realm == REALM_MAIN && dim == DIM_NETHER) || realm == REALM_POCKET;   // MAP_CEILING
  bool moved = follow != v.follow || realm != v.realm || dim != v.dim;
  v.follow = follow; v.realm = realm; v.dim = dim; v.centre = c; v.ceiling = ceiling;
  v.day = lv.info.day;
  v.timeOfDay = timeName(lv.info.time / DAY_LEN);
  if (auto* w = lv.system<WeatherSystem>()) v.weather = w->kind; else v.weather = "clear";
  v.rules = rules(lv);
  v.chunksTotal = 0;
  for (auto& [r, w] : lv.allRealms()) v.chunksTotal += (int)w->chunks().size();
  World* world = lv.worldIfOpen(realm);
  const auto& skip = mapSkipTable();
  int base = realm == REALM_MAIN ? dimBase(dim) : dimBase(DIM_OVERWORLD);
  int localC = ifloor(c.y) - base;

  // the map: every loaded chunk of this world, seen from above (every 1.5 s like the original; at once when the centre's world changes)
  static constexpr size_t MAX_CHUNKS = 6000;
  bool mapDue = req.map && (moved || !v.chunks || now - v.at >= 1.5 || (ceiling && now - v.at >= 0.75));
  if (mapDue) {
    auto out = std::make_shared<std::vector<ViewChunk>>();
    if (world) {
      int top = ceiling ? clampv(localC + 2, 0, DIM_H - 1) : DIM_H - 1;
      // nearest first, so a huge world keeps what's round the centre
      std::vector<std::pair<i64, const Chunk*>> list;
      int ccx = ifloor(c.x) >> 4, ccz = ifloor(c.z) >> 4;
      for (auto& [k, ch] : world->chunks()) {
        if (k.dim != dim || !ch) continue;
        i64 dx = k.cx - ccx, dz = k.cz - ccz;
        list.push_back({dx * dx + dz * dz, ch.get()});
      }
      if (list.size() > MAX_CHUNKS) {
        std::nth_element(list.begin(), list.begin() + MAX_CHUNKS, list.end(), [](auto& a, auto& b) { return a.first < b.first; });
        list.resize(MAX_CHUNKS);
      }
      out->resize(list.size());
      for (size_t i = 0; i < list.size(); i++) {
        const Chunk& ch = *list[i].second;
        ViewChunk& vc = (*out)[i];
        vc.cx = ch.cx; vc.cz = ch.cz;
        for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) vc.col[(lz << 4) | lx] = sampleColumn(ch, lx, lz, top, base, skip);
      }
    }
    v.chunks = out; v.chunksSeq++; v.at = now;
  }
  if (!req.map && v.chunks && now - v.at > 10) { v.chunks.reset(); }   // nobody's looking: let it go

  // the isometric view: the columns round the centre (2.5 s in the original; livelier here)
  int R = clampv(req.isoR, 8, 48);
  bool isoDue = req.iso && (moved || R != v.isoR || now - v.isoAt >= 0.5);
  if (isoDue) {
    int n = 2 * R + 1, x0 = ifloor(c.x) - R, z0 = ifloor(c.z) - R;
    v.iso.assign((size_t)n * n, ViewCol());
    if (world) {
      int top = ceiling ? clampv(localC + 6, 0, DIM_H - 1) : DIM_H - 1;
      const Chunk* ch = nullptr; int lcx = INT32_MIN, lcz = INT32_MIN;
      for (int dz = 0; dz < n; dz++) for (int dx = 0; dx < n; dx++) {
        int x = x0 + dx, z = z0 + dz;
        if ((x >> 4) != lcx || (z >> 4) != lcz) { lcx = x >> 4; lcz = z >> 4; ch = world->chunk(dim, lcx, lcz); }
        if (ch) v.iso[(size_t)dz * n + dx] = sampleColumn(*ch, x & 15, z & 15, top, base, skip);
      }
    }
    v.isoR = R; v.isoX0 = x0; v.isoZ0 = z0; v.isoBase = ifloor(c.y); v.isoSeq++; v.isoAt = now;
  }
  v.seq++;
}

} // namespace dq
