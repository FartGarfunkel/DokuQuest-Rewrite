// render/station_work_layer.cpp — what lies on the grindstone and the stonecutter (sim/station_work.h), and their scenes
// (ui/cinematic.h): the two pieces lifted to the wheel and stroked on it, the stone slid into the saw and the pieces out
// the other side, the shapes it cuts into on a dial over the blade (Cinematic Interactions' GrindstoneStation /
// StonecutterStation)
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/cinematic.h"
#include "sim/station_work.h"
#include "core/tool_parts.h"
#include "core/item_glint.h"
#include "core/registry.h"
#include "render/content_draw.h"
#include "render/tile_box.h"
#include "render/mesher.h"
#include "render/map_render.h"
#include "sim/maps.h"

namespace dq {
std::string stackModelKey(const ItemStack& s);   // render/pile_layer.cpp

namespace {

double win(double t, double a, double b) { return clampv((t - a) / (b - a), 0.0, 1.0); }
double smoother(double x) { x = clampv(x, 0.0, 1.0); return x * x * x * (x * (x * 6 - 15) + 10); }

// an item lying flat (or a block standing) at p, turned yaw about +y; size: a flat item's width (a block 0.6 of it)
void drawLying(VoxelRenderer& VR, const FrameCtx& f, const ItemStack& s, vec3 p, float yaw, float size, VoxelLight L) {
  if (s.empty()) return;
  const VoxelModel& im = itemModel(stackModelKey(s));
  if (!im.ready()) return;
  mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
  m = glm::rotate(m, yaw, vec3(0, 1, 0));
  if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.3f * size, 0)), vec3(0.6f * size));
  else if (im.stand) m = glm::scale(glm::translate(m, vec3(0, 0.36f * size, 0)), vec3(0.9f * size));
  else { m = glm::translate(m, vec3(0, (im.depth > 0 ? im.depth : 0.03f) * size + 0.004f, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(size)); }
  VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(s) ? 1.f : 0.f);
}

// an item upright facing you (a shape on the dial), turning slowly
void drawFloating(VoxelRenderer& VR, const FrameCtx& f, const ItemStack& s, vec3 p, float yaw, float size, VoxelLight L) {
  if (s.empty() || size <= 0.001f) return;
  const VoxelModel& im = itemModel(stackModelKey(s));
  if (!im.ready()) return;
  mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
  m = glm::rotate(m, yaw, vec3(0, 1, 0));
  m = glm::scale(m, vec3(im.kind == VoxelModel::Block ? size * 0.62f : size));
  VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(s) ? 1.f : 0.f);
}

// the stone on the stonecutter's table: one block, two side by side (the second further from the saw), a third on them
void drawCutHeap(VoxelRenderer& VR, const FrameCtx& f, const ItemStack& st, vec3 lie, vec3 right, vec3 out, float yaw, VoxelLight L) {
  static const vec3 AT[3] = {{0, 0, 0}, {-0.15f, 0, 0.03f}, {-0.075f, 0.145f, 0.015f}};   // (right, up, out)
  for (int i = 0; i < std::min(st.n, 3); i++) {
    ItemStack one = st; one.n = 1;
    drawLying(VR, f, one, lie + right * AT[i].x + vec3(0, AT[i].y, 0) + out * AT[i].z, yaw + (i == 2 ? 0.3f : i * 0.12f), 0.24f, L);
  }
}

class StationWorkLayer : public RenderLayer {
  struct Sheet { const PaperMap* map; mat4 M; float S; vec2 light; };
  std::vector<Sheet> sheets;   // maps lying on the cartography table: drawn after the voxels, with the content shader
public:
  const char* name() const override { return "station-work"; }
  int order() const override { return 111; }
  // the grindstone at work: left out of the chunk mesh while its scene is open and drawn here, its wheel turning on its
  // axle as the pieces are held to it (spun up while grinding, running down after)
  ivec3 grindAt{0}; bool grindHidden = false; float wheelAng = 0, wheelSpd = 0; double lastT = -1;
  void grindstone(GameSession& s, const FrameCtx& f, const PairView* pv) {
    World& w = s.world();
    const bool on = pv && pv->kind == 6 && reg().block(w.getBlock(pv->at.x, pv->at.y, pv->at.z)).key == "grindstone";
    if (grindHidden && (!on || pv->at != grindAt)) {
      auto& H = meshHiddenBlocks();
      for (size_t i = 0; i < H.size(); i++) if (H[i] == grindAt) { H.erase(H.begin() + i); break; }
      w.markBlockDirty(grindAt.x, grindAt.y, grindAt.z, true); grindHidden = false;
    }
    if (!on) { wheelSpd = 0; lastT = -1; return; }
    if (!grindHidden) { grindAt = pv->at; meshHiddenBlocks().push_back(grindAt); w.markBlockDirty(grindAt.x, grindAt.y, grindAt.z, true); grindHidden = true; }
    const float dt = lastT < 0 ? 0.f : (float)std::clamp(f.time - lastT, 0.0, 0.1); lastT = f.time;
    const bool grinding = pv->t >= 0.28 && pv->t < pairBeats(6).letGo;
    wheelSpd += ((grinding ? 14.f : 0.f) - wheelSpd) * std::min(1.f, dt * (grinding ? 3.f : 1.2f));
    wheelAng += wheelSpd * dt;
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    const GLuint round = tileTexture(s.app, "grindstone_round"), disc = tileTexture(s.app, "grindstone_side"),
                 leg = tileTexture(s.app, "grindstone_leg"), piv = tileTexture(s.app, "grindstone_pivot");
    const u8 l = w.getLight(grindAt.x, grindAt.y, grindAt.z);
    const vec2 light((l >> 4) / 15.f, (l & 15) / 15.f);
    const int m = w.getMeta(grindAt.x, grindAt.y, grindAt.z) & 3;
    mat4 M = glm::translate(mat4(1), f.cam.rel(glm::dvec3(grindAt)) + vec3(0.5f, 0, 0.5f));
    M = glm::rotate(M, -m * PI / 2, vec3(0, 1, 0));
    M = glm::translate(M, vec3(-0.5f, 0, -0.5f));
    drawTileBox(cd, M, vec3(0, 0, 6), vec3(2, 13, 10), leg, leg, leg, light);
    drawTileBox(cd, M, vec3(14, 0, 6), vec3(16, 13, 10), leg, leg, leg, light);
    drawTileBox(cd, M, vec3(2, 7, 6), vec3(4, 13, 10), piv, piv, piv, light);
    drawTileBox(cd, M, vec3(12, 7, 6), vec3(14, 13, 10), piv, piv, piv, light);
    // the wheel, its axle along x: drawn with the axle as the box's own y (its ends the stone's faces), turned on it
    mat4 W = glm::translate(M, vec3(8, 10, 8) / 16.f);
    W = glm::rotate(W, -PI / 2, vec3(0, 0, 1));
    W = glm::rotate(W, wheelAng, vec3(0, 1, 0));
    drawTileBox(cd, W, vec3(-6, -4, -6), vec3(6, 4, 6), round, disc, disc, light);
    cd.end();
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    { PairView gv; const bool on = cinematicPairView(gv); grindstone(s, f, on ? &gv : nullptr); }
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    auto begin = [&] { if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; } };
    PairView pv; CutView cv; JukeView jv;
    FireView fv;
    InscribeView insV; const bool inscribeOn = cinematicInscribeView(insV);
    const bool pairOn = cinematicPairView(pv), cutOn = cinematicCutView(cv), jukeOn = cinematicJukeView(jv), fireOn = cinematicFireView(fv);
    // a record standing up (in a jukebox's slot, on the dial): centred at c, facing along yaw, turned spin about its face
    auto record = [&](const std::string& id, vec3 c, float yaw, float spin, float size, VoxelLight L) {
      const VoxelModel& im = itemModel(id);
      if (!im.ready() || size <= 0.001f) return;
      begin();
      mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(c)));
      m = glm::rotate(m, yaw, vec3(0, 1, 0)); m = glm::rotate(m, spin, vec3(0, 0, 1));
      m = glm::scale(m, vec3(size));   // (item models are centred on their origin)
      VR.draw(im.gl, m * im.matrix(), L);
    };
    const float t = (float)f.time;
    // one thing laid flat at p: a map with its picture as a sheet, anything else its voxel model
    auto lay = [&](const ItemStack& st, vec3 p, float yaw, float size, VoxelLight L, vec4 tint = vec4(0)) {
      if (st.empty()) return;
      if (isMapStack(st) && !stackMapId(st).empty()) if (const PaperMap* pm = mapStore(*s.level).get(stackMapId(st))) {
        mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p + vec3(0, 0.004f, 0))));
        m = glm::rotate(m, yaw, vec3(0, 1, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0));
        sheets.push_back({pm, m, size * 0.89f, vec2(L.sky, L.block)});
        return;
      }
      begin();
      const VoxelModel& im = itemModel(stackModelKey(st));
      if (!im.ready()) return;
      mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
      m = glm::rotate(m, yaw, vec3(0, 1, 0));
      if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.3f * size, 0)), vec3(0.6f * size));
      else if (im.stand) m = glm::scale(glm::translate(m, vec3(0, 0.36f * size, 0)), vec3(0.9f * size));
      else { m = glm::translate(m, vec3(0, (im.depth > 0 ? im.depth : 0.03f) * size + 0.004f, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(size)); }
      VR.draw(im.gl, m * im.matrix(), L, tint, itemGlints(st) ? 1.f : 0.f);
    };
    // a stack lying: up to three of it, a little heap (blocks: two side by side, the third on them; a map: the top sheet)
    auto heap = [&](const ItemStack& st, vec3 p, float yaw, float size, VoxelLight L, vec3 right) {
      if (st.empty()) return;
      const int n = isMapStack(st) ? 1 : std::min(st.n, 3);
      ItemStack one = st; one.n = 1;
      const bool block = itemModel(stackModelKey(one)).kind == VoxelModel::Block;
      const float bs = 0.6f * size;
      for (int i = 0; i < n; i++) {
        const vec3 o = block ? (i == 0 ? vec3(0) : i == 1 ? -right * (bs * 1.05f) : -right * (bs * 0.52f) + vec3(0, bs, 0))
                             : right * (i == 1 ? 0.05f : i == 2 ? -0.03f : 0.f) + vec3(0, i * 0.03f, 0);
        lay(one, p + o, yaw + (block ? i * 0.12f : i * 0.5f), size, L);
      }
    };
    forBlockEntities(w, f, 32.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const std::string type = e.value("type", "");
      const bool cutwork = type == "cutwork";
      if (type == "campfire") {   // the food on a campfire's four spots, browning as it cooks (sim/camping.cpp)
        const vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
        if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.f)) return;
        const ivec3 at(x, y, z);
        const u8 l = w.getLight(x, y, z);
        const VoxelLight L{(l >> 4) / 15.f, std::max(l & 15, 10) / 15.f};   // (lit by the fire under it)
        const std::vector<ItemStack> food = campfireFood(w, at);
        for (int k = 0; k < 4; k++) {
          if (food[k].empty()) continue;
          const float done = campfireCooked(w, at, k), lift = fireOn && fv.at == at && fv.hover == k ? 0.02f + 0.006f * std::sin(t * 4.f) : 0.f;
          lay(food[k], campfireSpot(at, k) + vec3(0, lift, 0), k * PI / 2 + 0.4f, 0.3f, L, vec4(0.22f, 0.1f, 0.03f, 0.6f * done));
        }
        return;
      }
      if (type == "inscribework") {   // the inscription table's board: book, scroll, paper, lapis (and its scene's work, ui/cinematic_inscribe.cpp)
        const vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
        if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) return;
        const ivec3 at(x, y, z);
        const u8 l = w.getLight(x, y + 1, z);
        const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
        vec3 out, right; stationWorkFrame(w, at, out, right);
        const float yaw = std::atan2(out.x, out.z);
        const std::vector<ItemStack> v = inscribeWorkItems(w, at);
        InscribeView iv; const bool on = inscribeOn && insV.at == at; if (on) iv = insV;
        const bool work = on && iv.t >= 0 && iv.t < INSCRIBE_LENGTH;
        const bool madeYet = work && iv.t >= INSCRIBE_CONTACT;
        static const float SZ[4] = {0.36f, 0.26f, 0.24f, 0.18f};
        vec3 lie[4]; for (int k = 0; k < 4; k++) lie[k] = inscribeLiePos(w, at, k);
        for (int k = 0; k < 4; k++) {
          ItemStack st = work && !madeYet && k < 2 ? iv.before[k] : v[k];
          if (work && iv.mode == 0 && k == 1 && !madeYet) continue;   // (the scroll's in the hand, below)
          const float lift = on && iv.hover == k && !work ? 0.02f + 0.006f * std::sin(t * 4.f) : 0.f;
          // the book glowing as a spell goes in or comes out; the paper as it's written on
          const float glow = !work ? 0.f : (k == 0 && iv.mode < 2) || (k == 2 && iv.mode == 2) ? (float)std::sin(std::clamp(iv.t / INSCRIBE_LENGTH, 0.0, 1.0) * PI) : 0.f;
          if (glow > 0.01f) { lay(st, lie[k] + vec3(0, lift, 0), yaw + (k ? 0.3f : 0.f), SZ[k], VoxelLight{L.sky, std::max(L.block, glow)}, vec4(0.45f, 0.6f, 1.f, 0.6f * glow)); continue; }
          heap(st, lie[k] + vec3(0, lift, 0), yaw + (k ? 0.3f : 0.f), SZ[k], L, right);
        }
        if (work && iv.mode == 0 && !madeYet) { ItemStack sc = iv.before[1]; sc.n = 1; lay(sc, inscribeScrollPos(iv, lie[0], lie[1]), yaw + 0.3f, SZ[1], L); }
        if (madeYet && iv.mode > 0) lay(iv.made, inscribeRisePos(iv, iv.mode == 1 ? lie[0] : lie[2]), yaw + 0.3f, SZ[1] * (1 - 0.5f * (float)((iv.t - INSCRIBE_CONTACT) / (INSCRIBE_LENGTH - INSCRIBE_CONTACT))), VoxelLight{L.sky, std::max(L.block, 0.5f)});
        return;
      }
      if (type == "jukebox") {   // the record playing: standing in the slot, half out, turning slowly
        if (jukeOn && jv.at == ivec3(x, y, z)) return;
        const vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
        if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f) || !e.contains("disc") || !e["disc"].is_string()) return;
        const u8 l = w.getLight(x, y + 1, z);
        record(e["disc"].get<std::string>(), vec3(x, y, z) + vec3(0.5f, 1.06f, 0.5f), 0, -t * 1.6f, 0.5f, VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f});
        return;
      }
      if (!cutwork && type != "grindwork" && type != "smithwork" && type != "loomwork" && type != "fletchwork" && type != "mapwork" && type != "forgework") return;
      const ivec3 at(x, y, z);
      if ((pairOn && pv.at == at) || (cutOn && cv.at == at)) return;   // (the scene draws it, below)
      const vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) return;
      const u8 l = cutwork || type == "grindwork" ? w.getLight(x, y, z) : w.getLight(x, y + 1, z);   // (a full block holds no light: the air over it)
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      vec3 out, right; stationWorkFrame(w, at, out, right);
      const float yaw = std::atan2(out.x, out.z);
      if (cutwork) { begin(); drawCutHeap(VR, f, cutWorkItem(w, at), cutLiePos(w, at), right, out, yaw, L); return; }
      const std::string st = pairStationAt(w, at);
      const std::vector<ItemStack> v = pairWorkItems(w, at);
      for (int k = 0; k < 2; k++) heap(v[k], pairLiePos(w, at, k), yaw + (k ? -0.35f : 0.35f) * (st == "cartography" && k == 0 ? 0.f : 1.f), pairLieSize(st, k), L, right);
    });
    if (pairOn && !pairStationAt(w, pv.at).empty()) { begin(); drawPairScene(w, f, pv, t, lay, heap); }
    if (jukeOn && reg().block(w.getBlock(jv.at.x, jv.at.y, jv.at.z)).has("jukebox")) {   // the jukebox's scene: the dial of records, the one moving
      const u8 l = w.getLight(jv.at.x, jv.at.y + 1, jv.at.z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f}, Lb{L.sky, std::max(L.block, 0.45f)};
      const int n = (int)jv.dial.size();
      const std::string playing = jukeboxDisc(w, jv.at);
      if (jv.t < 0 && !playing.empty()) record(playing, vec3(jv.at) + vec3(0.5f, 1.06f, 0.5f), 0, -t * 1.6f, 0.5f, L);
      for (int k = 0; k < n; k++) {
        if (jv.t >= 0 && !jv.eject && k == jv.hover) continue;   // (that one's on its way in)
        if (!playing.empty() && jv.t < 0) break;                  // (one playing: the dial put away)
        const float shown = cutDialShown(k, n, jv.scroll) * (jv.t >= 0 ? 0.6f : 1.f);
        const float size = 0.26f * shown * (jv.hover == k && jv.t < 0 ? 1.25f : 1.f);
        record(jv.dial[k], jukeDialPoint(jv.at, jv.viewYaw, k, n, jv.scroll, t), jv.viewYaw, jv.hover == k ? -t * 2.f : 0.f, size, Lb);
      }
      if (jv.t >= 0 && !jv.disc.empty()) {
        const vec3 from = jv.hover >= 0 && jv.hover < n ? jukeDialPoint(jv.at, jv.viewYaw, jv.hover, n, jv.scroll, t) : vec3(jv.at) + vec3(0.5f, 1.5f, 0.5f);
        const vec3 c = jukeDiscPos(jv, from);
        const float turn = (float)smoother(win(jv.t, 0.2, 0.6));   // (turned from facing you to along the slot)
        const float yaw = jv.eject ? jv.viewYaw * (float)smoother(win(jv.t, 0.4, 0.8)) : jv.viewYaw * (1 - turn);
        const float size = jv.eject ? 0.5f - 0.3f * (float)win(jv.t, 0.4, JUKE_EJECT) : 0.26f + 0.24f * turn;
        record(jv.disc, c, yaw, -t * 3.f, size, L);
      }
    }
    if (cutOn && w.getBlock(cv.at.x, cv.at.y, cv.at.z)) {
      begin();
      const ivec3 at = cv.at;
      const u8 l = w.getLight(at.x, at.y, at.z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      vec3 out, right; stationWorkFrame(w, at, out, right);
      const float yaw = std::atan2(out.x, out.z);
      const vec3 lie = cutLiePos(w, at);
      const vec3 blade = vec3(at) + vec3(0.5f, 0.5625f, 0.5f) - right * 0.25f;   // (the saw's plane, just as far along it)
      const ItemStack stone = cv.t >= 0 ? cv.input : cutWorkItem(w, at);
      if (cv.t < 0) {
        drawCutHeap(VR, f, stone, lie, right, out, yaw, L);
        // the dial: what it cuts into, on a half ring over the saw
        const auto opts = cutWorkOptions(w, at);
        const int n = (int)opts.size();
        for (int k = 0; k < n; k++) {
          const float shown = cutDialShown(k, n, cv.scroll);
          if (shown <= 0.01f) continue;
          const float size = 0.19f * shown * (cv.hover == k ? 1.3f : 1.f);
          const float spin = cv.hover == k ? t * 1.6f : 0.25f * std::sin(t * 0.7f + k);
          drawFloating(VR, f, ItemStack(opts[k].first, 1), cutDialPoint(at, cv.viewYaw, k, n, cv.scroll, t), cv.viewYaw + spin, size, VoxelLight{L.sky, std::max(L.block, 0.45f)});
        }
      } else {
        // the cut: a block (all of them, cutting all) slides from where it lay into the saw by CUT_BITE; the pieces come out
        // toward you on the far side, settle, then rise off to the bag
        const float len = (float)(cv.all ? CUT_ALL_LENGTH : CUT_LENGTH);
        const float slide = (float)(win(cv.t, 0.0, CUT_BITE));
        if (cv.t < CUT_BITE) {
          ItemStack one = stone; one.n = 1;
          const float judder = 0.006f * std::sin((float)cv.t * 90.f) * slide;
          drawLying(VR, f, one, lie + (blade - lie) * (float)(slide * slide) + right * judder, yaw, 0.24f, L);
        }
        if (cv.t >= CUT_BITE) {
          const float u = (float)win(cv.t, CUT_BITE, len);
          const float rise = (float)win(cv.t, len - 0.35f, len);
          const int pieces = std::clamp(cv.piece.n, 1, 4);
          for (int i = 0; i < pieces; i++) {
            ItemStack one = cv.piece; one.n = 1;
            const vec3 p = blade + right * (0.16f + 0.12f * std::min(1.f, u * 3.f) + i * 0.07f) + out * ((i - (pieces - 1) * 0.5f) * 0.14f * std::min(1.f, u * 3.f))
                         + vec3(0, 0.25f * rise, 0) + vec3(std::sin(cv.viewYaw), 0, std::cos(cv.viewYaw)) * (0.4f * rise);
            drawLying(VR, f, one, p, yaw + i * 0.4f, 0.18f * (1 - 0.5f * rise), L);
          }
          if (stone.n > 1 && !cv.all) {   // (what's left stays on the table)
            ItemStack rest = stone; rest.n -= 1;
            drawCutHeap(VR, f, rest, lie, right, out, yaw, L);
          }
        }
      }
    }
    if (begun) VR.end();
    if (!sheets.empty()) {
      ContentDraw& cd = contentDraw();
      if (cd.begin(f, f.cam.viewProj)) { for (auto& sh : sheets) drawMapSheet(cd, *s.level, sh.map, s.player, sh.M, sh.S, sh.light); cd.end(); }
      sheets.clear();
    }
  }

  // the work at a pair station, as the scene has it (pairBeats): before contact the pieces taken up and carried; then
  // what was made, where the work was, till it's lifted off toward you (into the bag); what's left over lies on
  template <class Lay, class Heap> void drawPairScene(World& w, const FrameCtx& f, const PairView& v, float clock, Lay& lay, Heap& heap) {
    const ivec3 at = v.at;
    const std::string st = pairStationAt(w, at);
    const u8 l = st == "grindstone" ? w.getLight(at.x, at.y, at.z) : w.getLight(at.x, at.y + 1, at.z);
    const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
    vec3 out, right; stationWorkFrame(w, at, out, right);
    const float yaw = std::atan2(out.x, out.z);
    const vec3 lying[2] = {pairLiePos(w, at, 0), pairLiePos(w, at, 1)};
    const float sz[2] = {pairLieSize(st, 0), pairLieSize(st, 1)};
    const float tilt[2] = {st == "cartography" ? 0.f : 0.35f, -0.35f};
    if (v.t < 0) {   // at rest: the one under the mouse rises a little
      const std::vector<ItemStack> now = pairWorkItems(w, at);
      for (int k = 0; k < 2; k++) heap(now[k], lying[k] + vec3(0, v.hover == k ? 0.02f + 0.006f * std::sin(clock * 4.f) : 0.f, 0), yaw + tilt[k], sz[k], L, right);
      return;
    }
    const PairBeats B = pairBeats(v.kind);
    if (v.t >= B.length) return;
    const bool made = v.t >= B.contact;
    const std::vector<ItemStack> now = pairWorkItems(w, at);
    const float away = (float)win(v.t, B.letGo, B.length);
    const vec3 toYou = vec3(std::sin(v.viewYaw), 0, std::cos(v.viewYaw)), vright = vec3(std::cos(v.viewYaw), 0, -std::sin(v.viewYaw));
    auto flyOff = [&](vec3 p) { return p + toYou * (0.45f * away) + vec3(0, 0.15f * away, 0); };
    const float shrink = 1 - 0.6f * away;
    auto one = [](ItemStack s) { if (!s.empty()) s.n = 1; return s; };
    auto less = [](ItemStack s) { if (!s.empty() && --s.n <= 0) s = ItemStack(); return s; };
    // the leftovers lying still: before contact all but the one taken up (moves[k]); after it, what the block holds now
    auto leftovers = [&](bool m0, bool m1) {
      for (int k = 0; k < 2; k++) heap(made ? now[k] : (k == 0 ? m0 : m1) ? less(v.before[k]) : v.before[k], lying[k], yaw + tilt[k], sz[k], L, right);
    };
    switch (v.kind) {
      case 6: {   // the grindstone: both up on the wheel; clean at GRIND_CLEAN (the result where the first piece was); let go
        const bool clean = v.t >= GRIND_CLEAN;
        for (int k = 0; k < 2; k++) {
          const int resultSide = v.before[0].empty() ? 1 : 0;
          const ItemStack s = !clean ? v.before[k] : k == resultSide ? v.after : ItemStack();
          if (s.empty()) continue;
          PairView at2 = v; at2.t = std::min(v.t, B.letGo);
          const vec3 p = flyOff(pairHeldPos(at2, k, lying));
          // stood up on its edge against the wheel while grinding (a flat piece), laid back down as it's let go
          const float stand = (float)(win(v.t, 0.28, B.contact) * (1 - win(v.t, B.letGo - 0.1, B.letGo + 0.2)));
          const VoxelModel& im = itemModel(stackModelKey(s));
          if (!im.ready()) continue;
          const float size = 0.34f * shrink;
          mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
          m = glm::rotate(m, v.viewYaw + (k ? -0.25f : 0.25f), vec3(0, 1, 0));
          if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.3f * size, 0)), vec3(0.6f * size));
          else { m = glm::translate(m, vec3(0, 0.5f * size * stand + 0.01f, 0)); m = glm::rotate(m, -PI / 2 * (1 - stand), vec3(1, 0, 0)); m = glm::translate(m, vec3(0, -0.5f * size * stand, 0)); m = glm::scale(m, vec3(size)); }
          voxelRenderer().draw(im.gl, m * im.matrix(), VoxelLight{L.sky, std::max(L.block, clean ? 0.f : 0.35f)}, vec4(0), itemGlints(s) && !clean ? 1.f : 0.f);
        }
        break;
      }
      case 8: case 15: {   // the smithing table (the Tool Forge): the material onto the gear (the gem into the tool); pressed, it glows (PEAK) and cools (COOLED); then off
        if (!made) { heap(v.before[0], lying[0], yaw + tilt[0], sz[0], L, right); lay(one(v.before[1]), pairHeldPos(v, 1, lying), yaw, sz[1], L); }
        else {
          const float heat = (float)(win(v.t, B.contact, SMITH_PEAK) * (1 - win(v.t, SMITH_PEAK, SMITH_COOLED)));
          if (v.kind == 15 && !now[0].empty()) heap(now[0], lying[0], yaw + tilt[0], sz[0], L, right);   // (a head forged: the rest of the material, the head on it)
          lay(v.after, flyOff(lying[0] + vec3(0, v.kind == 15 && !now[0].empty() ? 0.06f : 0.f, 0)), yaw + tilt[0], sz[0] * shrink, VoxelLight{L.sky, std::max(L.block, heat)}, vec4(1.f, 0.42f, 0.08f, 0.75f * heat));
        }
        heap(made ? now[1] : less(v.before[1]), lying[1], yaw + tilt[1], sz[1], L, right);
        break;
      }
      case 9: {   // the loom: the dye as the shuttle; halfway through the weave the cloth shows its new colour
        const bool dyed = v.t >= (LOOM_FROM + LOOM_TO) * 0.5;
        if (!dyed) heap(v.before[0], lying[0], yaw + tilt[0], sz[0], L, right);
        else { heap(v.after, flyOff(lying[0]), yaw + tilt[0], sz[0] * shrink, L, right); if (away > 0) heap(now[0], lying[0], yaw + tilt[0], sz[0], L, right); }
        if (v.t < LOOM_TO) lay(one(v.before[1]), pairHeldPos(v, 1, lying), v.viewYaw + clock * 3.f, sz[1] * 1.3f, L);
        heap(made ? now[1] : less(v.before[1]), lying[1], yaw + tilt[1], sz[1], L, right);
        break;
      }
      case 10: {   // the fletching table: flint and feather together; the arrows fanned out over the middle
        if (!made) { for (int k = 0; k < 2; k++) lay(one(v.before[k]), pairHeldPos(v, k, lying), yaw + tilt[k], sz[k], L); leftovers(true, true); break; }
        const vec3 mid = flyOff((lying[0] + lying[1]) * 0.5f + vec3(0, 0.02f, 0));
        const int n = std::min(std::max(1, v.after.n), 6);
        const float fan = (float)smoother(win(v.t, B.contact, B.contact + 0.35));
        for (int i = 0; i < n; i++) lay(one(v.after), mid + vec3(0, i * 0.004f, 0), v.viewYaw + PI / 4 + (i - (n - 1) * 0.5f) * 0.22f * fan, 0.34f * shrink, L);
        leftovers(true, true);
        break;
      }
      case 11: {   // the cartography table: the paper laid on the map; the new map (copies: a second sheet slid out) lifted off
        if (!made) { lay(v.before[0], lying[0], yaw, sz[0], L); lay(one(v.before[1]), pairHeldPos(v, 1, lying), yaw + tilt[1], sz[1], L); }
        else {
          const float slide = (float)smoother(win(v.t, B.contact, B.contact + 0.4));
          if (v.after.n > 1) lay(one(v.after), flyOff(lying[0] + vright * (0.16f * slide) + vec3(0, 0.002f, 0)), yaw + 0.12f * slide, sz[0] * shrink, L);
          lay(one(v.after), flyOff(lying[0] + vec3(0, 0.004f, 0)), yaw, sz[0] * shrink, L);
        }
        heap(made ? now[1] : less(v.before[1]), lying[1], yaw + tilt[1], sz[1], L, right);
        break;
      }
    }
  }
};

}  // namespace

void registerStationWorkLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<StationWorkLayer>(); }); }

}  // namespace dq
