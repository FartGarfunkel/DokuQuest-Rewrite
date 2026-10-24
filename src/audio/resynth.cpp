// audio/resynth.cpp — see resynth.h; mirrors resynth() in tools/port/sound_analyze.py.
#include "audio/resynth.h"
#include "common/fs.h"
#include <cstring>

namespace dq::snd {

static double erb(double f) { return 21.4 * std::log10(1 + 0.00437 * f); }
static inline float codeDb(u8 c) { return c == 0 ? -300.f : -100.f + c * 0.5f; }

bool PackSounds::load(const std::filesystem::path& jp, const std::filesystem::path& bp) {
  auto j = readJson(jp); auto raw = readBinary(bp);
  if (!j || !raw) return false;
  std::vector<u8> blob;
  size_t expect = j->value("raw", (size_t)0);
  if (!zDecompress(raw->data(), raw->size(), blob, expect)) { DQ_WARN("audio: %s is damaged", bp.string().c_str()); return false; }
  nb = j->value("bands", 26); nfft = j->value("nfft", 2048); fine = j->value("fine", 128); db0 = j->value("db0", -100.f);
  std::vector<double> edges; for (auto& e : (*j)["edges"]) edges.push_back(e.get<double>());
  if ((int)edges.size() != nb + 1) return false;
  centE.resize(nb);
  for (int b = 0; b < nb; b++) centE[b] = (float)((erb(edges[b]) + erb(edges[b + 1])) / 2);
  int xb = 0; for (int b = 0; b <= nb; b++) if (std::abs(edges[b] - 1000) < std::abs(edges[xb] - 1000)) xb = b;
  xo = (float)edges[xb];
  auto undelta = [](const u8* p, size_t n, std::vector<u8>& out) { out.resize(n); u8 acc = 0; for (size_t i = 0; i < n; i++) { acc = (u8)(acc + p[i]); out[i] = acc; } };
  for (auto& [name, ref] : (*j)["sounds"].items()) {
    size_t off = ref[0].get<size_t>(), len = ref[1].get<size_t>();
    if (off + len > blob.size() || len < 10) continue;
    const u8* p = blob.data() + off; size_t o = 0;
    Profile pr; u16 hop, nfr, ntr;
    memcpy(&pr.n, p, 4); memcpy(&hop, p + 4, 2); memcpy(&nfr, p + 6, 2); memcpy(&ntr, p + 8, 2); o = 10;
    pr.hop = hop; pr.nfr = nfr;
    size_t nlo = (pr.n + fine - 1) / fine, nhi = nlo;
    if (o + nlo + nhi + (size_t)nfr * nb > len) continue;
    undelta(p + o, nlo, pr.fineLo); o += nlo;
    undelta(p + o, nhi, pr.fineHi); o += nhi;
    undelta(p + o, (size_t)nfr * nb, pr.bands); o += (size_t)nfr * nb;
    bool bad = false;
    for (int t = 0; t < ntr && !bad; t++) {
      if (o + 6 > len) { bad = true; break; }
      u16 s0, L, f0; memcpy(&s0, p + o, 2); memcpy(&L, p + o + 2, 2); memcpy(&f0, p + o + 4, 2); o += 6;
      if (o + 2 * (size_t)L > len + 1) { bad = true; break; }
      Profile::Track tr; tr.s = s0; tr.f.resize(L); tr.a.resize(L);
      int lf = f0; tr.f[0] = (float)(10 * std::pow(2.0, lf / 1024.0));
      for (int i = 1; i < L; i++) { lf += (i8)p[o++]; tr.f[i] = (float)(10 * std::pow(2.0, lf / 1024.0)); }
      int am = p[o++]; tr.a[0] = std::pow(10.f, codeDb((u8)am) / 20);
      for (int i = 1; i < L; i++) { am += (i8)p[o++]; tr.a[i] = std::pow(10.f, codeDb((u8)std::clamp(am, 0, 255)) / 20); }
      pr.tracks.push_back(std::move(tr));
    }
    if (bad) continue;
    profiles.emplace(name, std::move(pr));
  }
  for (auto& [key, list] : (*j)["events"].items()) {
    std::vector<Variant> v;
    for (auto& x : list) if (profiles.count(x[0].get<std::string>())) v.push_back({x[0].get<std::string>(), x[1].get<float>(), x[2].get<float>()});
    if (!v.empty()) events[key] = std::move(v);
  }
  DQ_LOG("audio: %zu resource-pack sound profiles, %zu events", profiles.size(), events.size());
  return !profiles.empty();
}

// scale y block by block to the stored loudness (3-block smoothing on both sides, gain at most 30)
static void matchFine(std::vector<float>& y, u32 n, const std::vector<u8>& fdb, int B) {
  size_t nf = fdb.size();
  std::vector<double> e(nf, 0), tg(nf), g(nf);
  for (size_t b = 0; b < nf; b++) {
    double s = 0; for (int i = 0; i < B; i++) { size_t k = b * B + i; if (k < n) s += (double)y[k] * y[k]; }
    e[b] = s / B; tg[b] = fdb[b] ? std::pow(10.0, codeDb(fdb[b]) / 10.0) : 0;
  }
  auto sm = [&](const std::vector<double>& v, size_t b) { return 0.5 * v[b] + 0.25 * (b > 0 ? v[b - 1] : 0) + 0.25 * (b + 1 < nf ? v[b + 1] : 0); };
  for (size_t b = 0; b < nf; b++) { double ts = sm(tg, b), es = sm(e, b); g[b] = ts < 1e-12 ? 0 : std::min(30.0, std::sqrt(ts / std::max(es, 1e-20))); }
  for (u32 i = 0; i < n; i++) {
    double c = (i - B / 2.0) / B; double gi;
    if (c <= 0) gi = g[0]; else if (c >= nf - 1) gi = g[nf - 1];
    else { size_t b = (size_t)c; double u = c - b; gi = g[b] * (1 - u) + g[b + 1] * u; }
    y[i] = (float)(y[i] * gi);
  }
}

std::vector<float> PackSounds::render(const Profile& p, u32 seed) const {
  Rng r(seed);
  const int N = nfft, hs = N / 4, half = N / 2;
  u32 n = p.n;
  std::vector<float> lo(n + 2 * N, 0.f), hi(n + 2 * N, 0.f);
  // window and per-bin interpolation weights on the ERB scale
  std::vector<float> ws(N); double sw2 = 0;
  for (int i = 0; i < N; i++) { ws[i] = (float)(0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / (N - 1))); sw2 += (double)ws[i] * ws[i]; }
  std::vector<int> kb(half + 1); std::vector<float> kw(half + 1); std::vector<u8> klow(half + 1);
  for (int k = 0; k <= half; k++) {
    double fk = (double)k * SR / N, e = erb(fk);
    klow[k] = fk < xo;
    if (e <= centE[0]) { kb[k] = 0; kw[k] = 0; }
    else if (e >= centE[nb - 1]) { kb[k] = nb - 2; kw[k] = 1; }
    else { int b = 0; while (b < nb - 2 && centE[b + 1] < e) b++; kb[k] = b; kw[k] = (float)((e - centE[b]) / (centE[b + 1] - centE[b])); }
  }
  const double mscale = (double)N * hs / sw2;
  std::vector<float> re(N), im(N), lv(nb);
  int nframes = (int)(n / hs) + 2;
  for (int j = 0; j < nframes; j++) {
    double t = (double)j * hs / p.hop; int i0 = std::min((int)t, p.nfr - 1), i1 = std::min(i0 + 1, p.nfr - 1); double u = t - std::floor(t);
    for (int b = 0; b < nb; b++) lv[b] = (float)(codeDb(p.bands[(size_t)b * p.nfr + i0]) * (1 - u) + codeDb(p.bands[(size_t)b * p.nfr + i1]) * u);
    // X = A + iB packs the two halves' spectra into one complex inverse FFT: re = below XO, im = above
    std::fill(re.begin(), re.end(), 0.f); std::fill(im.begin(), im.end(), 0.f);
    for (int k = 1; k <= half; k++) {
      float db = lv[kb[k]] * (1 - kw[k]) + lv[kb[k] + 1] * kw[k];
      if (db < -160) continue;
      double M = std::sqrt(std::pow(10.0, db / 10.0) * mscale), ph = r() * 6.283185307179586;
      float ar = (float)(M * std::cos(ph)), ai = (float)(M * std::sin(ph));
      if (k == half) ai = 0;
      // A (k) = a, A(N-k) = conj(a); X = A + iB
      if (klow[k]) { re[k] += ar; im[k] += ai; if (k != half) { re[N - k] += ar; im[N - k] -= ai; } }
      else { re[k] -= ai; im[k] += ar; if (k != half) { re[N - k] += ai; im[N - k] += ar; } }
    }
    fft(re, im, true);
    int a = j * hs - half;
    for (int i = 0; i < N; i++) {
      int k = a + i; if (k < 0 || k >= (int)lo.size()) continue;
      lo[k] += re[i] / N * ws[i]; hi[k] += im[i] / N * ws[i];
    }
  }
  // the partials
  for (auto& tr : p.tracks) {
    int L = (int)tr.f.size(); if (L < 1) continue;
    double f0pos = (double)tr.s * p.hop, fLpos = (double)(tr.s + L - 1) * p.hop;
    int a = std::max(0, (int)(f0pos - p.hop)), b = std::min((int)n, (int)(fLpos + p.hop));
    if (b <= a) continue;
    std::vector<float> fs(tr.f); std::sort(fs.begin(), fs.end());
    std::vector<float>& dst = fs[L / 2] < xo ? lo : hi;
    double ph = r() * 6.283185307179586;
    for (int i = a; i < b; i++) {
      double x = (i - f0pos) / p.hop, f, amp;
      if (x <= 0) f = tr.f[0]; else if (x >= L - 1) f = tr.f[L - 1]; else { int q = (int)x; double u = x - q; f = tr.f[q] * (1 - u) + tr.f[q + 1] * u; }
      double xa = x + 1;   // amplitude knots: 0 one hop before, the frames, 0 one hop after
      if (xa <= 0 || xa >= L + 1) amp = 0;
      else { int q = (int)xa; double u = xa - q; double A0 = q == 0 ? 0 : tr.a[q - 1], A1 = q >= L ? 0 : tr.a[q]; amp = A0 * (1 - u) + A1 * u; }
      ph += 6.283185307179586 * f / SR;
      if (f < SR * 0.475) dst[i] += (float)(amp * std::sin(ph));
    }
  }
  lo.resize(n); hi.resize(n);
  matchFine(lo, n, p.fineLo, fine); matchFine(hi, n, p.fineHi, fine);
  // random phases can line up into peaks the recording never had: soften anything over 0.9
  for (u32 i = 0; i < n; i++) { float x = lo[i] + hi[i], a = std::abs(x); lo[i] = a < 0.9f ? x : std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f), x); }
  return lo;
}

} // namespace dq::snd
