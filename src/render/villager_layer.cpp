// render/villager_layer.cpp — villagers drawn as voxel people, their name tags and marks, your jobs on the HUD, and the
// screens the villages open (see villager_layer.h).
#include "render/villager_layer.h"
#include "render/char_anim.h"
#include "assets/char_model.h"
#include "assets/skin.h"
#include "client/app.h"
#include "ui/theme.h"
#include "ui/villager_screens.h"
#include "sim/villagers.h"
#include "core/registry.h"
#include <cstdlib>
#include <unordered_map>

namespace dq {
using ui::operator*; using ui::operator+; using ui::operator-;

namespace {
float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
mat4 heldMatrix(float tilt, float s, vec2 grip, bool upright) {   // render/char_layer.cpp's, for the hands
  vec3 X, Y;
  if (upright) { X = vec3(0, 0, -s); Y = vec3(0, s * std::cos(tilt), -s * std::sin(tilt)); }
  else {
    float r2 = std::sqrt(0.5f);
    vec3 F(0, std::sin(tilt), -std::cos(tilt)), Bn(0, -std::cos(tilt), -std::sin(tilt));
    X = (F - Bn) * r2 * s; Y = (F + Bn) * r2 * s;
  }
  vec3 Z(s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}
const mat4& heldMat(const std::string& id) {
  static const mat4 TOOL = heldMatrix(0.25f, 0.85f, {-0.34f, -0.24f}, false), ITEM = heldMatrix(0.2f, 0.55f, {0, -0.25f}, true), ROD = heldMatrix(0.75f, 0.85f, {-0.34f, -0.24f}, false);
  return id == "fishing_rod" || id == "fishing_rod_tp" ? ROD : isHandheld(id) ? TOOL : ITEM;
}
// a villager's skin, registered once as "npc:<seed>:<job>" (villagers.js NPC_SKINS)
std::string npcSkinRef(u32 seed, const std::string& job) {
  static std::unordered_map<std::string, bool> made;
  std::string ref = "npc:" + std::to_string(seed) + ":" + job;
  if (!made.count(ref)) {
    SkinImage img; std::vector<u8> px; npcSkinPixels(seed, job, px);
    if (px.size() == img.px.size()) img.px = px;
    registerSkin(ref, img);
    made[ref] = true;
  }
  return ref;
}
}

class VillagerLayer : public RenderLayer {
public:
  explicit VillagerLayer(GameSession& s) : app(s.app) {
    hScreen = app.events.on("villagerScreen", [this](const json& a) {
      GameSession* s2 = app.session.get();
      if (!s2 || !s2->player || !a.is_array() || a.size() < 3 || a[0] != s2->player->name) return true;
      std::string id = a[1].get<std::string>(); const json& args = a[2];
      if (id == "close") { if (Screen* t = app.topScreen(); t && std::string(t->id()) == "npc-talk") app.popScreen(); return true; }
      if (app.topScreen()) {   // already in a menu: a word instead (villagers.js: not in play mode)
        if (id == "talk" && args.value("confront", false)) if (Entity* e = s2->level->entity(args.value("npc", 0u))) if (const json* N = npcOf(*e))
          s2->level->fx->message(s2->player->id, N->value("n", std::string("A guard")) + " the guard: \"Hey! I saw that. Watch yourself.\"", "warn");
        return true;
      }
      if (id == "talk") app.pushScreen(makeVillagerTalkScreen(args.value("npc", 0u), args.value("confront", false), args.value("victim", std::string())));
      else if (id == "board") app.pushScreen(makeQuestBoardScreen(ivec3(args.value("x", 0), args.value("y", 0), args.value("z", 0))));
      else if (id == "path") app.pushScreen(makePathScreen(args.value("hint", std::string())));
      return true;
    });
  }
  ~VillagerLayer() override { app.events.off(hScreen); }
  const char* name() const override { return "villagers"; }
  int order() const override { return 205; }

  void update(GameSession& s, double dt) override {
    time += dt;
    devSwitches(s);
    // the head turns to whoever is looking (smoothed, villagers.js this.hy)
    Player* me = s.player;
    for (auto& up : s.level->entities()) {
      auto* c = dynamic_cast<Creature*>(up.get());
      if (!c || c->removed || !c->T->npc || !me) continue;
      Look& L = looks[c->id];
      const json* N = npcOf(*c);
      bool sleep = N && (N->value("p", 0) & NP_SLEEP);
      float want = 0, pitch = 0;
      if (!sleep) {
        float lx = me->pos.x - c->pos.x, lz = me->pos.z - c->pos.z, ly = me->pos.y + 1.5f - (c->pos.y + 1.6f), d = std::hypot(lx, lz);
        if (d < 8) { want = clampf(wrapAngle(std::atan2(-lx, -lz) - c->yaw), -1.1f, 1.1f); pitch = clampf(-std::atan2(ly, d), -0.5f, 0.5f); }
      }
      L.hy += (want - L.hy) * std::min(1.f, (float)dt * 9); L.pitch = pitch; L.seen = frameNo;
    }
    if (++frameNo % 600 == 0) for (auto it = looks.begin(); it != looks.end();) { if (frameNo - it->second.seen > 300) it = looks.erase(it); else ++it; }
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    float a = clampv((float)(s.tickAcc / TICK_DT), 0.f, 1.f);
    bool begun = false;
    VoxelRenderer& VR = voxelRenderer();
    const TileSet& ts = s.app.tiles;
    World& w = s.world();
    for (auto& up : s.level->entities()) {
      auto* c = dynamic_cast<Creature*>(up.get());
      if (!c || c->removed || !c->T->npc || c->realm != s.player->realm) continue;
      const json* N = npcOf(*c);
      vec3 pos = glm::mix(c->prevPos, c->pos, a);
      vec3 rel = f.cam.rel(glm::dvec3(pos));
      if (glm::dot(rel, rel) > 128.f * 128.f || !f.cam.frustum.sphere(rel + vec3(0, 1, 0), 1.8f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      u32 seed = N ? N->value("s", 0u) : 0; std::string job = N ? N->value("j", std::string("none")) : "none";
      auto spec = resolveCharacter(npcSkinRef(seed, job));
      const CharModel& M = charModel(*spec);
      const int P = N ? N->value("p", 0) : 0;
      const bool sleep = P & NP_SLEEP, sit = P & NP_SIT;
      u8 l = w.getLight(ifloor(pos.x), ifloor(pos.y + 1), ifloor(pos.z));
      VoxelLight light{(l >> 4) / 15.f, (l & 15) / 15.f};
      vec4 tint = c->hurtTime > 0 ? vec4(1, 0.15f, 0.1f, 0.4f) : vec4(0);
      Look& L = looks[c->id];
      CharPose ps;
      ps.yaw = c->prevYaw + wrapAngle(c->yaw - c->prevYaw) * a;
      ps.headPitch = sleep ? 0 : L.pitch; ps.headYaw = sleep ? 0 : L.hy;
      ps.walk = c->walk * 1.4f; ps.walkAmt = std::min(1.f, c->walkAmt);
      ps.swing = c->swingT > 0 ? 1 - c->swingT / 0.3f : 0;
      ps.sit = sit; ps.sleep = sleep; ps.time = (float)time + (float)(seed % 50);
      ps.deathT = c->deathTime > 0 ? std::min(1.f, c->deathTime * 2) : 0;
      ps.wave = P & NP_WAVE ? 1.f : 0.f; ps.talk = P & NP_TALK; ps.fold = P & NP_FOLD; ps.read = P & NP_READ;
      std::string held = c->heldItem;
      if (sleep || ((P & NP_READ) && job != "librarian")) held.clear();
      else if (sit && N && N->contains("sh") && reg().item(N->value("sh", std::string()))) held = N->value("sh", std::string());
      ps.held = !held.empty();
      mat4 base = glm::translate(mat4(1), rel);
      if (sit) base = glm::translate(base, vec3(0, -0.45f, 0));
      if (sleep) base = glm::translate(base, vec3(0, -0.2f, 0));
      CharRig R;
      charRig(M, ps, base, R, false, false);
      // guards in iron: the 3D outer layer goes under the armour
      std::string AR[4];
      if (N && c->data.contains("npcArmor") && c->data["npcArmor"].is_array()) for (int i = 0; i < 4 && i < (int)c->data["npcArmor"].size(); i++) if (c->data["npcArmor"][i].is_string()) AR[i] = c->data["npcArmor"][i];
      bool hid[CP_COUNT] = {};
      hid[CP_HEAD] = !AR[0].empty();
      for (int i : {CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) hid[i] = !AR[1].empty();
      hid[CP_LEGR_U] = hid[CP_LEGL_U] = !AR[2].empty();
      hid[CP_LEGR_D] = hid[CP_LEGL_D] = !AR[2].empty() || !AR[3].empty();
      for (int i = 0; i < CP_COUNT; i++) {
        mat4 m = R.part[i] * R.scale[i];
        if (M.base[i].ready()) VR.draw(M.base[i].gl, m * M.base[i].local(), light, tint);
        if (!hid[i] && M.ovl[i].ready()) VR.draw(M.ovl[i].gl, m * M.ovl[i].local(), light, tint);
      }
      static const std::map<std::string, int> PART = {{"head", CP_HEAD}, {"body", CP_BODY}, {"armR_u", CP_ARMR_U}, {"armR_d", CP_ARMR_D}, {"armL_u", CP_ARML_U},
                                                      {"armL_d", CP_ARML_D}, {"legR_u", CP_LEGR_U}, {"legR_d", CP_LEGR_D}, {"legL_u", CP_LEGL_U}, {"legL_d", CP_LEGL_D}};
      for (int slot = 0; slot < 4; slot++) {
        if (AR[slot].empty() || !reg().item(AR[slot])) continue;
        const ArmorSet& A = armorMeshes(AR[slot], "", slot, M.slim, false, ts);
        for (auto& [pn, list] : A.byPart) { auto it = PART.find(pn); if (it == PART.end()) continue; mat4 m = R.part[it->second] * R.scale[it->second]; for (const CharMesh* cm : list) VR.draw(cm->gl, m * cm->local(), light, tint); }
      }
      if (!held.empty()) {
        std::string id = held == "fishing_rod" ? "fishing_rod_tp" : held;
        const VoxelModel& im = itemModel(id);
        if (im.ready()) {
          mat4 hm = glm::translate(R.part[CP_ARMR_D], vec3(0, -6, -1));
          if (im.kind == VoxelModel::Block) { hm = glm::translate(hm, vec3(0, 0, -1.5f)); hm = glm::scale(hm, vec3(1 / CHAR_PX)); hm = glm::rotate(hm, -0.3f, vec3(1, 0, 0)); hm = glm::scale(hm, vec3(0.375f)); }
          else hm = glm::scale(hm, vec3(1 / CHAR_PX)) * heldMat(id);
          VR.draw(im.gl, hm * im.matrix(), light, tint);
        }
      }
    }
    // towns3.js: the town gates (shut = the barrier blocks are there): a portcullis of iron bars in a stone wall, timber
    // doors banded with iron in a palisade. (The original slid/swung them; here they show while shut.)
    static const BlockId bar = reg().blockId("gate_barrier");
    if (bar && s.player->realm == REALM_MAIN) {
      const VoxelModel& iron = itemModel("iron_block"); const VoxelModel& wood = itemModel("spruce_planks"); const VoxelModel& log = itemModel("spruce_log");
      for (auto& v : villagesNear(*s.level, (float)f.cam.pos.x, (float)f.cam.pos.z, 96)) for (const VGate& G : v->gates) {
        vec3 rc = f.cam.rel(glm::dvec3(G.center));
        if (glm::dot(rc, rc) > 160.f * 160.f || !f.cam.frustum.sphere(rc + vec3(0, 2.5f, 0), G.span + 3.f)) continue;
        const bool along = G.d == 0 || G.d == 2;   // the wall runs east-west here: the gate is thin in z
        for (const ivec3& c : G.cells) {
          if (w.getBlock(c.x, c.y, c.z) != bar) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          u8 l = w.getLight(c.x + G.inX, c.y, c.z + G.inZ);
          VoxelLight light{(l >> 4) / 15.f, (l & 15) / 15.f};
          vec3 cc = f.cam.rel(glm::dvec3(c) + glm::dvec3(0.5));
          auto box = [&](const VoxelModel& m, vec3 off, vec3 size) {   // a unit block model scaled to a box (across, up, through)
            if (!m.ready()) return;
            vec3 o = along ? off : vec3(off.z, off.y, off.x), sz = along ? size : vec3(size.z, size.y, size.x);
            VR.draw(m.gl, glm::scale(glm::translate(mat4(1), cc + o), sz) * m.matrix(), light);
          };
          if (G.stone) {
            for (float x : {-0.25f, 0.25f}) box(iron, vec3(x, 0, 0), vec3(0.14f, 1, 0.14f));
            if ((c.y - G.h0) % 2 == 0) box(iron, vec3(0, 0.1f, 0), vec3(1, 0.12f, 0.18f));
          } else {
            box(wood, vec3(0), vec3(1, 1, 0.24f));
            if (c.y - G.h0 == 1 || c.y - G.h0 == 3 || c.y - G.h0 == 5) box(iron, vec3(0, -0.1f, 0), vec3(1, 0.18f, 0.34f));
            int s2 = along ? c.x - G.cx : c.z - G.cz;
            if (std::abs(s2) == G.hw + 1 || s2 == 0) box(log, vec3(0), vec3(0.3f, 1, 0.32f));
          }
        }
      }
    }
    if (begun) VR.end();
  }

  void drawHud(GameSession& s) override {
    if (!s.player || s.hudHidden) return;
    Player& me = *s.player; Level& lv = *s.level;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = (float)s.app.width, H = (float)s.app.height, z = ui::Z();
    Camera& cam = s.frame.cam;
    auto project = [&](vec3 wp, ImVec2& out) {
      vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
      if (c.w <= 0.1f) return false;
      out = ImVec2((c.x / c.w * 0.5f + 0.5f) * W, (1 - (c.y / c.w * 0.5f + 0.5f)) * H); return true;
    };
    // marks are worked out once a second (a ! is today's errand, a ? one you can hand in)
    if ((markT -= ImGui::GetIO().DeltaTime) <= 0) {
      markT = 1; marks.clear();
      json& S = npcStats(me);
      for (auto& up : lv.entities()) {
        auto* c = dynamic_cast<Creature*>(up.get()); const json* N = c ? npcOf(*c) : nullptr;
        if (!N || !c->T->npc || c->removed || std::hypot(c->pos.x - me.pos.x, c->pos.z - me.pos.z) > 26) continue;
        int i = N->value("i", 0); if (i >= 100) continue;
        VillageRef v = villageById(lv, N->value("v", std::string())); if (!v) continue;
        json* mine = nullptr; for (auto& q : S["quests"]) if (q.value("vid", "") == v->id && q.value("giver", -9) == i) mine = &q;
        if (mine) { if (questReady(lv, me, *mine)) marks[c->id] = '?'; }
        else { json q = npcQuestFor(lv, *v, i); if (q.is_object() && !S["qdone"].contains(q.value("id", ""))) marks[c->id] = '!'; }
      }
    }
    // names over their heads up close, and ! or ? over anyone with work for you
    for (auto& up : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(up.get()); const json* N = c ? npcOf(*c) : nullptr;
      if (!N || !c->T->npc || c->removed || !c->alive() || c->realm != me.realm || (N->value("p", 0) & NP_SLEEP)) continue;
      float d = std::hypot(c->pos.x - (float)cam.pos.x, c->pos.z - (float)cam.pos.z);
      auto mk = marks.find(c->id); char mark = mk == marks.end() ? 0 : mk->second;
      if (d > 24 || (d > 8 && !mark)) continue;
      ImVec2 sp; if (!project(c->pos + vec3(0, 2.15f, 0), sp)) continue;
      float y = sp.y;
      if (d <= 8) {
        std::string nm = N->value("n", std::string()), jt = jobTitle(N->value("j", std::string("none")));
        ImVec2 t2 = ui::textSize(ui::F_SMALL, jt.c_str()), t1 = ui::textSize(ui::F_MID, nm.c_str());
        y -= t2.y; ui::textShadow(dl, ui::F_SMALL, ImVec2(sp.x - t2.x / 2, y), ui::col::gold, jt.c_str(), ImVec2(1, 1) * z, ui::col::black);
        y -= t1.y + 2 * z;
        dl->AddRectFilled(ImVec2(sp.x - t1.x / 2 - 6 * z, y), ImVec2(sp.x + t1.x / 2 + 6 * z, y + t1.y), IM_COL32(0, 0, 0, 107), 2 * z);
        ui::textShadow(dl, ui::F_MID, ImVec2(sp.x - t1.x / 2, y), ui::col::white, nm.c_str(), ImVec2(1, 1) * z, ui::col::black);
      }
      if (mark) {
        char m[2] = {mark, 0};
        float bob = std::sin((float)time * 5.2f) * 2 * z;
        ImVec2 ts = ui::textSize(ui::F_DISP, m);
        ui::textOutline(dl, ui::F_DISP, ImVec2(sp.x - ts.x / 2, y - ts.y + bob), mark == '?' ? ui::col::rgb(0x9ff07a) : ui::col::rgb(0xffd84a), m, 1 * z, ui::col::rgb(0x3a2a00));
      }
    }
    // your jobs, top right (.quest-track)
    json& S = npcStats(me);
    float x1 = W - 10 * z, y0 = 120 * z, maxW = 260 * z;
    int n = 0;
    for (auto& q : S["quests"]) {
      if (n++ >= 5) break;
      int need = q.value("need", 1), have = std::min(need, questHave(lv, me, q));
      bool done = have >= need;
      std::string title = ui::ellipsize(ui::F_MID, q.value("title", std::string()), maxW - 70 * z), cnt = std::to_string(have) + "/" + std::to_string(need) + (done ? " \xE2\x9C\x93" : "");
      ImVec2 tt = ui::textSize(ui::F_MID, title.c_str()), tc = ui::textSize(ui::F_BIG, cnt.c_str());
      float w = std::min(maxW, tt.x + tc.x + 26 * z), h = std::max(tt.y, tc.y) + 6 * z;
      ImVec2 a0(x1 - w, y0);
      dl->AddRectFilled(a0, ImVec2(x1, y0 + h), IM_COL32(10, 15, 13, 158));
      dl->AddRectFilled(a0, ImVec2(a0.x + 3 * z, y0 + h), done ? ui::col::xp : ui::col::gold);
      ui::text(dl, ui::F_MID, ImVec2(a0.x + 11 * z, y0 + 3 * z), ui::col::rgb(0xfff3d6), title.c_str());
      ui::text(dl, ui::F_BIG, ImVec2(x1 - tc.x - 8 * z, y0 + 3 * z + (tt.y - tc.y) / 2), done ? ui::col::xp : ui::col::muted, cnt.c_str());
      y0 += h + 3 * z;
    }
  }

private:
  struct Look { float hy = 0, pitch = 0; u64 seen = 0; };
  App& app;
  int hScreen = 0;
  double time = 0; u64 frameNo = 0;
  std::unordered_map<u32, Look> looks;
  std::unordered_map<u32, char> marks; float markT = 0;
  int devFrames = 0;
  void devSwitches(GameSession& s) {
    devFrames++;
    if (!s.player) return;
    if (devFrames == 2) if (const char* t = std::getenv("DQ_DAYTIME")) s.level->info.time = std::floor(s.level->info.time / DAY_LEN) * DAY_LEN + std::atof(t) * DAY_LEN;
    if (devFrames == 20) if (const char* v = std::getenv("DQ_VILLAGERS")) {
      std::vector<std::string> jobs; std::string l = v; size_t c;
      while ((c = l.find(',')) != std::string::npos) { jobs.push_back(l.substr(0, c)); l = l.substr(c + 1); }
      if (!l.empty()) jobs.push_back(l);
      for (Creature* e : spawnDevVillagers(*s.level, *s.player, jobs)) { e->yaw = s.player->yaw + PI; e->prevYaw = e->yaw; }
    }
    if (devFrames == 60) if (const char* o = std::getenv("DQ_VILLAGER_OPEN")) {
      std::string what = o;
      Creature* best = nullptr; float bd = 1e9f;
      for (auto& up : s.level->entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) if (npcOf(*c) && c->T->npc) {
        bool want = what == "path" ? npcOf(*c)->value("j", "") == "guard" : what == "shop" ? hasShop(npcOf(*c)->value("j", "")) : true;
        float d = glm::distance(c->pos, s.player->pos); if (want && d < bd) { bd = d; best = c; }
      }
      if (what == "board") app.pushScreen(makeQuestBoardScreen(ivec3(ifloor(s.player->pos.x), ifloor(s.player->pos.y), ifloor(s.player->pos.z))));
      else if (what == "path") app.pushScreen(makePathScreen("knight"));
      else if (best) app.pushScreen(makeVillagerTalkScreen(best->id, false, "", what == "shop"));
    }
  }
};

void registerVillagerClient() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession& s) { return std::make_shared<VillagerLayer>(s); });
}

} // namespace dq
