// client/bench.cpp — DQ_BENCH per-frame timings (see bench.h)
#include "client/bench.h"
#include "common/common.h"
#include "render/gl.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>

namespace dq::bench {
namespace {
constexpr int kSlots = 6;
struct Pending { GLuint a = 0, b = 0; double inputAt = 0, offset = 0; bool used = false, hasA = false; };
struct State {
  FILE* f = nullptr;
  double t0 = 0, last = 0, inputAt = 0;
  double ph[PhaseCount] = {};
  std::map<std::string, double> scopes;
  u64 frame = 0;
  Pending pend[kSlots];
  bool gl = false;
  double offset = 0;        // CPU seconds minus GPU seconds (calibrated every 60 frames)
  double lastGpuEnd = 0;    // GPU seconds: the end of the last frame read back
  double outGpu = -1, outLat = -1;   // the newest frame read back (written on this frame's line: it is ~6 frames old)
};
State& S() {
  static State s = [] {
    State s;
    if (const char* p = std::getenv("DQ_BENCH")) {
      s.f = std::fopen(p, "w");
      if (s.f) std::fprintf(s.f, "frame,t,total,wait,input,update,render,ui,swap,sync,sleep,gpu,latency,top\n");
    }
    return s;
  }();
  return s;
}
}

bool on() { return S().f != nullptr; }

void frameBegin() {
  State& s = S();
  if (!s.f) return;
  s.t0 = s.last = nowSeconds();
  for (double& v : s.ph) v = 0;
  s.scopes.clear();
  s.inputAt = s.t0;
  if (!s.gl) return;
  // the GPU's marks come back a few frames late: this slot's old marks are read before it is used again
  Pending& p = s.pend[s.frame % kSlots];
  if (p.used) {
    GLuint64 ta = 0, tb = 0;
    glGetQueryObjectui64v(p.b, GL_QUERY_RESULT, &tb);
    if (p.hasA) glGetQueryObjectui64v(p.a, GL_QUERY_RESULT, &ta);
    const double end = (double)tb / 1e9;
    const double start = std::max(p.hasA ? (double)ta / 1e9 : 0.0, s.lastGpuEnd);
    s.outGpu = s.lastGpuEnd > 0 ? (end - start) * 1000.0 : -1;
    s.outLat = (end + p.offset - p.inputAt) * 1000.0;
    s.lastGpuEnd = end;
    p.used = p.hasA = false;
  }
}

void mark(Phase p) {
  State& s = S();
  if (!s.f) return;
  const double t = nowSeconds();
  s.ph[p] += t - s.last; s.last = t;
  if (p == Update && s.gl) {   // the world's drawing starts: the GPU's start mark for this frame
    Pending& q = s.pend[s.frame % kSlots];
    if (!q.a) glGenQueries(1, &q.a);
    glQueryCounter(q.a, GL_TIMESTAMP); q.hasA = true;
  }
}

void inputSampled() { State& s = S(); if (s.f) s.inputAt = nowSeconds(); }

void scope(const std::string& name, double ms) { State& s = S(); if (s.f) s.scopes[name] += ms; }

void frameEnd(bool gl) {
  State& s = S();
  if (!s.f) return;
  const double tEnd = nowSeconds();
  if (gl && s.gl) {   // the frame's end mark (after the swap and the limiter)
    if (s.frame % 60 == 0) {
      GLint64 g = 0; glGetInteger64v(GL_TIMESTAMP, &g);
      s.offset = nowSeconds() - (double)g / 1e9;
    }
    Pending& p = s.pend[s.frame % kSlots];
    if (!p.b) glGenQueries(1, &p.b);
    glQueryCounter(p.b, GL_TIMESTAMP);
    p.inputAt = s.inputAt; p.offset = s.offset; p.used = true;
  }
  std::vector<std::pair<double, std::string>> v;
  for (auto& [k, t] : s.scopes) v.push_back({t, k});
  std::sort(v.rbegin(), v.rend());
  std::string top;
  for (size_t i = 0; i < v.size() && i < 3; i++) { char b[96]; std::snprintf(b, sizeof b, "%s%s=%.2f", i ? " " : "", v[i].second.c_str(), v[i].first); top += b; }
  const double* P = s.ph;
  std::fprintf(s.f, "%llu,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s\n", (unsigned long long)s.frame, s.t0,
               (tEnd - s.t0) * 1000, P[Wait] * 1000, P[Input] * 1000, P[Update] * 1000, P[Render] * 1000, P[Ui] * 1000, P[Swap] * 1000,
               P[Sync] * 1000, P[Sleep] * 1000, s.outGpu, s.outLat, top.c_str());
  if (s.frame % 240 == 0) std::fflush(s.f);
  s.gl = gl;
  s.frame++;
}
}
