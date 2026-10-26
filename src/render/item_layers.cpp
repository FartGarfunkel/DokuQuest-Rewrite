// render/item_layers.cpp — the first-person hand with the held item, and dropped items (voxel models).
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "render/chest_layer.h"
#include "client/app.h"
#include "assets/tileset.h"
#include "assets/voxel_paint.h"
#include "assets/gui_textures.h"
#include "core/registry.h"
#include "core/item_glint.h"
#include "render/rod_tip.h"
#include "sim/fishing.h"
#include <cstdlib>
#include <cstdio>

namespace dq {

void registerItemLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<HeldItemLayer>(); });
  registerRenderLayer([](GameSession&) { return std::make_shared<DroppedItemLayer>(); });
  registerPileLayer();   // piles and placed backpacks (render/pile_layer.cpp)
  registerLecternLayer();   // books on lecterns (render/lectern_layer.cpp)
  registerChestLayer();     // chest lids that open with the chest's screen (render/chest_layer.cpp)
  registerFpBow();          // the bow drawn in first person (render/fp_bow.cpp)
  registerSpawnerLayer();   // the spinning monster inside a monster cage (render/spawner_layer.cpp)
}

// ------------------------------------------------------------------ the arm
const VoxelModel& firstPersonArm() {
  if (const VoxelModel* skinArm = localPlayerArm()) return *skinArm;   // the skin's own arm and sleeve (render/char_layer)
  static VoxelModel& arm = *new VoxelModel();   // kept past exit (no GL calls after the context is gone)
  static std::string builtFor;
  const TileSet& ts = app().tiles;
  std::string stamp = ts.packName + "#" + std::to_string(ts.count);
  if (builtFor == stamp && arm.ready()) return arm;
  builtFor = stamp;
  // a hand 8 x 11 x 8 voxels under a sleeve 8 x 13 x 8 (the original's boxes: hand -0.33..0, sleeve 0..0.42)
  VoxelGrid g(ivec3(8, 24, 8));
  auto tileOf = [&](const char* n) { auto it = ts.index.find(n); return it == ts.index.end() ? -1 : it->second; };
  int skin = tileOf("skin"), sleeve = tileOf("sleeve");
  for (int z = 0; z < 8; z++) for (int y = 0; y < 24; y++) for (int x = 0; x < 8; x++) {
    bool hand = y < 11; int y0 = hand ? 0 : 11, h = hand ? 11 : 13, t = hand ? skin : sleeve;
    // the whole tile on each face of the part (models.js addBox); a voxel takes the face it lies on
    vec3 n((x + 0.5f) / 8, (y - y0 + 0.5f) / h, (z + 0.5f) / 8);
    float d[6] = {1 - n.x, n.x, 1 - n.y, n.y, 1 - n.z, n.z};
    int f = 4; for (int i : {5, 0, 1, 2, 3}) if (d[i] < d[f] - 1e-4f) f = i;
    float s = f == 0 ? 1 - n.z : f == 1 ? n.z : f == 2 ? n.x : f == 3 ? 1 - n.x : f == 4 ? n.x : 1 - n.x;
    float tt = f == 2 || f == 3 ? n.z : 1 - n.y;
    u32 c = t >= 0 ? tileVoxel(ts, t, (int)(s * ts.size), (int)(tt * ts.size)) : 0;
    if (!c) c = hand ? voxRGBA(214, 168, 128) : voxRGBA(70, 110, 160);
    g.set(x, y, z, c | 0xFF000000u);
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  arm.gl.upload(md);
  arm.frame.voxel = 1.f / 32; arm.frame.origin = vec3(-0.125f, -0.34375f, -0.125f);
  arm.size = g.size; arm.kind = VoxelModel::Block; arm.built = true; arm.key = "player_arm";
  return arm;
}

// ------------------------------------------------------------------ held item (local.js hand())
namespace {
const vec3 FP_HAND(0.56f, -0.5f, -0.72f);   // where the first-person arms sit (x right, y up, -z ahead)
inline float wrapA(float a) { return std::fmod(std::fmod(a + PI * 3, TAU) + TAU, TAU) - PI; }
// Minecraft's first-person swing, turned about the fist: the item chops forward and down and eases back
// (an empty hand punches instead)
mat4 mcSwing(const mat4& am, float side, float sp, bool empty) {
  float r = std::sqrt(sp), f = std::sin(sp * sp * PI), f1 = std::sin(r * PI), D = PI / 180;
  float k = empty ? -0.3f : 0.35f;
  vec3 pv = vec3(am[1]) * k + vec3(am[3]);   // turn about the shoulder end
  mat4 S(1);
  if (empty) S = glm::translate(S, vec3(side * -0.3f * f1, 0.1f * std::sin(r * TAU) + 0.05f * f1, -0.38f * std::sin(sp * PI)));
  else {
    S = glm::translate(S, vec3(side * -0.4f * f1, 0.2f * std::sin(r * TAU), -0.2f * std::sin(sp * PI)));
    S = glm::translate(S, pv);
    S = glm::rotate(S, side * f * -20 * D, vec3(0, 1, 0));
    S = glm::rotate(S, side * f1 * -20 * D, vec3(0, 0, 1));
    S = glm::rotate(S, f1 * -58 * D, vec3(1, 0, 0));
    S = glm::translate(S, -pv);
  }
  return S * am;
}
}

void HeldItemLayer::update(GameSession& s, double dt) {
  time += dt;
  Player* p = s.player;
  if (!p) return;
  // development aid: DQ_DEV_ITEMS="held,offhand,drop1,drop2..." puts items in the hands and drops the rest
  // in front of you (to look at the voxel models in a smoke test)
  static bool devDone = false;
  if (!devDone) {
    devDone = true;
    if (const char* dv = std::getenv("DQ_DEV_ITEMS")) {
      for (const char* g : {"widget/button", "hud/hotbar", "hud/heart/full", "container/inventory"}) { GuiSprite gs = guiSprite(g); DQ_LOG("dev: gui sprite %s: %s %dx%d", g, gs.valid() ? "pack" : "none", gs.w, gs.h); }
      auto keys = splitStr(dv, ',');
      for (size_t i = 0; i < keys.size(); i++) {
        if (!reg().item(keys[i])) continue;
        if (i == 0) p->held() = ItemStack(keys[i], 1);
        else if (i == 1) { if (!p->offhand.empty()) p->offhand[0] = ItemStack(keys[i], 1); }
        else {
          auto e = std::make_unique<Entity>();
          e->type = "item"; e->realm = p->realm; e->noGravity = true;
          float a = p->yaw + (float)(i - 2) * 0.45f - 0.9f;
          e->pos = p->pos + vec3(-std::sin(a) * 2.2f, 0.4f, -std::cos(a) * 2.2f);
          e->data["stack"] = ItemStack(keys[i], (int)i * 9).toJson();
          s.level->spawn(std::move(e));
        }
      }
    }
  }
  float fdt = (float)std::min(dt, 0.1);
  bool menu = s.app.topScreen() != nullptr;
  // the step bob (player.js): its amount follows how fast you walk on the ground
  float hs = std::hypot(p->vel.x, p->vel.z);
  bobAmt += ((p->onGround && !p->flying ? std::min(1.f, hs / 4.3f) : 0.f) - bobAmt) * std::min(1.f, fdt * 8);
  bob += hs * fdt * 1.6f;
  // what's in hand: a new thing comes up from below
  std::string key = p->carrying.is_object() ? std::string("carry") : p->held().empty() ? std::string() : p->held().id;   // picking something up: the arms rise
  if (key != lastHeld) { equip = 0; lastHeld = key; }
  if (equip < 1) equip = std::min(1.f, equip + fdt * 5);
  if (eatAnim > 0) eatAnim -= fdt;
  // swings: attacking the air, placing and using, mining (held)
  if (!menu) {
    Input& in = s.app.input;
    if (in.pressed(Act::Attack)) swingArm();   // every attack swings: the air, a creature in front of a wall, a block (mining keeps it going)
    if (in.pressed(Act::Use) && s.target.hit) swingArm();
    else if (in.pressed(Act::Use) && p->held().id == "fishing_rod") swingArm();   // cast / reel in
    if (s.breaking) { swinging = true; if (swing == 0) swing = 0.0001f; }
  }
  if (swinging || swing > 0) { swing += fdt * 3.6f; if (swing >= 1) swing = 0; }
  swinging = false;
  // a light for the hand: where your eyes are, and what you carry
  World& w = s.world();
  u8 l = w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.62f), ifloor(p->pos.z));
  int carried = 0;
  for (const ItemStack* st : {&p->held(), p->offhand.empty() ? nullptr : &p->offhand[0]}) {
    if (!st || st->empty()) continue;
    const ItemDef* d = st->def();
    if (d && !d->block.empty()) carried = std::max<int>(carried, reg().block(reg().blockId(d->block)).light);
  }
  light = vec2((l >> 4) / 15.f, std::max<int>(l & 15, carried) / 15.f);
}

void HeldItemLayer::drawOverlay(GameSession& s, const FrameCtx& f) {
  Player* p = s.player;
  if (!p || p->dead || s.viewMode != 0 || s.hudHidden) return;
  const Settings& st = s.app.settings;
  // what you hold lags a little behind when you turn, and swings back
  float sx0 = 0, sy0 = 0;
  if (st.heldSway) {
    if (!haveLook) { hy = p->yaw; hp = p->pitch; haveLook = true; }
    float dyaw = wrapA(p->yaw - hy), dpit = p->pitch - hp;
    hy = p->yaw; hp = p->pitch;
    hsx = (hsx + clampv(dyaw, -0.2f, 0.2f) * 0.5f) * 0.85f; hsy = (hsy - clampv(dpit, -0.2f, 0.2f) * 0.5f) * 0.85f;
    sx0 = clampv(hsx, -0.08f, 0.08f); sy0 = clampv(hsy, -0.06f, 0.06f);
  }
  float bobK = st.viewBob ? bobAmt : 0;
  float hsn = std::sin(bob * 1.1f), bx = hsn * 0.025f * bobK + sx0, by = -hsn * hsn * 0.03f * bobK + sy0;
  float eq = 1 - equip;
  float eat = eatAnim > 0 ? std::sin(std::min(1.f, eatAnim / 0.45f) * PI) : 0;

  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_BLEND);
  float aspect = f.height > 0 ? (float)f.width / f.height : 1.f;
  mat4 proj = glm::perspective(70 * PI / 180, aspect, 0.01f, 10.f);
  int taken = handsHook ? handsHook(*this, s, f, proj) : 0;
  std::vector<std::pair<ItemStack, mat4>> over;
  VoxelRenderer& VR = voxelRenderer();
  VR.begin(f, proj, false);
  VoxelLight L{light.x, light.y};
  const VoxelModel& arm = firstPersonArm();
  // the arm, and the sleeve of a worn chestplate over it (the rest of your armour shows on the first-person body)
  auto drawArm = [&](const mat4& m) { VR.draw(arm.gl, m * arm.matrix(), L); drawFirstPersonSleeve(m, L); };

  auto holdIn = [&](float side, const ItemStack& stack, float lift) {
    // a fishing rod with its float out has no line hanging from the tip (fishing.js: fishing_rod_cast)
    bool rod = stack.id == "fishing_rod";
    float wind = rod ? castWindOf(*p) : 0.f;
    const VoxelModel& im = itemModel(rod ? std::string("fishing_rod_cast") : stack.id);   // (the hanging float is drawn live from the tip)
    bool tool = im.kind != VoxelModel::Block && isHandheld(stack.id);
    float e = side > 0 ? eat : 0, pitch = 1.62f + e * 0.35f, yaw = side * 0.32f;
    mat4 am = glm::translate(mat4(1), vec3(side * (FP_HAND.x + (side > 0 ? bx - e * 0.32f : -bx * 0.6f)),
      FP_HAND.y + by - (side > 0 ? eq * 0.55f : eq * 0.3f) + lift + (side > 0 ? e * 0.2f : std::sin((float)time * 1.3f) * 0.006f), FP_HAND.z + e * 0.12f));
    am = glm::rotate(am, yaw, vec3(0, 1, 0));
    am = glm::rotate(am, pitch, vec3(1, 0, 0));
    if (side > 0 && swing > 0) am = mcSwing(am, 1, swing, false);
    if (side > 0 && wind > 0) { am = glm::translate(am, vec3(0, 0.1f * wind, 0.05f * wind)); am = glm::rotate(am, -0.75f * std::sin(wind * PI / 2), vec3(1, 0, 0)); }   // the rod comes back over the shoulder before the cast
    drawArm(am);
    // the fist, turned back to face the way you look
    mat4 m = glm::translate(am, vec3(0, 0.3f - 0.6f, -0.02f));
    m = glm::rotate(m, -pitch, vec3(1, 0, 0)); m = glm::rotate(m, -yaw, vec3(0, 1, 0));
    if (im.torch3d) {   // held upright in the fist like the ones on the wall, leaning a little away
      m = glm::translate(m, vec3(-side * 0.02f, 0.05f, -0.05f));
      m = glm::rotate(m, -0.3f, vec3(1, 0, 0)); m = glm::rotate(m, side * 0.2f, vec3(0, 0, 1)); m = glm::rotate(m, side * 0.6f, vec3(0, 1, 0));
      m = glm::scale(m, vec3(0.46f)); m = glm::translate(m, vec3(0, 0.6f, 0));
    } else if (im.kind == VoxelModel::Block) {
      m = glm::translate(m, vec3(-side * 0.03f, 0.1f, -0.06f));
      m = glm::rotate(m, side * 0.78f, vec3(0, 1, 0)); m = glm::scale(m, vec3(0.4f));
    } else {
      // tuning aid: DQ_DEV_FPHELD="dx,dy,dz,scale,yaw,roll" (the fist frame's offsets) for screenshots
      static float T[6] = {0, 0, 0, 0, 0, 0}; static bool tl = false;
      if (!tl) { tl = true; if (const char* v = std::getenv("DQ_DEV_FPHELD")) std::sscanf(v, "%f,%f,%f,%f,%f,%f", &T[0], &T[1], &T[2], &T[3], &T[4], &T[5]); }
      m = glm::translate(m, vec3(side * T[0], T[1], T[2]));
      m = glm::rotate(m, side * (-1.5f + 0.35f + T[4] + (tool ? 0.22f : 0.f)), vec3(0, 1, 0));   // tools turn a little more face-on
      m = glm::rotate(m, side * (0.42f + T[5]), vec3(0, 0, 1));
      m = glm::scale(m, vec3(0.68f + T[3] + (tool ? 0.26f : 0.f)));   // swords and tools are drawn big, like Minecraft's first person
      if (rod) {   // a rod points out ahead with its tip (and the float hanging from it) in view, not up out of the picture
        static float RT[4] = {0.35f, -0.6f, 0.f, -0.08f}; static bool rl = false;
        if (!rl) { rl = true; if (const char* v = std::getenv("DQ_DEV_ROD")) std::sscanf(v, "%f,%f,%f,%f", &RT[0], &RT[1], &RT[2], &RT[3]); }
        m = glm::rotate(m, side * RT[2], vec3(0, 1, 0)); m = glm::rotate(m, side * RT[0], vec3(0, 0, 1)); m = glm::rotate(m, RT[1], vec3(1, 0, 0));
        m = glm::scale(m, vec3(1 + RT[3]));
      }
      // the grip goes in the fist: the handle end of a tool, the bottom of anything else
      m = glm::translate(m, tool ? vec3(0.3f * side, 0.28f, 0) : vec3(0, 0.3f, 0));
      if (side < 0) m = glm::scale(m, vec3(-1, 1, 1));
    }
    bool glint = itemGlints(stack);
    if (im.ready()) VR.draw(im.gl, m * im.matrix(), L, vec4(0), glint ? 1.f : 0.f);
    if (itemHook && im.ready()) over.push_back({stack, m});
    if (rod && im.ready() && side > 0) {   // the line leaves the tip of the rod: view space -> world
      vec3 v = vec3(m * vec4(0.47f, 0.47f, 0, 1));
      vec3 R = f.cam.right(), Fw = f.cam.forward(), Up = glm::cross(R, Fw);
      setRodTip(p->id, f.cam.pos + glm::dvec3(R * v.x + Up * v.y - Fw * v.z), f.time);
    }
  };

  if (p->hang || p->climb) {
    // local.js handState: hanging from a ledge (or hauling yourself up): both hands up on the edge just above your
    // eyes, coming up from the lower corners; hand over hand while you shimmy along it
    float k = p->climb ? std::min(1.f, p->climb->t / 0.55f) : 0.f;
    float sh = p->hang && p->hang->shimT > 0 ? 1.f : 0.f, ss = std::sin(p->hang ? p->hang->shimPh : 0.f), sc = std::cos(p->hang ? p->hang->shimPh : 0.f);
    for (float side : {-1.f, 1.f}) {
      float reach = sh * (side * ss * 0.06f + std::max(0.f, side * sc) * 0.05f);
      mat4 a = glm::translate(mat4(1), vec3(side * 0.42f, -0.2f - k * 0.35f + std::sin((float)f.time * 2 + side) * 0.008f + reach, -0.38f));
      a = glm::rotate(a, -side * (0.35f - k * 0.2f), vec3(0, 0, 1)); a = glm::rotate(a, PI - 0.15f - k * 0.9f, vec3(1, 0, 0));
      drawArm(a);
    }
    VR.end();
    return;
  }
  if (p->carrying.is_object()) {   // Carry On (local.js handState): whatever you hold sits in both arms in front of you
    drawCarriedThing(p->carrying, glm::translate(mat4(1), vec3(bx, -0.62f + by - eq * 0.5f, -0.95f)), true, L, vec4(0));
    for (float side : {-1.f, 1.f}) {
      mat4 a = glm::translate(mat4(1), vec3(side * 0.5f + bx, -0.5f + by - eq * 0.5f, -0.62f));
      a = glm::rotate(a, side * 0.28f, vec3(0, 1, 0)); a = glm::rotate(a, 1.72f, vec3(1, 0, 0));
      drawArm(a);
    }
    VR.end();
    return;
  }
  const ItemStack& held = p->held();
  if (taken & 1) {}
  else if (!held.empty() && held.def()) holdIn(1, held, 0);
  else {
    // empty hand: the forearm reaches in from the lower right, like the classic game
    mat4 m = glm::translate(mat4(1), vec3(0.5f + bx, -0.39f + by - eq * 0.55f - eat * 0.1f, -0.5f));
    m = glm::rotate(m, 0.33f, vec3(0, 1, 0));
    m = glm::rotate(m, 1.9f, vec3(1, 0, 0));
    if (swing > 0) m = mcSwing(m, 1, swing, true);
    drawArm(m);
  }
  // the off hand, mirrored on the left
  if (!(taken & 2) && !p->offhand.empty() && !p->offhand[0].empty() && p->offhand[0].def()) holdIn(-1, p->offhand[0], -0.02f);
  VR.end();
  for (auto& [st2, m2] : over) itemHook(s, f, proj, st2, m2, itemModel(st2.id));
}

// ------------------------------------------------------------------ dropped items (entities.js ItemEntity.draws)
void DroppedItemLayer::drawOpaque(GameSession& s, const FrameCtx& f) {
  if (!s.player) return;
  World& w = s.world();
  VoxelRenderer& VR = voxelRenderer();
  bool begun = false;
  for (auto& e : s.level->entities()) {
    if (!e || e->removed || e->type != "item" || e->realm != s.player->realm) continue;
    const json& sj = e->data.contains("stack") ? e->data["stack"] : json();
    ItemStack st = ItemStack::fromJson(sj);
    if (st.empty()) continue;
    vec3 rel = f.cam.rel(glm::dvec3(e->pos));
    if (glm::dot(rel, rel) > 96.f * 96.f || !f.cam.frustum.sphere(rel + vec3(0, 0.25f, 0), 0.8f)) continue;
    const VoxelModel& im = itemModel(st.id);
    if (!im.ready()) continue;
    if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
    u8 l = w.getLight(ifloor(e->pos.x), ifloor(e->pos.y + 0.2f), ifloor(e->pos.z));
    VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
    float age = e->age, spin = (float)((e->id * 2654435761u) % 6283) / 1000.f;
    bool floating = e->inWater || e->data.value("floating", false);
    // items hover and bob a little above whatever they rest on, turning slowly
    mat4 m = glm::translate(mat4(1), rel + vec3(0, (floating ? 0.05f : 0.22f) + std::sin(age * 2.5f) * 0.07f, 0));
    m = glm::rotate(m, spin + age * 1.8f, vec3(0, 1, 0));
    // the original's sizes: a quarter-block cube, a half-block picture
    float sc = im.kind == VoxelModel::Block ? 0.25f : 0.5f;
    m = glm::scale(m, vec3(sc));
    float gl = itemGlints(st) ? 1.f : 0.f;   // enchanted things shimmer on the ground too
    VR.draw(im.gl, m * im.matrix(), L, vec4(0), gl);
    // bigger stacks show more copies (2, 3, 4 or 5), a little scattered
    int n = st.n, copies = n > 48 ? 5 : n > 32 ? 4 : n > 16 ? 3 : n > 1 ? 2 : 1;
    for (int c = 1; c < copies; c++) {
      mat4 m2 = im.kind == VoxelModel::Block ? glm::translate(m, vec3(std::sin(c * 2.4f) * 0.45f, c * 0.35f, std::cos(c * 2.4f) * 0.45f))
                                             : glm::translate(m, vec3(std::sin(c * 2.4f) * 0.12f, std::cos(c * 1.7f) * 0.1f, -c * 0.12f));
      VR.draw(im.gl, m2 * im.matrix(), L, vec4(0), gl);
    }
  }
  if (begun) VR.end();
}

} // namespace dq
