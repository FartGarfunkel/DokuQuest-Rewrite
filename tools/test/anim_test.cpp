// tools/test/anim_test.cpp — headless checks for the player animation (render/char_anim, render/char_blend,
// render/char_poses): state changes blend instead of snapping, settled poses are untouched (no regression), the new
// poses (drink, hurt flinch, death crumple, sleep, jump lead leg, shield, crossbow, trident, spear) do what they say,
// and nothing produces NaN. Pure functions only, no GL. Run: ./build/test_anim (exit code = failures)
#include "render/char_anim.h"
#include "render/char_blend.h"
#include "render/fp_motion.h"
#include "render/place_pop.h"
#include "assets/item_models.h"
#include "render/cape_sim.h"
#include <cstdio>
#include <string>
#include <cmath>

using namespace dq;
static int fails = 0, passes = 0;
#define CHECK(c, ...) do { if (c) { printf("  ok   " __VA_ARGS__); passes++; } else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static float diff(const CharAngles& a, const CharAngles& b) {
  const vec3* pa = &a.body; const vec3* pb = &b.body; float d = std::fabs(a.lift - b.lift);
  for (int i = 0; i < 10; i++) d = std::max(d, glm::length(pa[i] - pb[i]));
  return d;
}
static float mdiff(const mat4& a, const mat4& b) { float d = 0; for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) d = std::max(d, std::fabs(a[c][r] - b[c][r])); return d; }
static bool finite(const CharAngles& a) { const vec3* p = &a.body; for (int i = 0; i < 10; i++) if (!std::isfinite(p[i].x + p[i].y + p[i].z)) return false; return std::isfinite(a.lift); }
static CharRig rigWith(float ang, float tx) {
  CharRig R;
  for (int i = 0; i < CP_COUNT; i++) {
    R.part[i] = glm::rotate(glm::translate(glm::scale(mat4(1), vec3(CHAR_PX)), vec3(tx, (float)i, 0)), ang, vec3(1, 0, 0));
    R.scale[i] = mat4(1);
  }
  R.body = R.bodyRaw = R.part[CP_BODY]; R.gear["cape"] = R.part[CP_BODY];
  return R;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::string(argv[1]) == "--dump") {   // angles through one cycle of a dev pose, for a stick-figure look (swim, paddle)
    const std::string what = argv[2];
    for (int i = 0; i < 12; i++) {
      CharPose p; p.time = 3; p.swimPh = i * TAU / 12; p.swimMv = 1;
      if (what == "swim") p.swim = true; else if (what == "paddle") p.paddle = true;
      if (what == "tread") { p.paddle = true; p.swimMv = 0; }
      const CharAngles A = charAnimate(p); const vec3* v = &A.body;
      for (int k = 0; k < 10; k++) printf("%.4f %.4f %.4f ", v[k].x, v[k].y, v[k].z);
      printf("%.4f\n", A.lift);
    }
    return 0;
  }
  printf("[state keys]\n");
  {
    CharPose a, b = a; b.walk = 3; b.time = 9; b.headPitch = 0.4f;
    CHECK(charStateKey(a) == charStateKey(b), "walk phase, time and look don't change the state");
    b.sneak = true; CHECK(charStateKey(a) != charStateKey(b), "sneaking is a state");
    CharPose c; c.swim = true; CharPose d; d.sleep = true; CharPose e; e.eat = 0.5f; CharPose f; f.air = true;
    CHECK(charStateKey(c) != charStateKey(a) && charStateKey(d) != charStateKey(a) && charStateKey(e) != charStateKey(a) && charStateKey(f) != charStateKey(a), "swim, sleep, eat and air are states");
    CHECK(charBlendDuration(charStateKey(a), charStateKey(c)) > charBlendDuration(charStateKey(a), charStateKey(f)), "lying down to swim takes longer than a hop");
    CharPose g; g.fp = true;
    CHECK(charBlendDuration(charStateKey(a), charStateKey(g)) == 0, "switching to first person snaps");
  }
  printf("[matrix blend]\n");
  {
    mat4 A = glm::rotate(glm::scale(glm::translate(mat4(1), vec3(1, 2, 3)), vec3(CHAR_PX)), 0.f, vec3(1, 0, 0));
    mat4 B = glm::rotate(glm::scale(glm::translate(mat4(1), vec3(3, 2, 1)), vec3(CHAR_PX)), PI / 2, vec3(1, 0, 0));
    CHECK(mdiff(blendMat(A, B, 0), A) < 1e-5f && mdiff(blendMat(A, B, 1), B) < 1e-5f, "w 0 is the old matrix, w 1 the new");
    mat4 M = blendMat(A, B, 0.5f);
    const float len = glm::length(vec3(M[1]));
    CHECK(std::fabs(len - CHAR_PX) < 1e-4f, "half way keeps the limb's size (%.5f vs %.5f): a turn, not a shrink", len, CHAR_PX);
    const float ang = std::atan2(M[1].z, M[1].y);
    CHECK(std::fabs(ang - PI / 4) < 1e-3f, "half way is turned half way (%.3f)", ang);
    CHECK(glm::length(vec3(M[3]) - vec3(2, 2, 2)) < 1e-4f, "and moved half way");
    // a squashed frame (a short or broad people's stature) blends back to itself
    mat4 S = glm::rotate(glm::scale(mat4(1), vec3(1.2f, 0.8f, 1.2f)), 0.7f, vec3(0, 1, 0));
    CHECK(mdiff(blendMat(S, S, 0.37f), S) < 1e-4f, "a scaled frame blended with itself is unchanged");
  }
  printf("[rig blender]\n");
  {
    const mat4 base = glm::translate(mat4(1), vec3(100, 64, -40));
    CharBlend bl;
    CharRig r0 = rigWith(0, 0); for (auto& m : r0.part) m = base * m; r0.body = base * r0.body; r0.gear["cape"] = base * r0.gear["cape"];
    CharRig live = r0;
    charBlendStep(bl, live, base, 1, 0.0);
    CHECK(mdiff(live.part[CP_HEAD], r0.part[CP_HEAD]) < 1e-6f, "the first frame is the live pose");
    live = r0; charBlendStep(bl, live, base, 1, 0.016);
    CHECK(mdiff(live.part[CP_ARMR_U], r0.part[CP_ARMR_U]) < 1e-6f, "settled in one state: exactly the live pose (no regression)");
    CharRig r1 = rigWith(1.2f, 4); for (auto& m : r1.part) m = base * m; r1.body = base * r1.body; r1.gear["cape"] = base * r1.gear["cape"];
    live = r1; charBlendStep(bl, live, base, 2, 0.032);
    CHECK(mdiff(live.part[CP_ARMR_U], r0.part[CP_ARMR_U]) < 1e-5f, "the frame the state changes starts where the last frame was (no snap)");
    float prev = 0; bool mono = true, bounded = true;
    for (int i = 1; i <= 40; i++) {
      live = r1; charBlendStep(bl, live, base, 2, 0.032 + i * 0.008);
      const float d = mdiff(live.part[CP_ARMR_U], r0.part[CP_ARMR_U]);
      if (d + 1e-6f < prev) mono = false;
      if (d - prev > 0.03f) bounded = false;
      prev = d;
    }
    CHECK(mono, "it moves steadily toward the new pose");
    CHECK(bounded, "no jump bigger than a small step per 8 ms");
    CHECK(mdiff(live.part[CP_ARMR_U], r1.part[CP_ARMR_U]) < 1e-6f && mdiff(live.gear["cape"], r1.gear["cape"]) < 1e-6f, "after the blend: exactly the new pose, gear too");
    // moving while blending: the blend is about the feet, so a walk doesn't drag the body behind
    const mat4 base2 = glm::translate(base, vec3(5, 0, 0));
    CharRig rm = r1; for (auto& m : rm.part) m = base2 * glm::inverse(base) * m;
    live = rm; charBlendStep(bl, live, base2, 2, 0.5);
    CHECK(mdiff(live.part[CP_HEAD], rm.part[CP_HEAD]) < 1e-5f, "a moved base carries the pose with it");
  }
  printf("[poses]\n");
  {
    CharPose p; p.time = 3;
    const CharAngles base = charAnimate(p);
    CharPose e = p; e.eat = 0.5f, e.held = true; CharPose d = e; d.drink = true;
    const CharAngles E = charAnimate(e), D = charAnimate(d);
    CHECK(D.head.x < E.head.x - 0.15f, "drinking tips the head back (%.2f vs eating %.2f)", D.head.x, E.head.x);
    CHECK(D.armR.x > E.armR.x, "and lifts the bottle higher");
    CharPose h = p; h.hurt = 0.06f;
    const CharAngles H = charAnimate(h);
    CHECK(diff(H, base) > 0.1f && H.body.x > base.body.x, "a hit flinches the body back");
    h.hurt = 0.6f; CHECK(diff(charAnimate(h), base) < 1e-6f, "the flinch is over after a moment");
    h.hurt = 0; CHECK(diff(charAnimate(h), base) < 1e-6f, "unhurt: the old pose exactly");
    CharPose k = p; k.deathT = 0.12f;
    const CharAngles K = charAnimate(k);
    CHECK(K.shinR.x < -0.4f && K.lift < -0.05f, "dying: the knees give first");
    CharPose s = p; s.sleep = true;
    const CharAngles Sl = charAnimate(s);
    CHECK(std::fabs(Sl.head.y) > 0.1f && std::fabs(Sl.armR.x) < 0.5f, "asleep: the head turned to the side, the arms at rest");
    CharPose j = p; j.air = true; j.vy = 1; CharPose jl = j; jl.airLeft = true;
    const CharAngles J = charAnimate(j), JL = charAnimate(jl);
    CHECK(std::fabs(J.legR.x - JL.legL.x) < 1e-5f && std::fabs(J.shinR.x - JL.shinL.x) < 1e-5f, "jumps lead with either leg");
    CharPose b = p; b.block = 1; b.off = true;
    const CharAngles B = charAnimate(b);
    CHECK(B.armL.x > 0.6f && B.foreL.x > 1.0f && B.body.y > 0.2f, "a shield raised across the body (the forearm across it, turned shield side on)");
    CharPose bg = b; bg.held = true;
    CHECK(charAnimate(bg).armR.x > 1.5f, "on guard, the weapon drawn back over the other shoulder");
    CharPose bm = p; bm.block = 1; bm.blockMain = true; bm.held = true;
    const CharAngles BM = charAnimate(bm);
    CHECK(std::fabs(BM.armR.x - B.armL.x) < 1e-4f && std::fabs(BM.armR.y + B.armL.y) < 1e-4f && BM.body.y < -0.2f, "a shield in the right hand: the same guard, mirrored");
    CharPose x = p; x.xbowAim = true; x.held = true;
    const CharAngles X = charAnimate(x);
    CHECK(X.armR.x > 1.2f && X.armL.x > 1.2f, "a crossbow levelled in both hands");
    CharPose xc = p; xc.xbowCharge = 0.5f; xc.held = true;
    CHECK(diff(charAnimate(xc), base) > 0.3f, "a crossbow being wound");
    CharPose tr = p; tr.throwCharge = 1; tr.held = true;
    CHECK(charAnimate(tr).armR.x > 2.5f, "a trident drawn back over the shoulder");
    CharPose sp = p; sp.spearCharge = 1; sp.held = true;
    const CharAngles SP = charAnimate(sp);
    CHECK(SP.armR.x > 0.8f && SP.armR.x < 1.8f && SP.body.x < base.body.x, "a spear couched, leaning into the charge");
    CharPose bk = p; bk.walkAmt = 1; bk.walk = 1; CharPose bw = bk; bw.backward = true;
    CHECK(diff(charAnimate(bk), charAnimate(bw)) > 0.05f, "walking backwards looks different");
  }
  printf("[swimming]\n");
  {
    bool range = true, mirror = true; float lo = 9, hi = -9, wide = 0, knee = 0;
    for (int i = 0; i < 200; i++) {
      CharPose sw; sw.swim = true; sw.swimPh = i * TAU / 200; sw.time = 3;
      const CharAngles S = charAnimate(sw);
      if (S.armR.x < 1.4f || S.armR.x > PI + 0.1f) range = false;
      if (std::fabs(S.armR.x - S.armL.x) > 1e-4f || std::fabs(S.armR.z + S.armL.z) > 1e-4f) mirror = false;
      lo = std::min(lo, S.armR.x); hi = std::max(hi, S.armR.x); wide = std::max(wide, S.armR.z); knee = std::min(knee, S.shinR.x);
    }
    CHECK(range, "a breaststroke: the arms stay out in front (%.2f..%.2f), never going round the back", lo, hi);
    CHECK(mirror, "both arms together, mirrored");
    CHECK(hi > 2.9f && lo < 2.0f, "reaching right out ahead, then pulling in under the chest");
    CHECK(wide > 0.8f, "sweeping out wide (%.2f)", wide);
    CHECK(knee < -1.0f, "a frog kick: the knees draw up (%.2f)", knee);
  }
  printf("[first person]\n");
  {
    CHECK(fpSwingRate("diamond_sword", false) == 3.6f && fpSwingRate("", false) == 3.6f && fpSwingRate("apple", false) == 3.6f, "a sword, a fist, an apple: Minecraft's swing as before");
    CHECK(fpSwingRate("iron_axe", false) < 3.6f, "an axe swings heavier");
    CHECK(fpSwingRate("iron_pickaxe", true) >= 3.6f, "mining doesn't slow down");
    CHECK(fpSwapLower(0) == 0 && fpSwapLower(1) == 0 && fpSwapLower(0.4f) > 0.9f, "a swap: the old thing goes down, the new comes up");
    float lo = 1; for (float t = 0.45f; t <= 1; t += 0.01f) lo = std::min(lo, fpSwapLower(t));
    CHECK(lo < 0 && lo > -0.15f, "and it overshoots a touch as it settles (%.3f)", lo);
    CHECK(fpSwapShowsOld(0.2f) && !fpSwapShowsOld(0.6f), "the old item is in the hand for the first half");
    FpState z; const FpOffset o0 = fpStateOffset(z, 2.f);
    CHECK(o0.dy == 0 && o0.pitch == 0 && o0.roll == 0 && o0.dz == 0, "standing still: no offset (no regression)");
    FpState r; r.sprint = 1; const FpOffset o1 = fpStateOffset(r, 2.f);
    CHECK(o1.dy < -0.03f && o1.pitch != 0, "sprinting: the item drops and tips");
    FpState l; l.land = 1; CHECK(fpStateOffset(l, 2.f).dy < -0.05f, "a landing dips the hand");
    FpState c; c.climb = 1; CHECK(fpStateOffset(c, 2.f).dy < -0.2f, "on a ladder the item goes down out of the way");
    FpState d; d.drink = 1; CHECK(fpStateOffset(d, 2.f).pitch > 0.3f, "drinking tips the bottle up");
  }
  printf("[placing a block]\n");
  {
    CHECK(placePopScale(0) == 1 && placePopScale(PLACE_POP_TIME) == 1 && placePopScale(-1) == 1 && placePopScale(5) == 1, "the block is its own size before and after the pop");
    float mx = 1; bool under = false;
    for (float t = 0; t <= PLACE_POP_TIME; t += 0.005f) { const float v = placePopScale(t); mx = std::max(mx, v); if (v < 0.999f) under = true; }
    CHECK(mx > 1.03f && mx < 1.1f, "it swells a little (%.3f) and settles", mx);
    CHECK(!under, "never smaller than the block (the real one is already there)");
  }
  printf("[no NaN]\n");
  {
    bool ok = true; unsigned r = 7;
    auto rnd = [&] { r = r * 1103515245u + 12345u; return ((r >> 8) & 0xffff) / 65535.f; };
    for (int i = 0; i < 4000 && ok; i++) {
      CharPose p;
      p.walk = rnd() * 50; p.walkAmt = rnd(); p.time = rnd() * 100; p.vy = rnd() * 40 - 20; p.headPitch = rnd() * 3 - 1.5f;
      p.sneak = rnd() < 0.2f; p.sprint = rnd() < 0.3f; p.air = rnd() < 0.3f; p.swim = rnd() < 0.1f; p.sleep = rnd() < 0.05f;
      p.eat = rnd() < 0.2f ? rnd() : 0; p.drink = rnd() < 0.5f; p.hurt = rnd() < 0.3f ? rnd() : 0; p.deathT = rnd() < 0.1f ? rnd() * 2 : 0;
      p.block = rnd() < 0.1f ? rnd() : 0; p.xbowCharge = rnd() < 0.1f ? rnd() : 0; p.xbowAim = rnd() < 0.1f;
      p.throwCharge = rnd() < 0.1f ? rnd() : 0; p.spearCharge = rnd() < 0.1f ? rnd() : 0; p.backward = rnd() < 0.2f; p.airLeft = rnd() < 0.5f;
      p.swing = rnd() < 0.3f ? rnd() : 0; p.land = rnd() < 0.2f ? rnd() * 0.3f : 0;
      if (!finite(charAnimate(p))) ok = false;
    }
    CHECK(ok, "4000 random poses, all finite");
  }
  {   // Eating Animation: the food in hand bitten down in three stages (assets/item_models.cpp biteVoxelGrid, eatModelKey)
    CHECK(eatModelKey("apple", 0) == "apple" && eatModelKey("apple", 0.1f) == "eat1:apple" && eatModelKey("apple", 0.5f) == "eat2:apple"
          && eatModelKey("apple", 0.99f) == "eat3:apple" && eatModelKey("apple", 1.f) == "eat3:apple", "the bite's stage by its progress: whole, then bitten 1, 2, 3 times");
    auto food = [] { VoxelGrid g(ivec3(16, 16, 3)); for (int z = 0; z < 3; z++) for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) g.set(x, y, z, voxRGBA(200, 30, 30)); return g; };
    VoxelGrid g1 = food(), g2 = food(), g3 = food(); biteVoxelGrid(g1, 1); biteVoxelGrid(g2, 2); biteVoxelGrid(g3, 3);
    const size_t all = food().count(), n1 = g1.count(), n2 = g2.count(), n3 = g3.count();
    CHECK(n1 < all && n2 < n1 && n3 < n2 && n3 > all / 3, "each bite takes more away, and some of it is left (%zu > %zu > %zu > %zu)", all, n1, n2, n3);
    CHECK(!g1.solid(15, 11, 1) && g1.solid(0, 0, 1) && g3.solid(0, 0, 1), "the bite comes out of the top corner; the far corner stays");
    bool paler = false;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (g1.solid(x, y, 1) && !g1.solid(x + 1, y, 1) && x < 15) paler |= (g1.at(x, y, 1) & 255) > 200 && ((g1.at(x, y, 1) >> 8) & 255) > 30;
    CHECK(paler && (g1.at(0, 0, 1) & 0xffffff) == (voxRGBA(200, 30, 30) & 0xffffff), "the bitten edge shows the paler inside; the rest keeps its colour");
  }
  {   // What Are They Up To: working a screen raises the forearms; dozing drops the head
    CharPose p; p.time = 1.3f;
    const CharAngles rest = charAnimate(p);
    CharPose g = p; g.gui = true; const CharAngles gu = charAnimate(g);
    CharPose d = p; d.doze = true; const CharAngles dz = charAnimate(d);
    CHECK(gu.armR.x > rest.armR.x + 0.6f && gu.armL.x > rest.armL.x + 0.6f && finite(gu), "in a menu: both arms up before them (%.2f, %.2f)", gu.armR.x, gu.armL.x);
    CHECK(dz.head.x > rest.head.x + 0.4f && finite(dz), "dozing: the head drops (%.2f)", dz.head.x);
  }
  {   // the cape as cloth (render/cape_sim): it hangs when still, trails behind a runner, never swings in through the body
    const vec3 anchor(0, 12, 2.1f);
    auto bodyAt = [](vec3 p) { return glm::scale(glm::translate(mat4(1), p), vec3(1.f / 16)); };   // facing -z: the back is +z
    auto run = [&](CapeSim& s, vec3 from, vec3 vel, vec3 wind, float secs, double t0 = 0) {
      for (int i = 0; i <= (int)(secs * 60); i++) capeStep(s, bodyAt(from + vel * (i / 60.f)), anchor, wind, t0 + i / 60.0, 1.45f);
    };
    auto maxAbs = [](const float* a) { float m = 0; for (int i = 0; i < CapeSim::N; i++) m = std::max(m, std::fabs(a[i])); return m; };
    auto finiteSim = [](const CapeSim& s) { for (int i = 0; i < CapeSim::N; i++) if (!std::isfinite(s.bend[i] + s.side[i] + s.lift[i])) return false; return true; };
    CapeSim still; run(still, vec3(0, 70, 0), vec3(0), vec3(0), 2);
    CHECK(maxAbs(still.lift) < 0.08f && maxAbs(still.sway) < 0.05f && finiteSim(still), "standing still: it hangs straight down (lift %.3f)", maxAbs(still.lift));
    CapeSim runner; run(runner, vec3(0, 70, 0), vec3(0, 0, -5.6f), vec3(0), 1.5);
    CHECK(runner.lift[CapeSim::N - 1] > 0.5f && runner.lift[CapeSim::N - 1] > runner.lift[0], "running: the hem flies out behind, more than the shoulders (%.2f, %.2f)", runner.lift[0], runner.lift[CapeSim::N - 1]);
    float maxBend = 0; for (int i = 1; i < CapeSim::N; i++) maxBend = std::max(maxBend, std::fabs(runner.bend[i]));
    CHECK(maxBend <= 0.36f, "no strip bends more than cloth does from the one above (%.2f)", maxBend);
    CapeSim back; run(back, vec3(0, 70, 0), vec3(0, 0, 5.6f), vec3(0), 1.5);
    float minLift = 1; for (float l : back.lift) minLift = std::min(minLift, l);
    CHECK(minLift >= -0.001f, "walking backwards: it never swings in through the back and legs (%.3f)", minLift);
    CapeSim windy; run(windy, vec3(0, 70, 0), vec3(0), vec3(6, 0, 0), 2);
    CHECK(windy.sway[CapeSim::N - 1] > 0.08f && windy.sway[CapeSim::N - 1] <= 0.43f, "a wind from the side blows it over (%.2f)", windy.sway[CapeSim::N - 1]);
    CapeSim stop; run(stop, vec3(0, 70, 0), vec3(0, 0, -5.6f), vec3(0), 1.5); run(stop, vec3(0, 70, -8.4f), vec3(0), vec3(0), 3, 1.6);
    CHECK(maxAbs(stop.lift) < 0.1f, "stopping: it falls back and hangs (%.3f)", maxAbs(stop.lift));
    CapeSim tp; run(tp, vec3(0, 70, 0), vec3(0), vec3(0), 1); capeStep(tp, bodyAt(vec3(500, 90, 0)), anchor, vec3(0), 1.1, 1.45f);
    CHECK(maxAbs(tp.lift) < 0.05f && finiteSim(tp), "a teleport hangs it afresh instead of flinging it");
    {   // at a high frame rate (1000 fps: most frames fall between the cloth's 60 steps a second) it must not twitch
      CapeSim fast; float worst = 0; float last[CapeSim::N] = {};
      for (int i = 0; i <= 2000; i++) {
        const float t = i / 1000.f;
        capeStep(fast, bodyAt(vec3(0, 70, 0) + vec3(0, 0, -5.6f) * t), anchor, vec3(0), t, 1.45f);
        if (i > 500) for (int k = 0; k < CapeSim::N; k++) worst = std::max(worst, std::fabs(fast.lift[k] - last[k]));
        for (int k = 0; k < CapeSim::N; k++) last[k] = fast.lift[k];
      }
      CHECK(worst < 0.06f, "at 1000 fps, running: no twitching from frame to frame (worst %.3f rad)", worst);
      CapeSim turn; worst = 0;   // standing, turning on the spot
      for (int i = 0; i <= 2000; i++) {
        const float t = i / 1000.f;
        const mat4 B = glm::rotate(bodyAt(vec3(0, 70, 0)), t * 3.f, vec3(0, 1, 0));
        capeStep(turn, B, anchor, vec3(0), t, 1.45f);
        if (i > 500) for (int k = 0; k < CapeSim::N; k++) worst = std::max(worst, std::fabs(turn.lift[k] - last[k]));
        for (int k = 0; k < CapeSim::N; k++) last[k] = turn.lift[k];
      }
      CHECK(worst < 0.06f, "at 1000 fps, turning on the spot: no twitching (worst %.3f rad)", worst);
    }
    CapeSim pack; run(pack, vec3(0, 70, 0), vec3(0, 0, -5.6f), vec3(0), 1.5); for (int i = 0; i <= 30; i++) capeStep(pack, bodyAt(vec3(0, 70, -8.4f) + vec3(0, 0, -5.6f) * (i / 60.f)), anchor, vec3(0), 1.6 + i / 60.0, 0.22f);
    CHECK(maxAbs(pack.lift) <= 0.221f, "under a backpack it only stirs (%.3f)", maxAbs(pack.lift));
  }
  printf("\n%d passed, %d failed\n", passes, fails);
  return fails;
}
