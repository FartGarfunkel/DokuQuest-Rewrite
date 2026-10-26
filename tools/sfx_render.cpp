// tools/sfx_render.cpp — dq-sfx: render the game's sounds offline (no sound card needed), to check them
// numerically against the originals (tools/port/sound_analyze.py metric) or to listen elsewhere.
//   dq-sfx list                                   every sound name the original plays
//   dq-sfx render <name> <out.wav|.f32> [take] [seed]   one take ("stone.step" = a material, "pack:step/stone1" = a profile)
//   dq-sfx packs <outdir>                         every pack profile resynthesised (.f32)
//   dq-sfx music <mood> <out.wav> [seconds] [seed]   a piece through the live band and the music bus
//   dq-sfx mix <out.wav> <seconds> name@time[@x,y,z] ...   the whole engine: queue, voices, panning, reverbs
//   dq-sfx bench                                  render every name once: time, peak, NaN check
#include "audio/audio.h"
#include "audio/engine.h"
#include "audio/music.h"
#include "core/init.h"
#include "common/fs.h"
#include <chrono>
#include <cstdio>

using namespace dq;

static void writeOut(const std::string& path, const std::vector<float>& d, int ch) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
  if (path.size() > 4 && path.substr(path.size() - 4) == ".f32") { fwrite(d.data(), 4, d.size(), f); fclose(f); return; }
  u32 n = (u32)d.size(), sr = snd::SR, br = sr * ch * 2; u16 ba = (u16)(ch * 2), bits = 16, fmt = 1, chs = (u16)ch;
  u32 sz = 36 + n * 2, fs16 = 16, dsz = n * 2;
  fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fs16, 4, 1, f);
  fwrite(&fmt, 2, 1, f); fwrite(&chs, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&dsz, 4, 1, f);
  for (float x : d) { i16 v = (i16)std::lround(std::clamp(x, -1.f, 1.f) * 32767); fwrite(&v, 2, 1, f); }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: dq-sfx list | render <name> <out> [take] [seed] | packs <dir> | music <mood> <out> [s] [seed] | mix <out> <s> name@t[@x,y,z]... | bench\n"); return 1; }
  std::string cmd = argv[1];
  Settings st; st.volume = 1; st.music = 1;
  AudioEngine eng;
  eng.initOffline(st);
  eng.preloadWait(20);
  if (cmd == "list") { for (auto& n : eng.soundNames()) printf("%s\n", n.c_str()); return 0; }
  if (cmd == "render" && argc >= 4) {
    auto d = eng.renderSound(argv[2], argc > 4 ? atoi(argv[4]) : 0, argc > 5 ? (u32)atoi(argv[5]) : 0);
    if (d.empty()) { fprintf(stderr, "no such sound: %s\n", argv[2]); return 2; }
    writeOut(argv[3], d, 1); printf("%s: %.3f s\n", argv[2], d.size() / (double)snd::SR); return 0;
  }
  if (cmd == "packs" && argc >= 3) {
    fs::path out = argv[2]; fs::create_directories(out);
    auto& m = *eng.impl;
    auto j = readJson(dataDir() / "sounds.json");
    auto t0 = std::chrono::steady_clock::now(); double secs = 0; int n = 0;
    for (auto& [name, ref] : (*j)["sounds"].items()) {
      const snd::Profile* p = m.lib.packs().profile(name);
      if (!p) continue;
      auto d = m.lib.packs().render(*p, 7);
      secs += d.size() / (double)snd::SR; n++;
      std::string fn = name; for (auto& c : fn) if (c == '/') c = '_';
      writeOut((out / (fn + ".f32")).string(), d, 1);
    }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%d profiles, %.1f s of sound rendered in %.2f s (%.0fx real time)\n", n, secs, el, secs / el);
    return 0;
  }
  if (cmd == "music" && argc >= 4) {
    double secs = argc > 4 ? atof(argv[4]) : 30;
    auto notes = snd::composeMusic(argv[2], argc > 5 ? (u32)atoi(argv[5]) : 1234);
    snd::MusicSynth syn;
    for (auto& n : notes) { AudioCmd c; c.t = AudioCmd::Note; c.at = n.t + 0.3; c.ins = n.ins; c.a = n.m; c.b = n.d; c.c = n.v; syn.command(c); }
    int N = (int)(secs * snd::SR); std::vector<float> out((size_t)N * 2, 0.f);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i += 512) { int k = std::min(512, N - i); syn.render(out.data() + 2 * i, k, (double)i / snd::SR, 1); }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    writeOut(argv[3], out, 2);
    printf("%zu notes, piece %.1f s; rendered %.1f s in %.2f s (%.0fx real time)\n", notes.size(), notes.empty() ? 0 : notes.back().t, secs, el, secs / el);
    return 0;
  }
  if (cmd == "mix" && argc >= 4) {
    double secs = atof(argv[3]);
    struct Ev { std::string name; double t; bool at; vec3 p; };
    std::vector<Ev> evs;
    for (int i = 4; i < argc; i++) {
      std::string a = argv[i]; auto p1 = a.find('@'), p2 = a.find('@', p1 + 1);
      Ev e{a.substr(0, p1), atof(a.c_str() + p1 + 1), p2 != std::string::npos, vec3(0)};
      if (e.at) sscanf(a.c_str() + p2 + 1, "%f,%f,%f", &e.p.x, &e.p.y, &e.p.z);
      evs.push_back(e);
    }
    eng.setListener(vec3(0), vec3(0, 0, -1), vec3(0, 1, 0));
    int N = (int)(secs * snd::SR), blk = 480; std::vector<float> out((size_t)N * 2, 0.f);
    for (int i = 0; i < N; i += blk) {
      double t = (double)i / snd::SR;
      for (auto& e : evs) if (e.t >= t && e.t < t + (double)blk / snd::SR) {
        if (e.name.rfind("music:", 0) == 0) continue;
        if (e.name.find('.') != std::string::npos && e.name.rfind("fx.", 0) != 0 && e.name.rfind("babble:", 0) != 0) { auto d = e.name.find('.'); if (e.at) eng.materialAt(e.name.substr(0, d), e.name.substr(d + 1), e.p); else eng.material(e.name.substr(0, d), e.name.substr(d + 1)); }
        else if (e.at) eng.playAt(e.name, e.p); else eng.play(e.name);
      }
      eng.update((double)blk / snd::SR);
      eng.preloadWait(2);       // offline: let the worker finish what was just asked for
      eng.update(0);
      eng.mixOffline(out.data() + 2 * i, std::min(blk, N - i));
    }
    writeOut(argv[2], out, 2);
    float pk = 0; for (float x : out) pk = std::max(pk, std::abs(x));
    printf("mixed %.1f s, peak %.3f\n", secs, pk);
    return 0;
  }
  if (cmd == "bench") {
    auto t0 = std::chrono::steady_clock::now(); double secs = 0; int n = 0, bad = 0;
    for (auto& name : eng.soundNames()) {
      auto d = eng.renderSound(name, 0, 1);
      float pk = 0; bool nan = false;
      for (float x : d) { if (!std::isfinite(x)) nan = true; pk = std::max(pk, std::abs(x)); }
      if (nan || (pk > 1.5f) || (d.size() > 1 && pk == 0)) { printf("  %-24s peak %.3f%s\n", name.c_str(), pk, nan ? " NaN" : ""); bad++; }
      secs += d.size() / (double)snd::SR; n++;
    }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%d names, %.1f s of sound in %.2f s (%.0fx real time), %d odd\n", n, secs, el, secs / el, bad);
    // every mood's pieces, by day and by night
    int pieces = 0; size_t notes = 0;
    for (const char* m : {"day", "night", "cave", "sky", "depths", "menu", "creative", "plains", "forest", "jungle", "desert", "savanna", "frost", "peaks", "swamp", "ocean", "village", "deep", "sky2", "depths2", "cave2"})
      for (u32 sd = 1; sd <= 20; sd++) { notes += snd::composeMusic(m, sd).size() + snd::composeMusic(std::string(m) + ":night", sd).size(); pieces += 2; }
    printf("%d pieces composed, %zu notes\n", pieces, notes);
    return 0;
  }
  fprintf(stderr, "unknown command\n");
  return 1;
}
