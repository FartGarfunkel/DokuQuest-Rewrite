// audio/dsp.cpp — see dsp.h. Ports of game/sfxlib.js (SndBuf), game/voices3.js (throat, hiss, clack,
// room, shape), game/sound3.js (hinge creak), game/bows.js (bow stave) and game/audio.js (note blocks).
#include "audio/dsp.h"
#include <algorithm>

namespace dq::snd {

static constexpr double DPI = 3.14159265358979323846;

u32 seedFromString(const std::string& str) {
  std::string s = str;
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  size_t b = 0; while (b < s.size() && isspace((unsigned char)s[b])) b++;
  s = s.substr(b);
  bool num = !s.empty() && (isdigit((unsigned char)s[0]) || (s[0] == '-' && s.size() > 1));
  for (size_t i = 1; num && i < s.size(); i++) num = isdigit((unsigned char)s[i]);
  if (num && s.size() < 11) { long long v = std::stoll(s); if (std::llabs(v) < (1ll << 31)) return (u32)(i32)v; }
  u32 h = 2166136261u;
  for (unsigned char c : s) { h ^= c; h *= 16777619u; }   // (ASCII names only: charCodeAt == byte)
  return h;
}

// ---- biquads ------------------------------------------------------------------------------------------------
char bqType(const std::string& t) {
  if (t == "lp" || t == "lowpass") return 'l';
  if (t == "hp" || t == "highpass") return 'h';
  return 'b';
}
void Biquad::set(char type, double f, double Q, double sr) {
  double w0 = 2 * DPI * std::min(f, sr * 0.45) / sr, cs = std::cos(w0), sn = std::sin(w0), al = sn / (2 * Q);
  double B0, B1, B2, A0, A1, A2;
  if (type == 'l') { B0 = (1 - cs) / 2; B1 = 1 - cs; B2 = (1 - cs) / 2; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; }
  else if (type == 'h') { B0 = (1 + cs) / 2; B1 = -(1 + cs); B2 = (1 + cs) / 2; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; }
  else { B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; }
  b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
}
void Biquad::setWA(const std::string& type, double f, double Q, double sr) {
  char t = bqType(type);
  // WebAudio: lowpass/highpass resonance Q is in dB; band-pass Q is the plain ratio
  set(t, std::max(10.0, f), t == 'b' ? std::max(1e-4, Q) : std::pow(10.0, Q / 20.0), sr);
}

// ---- AudioParam automation ---------------------------------------------------------------------------------------
float Param::at(double t) {
  while (cur < ev.size() && ev[cur].t <= t) cur++;
  float v0 = init; double t0 = 0;
  if (cur > 0) { v0 = ev[cur - 1].v; t0 = ev[cur - 1].t; }
  if (cur < ev.size()) {
    const Ev& e = ev[cur];
    if (e.kind == 1) { double u = (t - t0) / std::max(1e-9, e.t - t0); return (float)(v0 + (e.v - v0) * u); }
    if (e.kind == 2 && v0 * e.v > 0) { double u = (t - t0) / std::max(1e-9, e.t - t0); return (float)(v0 * std::pow((double)e.v / v0, u)); }
  }
  return v0;
}

// ---- oscillators ------------------------------------------------------------------------------------------------
int Osc::typeOf(const std::string& s) { return s == "square" ? 1 : s == "sawtooth" ? 2 : s == "triangle" ? 3 : 0; }
static inline float blep(double t, double dt) {
  if (t < dt) { t /= dt; return (float)(t + t - t * t - 1); }
  if (t > 1 - dt) { t = (t - 1) / dt; return (float)(t * t + t + t + 1); }
  return 0;
}
float Osc::next(double freq) {
  double dt = std::min(0.5, std::abs(freq) / SR);
  float y;
  switch (type) {
    case 1: y = (ph < 0.5 ? 1.f : -1.f) + blep(ph, dt) - blep(std::fmod(ph + 0.5, 1.0), dt); break;
    case 2: y = (float)(2 * ph - 1) - blep(ph, dt); break;
    case 3: y = (float)(ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph); break;
    case 4: { double u = std::fmod(freq, 1.0); if (u < 0) u += 1; y = (float)(std::sin(2*DPI*ph) + u * std::sin(2*DPI*(ph + freq/SR))); break; }
    case 5: { double df = std::abs(freq) / SR * 4294967296.0; ibph += (unsigned long long)(df > 1 ? df : 1);
             y = (float)((ibph & 0x80000000ull) ? -0.95 : 0.95); break; }
    case 6: { double P = SR / std::max(1e-3, std::abs(freq));
             ibph += (unsigned long long)std::llround(4294967296.0 / P);
             integ += ((ibph & 0x80000000ull) ? -1.0 : 1.0);          // band-limited impulse train
             double saw = integ * (P > 1 ? 1.0 / P : 1.0) * 2.0 - 1.0; // DC-leaky integrator -> ramp, normalized [-1,1]
             if (saw != saw || saw > 4 || saw < -4) { integ = 0; saw = 0; }
             y = (float)(2 * saw - 1 + blep(0.5 - 0.5, 1.0 / P)); break; }
    default: y = (float)std::sin(2 * DPI * ph);
  }
  ph += freq / SR; ph -= std::floor(ph);
  return y;
}

// ---- SndBuf -----------------------------------------------------------------------------------------------------
void SndBuf::noise(double t0, double dur, char type, double f, double Q, double gain, const NoiseOpt& o) {
  int i0 = (int)std::floor(t0 * sr), n = (int)std::floor(dur * sr), len = (int)d.size();
  if (i0 < 0) i0 = 0;
  Biquad c, c2; c.set(type, f, Q, sr); if (o.two) c2.set(type, f, Q, sr);
  int att = std::max(1, (int)std::floor(o.att * sr));
  double k = std::log(1000.0) / std::max(1, n - att);
  double brown = 0;
  for (int i = 0; i < n && i0 + i < len; i++) {
    float x = r->pm();
    if (o.brown) { brown = (brown + x * 0.08) * 0.995; x = (float)(brown * 6); }
    float y = c.run(x); if (o.two) y = c2.run(y);
    double env = i < att ? (double)i / att : std::exp(-(i - att) * k * o.curve);
    if (o.sweep != 0 && (i & 63) == 0) {
      double sw = o.sweep > 0 ? o.sweep : std::max(0.05, 1.0 + o.sweep), nf = f * std::pow(sw, (double)i / n);
      Biquad nc; nc.set(type, nf, Q, sr); c.b0 = nc.b0; c.b1 = nc.b1; c.b2 = nc.b2; c.a1 = nc.a1; c.a2 = nc.a2;
    }
    d[i0 + i] += (float)(y * env * gain);
  }
}
void SndBuf::grains(double t0, double span, int count, double fLo, double fHi, double Q, double gain, const GrainOpt& o) {
  for (int k = 0; k < count; k++) {
    double t = t0 + std::pow((double)(*r)(), o.front) * span, f = fLo * std::pow(fHi / fLo, (double)(*r)());
    double g = gain * (0.35 + (*r)() * 0.65) * (o.fade ? 1 - (t - t0) / span * 0.7 : 1);
    double du = o.dMin + (*r)() * (o.dMax - o.dMin), q = Q * (0.7 + (*r)() * 0.6);
    NoiseOpt no; no.att = 0.0006f;
    noise(t, du, o.type, f, q, g, no);
  }
}
void SndBuf::modes(double t0, const std::vector<Mode>& list, double gain, double droop) {
  int i0 = (int)std::floor(t0 * sr), len = (int)d.size();
  if (i0 >= len) return;
  for (const Mode& m : list) {
    double ph = (*r)() * DPI * 2, w = 2 * DPI * m.f / sr;
    int n = std::min(len - i0, (int)std::floor(m.dec * 7 * sr)), att = (int)std::floor(0.0015 * sr);
    double dk = std::exp(-1.0 / (m.dec * sr)), e = 1;
    for (int i = 0; i < n; i++) {
      d[i0 + i] += (float)(std::sin(ph + w * i * (1 + droop * i / n)) * e * (i < att ? (double)i / att : 1) * m.a * gain);
      e *= dk;
    }
  }
}
void SndBuf::thump(double t0, double f0, double f1, double dur, double gain) {
  int i0 = (int)std::floor(t0 * sr), n = (int)std::floor(dur * sr), len = (int)d.size();
  double ph = 0;
  for (int i = 0; i < n && i0 + i < len; i++) {
    double f = f0 * std::pow(f1 / f0, (double)i / n); ph += 2 * DPI * f / sr;
    d[i0 + i] += (float)(std::sin(ph) * std::exp(-i / (n * 0.28)) * std::min(1.0, i / (0.002 * sr)) * gain);
  }
}
void SndBuf::pluck(double t0, double f, double dur, double gain, double damp) {
  int L = std::max(2, (int)std::lround(sr / f)), i0 = (int)std::floor(t0 * sr), n = (int)std::floor(dur * sr), len = (int)d.size();
  std::vector<float> line(L);
  float lp = 0; for (int i = 0; i < L; i++) { lp += (r->pm() - lp) * 0.5f; line[i] = lp; }
  int p = 0;
  for (int i = 0; i < n && i0 + i < len; i++) {
    float a = line[p], b = line[(p + 1) % L]; line[p] = (float)((a + b) * 0.5 * damp);
    d[i0 + i] += (float)(a * gain * std::min(1.0, (n - i) / (0.02 * sr))); p = (p + 1) % L;
  }
}

// vowel resonances: sounds2.js VOWELS (3 formants) and voices3.js VOX_V (4) with VOX_BW
static const float* vowel3(char c) {
  static const float V[][3] = { {730, 1090, 2440}, {570, 840, 2410}, {300, 870, 2240}, {530, 1840, 2480}, {270, 2290, 3010}, {250, 1100, 2400}, {490, 1350, 1690}, {400, 1600, 2600} };
  const char* k = "aoueimry"; const char* p = strchr(k, c);
  return V[p && c ? p - k : 0];
}
static const float* vowel4(char c) {
  static const float V[][4] = { {730, 1090, 2440, 3400}, {570, 840, 2410, 3300}, {320, 870, 2240, 3200}, {530, 1840, 2480, 3500}, {280, 2250, 2900, 3600},
    {500, 1400, 2450, 3400}, {480, 1300, 1650, 3200}, {260, 1000, 2300, 3200}, {280, 1600, 2500, 3300}, {660, 1700, 2400, 3400} };
  const char* k = "aouei" "xrmny"; const char* p = strchr(k, c);
  return V[p && c ? p - k : 5];
}
static const float VOX_BW[4] = {80, 110, 170, 260};

void SndBuf::vox(double t0, double dur, double f0, double f1, const std::string& vowel, double gain, double breath) {
  int i0 = (int)std::floor(t0 * sr), n = (int)std::floor(dur * sr), len = (int)d.size();
  const float* V = vowel3(vowel.empty() ? 'a' : vowel[0]);
  if (vowel.size() != 1) V = vowel3('a');
  Biquad fs[3]; const float Qs[3] = {5, 7, 9}, lv[3] = {1, 0.5f, 0.22f};
  for (int k = 0; k < 3; k++) fs[k].set('b', V[k], Qs[k], sr);
  double ph = 0;
  for (int i = 0; i < n && i0 + i < len; i++) {
    double f = f0 * std::pow(f1 / f0, (double)i / n); ph += f / sr; if (ph > 1) ph -= 1;
    float src = (float)((ph * 2 - 1) * 0.7 + r->pm() * breath);
    float y = 0; for (int k = 0; k < 3; k++) y += fs[k].run(src) * lv[k];
    double env = std::min(1.0, i / (0.012 * sr)) * std::pow(1.0 - (double)i / n, 1.6);
    d[i0 + i] += (float)(y * env * gain * 2.4);
  }
}

static double jnum(const json& o, const char* k, double def) { auto it = o.find(k); return it != o.end() && it->is_number() ? it->get<double>() : def; }

void SndBuf::throat(double t0, double dur, const json& o) {
  int i0 = (int)std::floor(t0 * sr), n = std::min((int)std::floor(dur * sr), (int)d.size() - i0);
  if (n <= 0) return;
  std::vector<std::pair<double, double>> pts;
  if (o.contains("f")) for (auto& p : o["f"]) pts.push_back({p[0].get<double>(), p[1].get<double>()});
  if (pts.empty()) pts = {{0, 120}, {1, 100}};
  std::string vs = o.value("v", std::string("x")); if (vs.empty()) vs = "x";
  double k = jnum(o, "k", 1), bwk = jnum(o, "bw", 1), breath = jnum(o, "breath", 0);
  auto fAt = [&](double t) {
    for (size_t j = 1; j < pts.size(); j++) if (t <= pts[j].first) {
      auto [ta, fa] = pts[j - 1]; auto [tb, fb] = pts[j];
      double u = (t - ta) / std::max(1e-6, tb - ta); return fa + (fb - fa) * (u * u * (3 - 2 * u));
    }
    return pts.back().second;
  };
  auto vAt = [&](double t, int q) {
    if (vs.size() == 1) return (double)vowel4(vs[0])[q];
    double x = t * (vs.size() - 1); int j = std::min((int)vs.size() - 2, (int)std::floor(x)); double u = x - j, s = u * u * (3 - 2 * u);
    double a = vowel4(vs[j])[q], b = vowel4(vs[j + 1])[q]; return a + (b - a) * s;
  };
  struct Res { double a1 = 0, a2 = 0, b0 = 1, y1 = 0, y2 = 0; } R[4];
  auto setR = [&](double t) {
    for (int q = 0; q < 4; q++) {
      double F = std::min(sr * 0.45, vAt(t, q) * k), B = VOX_BW[q] * bwk * (1 + breath * 0.6), rr = std::exp(-DPI * B / sr);
      R[q].a1 = 2 * rr * std::cos(2 * DPI * F / sr); R[q].a2 = -rr * rr; R[q].b0 = 1 - R[q].a1 - R[q].a2;
    }
  };
  double open = jnum(o, "open", 0.5); if (open == 0) open = 0.5;
  double Tp = open * 0.72, Tn = open * 0.28;
  int att = std::max(1, (int)std::floor(jnum(o, "att", 0.04) * sr)), rel = (int)std::floor(n * jnum(o, "rel", 0.45));
  double ph = (*r)(), prev = 0, jf = 1, amp = 1, lp = 0, vibPh = (*r)() * 6; int odd = 0;
  double jit = jnum(o, "jit", 0.015), shim = jnum(o, "shim", 0.08), sub = jnum(o, "sub", 0), rough = jnum(o, "rough", 0), drive = jnum(o, "drive", 0);
  double pitch = jnum(o, "pitch", 1); if (pitch == 0) pitch = 1;
  bool vib = o.contains("vib") && o["vib"].is_array(); double vr = vib ? o["vib"][0].get<double>() : 0, vd = vib ? o["vib"][1].get<double>() : 0;
  std::vector<std::pair<double, double>> am; std::vector<double> amPh;
  if (o.contains("am") && o["am"].is_array()) for (auto& a : o["am"]) { am.push_back({a[0].get<double>(), a[1].get<double>()}); amPh.push_back((*r)() * 6.28); }
  auto gauss = [&]() { return ((*r)() + (*r)() + (*r)() + (*r)() - 2) * 0.87; };
  double nz = 0, peak = 1e-9;
  std::vector<float> tmp(n), envs(n);
  for (int i = 0; i < n; i++) {
    double t = (double)i / n;
    if ((i & 31) == 0) setR(t);
    double f = fAt(t) * pitch;
    if (vib) { vibPh += 2 * DPI * vr / sr; f += std::sin(vibPh) * vd; }
    ph += f * jf / sr;
    if (ph >= 1) {
      ph -= 1; odd ^= 1;
      jf = 1 + jit * gauss();
      amp = std::max(0.0, 1 + shim * gauss()) * (sub != 0 && odd ? 1 - sub : 1) * (rough != 0 ? 1 - rough * (*r)() : 1);
    }
    double g = ph < Tp ? 0.5 * (1 - std::cos(DPI * ph / Tp)) : ph < Tp + Tn ? std::cos(DPI / 2 * (ph - Tp) / Tn) : 0;
    double dg = (g - prev) * sr / std::max(40.0, f) * 0.12; prev = g;
    nz = nz * 0.3 + r->pm() * 0.7;
    double x = dg * amp * (1 - breath * 0.55) + nz * breath * (0.25 + 0.75 * g) * 0.9;
    for (int q = 0; q < 4; q++) { double y = R[q].b0 * x + R[q].a1 * R[q].y1 + R[q].a2 * R[q].y2; R[q].y2 = R[q].y1; R[q].y1 = y; x = y; }
    lp += (x - lp) * 0.6;
    tmp[i] = (float)lp; peak = std::max(peak, std::abs(lp));
    double env = i < att ? std::sin(DPI / 2 * i / att) : 1;
    if (i > n - rel && rel > 0) env *= std::pow((double)(n - i) / rel, 1.4);
    for (size_t j = 0; j < am.size(); j++) env *= 1 - am[j].second * (0.5 + 0.5 * std::sin(amPh[j] + 2 * DPI * am[j].first * i / sr));
    envs[i] = (float)env;
  }
  double g = jnum(o, "gain", 1) / peak, dn = drive != 0 ? std::tanh(1 + drive) : 1;
  for (int i = 0; i < n; i++) { double y = tmp[i] * g; if (drive != 0) y = std::tanh(y * (1 + drive)) / dn; d[i0 + i] += (float)(y * envs[i]); }
}

void SndBuf::hiss(double t0, double dur, const json& o) {
  int i0 = (int)std::floor(t0 * sr), n = std::min((int)std::floor(dur * sr), (int)d.size() - i0);
  if (n <= 0) return;
  struct B { Biquad c; double lv; };
  std::vector<B> bs;
  if (o.contains("bands")) for (auto& b : o["bands"]) { B x; x.c.set('b', b[0].get<double>(), b[1].get<double>(), sr); x.lv = b[2].get<double>(); bs.push_back(x); }
  else { B x; double f = jnum(o, "f", 4000), q = jnum(o, "q", 1.2); x.c.set('b', f ? f : 4000, q ? q : 1.2, sr); x.lv = 1; bs.push_back(x); }
  int att = std::max(1, (int)std::floor(jnum(o, "att", 0.05) * sr)), rel = (int)std::floor(n * jnum(o, "rel", 0.5));
  std::vector<std::pair<double, double>> am;
  if (o.contains("am")) for (auto& a : o["am"]) am.push_back({a[0].get<double>(), a[1].get<double>()});
  double gate = jnum(o, "gate", 0), gain = jnum(o, "gain", 1);
  for (int i = 0; i < n; i++) {
    float x = r->pm(); double y = 0;
    for (auto& b : bs) y += b.c.run(x) * b.lv;
    double env = i < att ? (double)i / att : 1;
    if (i > n - rel && rel > 0) env *= (double)(n - i) / rel;
    for (auto& [rate, dep] : am) env *= 1 - dep * (0.5 + 0.5 * std::sin(2 * DPI * rate * i / sr));
    if (gate != 0) env *= std::sin(2 * DPI * gate * i / sr) > 0 ? 1 : 0.15;
    d[i0 + i] += (float)(y * env * gain);
  }
}
void SndBuf::clack(double t0, double f, double gain, double ring) {
  modes(t0, {{f, ring, 1}, {f * 2.37, ring * 0.6, 0.5}, {f * 4.1, ring * 0.35, 0.25}}, gain);
  NoiseOpt o; o.att = 0.0004f;
  noise(t0, 0.004, 'b', f * 1.6, 1.5, gain * 0.6, o);
}
void SndBuf::room(double mix, double size) {
  std::vector<float> out(d.size());
  struct C { std::vector<float> buf; size_t p = 0; double lp = 0; };
  C cs[4]; const double dl[4] = {0.0297, 0.0371, 0.0411, 0.0437};
  for (int i = 0; i < 4; i++) cs[i].buf.assign(std::max(1, (int)std::floor(dl[i] * size * sr)), 0.f);
  double fb = 0.72 + std::min(0.2, (size - 1) * 0.1);
  for (size_t i = 0; i < d.size(); i++) {
    double s = 0;
    for (auto& c : cs) { double y = c.buf[c.p]; c.lp = y * 0.6 + c.lp * 0.4; c.buf[c.p] = (float)(d[i] + c.lp * fb); c.p = (c.p + 1) % c.buf.size(); s += y; }
    out[i] = (float)(s * 0.25);
  }
  for (size_t i = 0; i < d.size(); i++) d[i] = (float)(d[i] * (1 - mix * 0.5) + out[i] * mix);
}
void SndBuf::shape(char type, double f, double q) { Biquad c; c.set(type, f, q, sr); for (auto& x : d) x = c.run(x); }

// sound3.js: a hinge creaking — the wood sticks and slips, faster in the middle of the swing
void SndBuf::creak(double t0, double dur, double f0, double gain) {
  int i0 = (int)std::floor(t0 * sr), n = (int)std::floor(dur * sr), len = (int)d.size();
  Biquad c1, c2; c1.set('b', f0, 2.2, sr); c2.set('b', f0 * 2.3, 3, sr);
  double ph = 0;
  for (int i = 0; i < n && i0 + i < len; i++) {
    double t = (double)i / n, rate = 26 + 34 * std::sin(t * DPI) + (*r)() * 14;
    ph += rate / sr;
    double slip = std::fmod(ph, 1.0) < 0.18 ? ((*r)() * 0.6 + 0.4) : (*r)() * 0.08;
    double y = c1.run((float)slip) * 0.8 + c2.run((float)slip) * 0.35;
    d[i0 + i] += (float)(y * gain * std::sin(DPI * std::min(1.0, t * 1.15)));
  }
}
// bows.js: the stave creaks and the string stretches
void SndBuf::bowDraw() {
  int n = std::min((int)d.size(), (int)std::floor(0.6 * sr));
  Biquad c; c.set('b', 700, 4, sr); double ph = 0;
  for (int i = 0; i < n; i++) {
    double f = 18 + 30 * ((double)i / n) + (*r)() * 6; ph += f / sr;
    double stick = std::fmod(ph, 1.0) < 0.06 ? 1 : 0;
    d[i] += (float)(c.run((float)(stick * (0.5 + (*r)() * 0.5))) * 0.7 * std::min(1.0, i / (0.05 * sr)));
  }
}
// sfxlib.js: pop — a sine rising fast and dying
void SndBuf::popSweep(double f0) {
  int n = std::min((int)d.size(), (int)std::floor(0.07 * sr)); double ph = 0;
  for (int i = 0; i < n; i++) { ph += (f0 * (1 + 1.6 * i / n)) / sr; d[i] += (float)(std::sin(ph * 2 * DPI) * std::exp(-i / (n * 0.35)) * 0.8); }
}
void SndBuf::finish(float peak) {
  float m = 0;
  for (auto& x : d) { if (!std::isfinite(x)) x = 0; m = std::max(m, std::abs(x)); }
  float k = m > 0 ? peak / m : 1, th = std::tanh(1.1f);
  int fade = (int)std::floor(sr * 0.01), L = (int)d.size();
  for (int i = 0; i < L; i++) { float v = std::tanh(d[i] * k * 1.1f) / th; if (i > L - fade) v *= (float)(L - i) / fade; d[i] = v; }
}

// ---- recorded recipe ops ----------------------------------------------------------------------------------------
static NoiseOpt noiseOpt(const json& o) {
  NoiseOpt n;
  if (!o.is_object()) return n;
  n.att = (float)jnum(o, "att", 0.002); n.curve = (float)jnum(o, "curve", 1); n.sweep = (float)jnum(o, "sweep", 0);
  if (n.curve == 0) n.curve = 1;
  n.brown = o.value("brown", false); n.two = o.value("two", false);
  return n;
}
void runOp(SndBuf& b, const json& op, const std::string& key) {
  const std::string& m = op[0].get_ref<const std::string&>();
  auto N = [&](size_t i, double def = 0) { return i < op.size() && op[i].is_number() ? op[i].get<double>() : def; };
  auto O = [&](size_t i) -> const json& { static const json nul; return i < op.size() ? op[i] : nul; };
  if (m == "noise") b.noise(N(1), N(2), bqType(op[3].get<std::string>()), N(4), N(5), N(6), noiseOpt(O(7)));
  else if (m == "grains") {
    GrainOpt g; const json& o = O(8);
    if (o.is_object()) { g.front = (float)jnum(o, "front", 1); g.dMin = (float)jnum(o, "dMin", 0.004); g.dMax = (float)jnum(o, "dMax", 0.014); g.fade = o.value("fade", false); if (o.contains("type")) g.type = bqType(o["type"].get<std::string>()); }
    b.grains(N(1), N(2), (int)N(3), N(4), N(5), N(6), N(7), g);
  } else if (m == "modes") {
    std::vector<SndBuf::Mode> l;
    for (auto& x : op[2]) l.push_back({x[0].get<double>(), x[1].get<double>(), x[2].get<double>()});
    const json& o = O(4);
    b.modes(N(1), l, N(3), o.is_object() ? jnum(o, "droop", 0) : 0);
  } else if (m == "thump") b.thump(N(1), N(2), N(3), N(4), N(5));
  else if (m == "pluck") b.pluck(N(1), N(2), N(3), N(4), N(5, 0.996));
  else if (m == "vox") b.vox(N(1), N(2), N(3), N(4), op[5].is_string() ? op[5].get<std::string>() : "a", N(6), N(7, 0.3));
  else if (m == "throat") b.throat(N(1), N(2), O(3).is_object() ? O(3) : json::object());
  else if (m == "hiss") b.hiss(N(1), N(2), O(3).is_object() ? O(3) : json::object());
  else if (m == "clack") b.clack(N(1), N(2), N(3), N(4, 0.012));
  else if (m == "room") b.room(N(1, 0.15), N(2, 1));
  else if (m == "shape") b.shape(bqType(op[1].get<std::string>()), N(2), N(3, 0.7));
  else if (m == "_pop") b.popSweep(N(1));
  else if (m == "_raw") {
    if (key == "fx.bow_draw") { b.bowDraw(); return; }
    // creak(b, r, 0, dur, f0, gain): the length and level of each door's creak (sound3.js)
    double f0 = op[1][1].get<double>(), dur = 0.3, g = 0.45;
    if (key == "fx.door_open") { dur = 0.36; g = 0.55; } else if (key == "fx.door_close") { dur = 0.12; g = 0.25; }
    else if (key == "fx.trapdoor_open") { dur = 0.2; g = 0.4; } else if (key == "fx.gate_open") { dur = 0.3; g = 0.45; }
    else if (key == "fx.chest") { dur = 0.45; g = 0.5; }
    b.creak(0, dur, f0, g);
  }
}

// ---- live (WebAudio) sounds -------------------------------------------------------------------------------------
static double opsLength(const json& ops) {
  double L = 0.05;
  for (auto& o : ops) {
    const std::string& k = o[0].get_ref<const std::string&>();
    if (k == "tone") L = std::max(L, o[1].get<double>() + o[3].get<double>() + 0.05);
    else if (k == "nh") L = std::max(L, o[1].get<double>() + o[2].get<double>() + 0.05);
    else if (k == "voice" || k == "swell") L = std::max(L, jnum(o[1], "delay", 0) + std::max(0.05, jnum(o[1], "dur", k == "voice" ? 0.5 : 1)) + 0.05);
  }
  return L;
}
static void tone(std::vector<float>& d, double t, double freq, double dur, const std::string& type, double gain, double slideTo) {
  Osc o; o.type = Osc::typeOf(type);
  Param f; f.init = (float)freq; f.set((float)freq, t); if (slideTo > 0) f.exp((float)slideTo, t + dur);
  Param g; g.set(0.0001f, t); g.exp((float)gain, t + 0.01); g.exp(0.0001f, t + dur);
  int i0 = (int)(t * SR), i1 = std::min((int)d.size(), (int)((t + dur + 0.05) * SR));
  for (int i = std::max(0, i0); i < i1; i++) { double tt = (double)i / SR; d[i] += o.next(f.at(tt)) * g.at(tt); }
}
static void noiseHit(std::vector<float>& d, Rng& r, double t, double dur, double freq, double q, double gain, const std::string& type) {
  Biquad c; c.setWA(type, freq, q);
  Param g; g.set((float)gain, t); g.exp(0.001f, t + dur);
  int i0 = (int)(t * SR), i1 = std::min((int)d.size(), (int)((t + dur + 0.05) * SR));
  for (int i = std::max(0, i0); i < i1; i++) d[i] += c.run(r.pm()) * g.at((double)i / SR);
}
// sounds2.js Sfx.voice: a buzzy source through three vowel resonances with a pitch curve, vibrato, tremble, breath
static void voice(std::vector<float>& d, Rng& r, const json& o) {
  double t0 = jnum(o, "delay", 0), dur = std::max(0.05, jnum(o, "dur", 0.5)), pm = jnum(o, "pitch", 1); if (pm == 0) pm = 1;
  Osc osc; osc.type = Osc::typeOf(o.value("wave", std::string("sawtooth")));
  Param fr; std::vector<std::pair<double, double>> pts;
  if (o.contains("f")) for (auto& p : o["f"]) pts.push_back({p[0].get<double>(), p[1].get<double>()});
  if (pts.empty()) pts = {{0, 200}, {1, 200}};
  fr.set((float)std::max(20.0, pts[0].second * pm), t0);
  for (size_t i = 1; i < pts.size(); i++) fr.lin((float)std::max(20.0, pts[i].second * pm), t0 + pts[i].first * dur);
  bool vib = o.contains("vib"); double vr = vib ? o["vib"][0].get<double>() : 0, vd = vib ? o["vib"][1].get<double>() * pm : 0;
  double nzl = jnum(o, "noise", 0) * 2.2;
  std::string vs = o.value("v", std::string("a")); if (vs.empty()) vs = "a";
  double k = jnum(o, "k", 1); if (k == 0) k = 1;
  double qo = jnum(o, "q", 0);
  Biquad bp[3]; Param bf[3]; const double Qd[3] = {4, 6, 8}, lv[3] = {1, 0.55, 0.22};
  for (int i = 0; i < 3; i++) {
    bf[i].set((float)(vowel3(vs[0])[i] * k), t0);
    for (size_t j = 1; j < vs.size(); j++) bf[i].lin((float)(vowel3(vs[j])[i] * k), t0 + dur * j / (vs.size() - 1));
  }
  double g = jnum(o, "gain", 0.3), att = jnum(o, "att", 0.03), hold = jnum(o, "hold", 0.55);
  Param env; env.set(0.0001f, t0); env.exp((float)g, t0 + att); env.set((float)g, t0 + std::max(att + 0.01, dur * hold)); env.exp(0.0001f, t0 + dur);
  bool am = o.contains("am"); double ar = am ? o["am"][0].get<double>() : 0, ad = am ? o["am"][1].get<double>() : 0;
  double lpf = jnum(o, "lp", 0); Biquad lp; if (lpf > 0) lp.setWA("lowpass", lpf, 1);
  int i0 = (int)(t0 * SR), i1 = std::min((int)d.size(), (int)((t0 + dur + 0.05) * SR));
  double vph = 0, aph = 0;
  for (int i = std::max(0, i0); i < i1; i++) {
    double t = (double)i / SR;
    if ((i & 15) == 0) for (int q = 0; q < 3; q++) bp[q].setWA("bandpass", bf[q].at(t), qo > 0 ? qo : Qd[q]);
    double f = fr.at(t); if (vib) { f += std::sin(vph) * vd; vph += 2 * DPI * vr / SR; }
    float x = osc.next(f) + (nzl > 0 ? (float)(r.pm() * nzl) : 0.f);
    double y = 0; for (int q = 0; q < 3; q++) y += bp[q].run(x) * lv[q] * 3.2;
    y *= env.at(t);
    if (am) { y *= 1 - ad / 2 + ad / 2 * std::sin(aph); aph += 2 * DPI * ar / SR; }
    if (lpf > 0) y = lp.run((float)y);
    d[i] += (float)y;
  }
}
// sounds2.js Sfx.swell: a noise swell with a moving filter
static void swell(std::vector<float>& d, Rng& r, const json& o) {
  double t0 = jnum(o, "delay", 0), dur = jnum(o, "dur", 1); if (dur == 0) dur = 1;
  std::string type = o.value("type", std::string("bandpass")); double q = jnum(o, "q", 3); if (q == 0) q = 3;
  Param fr; std::vector<std::pair<double, double>> pts;
  if (o.contains("fr")) for (auto& p : o["fr"]) pts.push_back({p[0].get<double>(), p[1].get<double>()});
  if (pts.empty()) pts = {{0, 600}, {1, 600}};
  fr.set((float)pts[0].second, t0); for (size_t i = 1; i < pts.size(); i++) fr.lin((float)pts[i].second, t0 + pts[i].first * dur);
  double peak = jnum(o, "gain", 0.2), rise = jnum(o, "rise", 0.4);
  Param g; g.set(0.0001f, t0); g.exp((float)peak, t0 + dur * rise); g.exp(0.0001f, t0 + dur);
  Biquad c;
  int i0 = (int)(t0 * SR), i1 = std::min((int)d.size(), (int)((t0 + dur + 0.05) * SR));
  for (int i = std::max(0, i0); i < i1; i++) {
    double t = (double)i / SR;
    if (((i - i0) & 15) == 0) c.setWA(type, fr.at(t), q);
    d[i] += (float)(c.run(r.pm()) * g.at(t));
  }
}
std::vector<float> renderLive(const json& ops, u32 seed) {
  Rng r(seed);
  std::vector<float> d((size_t)(opsLength(ops) * SR) + 1, 0.f);
  for (auto& o : ops) {
    const std::string& k = o[0].get_ref<const std::string&>();
    if (k == "tone") tone(d, o[1].get<double>(), o[2].get<double>(), o[3].get<double>(), o[4].get<std::string>(), o[5].get<double>(), o[6].get<double>());
    else if (k == "nh") noiseHit(d, r, o[1].get<double>(), o[2].get<double>(), o[3].get<double>(), o[4].get<double>(), o[5].get<double>(), o[6].get<std::string>());
    else if (k == "voice") voice(d, r, o[1]);
    else if (k == "swell") swell(d, r, o[1]);
  }
  return d;
}
std::vector<float> renderSpecial(const std::string& name, u32 seed) {
  Rng r(seed);
  if (name == "cave_scrape") {   // sounds2.js: metal dragged somewhere, a saw sliding down with a fast wobble
    std::vector<float> d((size_t)(3.15 * SR), 0.f);
    Osc o; o.type = 2; Param f; f.set(330, 0); f.exp(190, 3); Param g; g.set(0.0001f, 0); g.exp(0.05f, 1.2); g.exp(0.0001f, 3);
    Biquad bp; bp.setWA("bandpass", 1400, 6);
    for (size_t i = 0; i < d.size(); i++) { double t = (double)i / SR; d[i] = (float)(bp.run(o.next(f.at(t) + 60 * std::sin(2 * DPI * 47 * t))) * g.at(t)); }
    return d;
  }
  // "cave" (audio.js): a low, distant swell — the only sound he ever makes
  std::vector<float> d((size_t)(4.6 * SR), 0.f);
  Biquad lp; lp.setWA("lowpass", 160, 6); Param g; g.set(0.0001f, 0); g.exp(0.16f, 1.6); g.exp(0.0001f, 4.5);
  for (size_t i = 0; i < d.size(); i++) d[i] = lp.run(r.pm()) * g.at((double)i / SR);
  tone(d, 0, 55, 4, "sine", 0.05, 52);
  return d;
}
std::vector<float> renderNote(int n, const std::string& mat, u32 seed) {
  Rng r(seed);
  double f = 185 * std::pow(2.0, n / 12.0);
  std::vector<float> d((size_t)(1.0 * SR), 0.f);
  if (mat == "stone") { tone(d, 0, f / 2, 0.25, "triangle", 0.16, f / 2.05); noiseHit(d, r, 0, 0.05, 200, 1, 0.2, "lowpass"); }
  else if (mat == "sand" || mat == "gravel") noiseHit(d, r, 0, 0.08, 4000, 2, 0.2, "highpass");
  else if (mat == "glass") tone(d, 0, f * 2, 0.12, "square", 0.04, f * 2);
  else if (mat == "metal" || mat == "gold") { tone(d, 0, f * 2, 0.9, "sine", 0.08, f * 2); tone(d, 0, f * 4.02, 0.6, "sine", 0.03, f * 4); }
  else if (mat == "wool") tone(d, 0, f / 2, 0.5, "sine", 0.12, f / 2);
  else { tone(d, 0, f, 0.45, "triangle", 0.12, f); tone(d, 0, f * 2, 0.15, "sine", 0.04, f * 2); }
  return d;
}

// ---- FFT and reverb ---------------------------------------------------------------------------------------------
void fft(std::vector<float>& re, std::vector<float>& im, bool inverse) {
  size_t n = re.size();
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = 2 * DPI / len * (inverse ? 1 : -1); double wr = std::cos(ang), wi = std::sin(ang);
    for (size_t i = 0; i < n; i += len) {
      double cr = 1, ci = 0;
      for (size_t k = 0; k < len / 2; k++) {
        size_t a = i + k, b = a + len / 2;
        double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = (float)(re[a] - xr); im[b] = (float)(im[a] - xi); re[a] += (float)xr; im[a] += (float)xi;
        double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}
void Reverb::init(float t60, float dmp, int sr) {
  static const int CT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, AT[4] = {556, 441, 341, 225};
  double sc = sr / 44100.0;
  for (int s = 0; s < 2; s++) {
    for (int i = 0; i < 8; i++) { c[s][i].b.assign((size_t)((CT[i] + s * 23) * sc), 0.f); c[s][i].p = 0; c[s][i].lp = 0; }
    for (int i = 0; i < 4; i++) { a[s][i].b.assign((size_t)((AT[i] + s * 23) * sc), 0.f); a[s][i].p = 0; }
  }
  fb = (float)std::pow(10.0, -3.0 * (1356 * sc / sr) / std::max(0.1f, t60));
  damp = dmp;
  outGain = (float)(std::sqrt(1.0 - (double)fb * fb) / (0.12 * std::sqrt(8.0)));
}
void Reverb::process(float inL, float inR, float& outL, float& outR) {
  float in[2] = {inL, inR}, out[2] = {0, 0};
  for (int s = 0; s < 2; s++) {
    float x = in[s] * 0.12f, acc = 0;
    for (auto& cb : c[s]) { float y = cb.b[cb.p]; cb.lp = y * (1 - damp) + cb.lp * damp; cb.b[cb.p] = x + cb.lp * fb; if (++cb.p >= (int)cb.b.size()) cb.p = 0; acc += y; }
    for (auto& ap : a[s]) { float bo = ap.b[ap.p]; float y = -acc + bo; ap.b[ap.p] = acc + bo * 0.5f; if (++ap.p >= (int)ap.b.size()) ap.p = 0; acc = y; }
    out[s] = acc * outGain;
  }
  outL = out[0]; outR = out[1];
}

} // namespace dq::snd
