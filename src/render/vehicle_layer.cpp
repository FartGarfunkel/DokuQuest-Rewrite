// render/vehicle_layer.cpp — minecarts (minecart.js/trains.js draws), rowboats, sailboats and the sailing ship
// (vehicles.js, boats2.js, ships.js), windmills, waterwheels, axles and machines (mech.js, machines.js),
// pistons on the move (redstone.js PistonAnim) and lit TNT (blockfx.js), as voxel models built from the
// original's box lists and coloured from the tiles, animated like the original. Also the client glue:
// riding input (sim/vehicles.h vehicleInput / vehiclesAfterPlayerMove) and container screens ("screenOpen").
#include "render/vehicle_layer.h"
#include "client/session.h"
#include "client/app.h"
#include "assets/voxel.h"
#include "assets/voxel_paint.h"
#include "assets/item_models.h"
#include "assets/tileset.h"
#include "sim/minecart.h"
#include "sim/vehicles.h"
#include "sim/machines.h"
#include "sim/redstone.h"
#include "sim/redstone_entities.h"
#include "ui/ui.h"
#include <glm/gtc/matrix_transform.hpp>

namespace dq {

namespace {
// one box of a model: centre, half size, turned by R; tiles per face (+x -x +y -y +z -z)
struct Part { vec3 c, h; mat3 R{1}; std::array<std::string, 6> tile; };
Part box(vec3 lo, vec3 hi, const std::string& t) { Part p; p.c = (lo + hi) * 0.5f; p.h = (hi - lo) * 0.5f; p.tile.fill(t); return p; }
Part boxEnds(vec3 lo, vec3 hi, const std::string& side, const std::string& ends, int axis) { Part p = box(lo, hi, side); p.tile[axis * 2] = p.tile[axis * 2 + 1] = ends; return p; }
// boats2.js boatBox: centre, half extents, turned about the upright
Part bb(float cx, float cy, float cz, float hx, float hy, float hz, float yaw, const std::string& t) {
  Part p; p.c = vec3(cx, cy, cz); p.h = vec3(hx, hy, hz); p.tile.fill(t);
  p.R = mat3(glm::rotate(mat4(1), yaw, vec3(0, 1, 0)));
  return p;
}
Part turned(Part p, float a, vec3 axis) { mat3 r = mat3(glm::rotate(mat4(1), a, axis)); p.c = r * p.c; p.R = r * p.R; return p; }

struct Model {
  VoxelMeshGL gl;
  vec3 origin{0}; float vox = 1.f / 32;
  bool built = false;
  mat4 m() const { return glm::scale(glm::translate(mat4(1), origin), vec3(vox)); }
};

void buildModel(Model& out, const std::vector<Part>& parts, float vox, const TileSet& ts) {
  out.built = true; out.vox = vox;
  vec3 lo(1e9f), hi(-1e9f);
  for (auto& p : parts) for (int k = 0; k < 8; k++) {
    vec3 c = p.c + p.R * (vec3(k & 4 ? 1 : -1, k & 2 ? 1 : -1, k & 1 ? 1 : -1) * glm::max(p.h, vec3(vox * 0.5f)));
    lo = glm::min(lo, c); hi = glm::max(hi, c);
  }
  if (lo.x > hi.x) return;
  lo -= vec3(vox); hi += vec3(vox);
  ivec3 size = ivec3(glm::ceil((hi - lo) / vox)) + 1;
  VoxelPainter P(size, lo, vox);
  for (size_t i = 0; i < parts.size(); i++) {
    const Part& p = parts[i];
    u32 id = (u32)i + 1;
    auto fn = [id](const PaintPt&) { return id; };
    if (p.R == mat3(1)) P.box(p.c - p.h, p.c + p.h, fn);
    else {
      std::array<vec3, 8> C;
      vec3 h = glm::max(p.h, vec3(vox * 0.5f));
      for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) for (int cz = 0; cz < 2; cz++)
        C[cx * 4 + cy * 2 + cz] = p.c + p.R * vec3(cx ? h.x : -h.x, cy ? h.y : -h.y, cz ? h.z : -h.z);
      P.hexa(C, fn);
    }
  }
  std::vector<int> tiles(parts.size() * 6, -1);
  for (size_t i = 0; i < parts.size(); i++) for (int f = 0; f < 6; f++) { auto it = ts.index.find(parts[i].tile[f]); tiles[i * 6 + f] = it == ts.index.end() ? 0 : it->second; }
  const int S = ts.size;
  VoxelFaceColor fc = [&](int x, int y, int z, int f, u32 v) -> u32 {
    int bi = (int)v - 1;
    if (bi < 0 || bi >= (int)parts.size()) return 0;
    // the part's own frame: the face's tile repeats once per block across it
    const Part& p = parts[bi];
    vec3 w = lo + (vec3(x, y, z) + 0.5f) * vox, l = glm::transpose(p.R) * (w - p.c) + p.h;
    int ax = f / 2;
    float a = ax == 0 ? l.z : l.x, b = ax == 1 ? l.z : l.y;
    int t = tiles[bi * 6 + f];
    int px = clampv((int)std::floor(fractf(a) * S), 0, S - 1), py = clampv((int)std::floor((1 - fractf(b)) * S), 0, S - 1);
    u32 c = tileVoxel(ts, t, px, py);
    if (!c) c = ts.avgColor(t) | 0xFF000000u;
    return c;
  };
  VoxelMeshData md;
  buildVoxelMesh(P.g, md, &fc);
  out.origin = lo;
  out.gl.upload(md);
}

VoxelLight lightAt(World& w, vec3 p) { u8 l = w.getLight(ifloor(p.x), ifloor(p.y), ifloor(p.z)); return {std::max(l >> 4, 4) / 15.f, (l & 15) / 15.f}; }
mat4 T(vec3 v) { return glm::translate(mat4(1), v); }
mat4 RX(mat4 m, float a) { return glm::rotate(m, a, vec3(1, 0, 0)); }
mat4 RY(mat4 m, float a) { return glm::rotate(m, a, vec3(0, 1, 0)); }
mat4 RZ(mat4 m, float a) { return glm::rotate(m, a, vec3(0, 0, 1)); }
} // namespace

class VehicleLayer : public RenderLayer {
public:
  explicit VehicleLayer(GameSession& s) {
    App* app = &s.app; Level* lv = s.level.get(); GameSession* sp = &s;
    hScreen = s.app.events.on("screenOpen", [app, lv, sp](const json& a) {
      if (!a.is_array() || a.size() < 3 || !sp->player || !a[0].is_string() || a[0].get<std::string>() != sp->player->name || a[1] != "container") return true;
      const json& o = a[2];
      InvOpen op; op.mode = "container";
      if (o.contains("pos") && o["pos"].is_array()) { op.hasPos = true; op.pos = ivec3(o["pos"][0].get<int>(), o["pos"][1].get<int>(), o["pos"][2].get<int>()); }
      else if (o.contains("entity")) {
        u32 id = o["entity"].get<u32>();
        op.entTitle = o.value("name", "");
        op.entItems = [lv, id]() -> json* {
          Entity* e = lv->entity(id);
          if (!e || e->removed) return nullptr;
          if (auto* c = dynamic_cast<Minecart*>(e)) return c->items();
          if (auto* b = dynamic_cast<Boat*>(e)) return b->items.is_array() ? &b->items : nullptr;
          return nullptr;
        };
      } else return true;
      uiDefer([op](App& ap) { openInventory(ap, op); });
      return true;
    });
    appRef = app;
  }
  ~VehicleLayer() override { if (appRef && hScreen) appRef->events.off(hScreen); }
  const char* name() const override { return "vehicles"; }

  // riding: the controller may call vehicleInput itself; otherwise the app's input drives the vehicle
  void afterPlayerMove(GameSession& s, double dt) override {
    if (!s.player || s.level->isRemote()) return;
    Player& p = *s.player;
    VehicleSystem* vs = vehicleSystem(*s.level);
    if (!vs) return;
    auto& st = vs->ps(p.id);
    bool controllerCalled = st.inputT == vs->time && st.inputT != selfT;
    if (!controllerCalled) {
      bool menu = s.app.topScreen() != nullptr;
      Input& in = s.app.input;
      vehicleInput(*s.level, p, menu ? vec2(0) : in.moveAxis(), !menu && in.down(Act::Jump), !menu && in.down(Act::Sneak));
      selfT = vs->time;
    }
    vehiclesAfterPlayerMove(*s.level, p, dt);
  }

  void update(GameSession& s, double dt) override {
    if (!s.player) return;
    // dev aid: DQ_DEV_RS=1 sets out carts, boats, a ship and a windmill in front of you (to look at them)
    if (!demoDone && std::getenv("DQ_DEV_RS") && s.level->world(s.player->realm).isLoaded(ifloor(s.player->pos.x), ifloor(s.player->pos.y), ifloor(s.player->pos.z) - 12)) {
      demoDone = true;
      Player& p = *s.player; World& w = s.world(); Level& lv = *s.level;
      int x0 = ifloor(p.pos.x) - 6, y0 = ifloor(p.pos.y) + 3, z0 = ifloor(p.pos.z) - 8;
      auto B = [](const char* k) { return reg().blockId(k); };
      for (int i = 0; i < 12; i++) { w.setBlock(x0 + i, y0 - 1, z0, B("stone")); w.setBlock(x0 + i, y0, z0, B("rail"), 1); }
      Minecart* a = placeCart(lv, p.realm, "furnace", x0 + 3, y0, z0, 5); placeCart(lv, p.realm, "chest", x0 + 6, y0, z0, 5); placeCart(lv, p.realm, "tnt", x0 + 8, y0, z0, 5);
      if (a) a->fuel = 0;
      spawnBoat(lv, p.realm, "rowboat", vec3(x0 + 2, y0 + 1, z0 - 4), 0.6f);
      spawnBoat(lv, p.realm, "sailboat", vec3(x0 + 7, y0 + 1, z0 - 5), -0.4f);
      spawnBoat(lv, p.realm, "ship", vec3(x0 + 4, y0 + 1, z0 - 14), PI / 2);
      w.setBlock(x0 + 12, y0 + 4, z0, B("windmill"), 2); w.setBlock(x0 + 12, y0 + 4, z0 + 1, B("axle"), 2); w.setBlock(x0 + 12, y0 + 4, z0 + 2, B("millstone"));
    }
    // wheels turn with the distance rolled
    for (auto& e : s.level->entities()) if (auto* c = dynamic_cast<Minecart*>(e.get())) {
      float v = std::hypot(c->vel.x, c->vel.z);
      float& a = wheel[c->id]; a += v * (float)dt / 0.09f;
    }
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    const TileSet& ts = s.app.tiles;
    if (packSeen != ts.packName || !built) { models.clear(); built = true; packSeen = ts.packName; }
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    int realm = s.player->realm;
    std::unordered_map<std::string, Minecart*> byUid;
    for (auto& e : s.level->entities()) if (auto* c = dynamic_cast<Minecart*>(e.get())) if (!c->removed && c->realm == realm) byUid[c->uid] = c;
    for (auto& e : s.level->entities()) {
      if (e->removed || e->realm != realm) continue;
      vec3 rel = f.cam.rel(glm::dvec3(e->pos));
      if (glm::dot(rel, rel) > 128.f * 128.f) continue;
      if (auto* c = dynamic_cast<Minecart*>(e.get())) { if (f.cam.frustum.sphere(rel, 2)) drawCart(s, w, *c, rel, byUid); }
      else if (auto* b = dynamic_cast<Boat*>(e.get())) { if (f.cam.frustum.sphere(rel, b->ship() ? 10.f : 4.f)) drawBoat(s, w, *b, rel); }
      else if (auto* a = dynamic_cast<PistonAnim*>(e.get())) drawPiston(s, w, *a, f);
      else if (auto* t = dynamic_cast<PrimedTnt*>(e.get())) {
        const VoxelModel& im = itemModel("tnt");
        if (!im.ready()) continue;
        float sc = 1 + std::max(0.f, 0.4f - t->fuse) * 0.4f;
        bool flash = (int)std::floor(t->age * 5) % 2 == 0;
        VR.draw(im.gl, T(rel + vec3(0, 0.5f, 0)) * glm::scale(mat4(1), vec3(sc)) * im.matrix(), VoxelLight{1, 1}, flash ? vec4(1, 1, 1, 0.5f) : vec4(0));
      }
    }
    drawMech(s, w, f);
    VR.end();
  }

private:
  App* appRef = nullptr;
  int hScreen = 0;
  double selfT = -2;
  bool built = false, demoDone = false;
  std::string packSeen;
  std::unordered_map<std::string, Model> models;
  std::unordered_map<u32, float> wheel;

  Model& model(const std::string& key, float vox, const std::function<std::vector<Part>()>& parts, const TileSet& ts) {
    Model& m = models[key];
    if (!m.built) buildModel(m, parts(), vox, ts);
    return m;
  }
  void draw(const Model& m, const mat4& at, VoxelLight L, vec4 tint = vec4(0)) { if (m.gl.ready()) voxelRenderer().draw(m.gl, at * m.m(), L, tint); }
  void drawItem(const std::string& key, const mat4& at, VoxelLight L, vec4 tint = vec4(0)) {
    const VoxelModel& im = itemModel(key);
    if (im.ready()) voxelRenderer().draw(im.gl, at * im.matrix(), L, tint);
  }

  // ---------------------------------------------------------------- minecarts (minecart.js build, trains.js)
  void drawCart(GameSession& s, World& w, Minecart& c, vec3 rel, std::unordered_map<std::string, Minecart*>& byUid) {
    const TileSet& ts = s.app.tiles;
    Model& body = model("cart", 1.f / 32, [] {
      const std::string S = "minecart_side", I = "minecart_inside";
      return std::vector<Part>{box({-0.44f, 0.12f, -0.62f}, {0.44f, 0.2f, 0.62f}, I),
        box({-0.48f, 0.12f, -0.62f}, {-0.4f, 0.68f, 0.62f}, S), box({0.4f, 0.12f, -0.62f}, {0.48f, 0.68f, 0.62f}, S),
        box({-0.4f, 0.12f, -0.66f}, {0.4f, 0.68f, -0.58f}, S), box({-0.4f, 0.12f, 0.58f}, {0.4f, 0.68f, 0.66f}, S)};
    }, ts);
    Model& wheelM = model("cart_wheel", 1.f / 32, [] { return std::vector<Part>{box({-0.06f, -0.09f, -0.09f}, {0.06f, 0.09f, 0.09f}, "minecart_wheel")}; }, ts);
    Model& seat = model("cart_seat", 1.f / 32, [] {
      return std::vector<Part>{box({-0.38f, 0.2f, -0.28f}, {0.38f, 0.4f, 0.3f}, "spruce_planks"), box({-0.38f, 0.4f, 0.2f}, {0.38f, 0.85f, 0.3f}, "spruce_planks"),
        box({-0.36f, 0.4f, -0.26f}, {0.36f, 0.44f, 0.2f}, "red_wool")};
    }, ts);
    VoxelLight L = lightAt(w, c.pos + vec3(0, 0.5f, 0));
    mat4 base = RX(RY(T(rel), c.yaw), c.pitch);
    if (c.shakeT > 0) base = RZ(base, std::sin(c.age * 50) * 0.08f);
    float wa = wheel[c.id];
    auto oneCart = [&](float z) {
      mat4 m = T(vec3(0, 0, z));
      draw(body, base * m, L);
      for (int sx : {-1, 1}) for (int sz : {-1, 1}) draw(wheelM, base * m * RX(T(vec3(sx * 0.42f, 0.09f, sz * 0.4f)), wa), L);
    };
    VoxelLight glow = c.fuel > 0 ? VoxelLight{L.sky, 13 / 15.f} : L;
    if (c.kind == "furnace") {
      // the double cart: the furnace in front, a seat behind
      oneCart(-0.68f); oneCart(0.68f);
      drawItem("furnace", base * T(vec3(0, 0.5f, -0.68f)) * RY(mat4(1), PI) * glm::scale(mat4(1), vec3(0.7f)), glow);
      draw(seat, base * T(vec3(0, 0, 0.68f)), L);
    } else {
      oneCart(0);
      std::string blk = !c.cargo.is_null() ? c.cargo.value("key", "stone") : c.blockShown();
      if (!blk.empty()) {
        bool flash = c.fuse > 0 && (int)std::floor(c.age * 5) % 2 == 0;
        drawItem(blk, base * T(vec3(0, 0.5f, 0)) * glm::scale(mat4(1), vec3(0.68f)), glow, flash ? vec4(1, 1, 1, 0.5f) : vec4(0));
      }
    }
    // the couplings
    Model& bar = model("cart_bar", 1.f / 32, [] { return std::vector<Part>{box({-0.035f, -0.035f, -0.5f}, {0.035f, 0.035f, 0.5f}, "chain")}; }, ts);
    for (auto& u : c.links) {
      auto it = byUid.find(u);
      if (it == byUid.end() || it->second->uid < c.uid) continue;
      Minecart* o = it->second;
      float dx = o->pos.x - c.pos.x, dz = o->pos.z - c.pos.z, d = std::hypot(dx, dz);
      mat4 m = RY(T(rel + vec3(dx / 2, (o->pos.y - c.pos.y) / 2 + 0.28f, dz / 2)), std::atan2(dx, dz));
      m = glm::scale(m, vec3(1, 1, std::max(0.05f, d - c.half() - o->half() + 0.12f)));
      draw(bar, m, L);
    }
  }

  // ---------------------------------------------------------------- boats and the ship
  static std::vector<Part> hullParts() {   // boats2.js: the clinker rowboat hull (the sailboat's too, scaled)
    const std::string P = "boat_planks", R = "boat_rail", IN = "planks";
    std::vector<Part> v;
    v.push_back(bb(0, 0.03f, -0.1f, 0.05f, 0.05f, 1.27f, 0, R));
    v.push_back(bb(0, 0.06f, 0.28f, 0.6f, 0.025f, 0.8f, 0, P));
    v.push_back(bb(0, 0.11f, 0.28f, 0.6f, 0.03f, 0.8f, 0, IN));
    v.push_back(bb(0, 0.1f, -0.66f, 0.45f, 0.04f, 0.15f, 0, IN)); v.push_back(bb(0, 0.1f, -0.94f, 0.28f, 0.04f, 0.13f, 0, IN)); v.push_back(bb(0, 0.1f, -1.18f, 0.12f, 0.04f, 0.11f, 0, IN));
    for (int sd : {-1, 1}) {
      v.push_back(bb(sd * 0.645f, 0.32f, 0.28f, 0.045f, 0.25f, 0.82f, 0, P));
      float L = std::hypot(0.6f, 0.9f), yaw = std::atan2(-sd * 0.6f, -0.9f);
      v.push_back(bb(sd * 0.345f, 0.33f, -0.97f, 0.045f, 0.26f, L / 2 + 0.02f, yaw, P));
      v.push_back(bb(sd * 0.645f, 0.585f, 0.28f, 0.08f, 0.03f, 0.84f, 0, R));
      v.push_back(bb(sd * 0.345f, 0.595f, -0.97f, 0.075f, 0.03f, L / 2 + 0.02f, yaw, R));
      for (float z : {-0.3f, 0.3f, 0.85f}) v.push_back(bb(sd * 0.57f, 0.34f, z, 0.03f, 0.2f, 0.035f, 0, R));
      v.push_back(bb(sd * 0.66f, 0.65f, -0.05f, 0.035f, 0.05f, 0.035f, 0, "iron_block"));
    }
    for (float z : {-0.3f, 0.3f, 0.85f}) v.push_back(bb(0, 0.145f, z, 0.55f, 0.012f, 0.035f, 0, R));
    v.push_back(bb(0, 0.32f, 1.125f, 0.69f, 0.25f, 0.045f, 0, P));
    v.push_back(bb(0, 0.585f, 1.125f, 0.72f, 0.03f, 0.075f, 0, R));
    v.push_back(bb(0, 0.35f, -1.44f, 0.06f, 0.31f, 0.065f, 0, R));
    v.push_back(bb(0, 0.33f, 0.25f, 0.6f, 0.03f, 0.12f, 0, IN)); v.push_back(bb(0, 0.33f, -0.55f, 0.52f, 0.03f, 0.11f, 0, IN));
    v.push_back(bb(0, 0.33f, 0.95f, 0.6f, 0.03f, 0.1f, 0, IN));
    return v;
  }
  static std::vector<Part> shipHull() {   // ships.js
    const std::string P = "boat_planks", D = "dark_oak_planks", R = "boat_rail", A = "axle";
    const float hw = SHIP.halfW, dk = SHIP.deck;
    std::vector<Part> v = {box({-1.25f, 0, -3.6f}, {1.25f, 0.2f, 4.3f}, P),
      box({-hw - 0.15f, 0.1f, -2}, {-hw, dk + 0.45f, 4.3f}, P), box({hw, 0.1f, -2}, {hw + 0.15f, dk + 0.45f, 4.3f}, P),
      box({-hw - 0.15f, dk + 0.45f, -2}, {-hw + 0.05f, dk + 0.58f, 4.3f}, R), box({hw - 0.05f, dk + 0.45f, -2}, {hw + 0.15f, dk + 0.58f, 4.3f}, R),
      box({-hw - 0.15f, 0.1f, 4.3f}, {hw + 0.15f, dk + 1, 4.5f}, D), box({-hw - 0.15f, dk + 1, 4.28f}, {hw + 0.15f, dk + 1.12f, 4.52f}, R),
      box({-hw + 0.1f, dk - 0.15f, -2}, {hw - 0.1f, dk, SHIP.stern}, P)};
    const float z0 = SHIP.bow - 0.25f, z1 = -2.0f, step = (z1 - z0) / 14;
    for (int i = 0; i < 14; i++) {
      float za = z0 + i * step, zb = za + step + 0.02f, zc = (za + zb) / 2;
      float half = std::max(0.16f, shipHalfAt(std::max(SHIP.bow + 0.05f, zc))) + 0.12f, t = 1 - i / 14.f;
      float bottom = 0.05f + t * 0.55f, rail = dk + 0.5f + t * 0.35f;
      v.push_back(box({-half - 0.03f, bottom, za}, {half + 0.03f, dk, zb}, P));
      v.push_back(box({-half - 0.03f, dk, za}, {-half + 0.12f, rail, zb}, P)); v.push_back(box({half - 0.12f, dk, za}, {half + 0.03f, rail, zb}, P));
      v.push_back(box({-half - 0.05f, rail, za}, {-half + 0.14f, rail + 0.1f, zb}, R)); v.push_back(box({half - 0.14f, rail, za}, {half + 0.05f, rail + 0.1f, zb}, R));
    }
    v.push_back(box({-0.2f, 0.55f, SHIP.bow - 0.45f}, {0.2f, dk + 0.95f, SHIP.bow - 0.15f}, P));
    v.push_back(box({-0.07f, dk + 0.7f, SHIP.bow - 2.6f}, {0.07f, dk + 0.84f, SHIP.bow - 0.2f}, A));
    v.push_back(box({-0.08f, dk, SHIP.helm.y - 0.08f}, {0.08f, dk + 0.95f, SHIP.helm.y + 0.08f}, D));
    v.push_back(box({-0.42f, dk + 0.55f, SHIP.helm.y - 0.12f}, {0.42f, dk + 0.62f, SHIP.helm.y - 0.06f}, D));
    v.push_back(box({-0.04f, dk + 0.18f, SHIP.helm.y - 0.12f}, {0.04f, dk + 1, SHIP.helm.y - 0.06f}, D));
    v.push_back(box({-0.6f, dk, 0.6f}, {0.6f, dk + 0.12f, 1.8f}, D));
    return v;
  }
  void drawBoat(GameSession& s, World& w, Boat& b, vec3 rel) {
    const TileSet& ts = s.app.tiles;
    Wind& wind = windOf(*s.level);
    float sc = b.scale();
    if (b.ship()) {
      const float dk = SHIP.deck;
      Model& hull = model("ship_hull", 1.f / 16, shipHull, ts);
      Model& mast = model("ship_mast", 1.f / 16, [dk] {
        return std::vector<Part>{box({-0.13f, dk, -0.62f}, {0.13f, dk + 7.6f, -0.36f}, "axle"), box({-0.55f, dk + 5.6f, -0.99f}, {0.55f, dk + 5.75f, 0.01f}, "dark_oak_planks"),
          box({-0.55f, dk + 5.75f, -0.99f}, {-0.45f, dk + 6.1f, 0.01f}, "dark_oak_planks"), box({0.45f, dk + 5.75f, -0.99f}, {0.55f, dk + 6.1f, 0.01f}, "dark_oak_planks")};
      }, ts);
      Model& yard = model("ship_yard", 1.f / 16, [] { return std::vector<Part>{box({-2.4f, -0.07f, -0.07f}, {2.4f, 0.07f, 0.07f}, "axle")}; }, ts);
      Model& sail = model("ship_sail", 1.f / 16, [] { return std::vector<Part>{box({-2.2f, -1, -0.02f}, {2.2f, 0, 0.02f}, "sail")}; }, ts);
      Model& flag = model("ship_flag", 1.f / 32, [] { return std::vector<Part>{box({-0.01f, 0, 0}, {0.01f, 0.35f, 0.9f}, "flag")}; }, ts);
      VoxelLight L = lightAt(w, b.pos + vec3(0, dk + 0.5f, 0));
      mat4 base = RX(RZ(RY(T(rel), b.yaw), b.heel * 0.5f + (b.shakeT > 0 ? std::sin(b.age * 60) * 0.03f : 0)), std::sin(b.bob * 1.1f) * 0.012f);
      draw(hull, base, L); draw(mast, base, L);
      // the yard swings to the wind; the sail drops as it's let out and bellies with the wind
      mat4 ym = RY(base * T(vec3(0, dk + 7.1f, -0.75f)), b.boom * 0.45f);
      draw(yard, ym, L);
      float set = b.sail, fill = set * (0.3f + b.eff * 0.7f);
      for (int k = 0; k < 3; k++) {
        mat4 sm = ym * T(vec3(0, -0.05f - k * 1.55f * (0.3f + set * 0.7f), -std::sin((k + 0.5f) / 3 * PI) * fill * 0.6f));
        draw(sail, glm::scale(sm, vec3(1 - k * 0.05f, 1.6f * (0.3f + set * 0.7f), 1)), L);
      }
      draw(flag, RY(T(rel + vec3(0, dk + 7.6f, 0)), wind.dir + PI + std::sin(b.age * 7) * 0.12f), L);
      if (!b.cargo.is_null() || b.items.is_array()) drawItem(!b.cargo.is_null() ? b.cargo.value("key", "chest") : (b.cargoKey.empty() ? "chest" : b.cargoKey), base * T(vec3(SHIP.cargo.x, dk + 0.5f, SHIP.cargo.y)), L);
      return;
    }
    Model& hull = model("boat_hull", 1.f / 32, hullParts, ts);
    VoxelLight L = lightAt(w, b.pos + vec3(0, 0.8f, 0));
    mat4 S = glm::scale(T(rel), vec3(sc));
    mat4 base = RX(RZ(RY(S, b.yaw), b.heel + (b.shakeT > 0 ? std::sin(b.age * 60) * 0.06f : 0)), std::sin(b.bob * 1.3f) * 0.025f + clampv(b.speed * 0.006f, -0.03f, 0.05f));
    draw(hull, base, L);
    if (b.kind == "sailboat") {
      Model& mast = model("boat_mast", 1.f / 32, [] { return std::vector<Part>{box({-0.06f, 0.15f, -0.06f}, {0.06f, 3.2f, 0.06f}, "axle"), box({-0.1f, 0.15f, -0.1f}, {0.1f, 0.4f, 0.1f}, "axle_end")}; }, ts);
      Model& boom = model("boat_boom", 1.f / 32, [] { return std::vector<Part>{box({-0.04f, 0.85f, 0}, {0.04f, 0.93f, 1.5f}, "axle")}; }, ts);
      Model& sail = model("boat_sail", 1.f / 32, [] { return std::vector<Part>{box({-0.015f, 0.93f, 0}, {0.015f, 3.0f, 0.48f}, "sail")}; }, ts);
      Model& flag = model("boat_flag", 1.f / 32, [] { return std::vector<Part>{box({-0.01f, 0, 0}, {0.01f, 0.2f, 0.55f}, "flag")}; }, ts);
      draw(mast, base, L);
      mat4 bm = RY(base * T(vec3(0, 0, 0.05f)), b.boom);
      draw(boom, bm, L);
      float fill = b.sail * (0.3f + b.eff * 0.7f);
      for (int k = 0; k < 3; k++) {
        float belly = std::sin((k + 0.5f) / 3 * PI) * fill * 0.28f * (b.boom < 0 ? -1.f : 1.f);
        draw(sail, glm::scale(bm * T(vec3(-belly, 0, 0.12f + k * 0.48f)), vec3(1, 0.25f + b.sail * 0.75f, 1)), L);
      }
      draw(flag, RY(glm::scale(T(rel), vec3(sc)) * T(vec3(0, 3.15f, 0)), wind.dir + PI + std::sin(b.age * 7) * 0.12f), L);
    } else {
      // the oars sit in their oarlocks and dip while rowing
      Model& oar = model("boat_oar", 1.f / 32, [] {
        return std::vector<Part>{bb(0, 0, -0.25f, 0.028f, 0.028f, 0.65f, 0, "axle"), bb(0, 0, 0.42f, 0.035f, 0.035f, 0.07f, 0, "boat_rail"), bb(0, 0, -1.1f, 0.012f, 0.11f, 0.22f, 0, "boat_planks")};
      }, ts);
      float row = b.riders[0] && std::fabs(b.speed) > 0.3f ? b.age * 6 : 0;
      for (int sd : {-1, 1}) {
        mat4 m = RZ(RY(T(rel), b.yaw), b.heel) * T(vec3(sd * 0.68f, 0.66f, -0.05f));
        m = RX(RY(m, sd * (1.2f + std::sin(row) * 0.45f) + PI), -(0.37f + std::cos(row) * 0.18f));
        draw(oar, m, L);
      }
    }
    if (!b.cargo.is_null() || b.items.is_array()) {
      vec3 sp = b.seatPos(1) - b.pos;
      drawItem(!b.cargo.is_null() ? b.cargo.value("key", "chest") : (b.cargoKey.empty() ? "chest" : b.cargoKey),
               glm::scale(RY(T(rel + sp + vec3(0, 0.28f, 0)), b.yaw), vec3(0.62f)), L);
    }
  }

  // ---------------------------------------------------------------- pistons on the move
  void drawPiston(GameSession& s, World& w, PistonAnim& a, const FrameCtx& f) {
    const TileSet& ts = s.app.tiles;
    float k = a.progress();
    BlockId head = rs::T().head;
    for (auto* list : {&a.pieces, &a.extra}) for (auto& p : *list) {
      vec3 at = glm::mix(vec3(p.from), vec3(p.to), k) + vec3(0.5f);
      vec3 rel = f.cam.rel(glm::dvec3(at));
      VoxelLight L = lightAt(w, at);
      if (p.id == head) {
        bool sticky = p.meta & 8;
        Model& hm = model(sticky ? "piston_head_s" : "piston_head", 1.f / 32, [sticky] {
          Part plate = box({-0.5f, 0.25f, -0.5f}, {0.5f, 0.5f, 0.5f}, "piston_side");
          plate.tile[2] = sticky ? "piston_top_sticky" : "piston_top"; plate.tile[3] = "piston_top";
          return std::vector<Part>{plate, box({-0.125f, -0.75f, -0.125f}, {0.125f, 0.25f, 0.125f}, "piston_side")};
        }, ts);
        // +y onto the head's six-way direction (0 down 1 up 2 north 3 south 4 west 5 east)
        static const vec3 AX[6] = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, 1}};
        static const float ANG[6] = {PI, 0, -PI / 2, PI / 2, PI / 2, -PI / 2};
        int d = (p.meta & 7) % 6;
        draw(hm, glm::rotate(T(rel), ANG[d], AX[d]), L);
      } else drawItem(reg().block(p.id).key, T(rel), L);
    }
  }

  // ---------------------------------------------------------------- windmills, waterwheels, axles, machines
  void drawMech(GameSession& s, World& w, const FrameCtx& f) {
    Mechanics* mech = mechanicsOf(*s.level, w.realm);
    if (!mech) return;
    const TileSet& ts = s.app.tiles;
    Model& axle = model("m_axle", 1.f / 32, [] { return std::vector<Part>{boxEnds({-0.13f, -0.13f, -0.5f}, {0.13f, 0.13f, 0.5f}, "axle", "axle_end", 2)}; }, ts);
    Model& sails = model("m_windmill", 1.f / 16, [] {
      std::vector<Part> v{box({-0.22f, -0.22f, -0.32f}, {0.22f, 0.22f, 0.25f}, "axle_end")};
      for (int k = 0; k < 4; k++) { v.push_back(turned(box({-0.07f, 0.2f, -0.05f}, {0.07f, 4.2f, 0.05f}, "axle"), k * PI / 2, {0, 0, 1})); v.push_back(turned(box({0.07f, 0.9f, -0.02f}, {1.05f, 4.1f, 0.02f}, "sail"), k * PI / 2, {0, 0, 1})); }
      return v;
    }, ts);
    Model& wheelM = model("m_waterwheel", 1.f / 32, [] {
      std::vector<Part> v{box({-0.25f, -0.25f, -0.45f}, {0.25f, 0.25f, 0.45f}, "axle_end")};
      for (int k = 0; k < 8; k++) for (Part p : {box({-0.06f, 0.2f, -0.06f}, {0.06f, 1.35f, 0.06f}, "axle"), box({-0.32f, 1.25f, -0.42f}, {0.32f, 1.5f, 0.42f}, "planks"),
                                                 box({-0.5f, 1.32f, -0.45f}, {0.5f, 1.42f, -0.37f}, "axle"), box({-0.5f, 1.32f, 0.37f}, {0.5f, 1.42f, 0.45f}, "axle")})
        v.push_back(turned(p, k * PI / 4, {0, 0, 1}));
      return v;
    }, ts);
    Model& runner = model("m_runner", 1.f / 32, [] { Part p = box({-0.4f, 0, -0.4f}, {0.4f, 0.22f, 0.4f}, "millstone_side"); p.tile[2] = p.tile[3] = "millstone_top"; return std::vector<Part>{p}; }, ts);
    Model& blade = model("m_blade", 1.f / 32, [] { return std::vector<Part>{box({-0.42f, -0.02f, -0.42f}, {0.42f, 0.02f, 0.42f}, "saw_blade")}; }, ts);
    Model& slat = model("m_slat", 1.f / 32, [] { return std::vector<Part>{box({-0.44f, 0, -0.06f}, {0.44f, 0.05f, 0.06f}, "conveyor_side")}; }, ts);
    Model& roller = model("m_roller", 1.f / 32, [] { return std::vector<Part>{box({-0.36f, -0.1f, -0.1f}, {0.36f, 0.1f, 0.1f}, "axle_end"), box({-0.3f, -0.13f, -0.03f}, {0.3f, 0.13f, 0.03f}, "axle")}; }, ts);
    Model& arm = model("m_arm", 1.f / 32, [] { return std::vector<Part>{box({-0.06f, 0, -0.55f}, {0.06f, 0.08f, 0.55f}, "axle"), box({-0.12f, -0.02f, 0.45f}, {0.12f, 0.2f, 0.58f}, "axle_end")}; }, ts);
    static const int DIR4[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (auto& [k, n] : mech->nodes()) {
      vec3 rel = f.cam.rel(glm::dvec3(vec3(n.p)));
      if (std::fabs(rel.x) > 64 || std::fabs(rel.z) > 64 || !f.cam.frustum.sphere(rel + vec3(0.5f), n.kind == "windmill" ? 4.5f : 1.8f)) continue;
      float ang = mech->angleOf(n);
      bool solid = n.kind != "axle" && n.kind != "windmill" && n.kind != "waterwheel" && n.kind != "belt";
      VoxelLight L = lightAt(w, vec3(n.p) + vec3(0.5f, solid ? 1.5f : 0.5f, 0.5f));
      mat4 base = T(rel + vec3(0.5f));
      if (n.axis == 1) base = RY(base, PI / 2); else if (n.axis == 0) base = RX(base, PI / 2);
      if (n.kind == "axle") draw(axle, RZ(base, ang), L);
      else if (n.kind == "windmill") draw(sails, RZ(base, ang), L);
      else if (n.kind == "waterwheel") draw(wheelM, RZ(base, -ang), L);
      else if (n.kind == "millstone") draw(runner, RY(T(rel + vec3(0.5f, 1, 0.5f)), ang * 0.6f), L);
      else if (n.kind == "sawmill") draw(blade, RY(T(rel + vec3(0.5f, 1.02f, 0.5f)), ang * 3), L);
      else if (n.kind == "belt") {
        int d = w.getMeta(n.p.x, n.p.y, n.p.z) & 3, dx = DIR4[d][0], dz = DIR4[d][1];
        float ph = std::fmod(ang * 0.45f, 1.f);
        for (int i = 0; i < 3; i++) {
          float t = std::fmod(ph + i / 3.f, 1.f) - 0.5f;
          mat4 m = T(rel + vec3(0.5f + dx * t, 0.06f, 0.5f + dz * t));
          draw(slat, dx ? RY(m, PI / 2) : m, L);
        }
      } else if (n.kind == "crusher") {
        for (int sd : {-1, 1}) draw(roller, RX(T(rel + vec3(0.5f, 1.08f, 0.5f + sd * 0.2f)), ang * 2 * sd), L);
      } else if (n.kind == "harvester") {
        for (int i = 0; i < 2; i++) draw(arm, RY(T(rel + vec3(0.5f, 1.0f, 0.5f)), ang * 0.7f + i * PI / 2), L);
      }
    }
  }
};

void registerVehicleLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession& s) { return std::make_shared<VehicleLayer>(s); });
}

} // namespace dq
