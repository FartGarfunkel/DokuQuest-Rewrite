// sim/beast_anim.cpp — the "beast" rig: imported creatures as our own (the creature import, the data local
// and git-ignored) move the way the mod moves them. Each model carries its setupAnim() translated into a small program
// ("beast": {"prog", "slots", "ents", "clipRole"}), run here every pose on the boxes' Java pose (turns in radians, pivots in
// model pixels, y down), with Citadel's helpers as Citadel writes them (walk / swing / flap: cos(f * speed + offset) *
// degree * f1 + weight * f1; bob; chainSwing / Wave / Flap; progressRotation(Prev) / progressPosition(Prev); faceTarget).
// The entity's own values the model asks for (sitProgress, isFlying()...) are named in "ents" with what they mean here: a
// progress ramping 0..max a tick at a time while our creature is sat / asleep / flying / swimming / running / fighting /
// grazing / rearing, a flag, the tick count, the clip's tick. Its ModelAnimator keyframes come as clips (data "clips"),
// played on a blow (role "attack") or while grazing ("eat"). What's left at the end, less the box's rest, is the turn and
// the move of each of our parts (Java -> ours: turns (-x, y, -z), moves (x, -y, z) / 16: the model mirrored in y only).
#include "sim/beast_anim.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include <cstdlib>
#include <unordered_map>

namespace dq {

namespace {
enum Op : u8 { N_NUM, N_VAR, N_IN, N_ENT, N_BONE, N_DEF, N_ANIM, N_NEG, N_NOT, N_COND, N_BIN, N_FN };
enum Bin : u8 { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_LT, B_GT, B_LE, B_GE, B_EQ, B_NE, B_AND, B_OR };
enum Fn : u8 { F_SIN, F_COS, F_ABS, F_MIN, F_MAX, F_SQRT, F_POW, F_SIGN, F_FLOOR, F_CEIL, F_ATAN2, F_ATAN, F_TAN, F_EXP, F_LOG, F_CLAMP, F_LERP, F_RAD, F_DEG, F_WRAP, F_NONE };
struct Node { Op op = N_NUM; u8 sub = 0; float v = 0; int a = -1, b = -1, c = -1; std::vector<int> args; };
enum St : u8 { S_LET, S_SET, S_WALK, S_SWING, S_FLAP, S_BOB, S_CSWING, S_CWAVE, S_CFLAP, S_PRP, S_PR, S_PPP, S_PP, S_FACE, S_IF, S_RET, S_SHOW, S_SCOPE, S_KNEE, S_NOP };
struct Stmt { St op = S_NOP; int bone = -1, attr = 0; char mode = '='; std::vector<int> e; std::vector<int> bones; std::vector<Stmt> a, b; };
enum State : u8 { ST_NEVER, ST_SIT, ST_SLEEP, ST_FLY, ST_SWIM, ST_RUN, ST_ATTACK, ST_GRAZE, ST_REAR, ST_BABY, ST_GROUND };
enum EntK : u8 { E_ZERO, E_PROG, E_FLAG, E_TICK, E_ANIM, E_VEL, E_HURT, E_ID };
struct Ent { EntK k = E_ZERO; State st = ST_NEVER; float max = 5; int axis = 0; };
struct Key { float t; float v[6]; };
struct Clip { float len = 0; std::string role; std::vector<std::pair<int, std::vector<Key>>> bones; };
struct Prog {
  std::vector<Node> nodes; std::vector<Stmt> stmts; int slots = 0;
  std::vector<Ent> ents; std::unordered_map<std::string, int> entIx;
  std::vector<std::array<float, 6>> rest;     // each box's Java pose (rx, ry, rz, px, py, pz)
  std::unordered_map<std::string, Clip> clips; std::unordered_map<std::string, int> animIx;   // (clip names asked for by "anim" tests)
  std::vector<std::string> animNames;
};

State stateOf(const std::string& s) {
  static const std::unordered_map<std::string, State> M = {{"sitting", ST_SIT}, {"sleeping", ST_SLEEP}, {"flying", ST_FLY}, {"swimming", ST_SWIM},
    {"running", ST_RUN}, {"attacking", ST_ATTACK}, {"grazing", ST_GRAZE}, {"rearing", ST_REAR}, {"baby", ST_BABY}, {"onGround", ST_GROUND}};
  auto it = M.find(s); return it == M.end() ? ST_NEVER : it->second;
}

struct Builder {
  Prog& P;
  int expr(const json& j) {
    Node n;
    if (j.is_number()) { n.op = N_NUM; n.v = j.get<float>(); }
    else if (j.is_array() && !j.empty() && j[0].is_string()) {
      const std::string k = j[0];
      auto at = [&](size_t i) { return i < j.size() ? expr(j[i]) : expr(json(0)); };
      if (k == "v") { n.op = N_VAR; n.a = j[1].get<int>(); }
      else if (k == "in") { const std::string i = j[1]; n.op = N_IN; n.a = i == "f" ? 0 : i == "f1" ? 1 : i == "age" ? 2 : i == "yaw" ? 3 : i == "pitch" ? 4 : 5; }
      else if (k == "e") { n.op = N_ENT; auto it = P.entIx.find(j[1].get<std::string>()); n.a = it == P.entIx.end() ? -1 : it->second; }
      else if (k == "b" || k == "d") { n.op = k == "b" ? N_BONE : N_DEF; n.a = j[1].get<int>(); n.b = j[2].get<int>(); }
      else if (k == "anim") {
        const std::string nm = j[1]; n.op = N_ANIM;
        auto it = P.animIx.find(nm);
        if (it == P.animIx.end()) { it = P.animIx.emplace(nm, (int)P.animNames.size()).first; P.animNames.push_back(nm); }
        n.a = it->second;
      }
      else if (k == "neg") { n.op = N_NEG; n.a = at(1); }
      else if (k == "not") { n.op = N_NOT; n.a = at(1); }
      else if (k == "?") { n.op = N_COND; n.a = at(1); n.b = at(2); n.c = at(3); }
      else if (k == "fn") {
        static const std::unordered_map<std::string, Fn> F = {{"sin", F_SIN}, {"cos", F_COS}, {"abs", F_ABS}, {"min", F_MIN}, {"max", F_MAX}, {"sqrt", F_SQRT},
          {"pow", F_POW}, {"sign", F_SIGN}, {"floor", F_FLOOR}, {"ceil", F_CEIL}, {"atan2", F_ATAN2}, {"atan", F_ATAN}, {"tan", F_TAN}, {"exp", F_EXP},
          {"log", F_LOG}, {"clamp", F_CLAMP}, {"lerp", F_LERP}, {"rad", F_RAD}, {"deg", F_DEG}, {"wrapdeg", F_WRAP}};
        n.op = N_FN; auto it = F.find(j[1].get<std::string>()); n.sub = it == F.end() ? F_NONE : it->second;
        for (size_t i = 2; i < j.size(); i++) n.args.push_back(expr(j[i]));
      } else {
        static const std::unordered_map<std::string, Bin> B = {{"+", B_ADD}, {"-", B_SUB}, {"*", B_MUL}, {"/", B_DIV}, {"%", B_MOD}, {"<", B_LT}, {">", B_GT},
          {"<=", B_LE}, {">=", B_GE}, {"==", B_EQ}, {"!=", B_NE}, {"&&", B_AND}, {"||", B_OR}};
        auto it = B.find(k);
        if (it == B.end()) { n.op = N_NUM; n.v = 0; }
        else { n.op = N_BIN; n.sub = it->second; n.a = at(1); n.b = at(2); }
      }
    }
    P.nodes.push_back(std::move(n));
    return (int)P.nodes.size() - 1;
  }
  std::vector<Stmt> block(const json& arr) { std::vector<Stmt> out; if (arr.is_array()) for (auto& s : arr) out.push_back(stmt(s)); return out; }
  Stmt stmt(const json& j) {
    Stmt s;
    if (!j.is_array() || j.empty() || !j[0].is_string()) return s;
    const std::string k = j[0];
    auto E = [&](size_t from, size_t to) { for (size_t i = from; i < to && i < j.size(); i++) s.e.push_back(expr(j[i])); };
    if (k == "let") { s.op = S_LET; s.attr = j[1].get<int>(); E(2, 3); }
    else if (k == "set") { s.op = S_SET; s.bone = j[1].get<int>(); s.attr = j[2].get<int>(); s.mode = j[3].get<std::string>()[0]; E(4, 5); }
    else if (k == "walk" || k == "swing" || k == "flap") { s.op = k == "walk" ? S_WALK : k == "swing" ? S_SWING : S_FLAP; s.bone = j[1].get<int>(); E(2, 9); }
    else if (k == "bob") { s.op = S_BOB; s.bone = j[1].get<int>(); E(2, 7); }
    else if (k == "chainSwing" || k == "chainWave" || k == "chainFlap") {
      s.op = k == "chainSwing" ? S_CSWING : k == "chainWave" ? S_CWAVE : S_CFLAP;
      for (auto& b : j[1]) s.bones.push_back(b.get<int>());
      E(2, 7);
    }
    else if (k == "prp" || k == "pr" || k == "ppp" || k == "pp") { s.op = k == "prp" ? S_PRP : k == "pr" ? S_PR : k == "ppp" ? S_PPP : S_PP; s.bone = j[1].get<int>(); E(2, 7); }
    else if (k == "face") { s.op = S_FACE; E(1, 4); for (auto& b : j[4]) s.bones.push_back(b.get<int>()); }
    else if (k == "if") { s.op = S_IF; E(1, 2); s.a = block(j[2]); s.b = block(j[3]); }
    else if (k == "ret") s.op = S_RET;
    else if (k == "knee") { s.op = S_KNEE; s.bone = j[1].get<int>(); s.attr = j[2].get<int>(); E(3, 5); }   // (lower part, the limb above, how much, which way)
    else if (k == "scope") { s.op = S_SCOPE; s.a = block(j[1]); }   // (a part's own program: its return ends only it)
    return s;
  }
};

std::unique_ptr<Prog> build(const RigModel& M) {
  const json* d = nullptr;
  const json& models = reg().models;
  if (models.is_object() && models.contains(M.key)) d = &models[M.key];
  if (!d || !d->contains("beast")) return nullptr;
  const json& A = (*d)["beast"];
  auto P = std::make_unique<Prog>();
  P->slots = A.value("slots", 0);
  if (A.contains("ents")) for (auto& [name, sp] : A["ents"].items()) {
    Ent e; const std::string k = sp.is_array() && !sp.empty() ? sp[0].get<std::string>() : "zero";
    if (k == "prog") { e.k = E_PROG; e.max = sp[1].get<float>(); e.st = stateOf(sp[2].get<std::string>()); }
    else if (k == "flag") { e.k = E_FLAG; e.st = stateOf(sp[1].get<std::string>()); }
    else if (k == "tick") e.k = E_TICK;
    else if (k == "anim") e.k = E_ANIM;
    else if (k == "vel") { e.k = E_VEL; e.axis = sp[1].get<int>(); }
    else if (k == "hurt") e.k = E_HURT;
    else if (k == "id") e.k = E_ID;
    P->entIx[name] = (int)P->ents.size(); P->ents.push_back(e);
  }
  for (auto& p : (*d)["parts"]) {
    std::array<float, 6> r{};
    if (p.contains("jrot")) for (int i = 0; i < 3; i++) r[i] = p["jrot"][i].get<float>();
    if (p.contains("jpos")) for (int i = 0; i < 3; i++) r[3 + i] = p["jpos"][i].get<float>();
    P->rest.push_back(r);
  }
  Builder B{*P};
  P->stmts = B.block(A["prog"]);
  const json& roles = A.contains("clipRole") ? A["clipRole"] : json::object();
  const json& all = reg().clips;
  if (all.is_object() && all.contains(M.key)) for (auto& [name, c] : all[M.key].items()) {
    Clip C; C.len = c.value("len", 0.f); C.role = roles.value(name, std::string("idle"));
    for (auto& [bs, ks] : c["bones"].items()) {
      std::vector<Key> keys;
      for (auto& k : ks) { Key K; K.t = k[0].get<float>(); for (int i = 0; i < 6; i++) K.v[i] = k[1 + i].get<float>(); keys.push_back(K); }
      C.bones.push_back({std::stoi(bs), std::move(keys)});
    }
    P->clips[name] = std::move(C);
  }
  return P;
}

const Prog* progFor(const RigModel& M) {
  static std::unordered_map<std::string, std::unique_ptr<Prog>> cache;
  auto it = cache.find(M.key);
  if (it != cache.end()) return it->second.get();
  auto p = build(M); auto* raw = p.get();
  cache[M.key] = std::move(p);
  return raw;
}

// each creature's progress values and the clip it's playing (the poser's own memory: nothing of it is saved)
struct Live { std::vector<float> prog; float lastAge = -1; std::string clip; float clipT0 = 0; float lastSwing = 0, lastGraze = 0; };
std::unordered_map<u32, Live>& lives() { static std::unordered_map<u32, Live> L; return L; }

bool inState(const Creature& m, State s) {
  switch (s) {
    case ST_SIT: return m.sitting || m.curled || m.perched || m.roosting || m.playDead;
    case ST_SLEEP: return m.sleeping;
    case ST_FLY: return !m.onGround && !m.inWater && m.T && (m.T->fly > 0 || m.T->ai == "flyer");
    case ST_SWIM: return m.inWater;
    case ST_RUN: return m.walkAmt > 0.5f && m.T && m.speedNow > m.T->speed * 1.4f;
    case ST_ATTACK: return m.attack.on || m.swingT > 0 || m.lungeT > 0 || m.angryT > 0 || m.striking > 0;
    case ST_GRAZE: return m.grazeT > 0 || m.peckT > 0 || m.drinkT > 0;
    case ST_REAR: return m.rearT > 0 || m.displayT > 0;
    case ST_BABY: return m.baby;
    case ST_GROUND: return m.onGround;
    default: return false;
  }
}

struct Run {
  const Prog& P; const Creature& m; Live& L;
  std::vector<float> vars; float in[6]; std::vector<std::array<float, 6>> cur;
  bool done = false;

  float ent(int i) {
    if (i < 0 || i >= (int)P.ents.size()) return 0;
    const Ent& e = P.ents[i];
    switch (e.k) {
      case E_PROG: return i < (int)L.prog.size() ? L.prog[i] : 0;
      case E_FLAG: return inState(m, e.st) ? 1.f : 0.f;
      case E_TICK: return m.age * 20.f;
      case E_ANIM: return L.clip.empty() ? 0.f : (m.age - L.clipT0) * 20.f;
      case E_VEL: return m.vel[e.axis];
      case E_HURT: return std::max(0.f, m.invulnT) * 20.f;
      case E_ID: return (float)(m.id % 1000);
      default: return 0;
    }
  }
  float ev(int ni) {
    if (ni < 0) return 0;
    const Node& n = P.nodes[ni];
    switch (n.op) {
      case N_NUM: return n.v;
      case N_VAR: return n.a >= 0 && n.a < (int)vars.size() ? vars[n.a] : 0;
      case N_IN: return n.a >= 0 && n.a < 6 ? in[n.a] : 0;
      case N_ENT: return ent(n.a);
      case N_BONE: return n.a >= 0 && n.a < (int)cur.size() ? cur[n.a][n.b] : 0;
      case N_DEF: return n.a >= 0 && n.a < (int)P.rest.size() ? P.rest[n.a][n.b] : 0;
      case N_ANIM: return n.a >= 0 && n.a < (int)P.animNames.size() && L.clip == P.animNames[n.a] ? 1.f : 0.f;
      case N_NEG: return -ev(n.a);
      case N_NOT: return ev(n.a) == 0 ? 1.f : 0.f;
      case N_COND: return ev(n.a) != 0 ? ev(n.b) : ev(n.c);
      case N_BIN: {
        const float a = ev(n.a);
        if (n.sub == B_AND) return a != 0 && ev(n.b) != 0 ? 1.f : 0.f;
        if (n.sub == B_OR) return a != 0 || ev(n.b) != 0 ? 1.f : 0.f;
        const float b = ev(n.b);
        switch (n.sub) {
          case B_ADD: return a + b; case B_SUB: return a - b; case B_MUL: return a * b; case B_DIV: return b != 0 ? a / b : 0.f;
          case B_MOD: return b != 0 ? std::fmod(a, b) : 0.f; case B_LT: return a < b; case B_GT: return a > b; case B_LE: return a <= b;
          case B_GE: return a >= b; case B_EQ: return a == b; case B_NE: return a != b; default: return 0;
        }
      }
      case N_FN: {
        float x[3] = {0, 0, 0};
        for (size_t i = 0; i < n.args.size() && i < 3; i++) x[i] = ev(n.args[i]);
        switch (n.sub) {
          case F_SIN: return std::sin(x[0]); case F_COS: return std::cos(x[0]); case F_ABS: return std::fabs(x[0]);
          case F_MIN: return std::min(x[0], x[1]); case F_MAX: return std::max(x[0], x[1]); case F_SQRT: return std::sqrt(std::max(0.f, x[0]));
          case F_POW: return std::pow(x[0], x[1]); case F_SIGN: return (float)((x[0] > 0) - (x[0] < 0)); case F_FLOOR: return std::floor(x[0]);
          case F_CEIL: return std::ceil(x[0]); case F_ATAN2: return std::atan2(x[0], x[1]); case F_ATAN: return std::atan(x[0]);
          case F_TAN: return std::tan(x[0]); case F_EXP: return std::exp(x[0]); case F_LOG: return x[0] > 0 ? std::log(x[0]) : 0.f;
          case F_CLAMP: return std::clamp(x[0], std::min(x[1], x[2]), std::max(x[1], x[2]));
          case F_LERP: return x[1] + (x[2] - x[1]) * x[0];   // (Mth.lerp(delta, start, end))
          case F_RAD: return glm::radians(x[0]); case F_DEG: return glm::degrees(x[0]);
          case F_WRAP: { float a = std::fmod(x[0], 360.f); if (a >= 180) a -= 360; if (a < -180) a += 360; return a; }
          default: return 0;
        }
      }
    }
    return 0;
  }
  bool ok(int b) const { return b >= 0 && b < (int)cur.size(); }
  void exec(const std::vector<Stmt>& ss) {
    for (const Stmt& s : ss) {
      if (done) return;
      auto E = [&](int i) { return i < (int)s.e.size() ? ev(s.e[i]) : 0.f; };
      switch (s.op) {
        case S_LET: if (s.attr >= 0 && s.attr < (int)vars.size()) vars[s.attr] = E(0); break;
        case S_SET: if (ok(s.bone) && s.attr >= 0 && s.attr < 6) {
          float& t = cur[s.bone][s.attr]; const float v = E(0);
          if (s.mode == '=') t = v; else if (s.mode == '+') t += v; else if (s.mode == '-') t -= v; else if (s.mode == '*') t *= v; else if (s.mode == '/' && v != 0) t /= v;
        } break;
        case S_WALK: case S_SWING: case S_FLAP: if (ok(s.bone)) {   // Citadel calculateRotation (movementScale 1)
          const float speed = E(0), degree = E(1), offset = E(3), weight = E(4), f = E(5), f1 = E(6);
          float r = std::cos(f * speed + offset) * degree * f1 + weight * f1;
          if (E(2) != 0) r = -r;
          cur[s.bone][s.op == S_WALK ? 0 : s.op == S_SWING ? 1 : 2] += r;
        } break;
        case S_BOB: if (ok(s.bone)) {
          const float speed = E(0), degree = E(1), f = E(3), f1 = E(4);
          const float bob = E(2) != 0 ? -std::fabs(std::sin(f * speed) * f1 * degree) : std::sin(f * speed) * f1 * degree - f1 * degree;
          cur[s.bone][4] += bob;
        } break;
        case S_CSWING: case S_CWAVE: case S_CFLAP: {
          const int n = (int)s.bones.size(); if (!n) break;
          const float speed = E(0), degree = E(1), root = E(2), f = E(3), f1 = E(4);
          const float off = (float)(root * glm::pi<double>() / (2.0 * n));
          const int ax = s.op == S_CSWING ? 1 : s.op == S_CWAVE ? 0 : 2;
          for (int i = 0; i < n; i++) if (ok(s.bones[i])) cur[s.bones[i]][ax] += std::cos(f * speed + off * i) * f1 * degree;
        } break;
        case S_PRP: case S_PR: case S_PPP: case S_PP: if (ok(s.bone)) {
          const float p = E(0), dv = E(4); if (dv == 0) break;
          const int o = s.op == S_PRP || s.op == S_PR ? 0 : 3;
          const bool rel = s.op == S_PR || s.op == S_PP;   // (progressRotation / Position: toward the value from the box's rest)
          for (int i = 0; i < 3; i++) cur[s.bone][o + i] += p * (E(1 + i) - (rel ? P.rest[s.bone][o + i] : 0.f)) / dv;
        } break;
        case S_FACE: {
          const int n = (int)s.bones.size(); if (!n) break;
          const float div = E(2) * n; if (div == 0) break;
          const float ya = E(0) / 57.295776f / div, pa = E(1) / 57.295776f / div;
          for (int b : s.bones) if (ok(b)) { cur[b][1] += ya; cur[b][0] += pa; }
        } break;
        case S_IF: exec(E(0) != 0 ? s.a : s.b); break;
        case S_RET: done = true; return;
        case S_SCOPE: exec(s.a); done = false; break;
        case S_KNEE: if (ok(s.bone) && ok(s.attr)) {   // folds as the limb above it comes forward (and a little as it goes back)
          const float swing = cur[s.attr][0] - P.rest[s.attr][0];
          cur[s.bone][0] += E(1) * E(0) * (std::max(0.f, -swing) + 0.25f * std::max(0.f, swing));
        } break;
        default: break;
      }
    }
  }
};

void beastAnim(Creature& m, const RigModel& M, RigAnimOut& o) {
  const Prog* P = progFor(M);
  if (!P) return;
  static const bool still = std::getenv("DQ_AM_STILL") != nullptr;   // (looking at the rest pose: nothing moves)
  if (still) return;
  Live& L = lives()[m.id];
  if (lives().size() > 4096) { lives().clear(); return; }   // (old ones never cleaned otherwise; the next pose starts afresh)
  // the progress values: a tick's step (1 a twentieth of a second, as the mod) toward their max while the state holds
  const float dt = L.lastAge < 0 ? 0.f : std::clamp(m.age - L.lastAge, 0.f, 0.5f);
  L.lastAge = m.age;
  if (L.prog.size() != P->ents.size()) L.prog.assign(P->ents.size(), 0.f);
  for (size_t i = 0; i < P->ents.size(); i++) if (P->ents[i].k == E_PROG) {
    const Ent& e = P->ents[i];
    const bool on = inState(m, e.st);
    L.prog[i] = std::clamp(L.prog[i] + (on ? 20.f : -20.f) * dt * std::max(1.f, e.max / 5.f), 0.f, e.max);
  }
  // the clip: a blow (swingT rising) plays the attack, grazing the eating; a clip runs its length once
  if (!L.clip.empty()) { auto it = P->clips.find(L.clip); if (it == P->clips.end() || m.age - L.clipT0 > it->second.len) L.clip.clear(); }
  auto start = [&](const char* role) {
    std::vector<const std::string*> c;
    for (auto& [n, C] : P->clips) if (C.role == role) c.push_back(&n);
    if (c.empty()) return;
    L.clip = *c[(m.id + (u32)(m.age * 3)) % c.size()]; L.clipT0 = m.age;
  };
  const bool swing = m.swingT > 0 || m.attack.on || m.striking > 0;
  if (swing && L.lastSwing <= 0) start("attack");
  L.lastSwing = swing ? 1.f : 0.f;
  const bool graze = m.grazeT > 0 || m.peckT > 0;
  if (graze && L.lastGraze <= 0 && L.clip.empty()) start("eat");
  L.lastGraze = graze ? 1.f : 0.f;

  Run R{*P, m, L};
  R.vars.assign(std::max(0, P->slots), 0.f);
  R.cur = P->rest;
  R.in[0] = m.walk * 1.25f;                          // limbSwing (Minecraft: about 4 a second a block a second; ours 3.2)
  R.in[1] = std::min(1.f, m.walkAmt);                // limbSwingAmount
  R.in[2] = m.age * 20.f;                            // ageInTicks
  R.in[3] = -glm::degrees(m.headYawRel);             // netHeadYaw (degrees; Java's y turn is ours the other way)
  R.in[4] = glm::degrees(m.headPitch);               // headPitch
  R.in[5] = 0;
  R.exec(P->stmts);
  if (!L.clip.empty()) {   // ModelAnimator's keyframes, on top
    const Clip& C = P->clips.at(L.clip);
    const float t = m.age - L.clipT0;
    for (auto& [b, keys] : C.bones) {
      if (b < 0 || b >= (int)R.cur.size() || keys.empty()) continue;
      size_t i = 0; while (i + 1 < keys.size() && keys[i + 1].t <= t) i++;
      const Key& a = keys[i]; const Key& z = i + 1 < keys.size() ? keys[i + 1] : keys[i];
      const float u = z.t > a.t ? std::clamp((t - a.t) / (z.t - a.t), 0.f, 1.f) : 0.f;
      for (int k = 0; k < 6; k++) R.cur[b][k] += a.v[k] + (z.v[k] - a.v[k]) * u;
    }
  }
  const size_t n = std::min(R.cur.size(), M.parts.size());
  if (const char* dbg = std::getenv("DQ_AM_DEBUG"); dbg && M.key == dbg && (int)(m.age * 2) != (int)((m.age - dt) * 2)) {   // (twice a second: each part's Java pose)
    std::string line = "am " + M.key + " clip '" + L.clip + "' f1 " + std::to_string(R.in[1]) + ":";
    for (size_t i = 0; i < n; i++) { char b[160]; std::snprintf(b, sizeof b, " %s[%.2f %.2f %.2f | %.1f %.1f %.1f]", M.parts[i].name.c_str(), R.cur[i][0] - P->rest[i][0], R.cur[i][1] - P->rest[i][1], R.cur[i][2] - P->rest[i][2], R.cur[i][3] - P->rest[i][3], R.cur[i][4] - P->rest[i][4], R.cur[i][5] - P->rest[i][5]); line += b; }
    DQ_LOG("%s", line.c_str());
  }
  for (size_t i = 0; i < n; i++) {
    const auto& c = R.cur[i]; const auto& r = P->rest[i];
    const vec3 dr(-(c[0] - r[0]), c[1] - r[1], -(c[2] - r[2]));   // (y flipped, Java's y down: a turn about x or z goes the other way, about y the same)
    if (dr != vec3(0)) { if (!o.has[i]) { o.has[i] = 1; o.rot[i] = vec3(0); } o.rot[i] += dr; }
    const vec3 dp(c[3] - r[3], -(c[4] - r[4]), c[5] - r[5]);
    if (o.pos && dp != vec3(0) && i < o.pos->size()) (*o.pos)[i] += dp / 16.f;
  }
}
} // namespace

void registerBeastAnim() { registerRigAnim("beast", [](Creature& m, const RigModel& M, RigAnimOut& o) { beastAnim(m, M, o); }); }

} // namespace dq
