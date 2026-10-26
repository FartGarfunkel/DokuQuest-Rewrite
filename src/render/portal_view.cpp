// render/portal_view.cpp — true portal views, portal surfaces, client glue (see portal_view.h).
#include "render/portal_view.h"
#include "client/app.h"
#include "render/sky.h"
#include "sim/portals.h"
#include <cstdlib>
#include <set>

namespace dq {

namespace {

struct PVert { float x, y, z, u, v, px, py; };

// Lengyel's oblique near plane: the clip plane C (eye space, camera on its negative side) becomes the near plane
mat4 obliqueProjection(mat4 P, vec4 C) {
  auto sgn = [](float a) { return a > 0 ? 1.f : a < 0 ? -1.f : 0.f; };
  vec4 q;
  q.x = (sgn(C.x) + P[2][0]) / P[0][0];
  q.y = (sgn(C.y) + P[2][1]) / P[1][1];
  q.z = -1.0f;
  q.w = (1.0f + P[2][2]) / P[3][2];
  vec4 c = C * (2.0f / glm::dot(C, q));
  P[0][2] = c.x; P[1][2] = c.y; P[2][2] = c.z + 1.0f; P[3][2] = c.w;
  return P;
}

struct Live { PortalWindow w; PortalDest d; float dist; };

class PortalViewLayer : public RenderLayer {
public:
  explicit PortalViewLayer(GameSession& s) {
    shader.load("portal.glsl", "portal.glsl");
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PVert), (void*)20);
    glBindVertexArray(0);
    if (const char* t = std::getenv("DQ_PORTAL_TEST")) { test.kind = t; test.on = true; }
    if (const char* t = std::getenv("DQ_PORTAL_TEST_SHOT")) test.shot = t;
  }
  ~PortalViewLayer() override { glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao); }
  const char* name() const override { return "portalView"; }
  int order() const override { return 15; }   // before particles (20): rain in front of a window must draw over it

  void afterPlayerMove(GameSession& s, double dt) override {
    if (test.on) testStep(s, dt);
    if (PortalSystem* ps = portalSystem(*s.level)) ps->track(*s.player, dt, true);
  }

  void streamAnchors(GameSession& s, std::vector<StreamAnchor>& a) override {
    want.clear();
    PortalSystem* ps = portalSystem(*s.level);
    if (!ps) return;
    ps->farAnchors(s.player->realm, s.player->pos, s.app.settings.renderDist, want);
    auto it = want.find(s.player->realm);
    s.worldRenderer.keepNear.clear();
    if (it != want.end()) {
      a.insert(a.end(), it->second.begin(), it->second.end());
      for (auto& sa : it->second) s.worldRenderer.keepNear.push_back(sa.pos);
    }
  }

  void update(GameSession& s, double dt) override {
    int realm = s.player->realm;
    if (realm != lastRealm) {
      // crossed into another realm: the session's renderer was switched over, mesh it all
      if (lastRealm != -1) s.worldRenderer.remeshAll();
      lastRealm = realm;
      realmRenderers.erase(realm);
    }
    // the far realms seen through portals: keep their far ends streamed (the player's own is the session's)
    std::set<int> now;
    for (auto& [r, a] : want) if (r != realm) { s.level->world(r).stream(a, 1.5); now.insert(r); streamed.insert(r); }
    for (auto it = streamed.begin(); it != streamed.end();) {
      if (now.count(*it) || *it == realm) { ++it; continue; }
      s.level->world(*it).stream({}, 0.5);   // let it go
      it = streamed.erase(it);
    }
    for (auto it = realmRenderers.begin(); it != realmRenderers.end();) {
      if (!now.count(it->first) && s.app.time - it->second.used > 5) { it = realmRenderers.erase(it); continue; }
      Camera c = s.frame.cam; c.pos = want.count(it->first) && !want[it->first].empty() ? glm::dvec3(want[it->first][0].pos) : c.pos;
      it->second.r->update(c, *s.app.pool, 2.0);
      ++it;
    }
    // a window fades in once, when it first opens
    for (auto it = fades.begin(); it != fades.end();) it = lastLive.count(it->first) ? std::next(it) : fades.erase(it);
    for (auto& k : lastLive) fades[k] = std::min(1.f, fades[k] + (float)dt * 2.5f);
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (drawing) return;
    lastLive = std::move(live[0]);
    for (auto& l : live) l.clear();
    PortalSystem* ps = portalSystem(*s.level);
    if (!ps || !s.app.settings.portalViews || !shader.ok()) return;
    const std::string& pre = s.app.settings.preset;
    maxDepth = (pre == "high" || pre == "ultra" || (pre == "auto" && gfx.modern)) ? 2 : 1;
    if (const char* d = std::getenv("DQ_PORTAL_DEPTH")) maxDepth = clampv(std::atoi(d), 1, 3);
    drawing = true;
    glEnable(GL_STENCIL_TEST); glStencilMask(0xFF);
    drawViews(s, f, s.player->realm, 0, "");
    glDisable(GL_STENCIL_TEST); glStencilFunc(GL_ALWAYS, 0, 0xFF); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glDepthFunc(GL_LEQUAL); glEnable(GL_DEPTH_TEST);
    drawing = false;
  }

  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    if (drawing) return;
    drawSurfaces(s, f, s.player->realm, live[0], glm::dvec3(1e30));
  }

private:
  Shader shader;
  GLuint vao = 0, vbo = 0;
  std::vector<PVert> verts;
  struct RealmR { std::unique_ptr<WorldRenderer> r; double used = 0; };
  std::map<int, RealmR> realmRenderers;
  std::map<int, std::vector<StreamAnchor>> want;
  std::set<int> streamed;
  std::map<std::string, float> fades;
  std::set<std::string> live[4], lastLive;   // windows drawn as views this frame, by depth
  int lastRealm = -1, maxDepth = 1;
  bool drawing = false;

  WorldRenderer* rendererFor(GameSession& s, int realm) {
    if (realm == s.player->realm) return &s.worldRenderer;
    auto& rr = realmRenderers[realm];
    if (!rr.r) {
      rr.r = std::make_unique<WorldRenderer>();
      rr.r->init(s.app.settings, s.app.tileTex);
      rr.r->setWorld(&s.level->world(realm));
      rr.r->remeshAll();
    }
    rr.used = s.app.time;
    return rr.r.get();
  }

  // ---- geometry ----
  void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, vec2 pa, vec2 pb, vec2 pc, vec2 pd) {
    verts.push_back({a.x, a.y, a.z, ua.x, ua.y, pa.x, pa.y}); verts.push_back({b.x, b.y, b.z, ub.x, ub.y, pb.x, pb.y}); verts.push_back({c.x, c.y, c.z, uc.x, uc.y, pc.x, pc.y});
    verts.push_back({a.x, a.y, a.z, ua.x, ua.y, pa.x, pa.y}); verts.push_back({c.x, c.y, c.z, uc.x, uc.y, pc.x, pc.y}); verts.push_back({d.x, d.y, d.z, ud.x, ud.y, pd.x, pd.y});
  }
  // the window's cells as quads on its plane, `off` along the normal (both faces of a block: -0.5 and +0.5)
  void buildPlane(const PortalWindow& w, const Camera& cam, float off) {
    glm::dvec3 o = cam.origin;
    auto R = [&](double x, double y, double z) { return vec3(glm::dvec3(x, y, z) - o); };
    if (w.axis == 3) {
      double hw = w.w * 0.62, hh = w.h * 0.56;
      glm::dvec3 c = w.c + glm::dvec3(w.n) * (double)off, al(w.along);
      glm::dvec3 a = c - al * hw - glm::dvec3(0, hh, 0), b = c + al * hw - glm::dvec3(0, hh, 0), cc = c + al * hw + glm::dvec3(0, hh, 0), d = c - al * hw + glm::dvec3(0, hh, 0);
      quad(R(a.x, a.y, a.z), R(b.x, b.y, b.z), R(cc.x, cc.y, cc.z), R(d.x, d.y, d.z), {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {w.w, 0}, {w.w, w.h}, {0, w.h});
      return;
    }
    vec3 sz = vec3(w.hi - w.lo) + vec3(1);
    for (auto& p : w.cells) {
      double x = p.x, y = p.y, z = p.z;
      if (w.axis == 2) {
        double yy = y + 0.755 + off;   // just over the pool's slab top (theend.js SHAPES.end_portal 11.5..12/16)
        float u0 = (p.x - w.lo.x) / sz.x, u1 = u0 + 1 / sz.x, v0 = (p.z - w.lo.z) / sz.z, v1 = v0 + 1 / sz.z;
        vec2 q((float)(p.x - w.anchor.x), (float)(p.z - w.anchor.z));
        quad(R(x, yy, z), R(x + 1, yy, z), R(x + 1, yy, z + 1), R(x, yy, z + 1), {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}, q, q + vec2(1, 0), q + vec2(1, 1), q + vec2(0, 1));
        continue;
      }
      float h = w.axis ? (float)(p.z - w.lo.z) : (float)(p.x - w.lo.x), hs = w.axis ? sz.z : sz.x;
      float u0 = h / hs, u1 = u0 + 1 / hs, v0 = (p.y - w.lo.y) / sz.y, v1 = v0 + 1 / sz.y;
      vec2 q(w.axis ? (float)(p.z - w.anchor.z) : (float)(p.x - w.anchor.x), (float)(p.y - w.anchor.y));
      if (w.axis) { double xx = x + 0.5 + off; quad(R(xx, y, z), R(xx, y, z + 1), R(xx, y + 1, z + 1), R(xx, y + 1, z), {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}, q, q + vec2(1, 0), q + vec2(1, 1), q + vec2(0, 1)); }
      else { double zz = z + 0.5 + off; quad(R(x, y, zz), R(x + 1, y, zz), R(x + 1, y + 1, zz), R(x, y + 1, zz), {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}, q, q + vec2(1, 0), q + vec2(1, 1), q + vec2(0, 1)); }
    }
  }
  // where the mask goes: the portal's plane, or its far face while the camera is inside the portal slab
  // (so the near plane never cuts the window away as you step through)
  float maskOffset(const PortalWindow& w, glm::dvec3 cam) {
    double s = glm::dot(glm::dvec3(w.n), cam - w.c);
    if (w.axis == 3) return std::abs(s) < 0.1 ? (s > 0 ? -0.1f : 0.1f) : 0.f;
    if (std::abs(s) > 0.56) return 0;
    glm::dvec3 lo = glm::dvec3(w.lo) - 0.3, hi = glm::dvec3(w.hi) + 1.3;
    for (int k = 0; k < 3; k++) if (w.n[k] == 0 && (cam[k] < lo[k] || cam[k] > hi[k])) return 0;
    return s > 0 ? -0.5f : 0.5f;
  }
  void flush(GLenum mode = GL_TRIANGLES) {
    if (verts.empty()) return;
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(PVert), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    verts.clear();
  }
  void setup(const FrameCtx& f, int mode, const PortalWindow* w, float fade = 1) {
    shader.use();
    f.apply(shader);
    shader.set("uViewProj", f.cam.viewProj);
    shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
    shader.set("uMode", mode);
    if (w) {
      shader.set("uKind", (int)w->kind);
      shader.set("uRim", w->rim);
      shader.set("uTorn", w->torn ? 1 : 0);
    }
    shader.set("uFade", fade);
  }

  // ---- choosing the windows to look through ----
  std::vector<Live> pick(GameSession& s, const FrameCtx& f, int realm, int level, const glm::dvec3& skipNear) {
    std::vector<Live> out;
    PortalSystem* ps = portalSystem(*s.level);
    float far = std::max(48.f, s.app.settings.renderDist * 16.f);
    for (auto& w : ps->windows(realm)) {
      float dist = (float)glm::distance(f.cam.pos, w.c);
      if (dist > far || glm::distance(w.c, skipNear) < 1.5) continue;
      vec3 lo = vec3(glm::dvec3(w.lo) - f.cam.origin), hi = vec3(glm::dvec3(w.hi) + 1.0 - f.cam.origin);
      if (dist > 2 && !f.cam.frustum.box(lo, hi)) continue;
      PortalDest d = ps->dest(w);
      if (!d.ready) continue;
      out.push_back({w, d, dist});
    }
    std::sort(out.begin(), out.end(), [](const Live& a, const Live& b) { return a.dist < b.dist; });
    if ((int)out.size() > (level == 0 ? 3 : 1)) out.resize(level == 0 ? 3 : 1);
    return out;
  }

  // ---- the view through one window, then the windows inside it ----
  void drawViews(GameSession& s, const FrameCtx& f, int realm, int level, const std::string& parentKey, glm::dvec3 skipNear = glm::dvec3(1e30)) {
    std::vector<Live> lives = pick(s, f, realm, level, skipNear);
    for (auto& L : lives) {
      int ref = level;
      float off = maskOffset(L.w, f.cam.pos);
      // 1. mark the window where it's in sight (stencil ref -> ref+1)
      glStencilFunc(GL_EQUAL, ref, 0xFF); glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_FALSE); glDepthFunc(GL_LEQUAL); glEnable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      setup(f, 1, &L.w); buildPlane(L.w, f.cam, off); flush();
      // 2. clear depth inside it
      glStencilFunc(GL_EQUAL, ref + 1, 0xFF); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
      glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
      setup(f, 3, nullptr); glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthFunc(GL_LEQUAL);
      // 3. the far side, from where you'd be standing over there
      PortalXform x = portalXform(L.w, L.d, f.cam.pos);
      FrameCtx vf = f;
      Camera& vc = vf.cam;
      vc.pos = x.point(f.cam.pos); vc.yaw = f.cam.yaw + x.yaw; vc.realm = L.d.realm;
      vc.update();
      s.sky->computeFrame(*s.level, vf, s.app.settings.renderDist, false);
      s.sky->draw(vf);
      vec3 n = L.d.n;
      if (glm::dot(glm::dvec3(n), vc.pos - L.d.c) > 0) n = -n;
      // cut away what lies between that camera and the far portal's plane (the mask's own plane: the far
      // face while you stand in the portal), so the floor runs on unbroken through the window
      vec4 clipRel(n, (float)(glm::dot(glm::dvec3(n), vc.origin - L.d.c) - (std::abs(off) + 0.01)));
      vec4 clipEye = glm::transpose(glm::inverse(vc.view)) * clipRel;
      vc.proj = obliqueProjection(vc.proj, clipEye);
      vc.viewProj = vc.proj * vc.view;
      vc.frustum.fromMatrix(vc.viewProj);
      vf.portalDepth = level + 1;
      glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
      WorldRenderer* wr = rendererFor(s, L.d.realm);
      bool sameRealm = L.d.realm == s.player->realm;
      wr->pushView();
      vec4 keepClip = wr->clipPlane;
      wr->clipPlane = clipRel;
      wr->drawOpaque(vf);
      wr->drawCutout(vf);
      if (sameRealm) for (auto& l : s.layers) if (l.get() != this) l->drawOpaque(s, vf);
      if (level + 1 < maxDepth) {
        wr->clipPlane = keepClip;
        drawViews(s, vf, L.d.realm, level + 1, L.w.key, L.d.c);
        glStencilFunc(GL_EQUAL, ref + 1, 0xFF); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glDepthFunc(GL_LEQUAL); glEnable(GL_DEPTH_TEST);
        wr->clipPlane = clipRel;
      }
      wr->drawTranslucent(vf);
      if (sameRealm) for (auto& l : s.layers) if (l.get() != this) l->drawTranslucent(s, vf);
      drawSurfaces(s, vf, L.d.realm, live[std::min(level + 1, 3)], L.d.c);
      wr->clipPlane = keepClip;
      wr->popView();
      // 4. the glow at its rim, and its fade-in the first time it opens
      glStencilFunc(GL_EQUAL, ref + 1, 0xFF);
      glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      float fade = level == 0 ? (fades.count(L.w.key) ? fades[L.w.key] : 0.f) : 1.f;
      setup(f, 2, &L.w, fade); buildPlane(L.w, f.cam, off); flush();
      glDisable(GL_BLEND);
      // 5. put the window's depth back and return its stencil to ref
      glEnable(GL_DEPTH_TEST);
      glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
      setup(f, 1, &L.w); buildPlane(L.w, f.cam, off); flush();
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE);
      glStencilFunc(GL_EQUAL, ref, 0xFF); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
      live[std::min(level, 3)].insert(L.w.key);
    }
    (void)parentKey;
  }

  // the portals that aren't windows right now: their own animated picture (both faces of the block)
  void drawSurfaces(GameSession& s, const FrameCtx& f, int realm, const std::set<std::string>& skip, glm::dvec3 skipNear) {
    PortalSystem* ps = portalSystem(*s.level);
    if (!ps || !shader.ok()) return;
    float far = std::max(48.f, s.app.settings.renderDist * 16.f) * 2;
    bool any = false;
    for (auto& w : ps->windows(realm)) {
      if (skip.count(w.key) || glm::distance(w.c, skipNear) < 1.5) continue;
      if (glm::distance(f.cam.pos, w.c) > far) continue;
      vec3 lo = vec3(glm::dvec3(w.lo) - f.cam.origin), hi = vec3(glm::dvec3(w.hi) + 1.0 - f.cam.origin);
      if (!f.cam.frustum.box(lo, hi)) continue;
      if (!any) {
        any = true;
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
      }
      setup(f, 0, &w);
      if (w.axis == 2 || w.axis == 3) buildPlane(w, f.cam, 0);
      else { buildPlane(w, f.cam, -0.499f); buildPlane(w, f.cam, 0.499f); }
      flush();
    }
    if (any) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); }
  }

  // ---- smoke test: build, light and walk through a portal ----
  struct Test {
    bool on = false; std::string kind, shot;
    int phase = 0, frames = 0, wait = 0;
    ivec3 base{0}; std::string key; int startDim = 0, startRealm = 0; float walked = 0, after = 0;
  } test;
  void shotNamed(GameSession& s, const char* suffix) {
    if (test.shot.empty()) return;
    s.app.takeScreenshot(test.shot + suffix);
  }
  void testStep(GameSession& s, double dt) {
    Test& t = test;
    t.frames++;
    Player& p = *s.player;
    World& W = s.world();
    PortalSystem* ps = portalSystem(*s.level);
    if (!ps) return;
    const Registry& R = reg();
    float step = std::min((float)dt, 0.05f);
    switch (t.phase) {
      case 0: {
        bool ready = t.frames > 20 && W.pendingGeneration() == 0 && s.worldRenderer.pendingJobs == 0 && p.onGround;
        if (!ready && t.frames < 160) return;
        p.yaw = 0; p.pitch = t.kind == "end" ? -0.55f : -0.06f;
        int bx = ifloor(p.pos.x) - 1, by = ifloor(p.pos.y + 0.01f), bz = ifloor(p.pos.z) - 4;
        t.base = ivec3(bx, by, bz); t.startDim = dimOfY(by); t.startRealm = p.realm;
        BlockId cobble = R.blockId("cobblestone");
        for (int dz = -4; dz <= 6; dz++) for (int dx = -3; dx <= 6; dx++) {
          W.setBlock(bx + dx, by - 1, bz + dz, cobble, 0);
          for (int dy = 0; dy < 7; dy++) W.setBlock(bx + dx, by + dy, bz + dz, AIR, 0);
        }
        p.pos = vec3(bx + 1.5f + (t.kind == "end" ? 0.5f : 0.f), (float)by, bz + 4.5f); p.vel = vec3(0);
        json use;
        if (t.kind == "end") {
          BlockId fr = R.blockId("end_portal_frame"), eye = R.blockId("end_portal_frame_eye");
          int cx = bx + 2, cz = bz;   // pool centre; frames at floor level round it
          for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { W.setBlock(cx + dx, by - 1, cz + dz, AIR, 0); W.setBlock(cx + dx, by - 2, cz + dz, cobble, 0); }
          for (int i = -1; i <= 1; i++) for (ivec2 r : {ivec2(cx + i, cz - 2), ivec2(cx + i, cz + 2), ivec2(cx - 2, cz + i), ivec2(cx + 2, cz + i)}) W.setBlock(r.x, by - 1, r.y, eye, 0);
          W.setBlock(cx, by - 1, cz + 2, fr, 0);
          p.inv[p.selected] = ItemStack("ender_eye", 1);
          use = json::array({p.name, "ender_eye", cx, by - 1, cz + 2, 2});
          t.base = ivec3(cx, by - 1, cz);
        } else if (t.kind == "rift") {
          // a free-standing tear into the pocket realm (a side World), the way riftarc.js's gateway is
          VirtualPortal v;
          v.realm = p.realm; v.bottom = glm::dvec3(bx + 2, by, bz + 0.5); v.yaw = 0;
          v.destRealm = REALM_POCKET;
          DimGenerator* g = s.level->world(REALM_POCKET).gen->forDim(DIM_OVERWORLD);
          int h = g ? g->spawnHeight(0, 0) : 80;
          v.destBottom = glm::dvec3(0.5, dimBase(DIM_OVERWORLD) + std::max(h, SEA_LEVEL + 1), 0.5); v.destYaw = 0;
          ps->setVirtual("test", v);
          t.base = ivec3(bx, by, bz);
          DQ_LOG("portal test: rift into realm %d at floor %.0f", v.destRealm, v.destBottom.y);
          t.phase = 1; t.wait = 0;
          return;
        } else if (t.kind == "sky") {
          BlockId glow = R.blockId("glowstone"), water = R.blockId("water");
          for (int dx = 0; dx < 4; dx++) for (int dy = 0; dy < 5; dy++) W.setBlock(bx + dx, by - 1 + dy, bz, (dx == 0 || dx == 3 || dy == 0 || dy == 4) ? glow : AIR, 0);
          W.setBlock(bx + 1, by, bz, water, 0);   // an earlier try's water standing in it
          p.inv[p.selected] = ItemStack("water_bucket", 1);
          use = json::array({p.name, "water_bucket", bx, by + 1, bz, 4});   // the frame's side, from the front
        } else {
          BlockId obs = R.blockId("obsidian");
          for (int dx = 0; dx < 4; dx++) for (int dy = 0; dy < 5; dy++) W.setBlock(bx + dx, by - 1 + dy, bz, (dx == 0 || dx == 3 || dy == 0 || dy == 4) ? obs : AIR, 0);
          p.inv[p.selected] = ItemStack("flint_and_steel", 1);
          use = json::array({p.name, "flint_and_steel", bx + 1, by - 1, bz, 2});
        }
        bool def = s.app.events.emit("itemUse", use);
        DQ_LOG("portal test: built %s portal at %d,%d,%d (itemUse handled: %s, held now %s)", t.kind.c_str(), t.base.x, t.base.y, t.base.z, def ? "no" : "yes", p.held().empty() ? "-" : p.held().id.c_str());
        t.phase = 1; t.wait = 0;
        return;
      }
      case 1: {
        const PortalWindow* win = nullptr;
        for (auto& w : ps->windows(p.realm)) if (std::abs(w.c.x - (t.base.x + 2)) < 2.1 && std::abs(w.c.z - t.base.z - 0.5) < 1.1) { win = &w; break; }
        if (!win) { if (t.frames > 2000) t.phase = 9; return; }
        t.key = win->key;
        PortalDest d = ps->dest(*win);
        if (!d.ready) return;
        if (t.wait == 0) DQ_LOG("portal test: %s leads to realm %d (%.1f, %.1f, %.1f) [%s]", win->key.c_str(), d.realm, d.c.x, d.c.y, d.c.z, dimName(dimOfY(ifloor(d.c.y))));
        t.wait++;
        if (t.wait < 30 || (t.wait < 200 && (s.worldRenderer.pendingJobs > 0 || W.pendingGeneration() > 0))) return;
        shotNamed(s, "-window.png");
        t.phase = 2;
        return;
      }
      case 2: {
        // walk in (the End's pool: walk over it and drop in)
        bool across = p.realm != t.startRealm || dimOfY(ifloor(p.pos.y)) != t.startDim;
        if (!across) {
          p.pos.z -= 3.0f * step; t.walked += 3.0f * step;
          if (t.walked > 12) { DQ_LOG("portal test: walked 12 blocks without crossing"); t.phase = 9; }
          return;
        }
        DQ_LOG("portal test: across at %.2f %.2f %.2f (%s, realm %d), yaw %.2f", p.pos.x, p.pos.y, p.pos.z, dimName(dimOfY(ifloor(p.pos.y))), p.realm, p.yaw);
        t.phase = 3; t.after = 0; t.wait = 0;
        return;
      }
      case 3: {
        if (t.kind != "end" && t.after < 1.2f) { vec3 f(-std::sin(p.yaw), 0, -std::cos(p.yaw)); p.pos += f * 3.0f * step; t.after += 3.0f * step; return; }
        if (++t.wait < 15) return;
        shotNamed(s, "-crossed.png");
        t.phase = 4; t.wait = 0;
        return;
      }
      case 4:   // turn round the frame after the shot was taken
        p.yaw += PI; p.pitch = t.kind == "end" ? 0.4f : -0.06f;
        t.phase = 5;
        return;
      case 5:
        if (++t.wait < 12) return;
        shotNamed(s, "-back.png");
        DQ_LOG("portal test: done at %.2f %.2f %.2f (%s)", p.pos.x, p.pos.y, p.pos.z, dimName(dimOfY(ifloor(p.pos.y))));
        t.phase = 9;
        return;
      default: return;
    }
  }
};

} // namespace

void registerPortalView() {
  registerRenderLayer([](GameSession& s) -> std::shared_ptr<RenderLayer> { return std::make_shared<PortalViewLayer>(s); });
}

} // namespace dq
