// sim/creature_rig.cpp — rigs, their animation and dismemberment (see creature_rig.h).
// Ports render/rigs.js (RIG_ANIM biped/quad/spider/bird/slime/pod, rigMatrices, rigPartShown, rigHitPart),
// render/rigdefs2.js (fish, squid, bat, bee, turtle, frog; RIG_POST sit/ram/bear/swimTuck/fan/perch/fuse),
// render/monstex.js (slither, sting, drift, orbit), game/aquatic.js (squid & stranded fish), render/rigs2.js
// (rigLife), game/anim.js (creature walk additions) and game/dismember.js.
#include "sim/creature_rig.h"
#include "sim/creature_entities.h"
#include "core/registry.h"

namespace dq {

static constexpr float RPX = 1.f / 16;

// ------------------------------------------------------------------ models
static std::unordered_map<std::string, RigModel>& rigCache() { static std::unordered_map<std::string, RigModel> m; return m; }
static vec3 v3(const json& a, vec3 d = vec3(0)) { return a.is_array() && a.size() >= 3 ? vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()) : d; }
static float num(const json& j, const char* k, float d) { auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<float>() : d; }
static bool flag(const json& j, const char* k) { auto it = j.find(k); return it != j.end() && !it->is_null() && *it != false && *it != 0; }

const RigModel* rigModel(const std::string& key) {
  auto& C = rigCache();
  auto it = C.find(key);
  if (it != C.end()) return &it->second;
  const json& all = reg().models;
  if (!all.is_object() || !all.contains(key)) return nullptr;
  const json& d = all[key];
  RigModel M; M.key = key;
  M.rig = d.value("rig", ""); M.post = d.value("post", "");
  M.scale = num(d, "scale", 1); M.hipY = num(d, "hipY", 0); M.hasRollY = d.contains("rollY"); M.rollY = num(d, "rollY", 0);
  M.bodyDepth = num(d, "bodyDepth", 4); M.height = num(d, "height", 16); M.bodyY = num(d, "bodyY", 0); M.bodyLen = num(d, "bodyLen", 12);
  M.legH = num(d, "legH", 6); M.pitch0 = num(d, "pitch0", 0); M.seatY = num(d, "seatY", 0);
  M.gallop = flag(d, "gallop"); M.armsReach = flag(d, "armsReach"); M.heavy = flag(d, "heavy"); M.flukes = flag(d, "flukes");
  for (auto& pj : d["parts"]) {
    RigPart p;
    p.name = pj.value("name", ""); p.role = pj.value("role", ""); p.limb = pj.is_object() && pj.contains("limb") && pj["limb"].is_string() ? pj["limb"].get<std::string>() : "";
    p.only = pj.contains("only") && pj["only"].is_string() ? pj["only"].get<std::string>() : "";
    p.notFlag = pj.contains("not") && pj["not"].is_string() ? pj["not"].get<std::string>() : "";
    p.side = (int)num(pj, "side", 0); p.idx = (int)num(pj, "idx", 0); p.front = flag(pj, "front");
    p.antler = flag(pj, "antler"); p.wool = flag(pj, "wool"); p.glow = flag(pj, "glow"); p.unlit = flag(pj, "unlit"); p.noCull = flag(pj, "noCull"); p.eyes = flag(pj, "eyes");
    p.alpha = num(pj, "alpha", 1);
    p.pivot = v3(pj["pivot"]); p.min = v3(pj["min"]); p.max = v3(pj["max"]); p.rest = v3(pj.value("rest", json()));
    p.hasHand = pj.contains("hand"); p.hand = v3(pj.value("hand", json()));
    p.tex = pj.value("tex", json());
    p.swing = num(pj, "swing", 0); p.look = pj.contains("look") ? (pj["look"].is_number() ? pj["look"].get<float>() : 1.f) : 0; p.hop = num(pj, "hop", 0);
    p.flap = num(pj, "flap", 0); p.reach = num(pj, "reach", 0); p.slam = num(pj, "slam", 0);
    std::string par = pj.contains("parent") && pj["parent"].is_string() ? pj["parent"].get<std::string>() : "";
    p.parent = par.empty() ? -1 : M.find(par);
    if (p.name == "neck") M.hasNeck = true;
    M.index[p.name] = (int)M.parts.size();
    M.parts.push_back(std::move(p));
  }
  return &(C[key] = std::move(M));
}

// ------------------------------------------------------------------ pose buffers
struct Pose {
  const RigModel& M;
  std::vector<vec3> P; std::vector<u8> has;
  float lift = 0, pitch = 0, roll = 0, headShift = 0;
  vec3 scratch{0};
  explicit Pose(const RigModel& m) : M(m), P(m.parts.size(), vec3(0)), has(m.parts.size(), 0) {}
  bool any(const char* n) const { int i = M.find(n); return i >= 0 && has[i]; }
  vec3& at(const std::string& n) { int i = M.find(n); if (i < 0) { scratch = vec3(0); return scratch; } if (!has[i]) { has[i] = 1; P[i] = vec3(0); } return P[i]; }
  void set(const std::string& n, vec3 v) { int i = M.find(n); if (i >= 0) { has[i] = 1; P[i] = v; } }
  void add(const std::string& n, float x, float y, float z) { vec3& p = at(n); p += vec3(x, y, z); }
  vec3& atI(int i) { if (!has[i]) { has[i] = 1; P[i] = vec3(0); } return P[i]; }
};
static float age(const Creature& m) { return m.age; }
static bool inAir(const Creature& m) { return !m.onGround && !m.inWater && !m.ghost; }

// ------------------------------------------------------------------ RIG_ANIM (+ anim.js additions)
static float rigTurnLean(Creature& m, float k) {
  float t = m.age, dt = m.life.anT < 0 ? 0 : clampv(t - m.life.anT, 0.f, 0.1f); m.life.anT = t;
  float rate = dt > 0 ? wrapAngle(m.yaw - m.life.anYaw) / dt : 0; m.life.anYaw = m.yaw;
  float want = clampv(rate * k * std::min(1.f, m.walkAmt * 1.5f), -0.16f, 0.16f);
  m.life.anLean += (want - m.life.anLean) * std::min(1.f, dt * 6);
  return m.life.anLean;
}

static void animBiped(Creature& m, const RigModel& d, Pose& P) {
  float a = m.walkAmt, ph = m.walk, t = age(m);
  float run = clampv(m.speedNow / 4.6f, 0.f, 1.f); bool air = inAir(m);
  float sL = std::sin(ph), cL = std::cos(ph), amp = (0.7f + 0.3f * run) * a;
  bool crawl = m.lost.count("legL") || m.lost.count("legR") || m.lost.count("shinL") || m.lost.count("shinR");
  P.set("legL", {sL * amp, 0, 0}); P.set("legR", {-sL * amp, 0, 0});
  P.set("shinL", {-a * (0.08f + (0.75f + 0.4f * run) * std::max(0.f, cL)), 0, 0});
  P.set("shinR", {-a * (0.08f + (0.75f + 0.4f * run) * std::max(0.f, -cL)), 0, 0});
  float idle = 1 - a, breathe = std::sin(t * 1.9f);
  P.set("armR", {sL * amp * 0.85f, 0, 0.06f * idle + 0.03f * breathe * idle}); P.set("armL", {-sL * amp * 0.85f, 0, -0.06f * idle - 0.03f * breathe * idle});
  P.set("foreR", {0.12f + 0.35f * run * a + 0.05f * idle, 0, 0}); P.set("foreL", {0.12f + 0.35f * run * a + 0.05f * idle, 0, 0});
  P.set("body", {-0.14f * run * a, sL * 0.11f * a, 0});
  P.lift = (1 - std::fabs(sL)) * 0.045f * a + breathe * 0.004f * idle;
  if (air) {
    float fall = clampv(-m.vel.y / 10, 0.f, 1.f);
    P.set("legL", {0.55f, 0, 0.05f}); P.set("shinL", {-1.0f, 0, 0}); P.set("legR", {-0.15f, 0, -0.05f}); P.set("shinR", {-0.45f, 0, 0});
    P.set("armR", {0.3f + fall * 2.2f, 0, 0.35f + fall * 0.5f}); P.set("armL", {0.3f + fall * 2.2f, 0, -0.35f - fall * 0.5f});
  }
  if (m.landT > 0) {
    float k = m.landT / 0.18f; P.lift -= 0.09f * k;
    P.at("legL").x += 0.5f * k; P.at("legR").x += 0.5f * k; P.at("shinL").x -= 1.0f * k; P.at("shinR").x -= 1.0f * k; P.at("body").x -= 0.25f * k;
  }
  float sw = m.swingT > 0 ? std::sin(m.swingT / 0.3f * PI) : 0;
  bool reach = d.armsReach && m.T->hostile && !m.calm;
  if (reach) {
    float sway = std::sin(t * 2.3f) * 0.06f;
    P.set("armR", {1.45f + sway - sw * 0.6f, 0.05f, 0.02f}); P.set("armL", {1.45f - sway - sw * 0.6f, -0.05f, -0.02f});
    P.set("foreR", {0.05f + sw * 0.5f, 0, 0}); P.set("foreL", {0.05f + sw * 0.5f, 0, 0});
  } else if (m.aiming) {
    float dk = clampv(1 - m.shootT / 1.6f, 0.f, 1.f);
    P.set("armL", {1.52f, 0.1f, 0}); P.set("foreL", {0.04f, 0, 0}); P.set("armR", {1.45f, -0.45f - 0.25f * dk, 0}); P.set("foreR", {0.1f + 0.15f * dk, 0, -1.1f * dk});
  } else if (sw) {
    vec3 ar = P.at("armR"); P.set("armR", {ar.x + sw * 1.9f, -sw * 0.25f, 0}); P.set("foreR", {0.25f + sw * 0.5f, 0, 0}); P.at("body").y -= sw * 0.25f;
  }
  if (m.attack.on && m.state == "tele") {
    float k = m.attack.progress;
    if (m.attack.kind == "slam") { P.set("armR", {PI * 0.95f * k, 0, 0.1f}); P.set("armL", {PI * 0.95f * k, 0, -0.1f}); P.at("body").x += 0.25f * k; }
    else if (m.attack.kind == "volley") { P.set("armR", {1.5f * k, 0, 0}); P.set("armL", {1.5f * k, 0, 0}); }
  }
  if (m.state == "volleying") { P.set("armR", {1.5f, 0, 0}); P.set("armL", {1.5f, 0, 0}); }
  if (crawl) {
    P.pitch = -1.35f; P.lift = -(d.hipY * RPX) + (d.bodyDepth * RPX) * 0.55f;
    float pull = std::sin(ph * 0.8f);
    P.set("armR", {2.9f + pull * 0.45f, 0, 0.15f}); P.set("armL", {2.9f - pull * 0.45f, 0, -0.15f}); P.set("foreR", {0.3f - pull * 0.3f, 0, 0}); P.set("foreL", {0.3f + pull * 0.3f, 0, 0});
    P.set("legL", {0.1f, 0, 0.1f}); P.set("legR", {-0.1f, 0, -0.1f}); P.set("shinL", {-0.3f, 0, 0}); P.set("shinR", {-0.3f, 0, 0});
    P.set("head", {1.1f, 0, 0});
  }
  vec3& hd = P.at("head");
  hd.x += m.headPitch; hd.y += m.headYawRel - P.at("body").y;
  // anim.js: hips and shoulders, a roll from foot to foot, the head nods with each step
  float lean = rigTurnLean(m, 0.05f);
  if (m.sleeping || !m.lost.empty()) return;
  if (a == 0) { vec3& h2 = P.at("head"); h2.y += std::sin(t * 0.41f + m.id) * 0.06f; h2.x += std::sin(t * 0.29f + m.id * 2) * 0.03f; return; }
  vec3& b = P.at("body"); b.y += sL * 0.06f * a; b.z += sL * 0.03f * a + lean;
  P.at("legL").y -= sL * 0.08f * a; P.at("legR").y -= sL * 0.08f * a;
  if (!m.aiming && !(m.swingT > 0) && !reach) { vec3& r = P.at("armR"); vec3& l = P.at("armL"); r.x *= 1.2f; l.x *= 1.2f; r.z += 0.05f * a; l.z -= 0.05f * a; }
  P.at("head").x += std::fabs(cL) * 0.05f * a;
}

static void animQuad(Creature& m, const RigModel& d, Pose& P) {
  float a = m.walkAmt, ph = m.walk, t = age(m);
  float run = clampv(m.speedNow / 5, 0.f, 1.f);
  bool gallop = d.gallop && run > 0.55f; float ph2 = gallop ? 0.45f : PI;
  for (size_t i = 0; i < d.parts.size(); i++) {
    const RigPart& p = d.parts[i];
    if (p.role != "qleg") continue;
    float off = gallop ? (p.front ? 0 : PI) + (p.side > 0 ? ph2 : 0) : ((p.front == (p.side > 0)) ? 0 : PI);
    float s = std::sin(ph + off), c = std::cos(ph + off);
    P.atI((int)i) = vec3(s * (0.65f + 0.25f * run) * a, 0, 0);
    std::string shin = "shin" + p.name.substr(3);
    P.set(shin, {p.front ? -std::max(0.f, c) * (0.7f + 0.4f * run) * a : std::max(0.f, -c) * 0.55f * a, 0, 0});
  }
  if (inAir(m)) for (size_t i = 0; i < d.parts.size(); i++) if (d.parts[i].role == "qleg") P.atI((int)i) = vec3(d.parts[i].front ? 0.6f : -0.6f, 0, 0);
  P.lift = std::fabs(std::sin(ph * 2)) * 0.025f * a * (gallop ? 2.5f : 1);
  P.set("body", {std::sin(ph) * 0.035f * a * (gallop ? 2.5f : 1), 0, 0});
  float graze = m.grazeT > 0 ? std::min({1.f, m.grazeT * 3, (m.grazeMax - m.grazeT + 0.001f) * 3}) : 0;
  P.set("head", {std::sin(ph * 2) * 0.07f * a + m.headPitch * (1 - graze) - graze * 1.05f, m.headYawRel * (1 - graze), 0});
  if (graze) P.at("head").x += std::sin(t * 14) * 0.05f * graze;
  if (d.hasNeck) { vec3& h = P.at("head"); P.set("neck", {h.x * 0.4f - graze * 0.5f, h.y * 0.4f, 0}); h.x *= 0.6f; h.y *= 0.6f; }
  P.set("tail", {std::sin(t * 1.3f) * 0.08f + run * a * 0.6f, std::sin(t * 2.2f) * 0.22f + std::sin(ph) * 0.15f * a, 0});
  float flick = std::max(0.f, std::sin(t * 0.7f + m.id) - 0.93f) * 6;
  P.set("earL", {0, 0, -flick * 0.5f}); P.set("earR", {0, 0, flick * 0.5f});
  if (m.landT > 0) P.lift -= 0.06f * (m.landT / 0.18f);
  // anim.js: a rocking walk, a lean into turns; standing, a slow look round now and then, breathing
  P.roll += std::sin(ph) * 0.035f * a + rigTurnLean(m, 0.06f);
  P.lift += std::fabs(std::cos(ph)) * 0.015f * a;
  if (!(m.grazeT > 0)) P.at("head").y += std::sin(t * 0.33f + m.id) * 0.12f * (1 - a);
  P.at("body").z += std::sin(t * 1.1f + m.id) * 0.012f * (1 - a);
}

static void animSpider(Creature& m, const RigModel& d, Pose& P) {
  float a = m.walkAmt, ph = m.walk * 1.6f, t = age(m);
  for (size_t i = 0; i < d.parts.size(); i++) {
    const RigPart& p = d.parts[i];
    if (p.role != "sleg") continue;
    float off = ((p.idx % 2 == 0) == (p.side > 0)) ? 0 : PI;
    float s = std::sin(ph + off + p.idx * 0.35f), c = std::cos(ph + off + p.idx * 0.35f);
    P.atI((int)i) = vec3(0, s * 0.38f * a * p.side, std::max(0.f, c) * 0.35f * a * p.side);
    P.set("sshin" + p.name.substr(4), {0, 0, -std::max(0.f, c) * 0.2f * a * p.side});
  }
  P.set("abdomen", {std::sin(t * 2.2f) * 0.04f, std::sin(ph) * 0.05f * a, 0});
  P.set("head", {m.headPitch * 0.6f, m.headYawRel * 0.5f, 0});
  P.lift = std::fabs(std::sin(ph * 2)) * 0.02f * a;
  if (!m.onGround && !m.climbing && !m.ghost) for (size_t i = 0; i < d.parts.size(); i++) if (d.parts[i].role == "sleg") P.atI((int)i) = vec3(0, 0, -0.25f * d.parts[i].side);
}

static void animBird(Creature& m, const RigModel& d, Pose& P) {
  float a = m.walkAmt, ph = m.walk * 1.4f, t = age(m);
  bool air = inAir(m);
  P.set("legL", {std::sin(ph) * 0.9f * a, 0, 0}); P.set("legR", {-std::sin(ph) * 0.9f * a, 0, 0});
  float flap = air ? 0.5f + std::sin(t * 32) * 0.7f : 0.04f + (m.flutterT > 0 ? std::sin(t * 40) * 0.4f : 0);
  P.set("wingR", {0, 0, -flap}); P.set("wingL", {0, 0, flap});
  float peck = m.peckT > 0 ? std::sin(m.peckT / 0.35f * PI) : 0;
  P.set("head", {m.headPitch - peck * 0.9f, m.headYawRel, 0});
  P.headShift = std::sin(ph * 2) * 0.03f * a;
  // anim.js: a waddle, and the wings shuffled now and then
  P.roll += std::sin(ph) * 0.09f * a;
  if (m.onGround && P.any("wingR")) { float sh = std::max(0.f, std::sin(t * 0.9f + m.id) - 0.96f) * 8; P.at("wingR").z -= sh * 0.3f; P.at("wingL").z += sh * 0.3f; }
}

static void animPod(Creature& m, const RigModel& d, Pose& P) {
  float a = m.walkAmt, ph = m.walk, t = age(m);
  for (size_t i = 0; i < d.parts.size(); i++) { const RigPart& p = d.parts[i]; if (p.role == "qleg") { float off = (p.front == (p.side > 0)) ? 0 : PI; P.atI((int)i) = vec3(std::sin(ph + off) * 0.8f * a, 0, 0); } }
  P.set("body", {0, 0, std::sin(ph) * 0.06f * a});
  P.set("head", {m.headPitch * 0.6f + std::sin(t * 1.4f) * 0.03f, m.headYawRel * 0.8f, std::sin(t * 2) * 0.03f});
  P.set("crown", {0, t * 0.6f, 0});
  P.lift = std::fabs(std::sin(ph)) * 0.03f * a;
}

static void animFish(Creature& m, const RigModel& d, Pose& P) {
  float t = age(m) + m.id * 0.37f, sp = clampv(m.speedNow / 3, 0.f, 1.f); bool out = !m.inWater && !m.ghost && m.onGround;
  float f = out ? 16 : 5 + sp * 9;
  P.set("body", {0, std::sin(t * f) * (0.06f + sp * 0.1f), 0});
  P.set("tail", d.flukes ? vec3(std::sin(t * f * 0.6f + 1) * (0.35f + sp * 0.3f), 0, 0) : vec3(0, std::sin(t * f + 1) * (0.35f + sp * 0.35f), 0));
  P.set("finL", {0, 0, -0.35f - std::sin(t * 5) * 0.25f}); P.set("finR", {0, 0, 0.35f + std::sin(t * 5) * 0.25f});
  if (out && !d.flukes) { P.roll = PI / 2; P.at("body").y += std::sin(t * 22) * 0.35f; }
  P.pitch = m.swimPitch;
  // aquatic.js: on their side the whole time they're out
  if (m.inWater || m.ghost) return;
  float weak = clampv(1 - m.dryT / 14, 0.2f, 1.f), fl = m.flopT > 0 ? m.flopT / 0.35f : 0; float side = m.flopSide ? (float)m.flopSide : 1.f;
  if (!d.flukes) { P.roll = side * PI / 2; P.set("body", {0, std::sin(t * 22) * 0.35f * weak + fl * 0.4f * side, 0}); P.set("tail", {0, std::sin(t * 22 + 1) * 0.6f * weak, 0}); }
  else { P.pitch = -fl * 0.35f; P.set("tail", {std::sin(t * 10) * 0.5f * weak, 0, 0}); }
}

static void animSquid(Creature& m, const RigModel& d, Pose& P) {   // aquatic.js: point the way you swim
  float t = age(m) + m.id; bool wet = m.inWater || m.ghost;
  float now = age(m), dt = m.life.sqAge < 0 ? 0 : clampv(now - m.life.sqAge, 0.f, 0.1f); m.life.sqAge = now;
  if (wet) {
    float hs = m.speedNow, vy = m.vel.y, s = std::hypot(hs, vy);
    float k = clampv((s - 0.2f) / 0.9f, 0.f, 1.f), rest = 0.18f + std::sin(t * 0.37f) * 0.12f;
    float want = rest + (std::atan2(hs, vy) - rest) * k;
    if (m.life.tilt < 0) m.life.tilt = rest;
    m.life.tilt += (want - m.life.tilt) * std::min(1.f, dt * 2.2f);
    float dy = std::atan2(std::sin(m.yaw - m.life.sqYaw), std::cos(m.yaw - m.life.sqYaw)); m.life.sqYaw = m.yaw;
    float turn = dt > 0 ? clampv(dy / dt, -3.f, 3.f) : 0;
    m.life.sqRoll += ((-turn * 0.35f + std::sin(t * 0.23f) * 0.25f) - m.life.sqRoll) * std::min(1.f, dt * 1.5f);
    float pulse = (std::sin(t * 2.6f) + 1) / 2, open = (0.1f + pulse * 0.55f) * (1 - 0.45f * k);
    for (int i = 0; i < 8; i++) { float a = i / 8.f * TAU, wob = std::sin(t * 3.1f + i * 1.7f) * 0.06f; P.set("tent" + std::to_string(i), {-open * std::sin(a) + wob, 0, open * std::cos(a) + wob}); }
    P.pitch = -m.life.tilt; P.roll = m.life.sqRoll;
    return;
  }
  m.life.tilt = 0; m.life.sqRoll = 0;
  float side = m.flopSide ? (float)m.flopSide : 1.f, weak = clampv(1 - m.dryT / 16, 0.15f, 1.f), fl = m.flopT > 0 ? m.flopT / 0.35f : 0;
  P.roll = side * (PI / 2 - fl * 0.5f); P.lift = -12 * RPX * d.scale + fl * 0.05f;
  for (int i = 0; i < 8; i++) { float a = i / 8.f * TAU, w = std::sin(t * (5 + i * 0.4f) + i * 1.3f) * 0.35f * weak; P.set("tent" + std::to_string(i), {-0.15f * std::sin(a) + w, std::cos(t * 3 + i) * 0.2f * weak, 0.15f * std::cos(a) + w * 0.6f}); }
}

static void animBat(Creature& m, const RigModel& d, Pose& P) {
  float t = age(m) + m.id;
  if (m.roosting) {
    P.pitch = PI; P.lift = (d.hipY * 2) * RPX * d.scale;
    P.set("wingR", {0, -1.35f, 0}); P.set("wingL", {0, 1.35f, 0}); P.set("wingR2", {0, -1.5f, 0}); P.set("wingL2", {0, 1.5f, 0});
    P.set("head", {std::sin(t * 0.7f) * 0.1f, 0, 0});
    return;
  }
  float flap = std::sin(t * 26);
  P.pitch = -1.1f + m.vel.y * 0.05f;
  P.set("wingR", {0, flap * 0.9f - 0.15f, 0}); P.set("wingL", {0, -flap * 0.9f + 0.15f, 0});
  P.set("wingR2", {0, flap * 0.55f, 0}); P.set("wingL2", {0, -flap * 0.55f, 0});
  P.set("head", {0.9f, 0, 0}); P.set("earL", {0, 0, -0.2f}); P.set("earR", {0, 0, 0.2f});
}
static void animBee(Creature& m, const RigModel& d, Pose& P) {
  float t = age(m) + m.id, buzz = std::fabs(std::sin(t * 70));
  P.set("wingR", {0, 0, -0.15f - buzz * 0.8f}); P.set("wingL", {0, 0, 0.15f + buzz * 0.8f});
  P.lift = m.onGround ? 0 : std::sin(t * 5) * 0.04f;
  P.pitch = -clampv(m.speedNow * 0.08f, 0.f, 0.3f) + (m.angryT > 0 ? -0.15f : 0);
  P.set("legs", {std::sin(t * 4) * 0.12f, 0, 0});
  P.set("antL", {std::sin(t * 3) * 0.1f, 0, 0}); P.set("antR", {std::sin(t * 3 + 1) * 0.1f, 0, 0});
}
static void animTurtle(Creature& m, const RigModel& d, Pose& P) {
  float a = m.walkAmt, ph = m.walk, t = age(m) + m.id;
  if (m.inWater) {
    float s = std::sin(t * 3.2f);
    P.set("flipFR", {0, 0.3f, s * 0.55f}); P.set("flipFL", {0, -0.3f, -s * 0.55f});
    P.set("flipBR", {0, std::sin(t * 3.2f + 1) * 0.35f, 0}); P.set("flipBL", {0, -std::sin(t * 3.2f + 1) * 0.35f, 0});
    P.pitch = m.swimPitch;
  } else {
    float s = std::sin(ph * 1.4f) * a;
    P.set("flipFR", {0, s * 0.55f, 0}); P.set("flipFL", {0, s * 0.55f, 0}); P.set("flipBR", {0, -s * 0.35f, 0}); P.set("flipBL", {0, -s * 0.35f, 0});
    P.roll = std::sin(ph * 1.4f) * 0.05f * a;
  }
  P.set("head", {m.headPitch * 0.6f + std::sin(t * 0.8f) * 0.04f, m.headYawRel * 0.7f, 0});
}
static void animFrog(Creature& m, const RigModel& d, Pose& P) {
  float t = age(m) + m.id; bool air = inAir(m);
  if (air) { P.set("legBL", {-1.25f, 0, 0}); P.set("legBR", {-1.25f, 0, 0}); P.set("legFL", {0.9f, 0, 0}); P.set("legFR", {0.9f, 0, 0}); P.pitch = clampv(m.vel.y * 0.07f, -0.45f, 0.45f); }
  else if (m.inWater) { float k = std::fabs(std::sin(t * 5)); P.set("legBL", {-k * 1.1f, 0, 0}); P.set("legBR", {-k * 1.1f, 0, 0}); P.set("legFL", {0.5f, 0, 0}); P.set("legFR", {0.5f, 0, 0}); }
  else { float a = m.walkAmt; P.set("legFL", {std::sin(m.walk) * 0.4f * a, 0, 0}); P.set("legFR", {-std::sin(m.walk) * 0.4f * a, 0, 0}); }
  float croak = m.croakT > 0 ? std::sin(m.croakT / 0.6f * PI) : 0;
  P.set("throat", {0, 0, 0}); P.headShift = 0;
  P.set("head", {m.headPitch * 0.3f + croak * 0.25f, m.headYawRel * 0.5f, 0});
  P.lift += std::sin(t * 3) * 0.003f;
}

// ------------------------------------------------------------------ RIG_POST
static void postSit(Creature& m, const RigModel& d, Pose& P) {
  if (m.sleeping) {
    P.lift -= d.legH * RPX * 0.85f;
    P.set("legFL", {1.45f, 0, 0}); P.set("legFR", {1.45f, 0, 0}); P.set("legBL", {-1.45f, 0, 0}); P.set("legBR", {-1.45f, 0, 0});
    P.set("head", {-0.15f, 0.5f, 0}); P.set("tail", {0, 1.2f, 0});
    return;
  }
  if (!m.sitting) return;
  float k = 0.5f;
  P.pitch += k; P.lift += d.bodyLen * 0.5f * RPX * std::sin(k) * 0.55f;
  P.set("legFL", {-k, 0, 0}); P.set("legFR", {-k, 0, 0}); P.set("legBL", {1.15f, 0, 0}); P.set("legBR", {1.15f, 0, 0});
  P.set("shinBL", {-1.2f, 0, 0}); P.set("shinBR", {-1.2f, 0, 0});
  if (P.any("head")) P.at("head").x -= k;
}
static void applyPost(Creature& m, const RigModel& d, Pose& P) {
  const std::string& post = d.post;
  float t = age(m);
  if (post == "sit") postSit(m, d, P);
  else if (post == "ram") { if (m.ramming) P.at("head").x -= 0.75f; postSit(m, d, P); }
  else if (post == "bear") {
    if (m.rearT <= 0) return;
    float k = std::min(1.f, m.rearT * 3) * 1.05f;
    P.pitch += k; P.lift += (d.bodyLen ? d.bodyLen : 20) * 0.5f * RPX * std::sin(k) * 0.9f;
    float paw = -k + 0.5f + std::sin(t * 9) * 0.25f;
    P.set("legFL", {paw, 0, 0}); P.set("legFR", {paw, 0, 0}); P.set("legBL", {-k, 0, 0}); P.set("legBR", {-k, 0, 0});
    if (P.any("head")) P.at("head").x -= k * 0.8f;
  } else if (post == "swimTuck") {
    if (m.playDead) { P.roll = PI; return; }
    if (!m.inWater) return;
    float tt = t + m.id;
    P.set("legFL", {-1.3f, 0, -0.4f}); P.set("legFR", {-1.3f, 0, 0.4f}); P.set("legBL", {-1.3f, 0, -0.3f}); P.set("legBR", {-1.3f, 0, 0.3f});
    P.set("tail", {0, std::sin(tt * 7) * 0.55f, 0}); P.set("body", {0, std::sin(tt * 7 - 1) * 0.08f, 0});
    P.pitch = m.swimPitch;
  } else if (post == "fan") {
    float k = m.displayT > 0 ? std::min(1.f, m.displayT * 3) : 0;
    P.set("fan", {-k * 1.25f, 0, 0});
    if (k) { P.set("wingR", {0, 0, -0.35f * k}); P.set("wingL", {0, 0, 0.35f * k}); }
  } else if (post == "perch") {
    if (m.dancing) { P.at("head").x += std::sin(t * 12) * 0.3f; P.roll = std::sin(t * 6) * 0.15f; }
  } else if (post == "slither") {
    float tt = t * (m.speedNow > 0.2f ? 9 : 2.5f) + m.id, a = m.coiled ? 0.9f : m.speedNow > 0.2f ? 0.45f : 0.18f;
    for (int i = 1; i <= 8; i++) P.set("seg" + std::to_string(i), {0, std::sin(tt - i * 0.9f) * a, 0});
    if (m.striking > 0) P.set("head", {-0.5f, 0, 0});
    if (m.rattling) P.set("rattle", {0, std::sin(t * 60) * 0.4f, 0});
  } else if (post == "sting") {
    float k = m.striking > 0 ? 1 : 0;
    for (int i = 1; i <= 5; i++) P.set("tail" + std::to_string(i), {-0.15f * k - std::sin(t * 2 + i) * 0.04f, 0, 0});
    P.set("sting", {k * -0.9f, 0, 0});
    P.set("clawR", {0, std::sin(t * 3) * 0.15f, 0}); P.set("clawL", {0, -std::sin(t * 3) * 0.15f, 0});
  } else if (post == "drift") {
    float tt = t + m.id;
    P.lift = std::sin(tt * 1.6f) * 0.12f;
    P.set("armR", {std::sin(tt * 1.3f) * 0.2f + (m.swingT > 0 ? -0.8f : 0), 0, 0}); P.set("armL", {std::sin(tt * 1.3f + 1) * 0.2f + (m.swingT > 0 ? -0.8f : 0), 0, 0});
    P.set("hem", {std::sin(tt * 2.2f) * 0.12f, 0, std::sin(tt * 1.7f) * 0.1f});
  } else if (post == "orbit") {
    float tt = t * 2 + m.id;
    P.lift = std::sin(tt * 0.8f) * 0.1f;
    for (int i = 0; i < 6; i++) P.set("rod" + std::to_string(i), {std::sin(tt + i) * 0.25f, tt * 0.6f, std::cos(tt + i) * 0.25f});
  } else if (post == "fuse") {
    float k = clampv(m.fuse / 1.5f, 0.f, 1.f), tt = t + m.id;
    P.set("jaw", {-(0.08f + k * 0.75f) - std::sin(tt * 2) * 0.03f, 0, 0});
    for (int i = 0; i < 5; i++) P.set("frond" + std::to_string(i), {std::sin(tt * 1.3f + i) * 0.08f + k * 0.4f * std::cos(i / 5.f * TAU + 0.3f), 0, k * std::sin(tt * 40 + i) * 0.15f});
  }
}

// ------------------------------------------------------------------ rigs2.js: more life in every creature
static bool isRester(const std::string& t) {
  static const std::set<std::string> S = {"cow", "sheep", "pig", "horse", "donkey", "llama", "camel", "goat", "deer", "boar", "wolf", "fox", "snow_fox", "cat", "ocelot",
    "lion", "lioness", "tiger", "panther", "snow_leopard", "brown_bear", "polar_bear", "panda", "mooshroom", "turkey", "armadillo"};
  return S.count(t) > 0;
}
static bool isShaker(const std::string& t) {
  static const std::set<std::string> S = {"wolf", "fox", "snow_fox", "cat", "ocelot", "lion", "lioness", "tiger", "panther", "snow_leopard", "brown_bear", "polar_bear", "werewolf", "raccoon", "sheep", "goat"};
  return S.count(t) > 0;
}
static void rigLife(Creature& m, const RigModel& d, Pose& P) {
  RigLife& L = m.life;
  float t = m.age;
  if (!L.init) { L.init = true; L.lifeT = t; L.yaw = m.yaw; L.ear = 1 + frand() * 4; }
  float dt = clampv(t - L.lifeT, 0.f, 0.1f); L.lifeT = t;
  const std::string& kind = d.rig;
  float a = m.walkAmt, ph = m.walk;
  bool dead = m.deathTime > 0;
  float dyaw = wrapAngle(m.yaw - L.yaw); L.yaw = m.yaw;
  if (dt > 0) {
    L.turn += (clampv(dyaw / dt, -5.f, 5.f) - L.turn) * std::min(1.f, dt * 6);
    float sp = m.speedNow; L.acc += (clampv((sp - L.sp) / dt, -12.f, 12.f) - L.acc) * std::min(1.f, dt * 4); L.sp = sp;
  }
  bool ground = m.onGround || m.ghost;
  if (!dead && ground && (kind == "quad" || kind == "biped" || kind == "pod" || kind == "bird")) {
    float sp = clampv(m.speedNow / 4, 0.f, 1.f);
    P.roll += -L.turn * 0.05f * sp;
    if (kind == "quad") P.pitch += -clampv(L.acc, -6.f, 6.f) * 0.012f;
  }
  {   // tails: a spring that swings out against the turn; a wag for a pet near its owner
    float want = L.turn * 0.22f;
    L.tailV += ((want - L.tail) * 40 - L.tailV * 7) * dt; L.tail += L.tailV * dt;
    float wag = !m.owner.empty() && !dead && m.data.value("ownerNear", false) ? std::sin(t * 14) * 0.45f : 0;
    for (size_t i = 0; i < d.parts.size(); i++) if (d.parts[i].role == "tail" || d.parts[i].name == "tail") P.atI((int)i).y += clampv(L.tail, -0.9f, 0.9f) + wag;
  }
  L.ear -= dt;
  if (L.ear <= 0) { L.ear = 1.5f + frand() * 5; L.earK = 1; L.earSide = frand() < 0.5f ? 1 : -1; }
  L.earK = std::max(0.f, L.earK - dt * 6);
  {
    float tw = std::sin(L.earK * PI) * 0.6f, back = m.hurtTime > 0 || m.angryT > 0 ? -0.5f : 0;
    for (size_t i = 0; i < d.parts.size(); i++) if (d.parts[i].role == "ear") { int s = d.parts[i].name.back() == 'L' ? -1 : 1; P.atI((int)i) += vec3(back, 0, s * (L.earSide == s ? tw : tw * 0.2f)); }
  }
  bool resty = isRester(m.type) && kind == "quad" && !dead && !m.riddenBy && !m.baby;
  if (resty && a < 0.05f && ground && !m.inWater && m.hurtTime <= 0 && !(m.grazeT > 0)) L.idle += dt; else L.idle = 0;
  float wantRest = L.idle > 14 || (m.sitting && !m.owner.empty() && m.stay && isRester(m.type) && d.post.empty()) ? 1.f : 0.f;
  L.rest += (wantRest - L.rest) * std::min(1.f, dt * (wantRest ? 1.5f : 6));
  if (L.rest > 0.01f && kind == "quad") {
    float k = L.rest * L.rest * (3 - 2 * L.rest);
    P.lift += -d.legH * RPX * 0.8f * k;
    for (size_t i = 0; i < d.parts.size(); i++) {
      const RigPart& p = d.parts[i];
      if (p.role == "qleg") P.atI((int)i) += vec3((p.front ? -1.35f : 1.35f) * k, 0, p.side * 0.1f * k);
      else if (p.role == "qshin") P.atI((int)i) += vec3((p.front ? 1.3f : -1.3f) * k, 0, 0);
    }
    P.add("head", 0.15f * k + std::sin(t * 0.5f) * 0.05f * k, 0, std::sin(t * 0.3f) * 0.08f * k);
  }
  {   // in the water: a land animal floats low (the surface height comes from the sim: data.surf)
    float want = 0;
    if (m.inWater && !m.T->swims && !dead && m.data.contains("surf")) {
      float surf = m.data["surf"].get<float>();
      float H = d.height * RPX * d.scale * (m.baby ? 0.55f : 1);
      float frac = kind == "quad" ? 0.6f : kind == "biped" ? 0.74f : kind == "bird" ? 0.32f : kind == "spider" ? 0.7f : 0.5f;
      want = std::min(0.f, surf - (m.pos.y + frac * H)) + std::sin(t * 3.1f + m.id) * 0.025f;
    }
    L.sink += (want - L.sink) * std::min(1.f, dt * 5);
    if (std::fabs(L.sink) > 0.002f) P.lift += L.sink;
  }
  if (m.inWater && !m.T->swims && !dead) {   // swimming: land animals paddle, heads up; people-shaped things tread water
    if (kind == "quad") {
      P.pitch += -0.22f;
      for (size_t i = 0; i < d.parts.size(); i++) { const RigPart& p = d.parts[i]; if (p.role == "qleg") { float off = (p.front == (p.side > 0)) ? 0 : PI; P.atI((int)i) = vec3(std::sin(t * 9 + off) * 0.7f, 0, 0); } }
      P.add("head", -0.35f, 0, 0);
    } else if (kind == "biped") {
      for (int s = 0; s < 2; s++) { float sg = s == 0 ? 1.f : -1.f; std::string S = s == 0 ? "L" : "R";
        P.set("leg" + S, {std::sin(t * 7 + (sg > 0 ? 0 : PI)) * 0.5f, 0, 0}); P.set("arm" + S, {0.4f + std::sin(t * 3.5f) * 0.25f, 0, sg * (1.1f + std::sin(t * 3.5f) * 0.4f)}); }
      P.add("head", -0.25f, 0, 0);
    } else if (kind == "bird") {
      for (size_t i = 0; i < d.parts.size(); i++) { const RigPart& p = d.parts[i]; if (p.name.find("leg") != std::string::npos || p.name.find("Leg") != std::string::npos) P.atI((int)i) = vec3(std::sin(t * 8 + (p.side > 0 ? 0 : PI)) * 0.6f, 0, 0); }
    } else if (kind == "spider") {
      for (size_t i = 0; i < d.parts.size(); i++) { const RigPart& p = d.parts[i]; if (p.role == "sleg") P.atI((int)i) += vec3(0, std::sin(t * 10 + p.idx) * 0.25f, -0.2f * (p.side ? p.side : 1)); }
    }
  }
  if (isShaker(m.type)) { if (m.inWater) L.wet = true; else if (L.wet && ground) { L.wet = false; L.shake = 1.1f; } }
  if (L.shake > 0) { L.shake -= dt; float k = std::min(1.f, L.shake * 2); P.roll += std::sin(t * 38) * 0.22f * k; P.add("head", 0, 0, std::sin(t * 38 + 1) * 0.35f * k); }
  if (m.hurtTime > 0 && !dead) {   // a flinch when hit
    float k = std::sin(clampv(m.hurtTime / 0.35f, 0.f, 1.f) * PI);
    P.pitch += 0.1f * k * (kind == "quad" ? -1 : 1);
    P.add("head", -0.25f * k, 0, 0);
    if (kind == "biped") { P.add("armR", 0, 0, 0.4f * k); P.add("armL", 0, 0, -0.4f * k); P.add("body", 0.15f * k, 0, 0); }
  }
  if (kind == "biped" && (m.T->family == "ghoul" || m.type == "ghoul") && !dead) {   // the shamble
    P.roll += std::sin(ph) * 0.07f * a;
    P.add("head", std::sin(t * 0.9f + m.id) * 0.06f, 0, std::sin(t * 0.6f + m.id) * 0.18f);
    if (P.any("shinR")) P.at("shinR").x *= 0.45f;
    P.add("body", 0.06f, 0, 0);
  }
  if (m.type == "skeleton" || m.type == "stray") {
    float r = std::max(0.f, std::sin(t * 0.8f + m.id) - 0.94f) * 12;
    if (r > 0) { P.add("head", 0, std::sin(t * 60) * 0.06f * r, std::sin(t * 47) * 0.05f * r); P.add("armL", 0, 0, std::sin(t * 53) * 0.05f * r); }
  }
  if (kind == "bird" && !dead && P.any("head")) {   // quick, jerky looks
    if (t > L.lookT) { L.lookT = t + 0.3f + frand() * 1.2f; L.look = (frand() - 0.5f) * 1.2f; L.cock = (frand() - 0.5f) * 0.5f; }
    if (!m.lookAt || a > 0.3f) { vec3& h = P.at("head"); h.y += L.look * (1 - a); h.z += L.cock * (1 - a); }
  }
  if (!dead && a < 0.2f && kind != "slime") P.lift += std::sin(t * 1.9f + m.id) * 0.006f;
  if (dead) {   // knees buckle, limbs go limp (the fall itself is in creatureBase)
    float k = clampv(m.deathTime / 0.3f, 0.f, 1.f);
    if (kind == "biped") {
      P.add("shinL", -1.2f * k, 0, 0); P.add("shinR", -1.0f * k, 0, 0); P.add("legL", 0.6f * k, 0, 0.1f * k); P.add("legR", 0.5f * k, 0, -0.1f * k);
      P.add("armR", -0.3f * k, 0, 0.7f * k); P.add("armL", -0.2f * k, 0, -0.6f * k); P.add("head", 0.45f * k, 0, 0.3f * k);
      P.lift -= 0.12f * k;
    } else if (kind == "quad") {
      for (size_t i = 0; i < d.parts.size(); i++) { const RigPart& p = d.parts[i]; if (p.role == "qleg") P.atI((int)i) += vec3((p.front ? 0.4f : -0.4f) * k, 0, p.side * 0.5f * k); }
      P.add("head", 0.5f * k, 0, 0.2f * k); P.add("tail", 0.6f * k, 0, 0);
    } else if (kind == "spider") {
      for (size_t i = 0; i < d.parts.size(); i++) { const RigPart& p = d.parts[i]; if (p.role == "sleg") P.atI((int)i).z += -p.side * 0.9f * k; if (p.role == "sshin") P.atI((int)i).z += -p.side * 1.1f * k; }
    } else if (kind == "bird") { P.add("wingR", 0, 0, -0.9f * k); P.add("wingL", 0, 0, 0.9f * k); P.add("head", 0.6f * k, 0, 0); }
  }
}

// ------------------------------------------------------------------ matrices
mat4 creatureBase(const Creature& m, bool full) {
  mat4 b(1);
  if (m.baby) b = glm::scale(b, vec3(0.55f));
  if (full) {
    if (m.type == "slime" || m.T->slimy) {
      float s = m.size == 1 ? 0.5f : 1, sq = m.onGround ? 1 : 1 + std::min(0.25f, std::fabs(m.vel.y) * 0.04f);
      b = glm::scale(b, vec3(s / std::sqrt(sq), s * sq, s / std::sqrt(sq)));
    }
    if (m.fuse > 0) { float s = 1 + m.fuse * 0.12f + std::sin(m.fuse * 40) * 0.015f; b = glm::scale(b, vec3(s, 1 + m.fuse * 0.05f, s)); }
  }
  b = glm::rotate(b, m.yaw, vec3(0, 1, 0));
  if (full && m.deathTime > 0) {
    float k = clampv((m.deathTime - 0.12f) / 0.36f, 0.f, 1.f), fall = k * k;
    float bounce = m.deathTime > 0.48f ? std::sin(clampv((m.deathTime - 0.48f) / 0.16f, 0.f, 1.f) * PI) * 0.07f : 0;
    float px = -(m.w > 0 ? m.w : 0.3f);
    b = glm::translate(b, vec3(px, 0, 0)); b = glm::rotate(b, fall * PI / 2 - bounce, vec3(0, 0, 1)); b = glm::translate(b, vec3(-px, 0, 0));
  }
  if (m.hasTilt) {   // spiders on walls and ceilings
    float h = (m.h > 0 ? m.h : 1) * 0.5f;
    if (m.surfaceTilt.x) { b = glm::translate(b, vec3(0, h, 0)); b = glm::rotate(b, m.surfaceTilt.x, vec3(1, 0, 0)); b = glm::translate(b, vec3(0, -h, 0)); }
    if (m.surfaceTilt.y) { b = glm::translate(b, vec3(0, h, 0)); b = glm::rotate(b, m.surfaceTilt.y, vec3(0, 0, 1)); b = glm::translate(b, vec3(0, -h, 0)); }
  }
  return b;
}

void rigPose(Creature& m, const RigModel& M, const mat4& base) {
  Pose P(M);
  const std::string& k = M.rig;
  if (k == "biped") animBiped(m, M, P);
  else if (k == "quad") animQuad(m, M, P);
  else if (k == "spider") animSpider(m, M, P);
  else if (k == "bird") animBird(m, M, P);
  else if (k == "pod") animPod(m, M, P);
  else if (k == "fish") animFish(m, M, P);
  else if (k == "squid") animSquid(m, M, P);
  else if (k == "bat") animBat(m, M, P);
  else if (k == "bee") animBee(m, M, P);
  else if (k == "turtle") animTurtle(m, M, P);
  else if (k == "frog") animFrog(m, M, P);
  if (M.pitch0) P.pitch += M.pitch0;
  if (!M.post.empty()) applyPost(m, M, P);
  if (!k.empty()) rigLife(m, M, P);
  mat4 root = base;
  if (P.lift) root = glm::translate(root, vec3(0, P.lift, 0));
  if (M.scale != 1) root = glm::scale(root, vec3(M.scale));
  if (P.pitch) { float hy = M.hipY * RPX; root = glm::translate(root, vec3(0, hy, 0)); root = glm::rotate(root, P.pitch, vec3(1, 0, 0)); root = glm::translate(root, vec3(0, -hy, 0)); }
  if (P.roll) { float ry = (M.hasRollY ? M.rollY : M.hipY) * RPX; root = glm::translate(root, vec3(0, ry, 0)); root = glm::rotate(root, P.roll, vec3(0, 0, 1)); root = glm::translate(root, vec3(0, -ry, 0)); }
  m.mats.assign(M.parts.size(), mat4(0));
  // old-style models (no rig): legs swing, heads look (entities.js draws)
  float sw = std::sin(m.walk) * 0.7f * m.walkAmt;
  for (size_t i = 0; i < M.parts.size(); i++) {
    const RigPart& part = M.parts[i];
    if (m.lost.count(part.name)) continue;
    const mat4* parent = part.parent >= 0 ? &m.mats[part.parent] : &root;
    if ((*parent)[3][3] == 0) continue;
    mat4 mm = glm::translate(*parent, part.pivot - vec3(0, 0, part.name == "head" && P.headShift ? P.headShift : 0));
    vec3 r = P.has[i] ? P.P[i] : vec3(0);
    float rx = part.rest.x + r.x, ry = part.rest.y + r.y, rz = part.rest.z + r.z;
    if (k.empty()) { rx += part.swing * sw + part.look * clampv(m.headPitch, -0.6f, 0.6f); }
    if (ry) mm = glm::rotate(mm, ry, vec3(0, 1, 0));
    if (rx) mm = glm::rotate(mm, rx, vec3(1, 0, 0));
    if (rz) mm = glm::rotate(mm, rz, vec3(0, 0, 1));
    m.mats[i] = mm;
  }
  // the cinder wisp (a blaze): its rods circle the burning heart in two rings turning opposite ways, each
  // bobbing on its own, like Minecraft's blaze (the original only spun them where they stood)
  if (M.post == "orbit") {
    float tt = m.age + m.id * 0.37f;
    for (size_t i = 0; i < M.parts.size(); i++) {
      const RigPart& part = M.parts[i];
      if (part.role != "rod" || m.lost.count(part.name)) continue;
      bool upper = part.idx % 2;
      float a0 = std::atan2(part.pivot.z, part.pivot.x), r = std::hypot(part.pivot.x, part.pivot.z) * 1.25f;
      float a = a0 + (upper ? -1.f : 1.f) * tt * 1.8f;
      float bob = std::sin(tt * 2.6f + part.idx * 1.3f) * 0.08f + (upper ? 0.05f : -0.1f);
      mat4 mm = glm::translate(root, vec3(std::cos(a) * r, part.pivot.y + bob, std::sin(a) * r));
      mm = glm::rotate(mm, -a, vec3(0, 1, 0));
      mm = glm::rotate(mm, upper ? -0.15f : 0.15f, vec3(0, 0, 1));   // leaning in a little, top and bottom
      m.mats[i] = mm;
    }
  }
}

bool rigPartShown(const Creature& m, const RigPart& p) {
  if (p.antler && !m.stag) return false;
  if (p.wool && m.sheared) return false;
  auto state = [&](const std::string& f) {
    if (f == "saddled") return m.saddled; if (f == "puffed") return m.puffed; if (f == "curled") return m.curled; if (f == "roosting") return m.roosting;
    if (f == "tongueOut") return m.tongueOut; if (f == "sleeping") return m.sleeping; if (f == "sitting") return m.sitting; if (f == "playDead") return m.playDead;
    if (f == "hasSting") return m.hasSting; if (f == "pollen") return m.pollen; if (f == "angry") return m.angryT > 0; if (f == "baby") return m.baby;
    if (f == "stag") return m.stag; if (f == "sheared") return m.sheared; if (f == "rattling") return m.rattling; if (f == "coiled") return m.coiled;
    return false;
  };
  if (!p.only.empty() && !state(p.only)) return false;
  if (!p.notFlag.empty() && state(p.notFlag)) return false;
  return true;
}

static float rayAABB(vec3 o, vec3 d, vec3 lo, vec3 hi) {
  float t0 = -1e9f, t1 = 1e9f;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(d[a]) < 1e-9f) { if (o[a] < lo[a] || o[a] > hi[a]) return -1; continue; }
    float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return -1;
  }
  return t1 < 0 ? -1 : std::max(0.f, t0);
}
int rigHitPart(const Creature& m, const RigModel& M, vec3 o, vec3 d) {
  int best = -1; float bt = 1e9f;
  for (size_t i = 0; i < M.parts.size() && i < m.mats.size(); i++) {
    if (!rigPartPosed(m, (int)i)) continue;
    mat4 inv = glm::inverse(m.mats[i]);
    vec3 lo = vec3(inv * vec4(o - m.pos, 1)), ld = vec3(inv * vec4(d, 0));
    const RigPart& p = M.parts[i];
    float t = rayAABB(lo, ld, p.min - vec3(0.03f), p.max + vec3(0.03f));
    if (t >= 0 && t < bt) { bt = t; best = (int)i; }
  }
  return best;
}

// ------------------------------------------------------------------ dismember.js
static float severChance(const std::string& k) {
  if (k == "axe") return 0.3f; if (k == "sword") return 0.2f; if (k == "arrow") return 0.12f; if (k == "explosion") return 0.35f;
  if (k == "trident") return 0.16f; if (k == "hoe") return 0.08f; if (k == "pickaxe") return 0.06f; if (k == "shovel") return 0.04f;
  return 0;
}
static void sever(Level& lv, Creature& m, const RigModel& M, int pi, const DamageSrc& src) {
  std::vector<int> gone = {pi};
  for (size_t i = 0; i < M.parts.size(); i++) if (M.parts[i].parent >= 0 && std::find(gone.begin(), gone.end(), M.parts[i].parent) != gone.end()) gone.push_back((int)i);
  for (int i : gone) {
    if (rigPartPosed(m, i)) spawnGib(lv, m, M.parts[i].name, m.mats[i]);
    m.lost.insert(M.parts[i].name);
  }
  static const std::unordered_map<std::string, std::string> GIB = {{"skeleton", "skel_bone"}, {"slime", "slime_skin"}, {"cinder_golem", "golem_rock"}, {"fuseling", "fuse_moss"}};
  auto g = GIB.find(m.type);
  mobBurst(lv, m.pos + vec3(0, m.h * 0.55f, 0), g == GIB.end() ? "blood_particle" : g->second, 10, 0.8f);
  if (m.type == "skeleton") lv.fx->voice("rattle", "say", m.pos); else lv.fx->sound("hit", m.pos);
  if (Entity* a = m.ent(lv, src.attacker)) if (a->isPlayer()) floatText(lv, a->id, m.pos + vec3(0, m.h + 0.5f, 0), "Severed!", "#ff9a7a");
  const std::string& L = M.parts[pi].limb;
  if (L == "head") { if (m.hp > 0) { m.hp = 0; m.die(lv, src); } return; }
  if ((L == "legL" || L == "legR") && M.rig == "biped") { m.crawling = true; m.h = std::min(m.h, 0.8f); }
  if (L.size() == 5 && L.rfind("leg", 0) == 0 && (L[3] == 'F' || L[3] == 'B')) m.limp++;
  if (L.rfind("wing", 0) == 0) m.noFloaty = true;
  if (L == "armR" && !m.heldItem.empty()) m.dropWeapon(lv, true);
  if (L == "armL" && !m.offItem.empty()) m.dropWeapon(lv, false);
}
void Creature::tryDismember(Level& lv, const DamageSrc& src, float dealt) {
  if (dealt <= 0 || ghost || T->noDismember) return;
  float ch = severChance(src.type == "explosion" ? "explosion" : src.tool);
  if (ch <= 0) return;
  const RigModel* M = rigModel(modelKey());
  if (!M || M->rig.empty()) return;
  rigPose(*this, *M, creatureBase(*this, false));   // the pose right now
  int pi = -1;
  if (src.hasRay) pi = rigHitPart(*this, *M, src.rayO, src.rayD);
  if (pi < 0 || M->parts[pi].limb.empty()) {
    std::vector<int> limbs; for (size_t i = 0; i < M->parts.size(); i++) if (!M->parts[i].limb.empty() && rigPartPosed(*this, (int)i)) limbs.push_back((int)i);
    if (limbs.empty()) return;
    pi = limbs[(size_t)(frand() * limbs.size()) % limbs.size()];
  }
  const RigPart& part = M->parts[pi];
  ch *= clampv(dealt / 4, 0.5f, 1.6f) * T->j.value("sever", 1.f);
  if (part.limb == "head") { if (hp > maxHp * 0.4f) return; ch *= 0.5f; }
  if (lost.size() >= 4) return;
  if (frand() > ch) return;
  sever(lv, *this, *M, pi, src);
}

} // namespace dq
