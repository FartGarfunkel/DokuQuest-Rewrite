// sim/geo_anim.cpp — the "geo" rig: imported keyframe models (tools/local/boss_import.py) play their own clips, keyframe
// for keyframe (linear or Catmull-Rom). Which clip: the creature's data "clip" (set by its brain, with
// "clipT0" its age when it began), else idle / walking / running by how it moves. Turns go on each bone's rest turn, moves
// on its pivot, in our axes (x mirrored).
#include "sim/geo_anim.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

namespace {
struct Key { float t; vec3 v; bool cat; };
struct Chan { std::vector<Key> k; };
struct BoneTrack { int part = -1; Chan r, p; };
struct Clip { float len = 0; bool loop = false; std::vector<BoneTrack> bones; };
struct ModelClips { std::unordered_map<std::string, Clip> clips; };

const ModelClips* clipsFor(const RigModel& M) {
  static std::unordered_map<std::string, std::unique_ptr<ModelClips>> cache;   // (built once, on the main thread's first pose)
  auto it = cache.find(M.key);
  if (it != cache.end()) return it->second.get();
  std::unique_ptr<ModelClips> mc;
  const json& all = reg().clips;
  if (all.is_object() && all.contains(M.key)) {
    mc = std::make_unique<ModelClips>();
    for (auto& [name, c] : all[M.key].items()) {
      Clip C; C.len = c.value("len", 0.f); C.loop = c.value("loop", false);
      for (auto& [bn, ch] : c["bones"].items()) {
        BoneTrack bt; bt.part = M.find(bn);
        if (bt.part < 0) continue;
        auto read = [](const json& arr, Chan& out) {
          auto f = [](const json& v) { return v.is_number() ? v.get<float>() : 0.f; };
          for (auto& k : arr) {
            if (!k.is_array() || k.size() < 2 || !k[1].is_array()) continue;
            const json& v = k[1];
            out.k.push_back({f(k[0]), vec3(v.size() > 0 ? f(v[0]) : 0.f, v.size() > 1 ? f(v[1]) : 0.f, v.size() > 2 ? f(v[2]) : 0.f), k.size() > 2 && k[2] == "catmullrom"});
          }
        };
        if (ch.contains("r")) read(ch["r"], bt.r);
        if (ch.contains("p")) read(ch["p"], bt.p);
        C.bones.push_back(std::move(bt));
      }
      mc->clips[name] = std::move(C);
    }
  }
  auto* raw = mc.get();
  cache[M.key] = std::move(mc);
  return raw;
}

vec3 sample(const Chan& c, float t) {
  const auto& k = c.k;
  if (k.empty()) return vec3(0);
  if (t <= k.front().t || k.size() == 1) return k.front().v;
  if (t >= k.back().t) return k.back().v;
  size_t i = 0; while (i + 1 < k.size() && k[i + 1].t <= t) i++;
  const Key& a = k[i]; const Key& b = k[i + 1];
  const float u = (t - a.t) / std::max(1e-5f, b.t - a.t);
  if (!(a.cat || b.cat)) return a.v + (b.v - a.v) * u;
  const vec3 p0 = i > 0 ? k[i - 1].v : a.v, p3 = i + 2 < k.size() ? k[i + 2].v : b.v;   // Catmull-Rom through the neighbours
  const float u2 = u * u, u3 = u2 * u;
  return 0.5f * ((2.f * a.v) + (-p0 + b.v) * u + (2.f * p0 - 5.f * a.v + 4.f * b.v - p3) * u2 + (-p0 + 3.f * a.v - 3.f * b.v + p3) * u3);
}

void apply(const Clip& C, float t, RigAnimOut& o, float w) {
  for (const BoneTrack& b : C.bones) {
    if (!b.r.k.empty()) {
      const vec3 d = sample(b.r, t);
      const vec3 r = vec3(-glm::radians(d.x), glm::radians(d.y), -glm::radians(d.z)) * w;
      if (!o.has[b.part]) { o.has[b.part] = 1; o.rot[b.part] = vec3(0); }
      o.rot[b.part] += r;
    }
    if (!b.p.k.empty() && o.pos) { const vec3 d = sample(b.p, t); (*o.pos)[b.part] += vec3(-d.x, d.y, d.z) / 16.f * w; }
  }
}

void geoAnim(Creature& m, const RigModel& M, RigAnimOut& o) {
  const ModelClips* mc = clipsFor(M);
  if (!mc) return;
  auto find = [&](const std::string& n) -> const Clip* { auto it = mc->clips.find(n); return it == mc->clips.end() ? nullptr : &it->second; };
  std::string want; float t0 = 0;
  if (m.data.is_object() && m.data.contains("clip") && m.data["clip"].is_string()) { want = m.data["clip"].get<std::string>(); t0 = m.data.value("clipT0", 0.f); }
  const Clip* C = want.empty() ? nullptr : find(want);
  float t = m.age - t0;
  if (!C) {   // moving about: idle, walking, running
    const bool run = m.walkAmt > 0.6f && m.data.is_object() && m.data.value("running", false);
    C = m.walkAmt > 0.15f ? find(run ? "running" : "walking") : nullptr;
    if (!C) C = find("idle");
    t = m.age;
    // no walk clip of its own (the source walks them in code): legs and arms swung by name
    if (m.walkAmt > 0.15f && !find("walking")) {
      const float sw = std::sin(m.walk) * 0.7f * std::min(1.f, m.walkAmt);
      for (size_t i = 0; i < M.parts.size(); i++) {
        const std::string& n = M.parts[i].name;
        const bool leg = n.find("leg") != std::string::npos, arm = n.find("arm") != std::string::npos;
        if ((!leg && !arm) || M.parts[i].parent < 0 || M.parts[M.parts[i].parent].name.find(leg ? "leg" : "arm") != std::string::npos) continue;
        const bool left = n.find("left") != std::string::npos || n.find("Left") != std::string::npos;
        const float a = (left ? sw : -sw) * (arm ? -0.8f : 1.f);
        if (!o.has[i]) { o.has[i] = 1; o.rot[i] = vec3(0); }
        o.rot[i].x += a;
      }
    }
  }
  if (!C) return;
  if (C->loop && C->len > 0) t = std::fmod(std::max(0.f, t), C->len);
  else t = std::clamp(t, 0.f, std::max(C->len, 0.f));
  apply(*C, t, o, 1.f);
}
} // namespace

float geoClipLength(const std::string& model, const std::string& clip) {
  const RigModel* M = rigModel(model);
  if (!M) return 0;
  const ModelClips* mc = clipsFor(*M);
  if (!mc) return 0;
  auto it = mc->clips.find(clip);
  return it == mc->clips.end() ? 0 : it->second.len;
}

void registerGeoAnim() { registerRigAnim("geo", [](Creature& m, const RigModel& M, RigAnimOut& o) { geoAnim(m, M, o); }); }

} // namespace dq
