// sim/maps.cpp — paper maps: records, exploring, markers, pins, using, crafting (game/maps.js).
#include "sim/maps.h"

namespace dq {

const char* const MAP_PIN_COLS[6] = {"#e8484d", "#3a8ae8", "#4ac04a", "#e8b83a", "#b85ae8", "#f4f0e8"};

int mapAlign(int v, int scale) { int size = MAP_N << scale; return floordiv(v + 64, size) * size - 64; }

bool PaperMap::setPixel(int i, u16 id, u8 hh, u8 c) {
  if (ids[i] == id && h[i] == hh && cl[i] == c) return false;
  ids[i] = id; h[i] = hh; cl[i] = c; ver++;
  return true;
}
float PaperMap::explored() const { int n = 0; for (u8 v : h) if (v) n++; return (float)n / MAP_PIX; }

// ------------------------------------------------------------------ the record (world/chunk.js RLE + base64)
namespace {
void pushVar(std::vector<u8>& out, u32 n) { while (n >= 128) { out.push_back((u8)((n & 127) | 128)); n >>= 7; } out.push_back((u8)n); }
template <class T> std::vector<u8> rleEnc(const T* src, size_t n, bool wide) {
  std::vector<u8> out; int prev = -1; u32 run = 0;
  auto flush = [&]() { pushVar(out, run); if (wide) { out.push_back((u8)(prev & 255)); out.push_back((u8)(prev >> 8)); } else out.push_back((u8)prev); };
  for (size_t k = 0; k < n; k++) {
    int v = src[k];
    if (v == prev) run++;
    else { if (run) flush(); prev = v; run = 1; }
  }
  if (run) flush();
  return out;
}
template <class T> void rleDec(const std::vector<u8>& b, T* dst, size_t n, bool wide) {
  size_t p = 0, k = 0;
  while (p < b.size() && k < n) {
    u32 cnt = 0; int shift = 0; u8 c;
    do { if (p >= b.size()) return; c = b[p++]; cnt |= (u32)(c & 127) << shift; shift += 7; } while ((c & 128) && shift < 32);
    if (p >= b.size()) return;
    u32 v = b[p++];
    if (wide) { if (p >= b.size()) return; v |= (u32)b[p++] << 8; }
    for (u32 i = 0; i < cnt && k < n; i++) dst[k++] = (T)v;
  }
}
}
std::vector<u8> mapRleEncode(const u16* src, size_t n, bool wide) { return rleEnc(src, n, wide); }
std::vector<u8> mapRleEncode(const u8* src, size_t n) { return rleEnc(src, n, false); }
void mapRleDecode(const std::vector<u8>& b, u16* dst, size_t n, bool wide) { rleDec(b, dst, n, wide); }
void mapRleDecode(const std::vector<u8>& b, u8* dst, size_t n) { rleDec(b, dst, n, false); }

json encodeMap(const PaperMap& m) {
  json pal = json::array({"air"});
  std::unordered_map<u16, u16> at{{0, 0}};
  std::vector<u16> pi(MAP_PIX);
  for (int i = 0; i < MAP_PIX; i++) {
    u16 id = m.ids[i];
    auto it = at.find(id);
    if (it == at.end()) { u16 k = (u16)pal.size(); it = at.emplace(id, k).first; pal.push_back(id < reg().blocks.size() ? reg().block(id).key : "air"); }
    pi[i] = it->second;
  }
  bool wide = pal.size() > 255;
  auto b64 = [](const std::vector<u8>& v) { return base64Encode(v.data(), v.size()); };
  json rec = {{"v", 2}, {"k", "map~" + m.id}, {"t", unixMillis()}, {"map", 1}, {"dim", m.dim}, {"x0", m.x0}, {"z0", m.z0}, {"sc", m.scale}, {"pal", pal},
              {"ids", b64(mapRleEncode(pi.data(), MAP_PIX, wide))}, {"hs", b64(mapRleEncode(m.h.data(), MAP_PIX))}, {"cl", b64(mapRleEncode(m.cl.data(), MAP_PIX))}};
  if (wide) rec["wd"] = 1;
  return rec;
}
std::unique_ptr<PaperMap> decodeMap(const json& rec) {
  if (!rec.is_object() || !rec.contains("k")) return nullptr;
  auto m = std::make_unique<PaperMap>();
  std::string k = rec["k"].get<std::string>();
  m->id = k.size() > 4 ? k.substr(4) : k;
  m->dim = rec.value("dim", 0); m->x0 = rec.value("x0", 0); m->z0 = rec.value("z0", 0); m->scale = clampv(rec.value("sc", 0), 0, MAP_MAX_SCALE);
  std::vector<u16> pi(MAP_PIX, 0);
  mapRleDecode(base64Decode(rec.value("ids", "")), pi.data(), MAP_PIX, rec.value("wd", 0) != 0);
  std::vector<u16> ids;
  if (rec.contains("pal")) for (auto& k2 : rec["pal"]) ids.push_back(k2.is_string() ? reg().blockId(k2.get<std::string>()) : 0);
  for (int i = 0; i < MAP_PIX; i++) m->ids[i] = pi[i] < ids.size() ? ids[pi[i]] : 0;
  mapRleDecode(base64Decode(rec.value("hs", "")), m->h.data(), MAP_PIX);
  mapRleDecode(base64Decode(rec.value("cl", "")), m->cl.data(), MAP_PIX);
  return m;
}

// ------------------------------------------------------------------ the store
MapStore& mapStore(Level& lv) {
  if (auto s = lv.system<MapStore>()) return *s;
  auto s = std::make_shared<MapStore>(lv);
  lv.systems.push_back(s);
  return *s;
}
PaperMap* MapStore::get(const std::string& id) {
  if (id.empty()) return nullptr;
  auto it = maps.find(id);
  if (it != maps.end()) return it->second.get();
  if (missing.count(id)) return nullptr;
  if (auto rec = readJson(lv.folder / "maps" / (id + ".json"))) {
    if (auto m = decodeMap(*rec)) { m->id = id; PaperMap* r = m.get(); maps[id] = std::move(m); return r; }
  }
  missing.insert(id);
  return nullptr;
}
PaperMap* MapStore::create(const std::string& id, int dim, double x, double z, int scale) {
  auto m = std::make_unique<PaperMap>();
  m->id = id; m->dim = dim; m->scale = scale;
  m->x0 = mapAlign(ifloor(x), scale); m->z0 = mapAlign(ifloor(z), scale);
  PaperMap* r = m.get();
  maps[id] = std::move(m); dirty.insert(id); missing.erase(id);
  return r;
}
PaperMap* MapStore::zoomed(const PaperMap& src) {
  int sc = std::min(MAP_MAX_SCALE, src.scale + 1);
  auto m = std::make_unique<PaperMap>();
  m->id = newId(); m->dim = src.dim; m->scale = sc; m->x0 = mapAlign(src.x0, sc); m->z0 = mapAlign(src.z0, sc);
  int ox = (src.x0 - m->x0) >> sc, oz = (src.z0 - m->z0) >> sc;
  for (int z = 0; z < MAP_N / 2; z++) for (int x = 0; x < MAP_N / 2; x++) {
    int j = -1;
    for (auto [dx, dz] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) { int k = (z * 2 + dz) * MAP_N + x * 2 + dx; if (src.h[k]) { j = k; break; } }
    if (j < 0) continue;
    int i = (oz + z) * MAP_N + ox + x;
    if (i < 0 || i >= MAP_PIX) continue;
    m->ids[i] = src.ids[j]; m->h[i] = src.h[j]; m->cl[i] = src.cl[j];
  }
  PaperMap* r = m.get();
  dirty.insert(m->id);
  maps[m->id] = std::move(m);
  return r;
}
PaperMap* MapStore::forStack(Player& p, ItemStack& s) {
  std::string id = stackMapId(s);
  if (id.empty()) {
    id = newId();
    if (!s.tag.is_object()) s.tag = json::object();
    s.tag["map"] = id;
    return create(id, jsDimOf(p), p.pos.x, p.pos.z, 0);
  }
  return get(id);
}
std::vector<PaperMap*> MapStore::sameScale(const PaperMap& m) {
  // the records on disk too (maps.js collect: g.records)
  std::error_code ec;
  for (auto& e : fs::directory_iterator(lv.folder / "maps", ec)) {
    std::string id = e.path().stem().string();
    if (!maps.count(id) && !missing.count(id)) get(id);
  }
  std::vector<PaperMap*> out;
  for (auto& [k, mm] : maps) if (mm.get() != &m && mm->dim == m.dim && mm->scale == m.scale) out.push_back(mm.get());
  std::sort(out.begin(), out.end(), [](PaperMap* a, PaperMap* b) { return a->explored() < b->explored(); });
  return out;
}
void MapStore::save(Level& l) {
  if (l.isRemote() || dirty.empty()) return;
  for (auto& id : dirty) { auto it = maps.find(id); if (it != maps.end()) writeJson(l.folder / "maps" / (id + ".json"), encodeMap(*it->second), false); }
  dirty.clear();
}
void mapsSave(Level& lv) { mapStore(lv).save(lv); }

// ------------------------------------------------------------------ exploring
void mapWorldOf(int jsDim, int& realm, int& dim) {
  realm = jsDim == JS_POCKET ? REALM_POCKET : jsDim == JS_REALM ? REALM_HIS : REALM_MAIN;
  dim = realm != REALM_MAIN ? DIM_OVERWORLD : dimFromJs(jsDim);
}
const std::vector<u8>& mapSkipTable() {   // 1 = see through, 2 = water
  static std::vector<u8> t; static size_t n = 0;
  const Registry& R = reg();
  if (n == R.blocks.size()) return t;
  n = R.blocks.size(); t.assign(n, 0);
  static const std::set<std::string> thin = {"barrier", "light", "structure_void", "moving_block", "redstone_wire", "tripwire", "tripwire_hook",
                                             "rail", "powered_rail", "detector_rail", "activator_rail", "string"};
  for (size_t id = 0; id < n; id++) {
    const BlockDef& b = R.blocks[id];
    if (!id || b.render == Render::None || b.render == Render::Cross || b.render == Render::Torch || thin.count(b.key)) t[id] = 1;
    else if (b.str("liquid") == "water") t[id] = 2;
  }
  return t;
}
namespace {
const std::vector<u8>& skipTable() { return mapSkipTable(); }
void mapSample(PaperMap& m, int i, const Chunk& ch, int lx, int lz, int top) {
  const auto& skip = skipTable();
  int y = std::min(top, ch.maxY - 1);
  BlockId id = AIR;
  for (; y >= 0; y--) { id = ch.get(lx, y, lz); if (id >= skip.size() || skip[id] != 1) break; }
  if (y < 0) return;                       // nothing at all (the void): the paper stays blank
  int col = (lz << 4) | lx;
  u8 t = ch.climate[col * 2], hu = ch.climate[col * 2 + 1];
  int lo = hu >> 4;
  if (id < skip.size() && skip[id] == 2) { int d = 0; for (int yy = y; yy >= 0 && d < 15 && skip[ch.get(lx, yy, lz)] == 2; yy--) d++; lo = d; }
  m.setPixel(i, id, (u8)std::min(255, y + 1), (u8)(((t >> 4) << 4) | lo));
}
}
void mapExplore(Level& lv, Player& p, PaperMap& m, int columns) {
  int realm, dim; mapWorldOf(m.dim, realm, dim);
  if (realm != p.realm) return;
  World* wp = lv.worldIfOpen(realm);
  if (!wp) return;
  int s = 1 << m.scale;
  int ppx = ifloor((p.pos.x - m.x0) / s), ppz = ifloor((p.pos.z - m.z0) / s);
  if (ppx < -MAP_R || ppz < -MAP_R || ppx >= MAP_N + MAP_R || ppz >= MAP_N + MAP_R) return;
  int lo = std::max(0, ppx - MAP_R), hi = std::min(MAP_N - 1, ppx + MAP_R), span = hi - lo + 1;
  if (span <= 0) return;
  bool ceiling = m.dim == JS_DEPTHS || m.dim == JS_POCKET;   // under a roof: map the floor around you, not the roof
  int top = ceiling ? ifloor(p.pos.y) - dimBase(dim) + 2 : DIM_H - 1;
  u32& k = mapStore(lv).sweep[p.id];
  for (int n = 0; n < columns; n++) {
    int px = lo + (int)(k++ % (u32)span), dx = px - ppx, rr = (int)std::floor(std::sqrt((double)std::max(0, MAP_R * MAP_R - dx * dx)));
    int z0 = std::max(0, ppz - rr), z1 = std::min(MAP_N - 1, ppz + rr);
    if (z1 < z0) continue;
    int bx = m.x0 + px * s + (s >> 1), cx = bx >> 4, lx = bx & 15;
    const Chunk* ch = nullptr; int chz = INT32_MIN;
    for (int pz = z0; pz <= z1; pz++) {
      int bz = m.z0 + pz * s + (s >> 1), cz = bz >> 4;
      if (cz != chz) { ch = wp->chunk(dim, cx, cz); chz = cz; }
      if (!ch) continue;
      mapSample(m, pz * MAP_N + px, *ch, lx, bz & 15, top);
    }
  }
}
void mapsTick(Level& lv, double dt) {
  MapStore& st = mapStore(lv);
  for (Player* p : levelPlayers(lv)) {
    if (p->dead) continue;
    for (ItemStack* s : {&p->held(), p->offhand.empty() ? nullptr : &p->offhand[0]}) {
      if (!s || !isMapStack(*s)) continue;
      PaperMap* m = st.forStack(*p, *s);
      if (!m || m->dim != jsDimOf(*p)) continue;
      u32 v0 = m->ver;
      mapExplore(lv, *p, *m, 6);   // 3 columns a frame at 60 fps
      if (m->ver != v0) st.dirty.insert(m->id);
    }
  }
}

// ------------------------------------------------------------------ markers
static std::vector<MapMarkerSource>& markerSources() { static std::vector<MapMarkerSource> v; return v; }
void addMapMarkerSource(MapMarkerSource f) { markerSources().push_back(std::move(f)); }

std::vector<MapMarker> mapMarkers(Level& lv, const PaperMap& m, const Player* viewer) {
  std::vector<MapMarker> out;
  float s = (float)(1 << m.scale);
  auto P = [&](double x, double z) { return vec2((float)((x - m.x0) / s), (float)((z - m.z0) / s)); };
  auto inside = [](vec2 q) { return q.x >= 0 && q.y >= 0 && q.x <= MAP_N && q.y <= MAP_N; };
  auto mark = [&](int cell, vec2 q, const std::string& label = "") { MapMarker k; k.cell = cell; k.u = q.x; k.v = q.y; k.label = label; out.push_back(k); };
  auto who = [&](double x, double z, float yaw, int cell) {
    vec2 q = P(x, z);
    if (inside(q)) { MapMarker k; k.cell = cell; k.u = q.x; k.v = q.y; k.rot = yaw; out.push_back(k); }
    else if (q.x > -MAP_N * 0.6f && q.y > -MAP_N * 0.6f && q.x < MAP_N * 1.6f && q.y < MAP_N * 1.6f) {
      MapMarker k; k.cell = 3; k.u = clampv(q.x, 1.5f, MAP_N - 1.5f); k.v = clampv(q.y, 1.5f, MAP_N - 1.5f); k.small = true; out.push_back(k);
    }
  };
  const json& D = lv.info.data;
  if (D.is_object() && D.contains("mapPins") && D["mapPins"].is_array())
    for (auto& pn : D["mapPins"]) {
      if (!pn.is_object() || pn.value("d", 0) != m.dim) continue;
      vec2 q = P(pn.value("x", 0.0), pn.value("z", 0.0));
      if (inside(q)) { MapMarker k; k.cell = 6 + floormod(pn.value("c", 0), 6); k.u = q.x; k.v = q.y; k.pin = true; out.push_back(k); }
    }
  const json S = viewer && viewer->stats.is_object() ? viewer->stats : json::object();
  if (m.dim == JS_OVERWORLD) {
    vec3 sp = viewer && viewer->spawnPoint.y >= 0 ? viewer->spawnPoint : lv.info.spawn;
    if (sp.y >= 0) { vec2 q = P(sp.x, sp.z); if (inside(q)) mark(4, q); }
    // places you've found: villages, dungeons (green once cleared) and the rift
    const json R = D.is_object() && D.contains("rift") ? D["rift"] : json();
    bool riftOpen = R.is_object() && R.value("stage", 0) >= 1 && !(R.contains("sealed") && R["sealed"] != false && R["sealed"] != 0) && R.contains("at");
    auto& H = contentHooks();
    if (S.contains("vseen") && S["vseen"].is_object() && H.villageById)
      for (auto& [id, _] : S["vseen"].items()) {
        auto v = H.villageById(lv, id); if (!v) continue;
        vec2 q = P(v->x, v->z);
        if (inside(q)) mark(12, q, v->name + (riftOpen && R.value("village", "") == id ? " \xC2\xB7 the rift" : ""));
      }
    if (S.contains("dseen") && S["dseen"].is_object() && H.dungeonById)
      for (auto& [id, _] : S["dseen"].items()) {
        auto d = H.dungeonById(lv, id); if (!d) continue;
        bool cleared = D.contains("dungeons") && D["dungeons"].contains(id) && D["dungeons"][id].value("cleared", false);
        vec2 q = P((d->x0 + d->x1) * 0.5, (d->z0 + d->z1) * 0.5);
        if (inside(q)) mark(cleared ? 15 : 13, q, d->name + (cleared ? " (cleared)" : ""));
      }
    if (riftOpen && !(S.contains("vseen") && S["vseen"].contains(R.value("village", "")))) {
      const json& at = R["at"];
      if (at.is_array() && at.size() >= 3) { vec2 q = P(at[0].get<double>(), at[2].get<double>()); if (inside(q)) mark(14, q, "The rift"); }
    }
  }
  if (S.contains("grave") && S["grave"].is_object() && S["grave"].value("dim", 0) == m.dim) {
    vec2 q = P(S["grave"].value("x", 0.0) + 0.5, S["grave"].value("z", 0.0) + 0.5);
    if (inside(q)) mark(5, q);
  }
  if (viewer && jsDimOf(*viewer) == m.dim) {
    for (Player* o : levelPlayers(lv, true)) if (o != viewer && !o->dead && jsDimOf(*o) == m.dim) who(o->pos.x, o->pos.z, o->yaw, 2);
    who(viewer->pos.x, viewer->pos.z, viewer->yaw, 0);
  }
  // travel.js: the waystones you know
  if (S.contains("ways") && S["ways"].is_object())
    for (auto& [id, r] : S["ways"].items()) {
      if (r.value("dim", 0) != m.dim) continue;
      vec2 q = P(r.value("x", 0), r.value("z", 0)); if (inside(q)) mark(13, q, r.value("name", "A waystone"));
    }
  {
    // structures.js: the places you've found (0.9.0.28: in every layer, by the dimension they were found in)
    if (S.contains("sseen") && S["sseen"].is_object())
      for (auto& [id, v] : S["sseen"].items()) {
        if (!v.is_array() || v.size() < 3 || (v.size() > 3 && v[3].is_number() ? v[3].get<int>() != m.dim : m.dim != JS_OVERWORLD)) continue;
        bool cleared = D.contains("structs") && D["structs"].contains(id) && D["structs"][id].value("cleared", 0) != 0;
        vec2 q = P(v[0].get<double>(), v[1].get<double>()); if (inside(q)) mark(cleared ? 15 : 13, q, v[2].is_string() ? v[2].get<std::string>() : "");
      }
    // hearth.js: your houses
    if (m.dim == JS_OVERWORLD && S.contains("homes") && S["homes"].is_object())
      for (auto& [vid, h] : S["homes"].items()) {
        if (!h.contains("door") || !h["door"].is_array()) continue;
        vec2 q = P(h["door"][0].get<double>(), h["door"][2].get<double>()); if (inside(q)) mark(13, q, "Your house in " + h.value("name", std::string("the village")));
      }
  }
  for (auto& f : markerSources()) f(lv, m, viewer, out);
  return out;
}

// ------------------------------------------------------------------ pins
std::string mapTogglePin(Level& lv, Player& p, int dim, float x, float z, float near) {
  json& D = lv.info.data;
  json pins = json::array();
  if (D.contains("mapPins") && D["mapPins"].is_array()) for (auto& q : D["mapPins"]) if (q.is_object() && q.contains("x")) pins.push_back(q);
  int hitAt = -1;
  for (size_t i = 0; i < pins.size(); i++) if (pins[i].value("d", 0) == dim && std::hypot(pins[i].value("x", 0.f) - x, pins[i].value("z", 0.f) - z) < near) { hitAt = (int)i; break; }
  std::string msg;
  if (hitAt >= 0) { pins.erase(pins.begin() + hitAt); sfxUi(lv, p, "click_off"); }
  else {
    if (pins.size() >= 64) return "That's a lot of pins \xE2\x80\x94 clear some first";
    int used = 0; for (auto& q : pins) if (q.value("d", 0) == dim) used++;
    pins.push_back({{"d", dim}, {"x", (int)std::lround(x)}, {"z", (int)std::lround(z)}, {"c", used % 6}});
    sfxUi(lv, p, "click");
  }
  D["mapPins"] = pins;
  return msg;
}

// ------------------------------------------------------------------ using maps
ItemStack* mapStackAt(Player& p, int slot) {
  if (slot == -2) return p.offhand.empty() ? nullptr : &p.offhand[0];
  return slot >= 0 && slot < (int)p.inv.size() ? &p.inv[slot] : nullptr;
}
int mapBestSlot(Level& lv, Player& p) {
  if (isMapStack(p.held())) return p.selected;
  if (!p.offhand.empty() && isMapStack(p.offhand[0])) return -2;
  int best = -1, score = -1, dim = jsDimOf(p);
  MapStore& st = mapStore(lv);
  for (int i = 0; i < (int)p.inv.size(); i++) {
    if (!isMapStack(p.inv[i])) continue;
    PaperMap* m = st.get(stackMapId(p.inv[i]));
    int sc = !m ? 0 : m->dim != dim ? 1 : m->has(p.pos.x, p.pos.z) ? 10 - m->scale : 2;
    if (sc > score) { score = sc; best = i; }
  }
  return best;
}
namespace {
bool blockUsable(const BlockDef& b) {   // the block you look at has its own use (maps.js: pile, container, table...)
  static const char* K[] = {"pile", "placedBackpack", "container", "table", "furnace", "harvest", "door", "bed", "waystone", "frame", "lever", "button", "sign", "lectern", "anvil", "chest", "trapdoor", "gate", "seat", "furniture"};
  for (const char* k : K) if (b.has(k)) return true;
  using namespace ShapeKind;
  u8 s = b.shapeKind;
  return s == Door || s == Gate || s == Trapdoor || s == Lever || s == Button || s == Bed || s == Frame || s == Repeater || s == Comparator;
}
}
bool mapsUse(UseCtx& u) {
  Player& p = u.p;
  ItemStack& s = p.held();
  if (s.empty() || (s.id != "empty_map" && s.id != "filled_map") || p.sneaking) return false;
  if (u.item != s.id) return false;
  if (u.hasHit()) {
    const BlockDef& b = reg().block(u.world().getBlock(u.hit.x, u.hit.y, u.hit.z));
    if (blockUsable(b)) return false;
    // a torch in the other hand goes on the wall you look at, not the map up to your face
    if (s.id == "filled_map" && !p.offhand.empty() && p.offhand[0].def() && !p.offhand[0].def()->block.empty()) return false;
  }
  Level& lv = u.lv;
  if (s.id == "filled_map") {
    PaperMap* m = mapStore(lv).forStack(p, s);
    if (!m) { toast(lv, p, "This map is still on its way from the host"); return true; }
    sfxUi(lv, p, "map_paper");
    openContentScreen(lv, p, "map", {{"slot", p.selected}});
    return true;
  }
  std::string id = newId();
  mapStore(lv).create(id, jsDimOf(p), p.pos.x, p.pos.z, 0);
  ItemStack fm("filled_map", 1); fm.tag = {{"map", id}};
  if (creativeOf(p)) giveOrDrop(lv, p, fm);
  else { s.n--; if (s.n <= 0) s = fm; else giveOrDrop(lv, p, fm); }
  sfxUi(lv, p, "map_paper");
  toast(lv, p, "A new map \xE2\x80\x94 it fills in as you explore. Look down to read it, right-click to look closely", "good");
  return true;
}

// ------------------------------------------------------------------ crafting
bool mapCraft(Level& lv, const std::vector<ItemStack>& slots, int W, ItemStack& out, bool take) {
  const ItemStack* fm = nullptr; int nf = 0, ne = 0, np = 0, other = 0, fi = -1;
  for (int i = 0; i < (int)slots.size(); i++) {
    const ItemStack& s = slots[i]; if (s.empty()) continue;
    if (s.id == "filled_map") { nf++; fm = &s; fi = i; } else if (s.id == "empty_map") ne++; else if (s.id == "paper") np++; else other++;
  }
  if (!nf) return false;
  out.clear();
  if (nf != 1 || other) return true;     // a map recipe that doesn't work: nothing
  std::string id = stackMapId(*fm);
  if (ne && !np) {                        // a map and empty maps: copies of the same map
    out = ItemStack("filled_map", ne + 1);
    if (!id.empty()) out.tag = {{"map", id}};
    return true;
  }
  if (np == 8 && !ne && W == 3 && fi == 4) {   // a map ringed with 8 paper: zoomed out to twice the area
    PaperMap* src = id.empty() ? nullptr : mapStore(lv).get(id);
    if (!src || src->scale >= MAP_MAX_SCALE) return true;
    out = ItemStack("filled_map", 1);
    if (take) out.tag = {{"map", mapStore(lv).zoomed(*src)->id}};
    return true;
  }
  return true;
}
std::vector<std::string> mapItemInfo(Level& lv, const ItemStack& s) {
  std::vector<std::string> out;
  if (s.id == "filled_map") {
    std::string id = stackMapId(s);
    PaperMap* m = id.empty() ? nullptr : mapStore(lv).get(id);
    if (m) {
      out.push_back(strFormat("1:%d \xC2\xB7 %s \xC2\xB7 %d%% explored", 1 << m->scale, jsDimName(m->dim).c_str(), (int)std::lround(m->explored() * 100)));
      out.push_back(strFormat("x %d to %d, z %d to %d", m->x0, m->x0 + m->span() - 1, m->z0, m->z0 + m->span() - 1));
    } else if (id.empty()) out.push_back("Becomes a map of wherever you first hold it");
    out.push_back("Look down to read it \xC2\xB7 right-click or M to look closely \xC2\xB7 in an item frame it fills the frame");
  } else if (s.id == "empty_map") out.push_back("Use it to start a map of where you stand");
  return out;
}

} // namespace dq
