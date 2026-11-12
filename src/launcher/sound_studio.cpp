// launcher/sound_studio.cpp — the Sound Studio (Mod Studio → Sounds): every sound the game makes, to hear and see; a
// sandbox to shape one (pitch, gain, trim, fades, a low-pass) or a recording brought in (WAV, MP3, FLAC); normalise it
// and save it as the game's own: userDir()/sounds/<sound name>.wav replaces that sound (audio/sfxlib.cpp).
// Controller: the list and every control are ordinary menu items (d-pad, A, the sliders take left/right).
#include "launcher/launcher.h"
#include "audio/audio.h"
#include "ui/file_pick.h"
#include "common/fs.h"
#include <imgui_stdlib.h>
#include <miniaudio.h>
#include <algorithm>
#include <cmath>

namespace dq {

namespace {
struct SS {
  std::unique_ptr<AudioEngine> eng;
  std::vector<std::string> names; std::string filter, sel, status, path;
  std::vector<float> src, out;   // the sound as made (or brought in), and shaped
  float pitch = 1, gainDb = 0, trimA = 0, trimB = 1, fadeIn = 0, fadeOut = 0, lowpass = 0;   // lowpass 0 = off, else Hz
  bool dirty = true; int take = 0;
  FilePicker pick;
};
SS& S() { static SS s; return s; }

AudioEngine* engine(Launcher& L) {
  SS& s = S();
  if (!s.eng) {
    s.eng = std::make_unique<AudioEngine>();
    if (!s.eng->init(L.settings)) s.status = "No sound device: sounds can still be shaped and saved";
    s.eng->setCategoryVolume("sfx", 1); s.eng->setCategoryVolume("mobs", 1);
  }
  return s.eng.get();
}

void shape() {
  SS& s = S();
  s.dirty = false;
  const size_t n = s.src.size();
  if (!n) { s.out.clear(); return; }
  const size_t a = (size_t)(std::clamp(s.trimA, 0.f, 1.f) * n), b = std::max(a + 1, (size_t)(std::clamp(s.trimB, 0.f, 1.f) * n));
  s.out.assign(s.src.begin() + a, s.src.begin() + std::min(b, n));
  const float g = std::pow(10.f, s.gainDb / 20.f);
  const size_t fi = (size_t)(s.fadeIn * 48000), fo = (size_t)(s.fadeOut * 48000), m = s.out.size();
  float lp = 0; const float k = s.lowpass > 0 ? 1 - std::exp(-2 * 3.14159265f * s.lowpass / 48000.f) : 1;
  for (size_t i = 0; i < m; i++) {
    float v = s.out[i] * g;
    if (fi && i < fi) v *= (float)i / fi;
    if (fo && i + fo > m) v *= (float)(m - i) / fo;
    lp += (v - lp) * k; s.out[i] = lp;
  }
}
void select(Launcher& L, const std::string& name) {
  SS& s = S(); AudioEngine* e = engine(L);
  s.sel = name; s.take = 0;
  s.src = e ? e->renderSound(name, 0, 1) : std::vector<float>();
  s.pitch = 1; s.gainDb = 0; s.trimA = 0; s.trimB = 1; s.fadeIn = s.fadeOut = 0; s.lowpass = 0; s.dirty = true;
  s.status = s.src.empty() ? "That one is made live and can't be shown here" : "";
}
bool load(const std::string& path) {
  SS& s = S();
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 48000);
  ma_uint64 n = 0; void* pcm = nullptr;
  if (ma_decode_file(path.c_str(), &cfg, &n, &pcm) != MA_SUCCESS || !pcm) { s.status = "Couldn't read that file (WAV, MP3 or FLAC)"; return false; }
  s.src.assign((float*)pcm, (float*)pcm + std::min<ma_uint64>(n, 48000 * 60));
  ma_free(pcm, nullptr);
  s.dirty = true; s.status = "Brought in " + fs::path(path).filename().string();
  return true;
}
bool saveWav(const fs::path& p, const std::vector<float>& d, float pitch) {
  // the pitch is kept by resampling (a faster sound is a shorter one)
  std::vector<float> r;
  if (std::abs(pitch - 1) > 0.001f) { for (double t = 0; t < d.size() - 1; t += pitch) { size_t i = (size_t)t; float f = (float)(t - i); r.push_back(d[i] * (1 - f) + d[i + 1] * f); } }
  const std::vector<float>& x = r.empty() ? d : r;
  std::string o = "RIFF";
  auto u32le = [&](u32 v) { for (int i = 0; i < 4; i++) o.push_back((char)((v >> (8 * i)) & 255)); };
  auto u16le = [&](u16 v) { o.push_back((char)(v & 255)); o.push_back((char)(v >> 8)); };
  u32le(36 + (u32)x.size() * 2); o += "WAVEfmt "; u32le(16); u16le(1); u16le(1); u32le(48000); u32le(96000); u16le(2); u16le(16);
  o += "data"; u32le((u32)x.size() * 2);
  for (float v : x) u16le((u16)(i16)std::clamp((int)std::lround(v * 32767), -32767, 32767));
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  return writeFileAtomic(p, o);
}
} // namespace

void drawSoundStudio(Launcher& L) {
  SS& s = S();
  AudioEngine* e = engine(L);
  if (s.names.empty() && e) { s.names = e->soundNames(); std::sort(s.names.begin(), s.names.end()); }
  // the list
  ImGui::BeginGroup();
  ImGui::SetNextItemWidth(240); ImGui::InputTextWithHint("##filter", "Find a sound (step, wolf, chest...)", &s.filter);
  ImGui::BeginChild("##sounds", ImVec2(240, std::max(200.f, ImGui::GetContentRegionAvail().y - 8)), ImGuiChildFlags_Borders, ImGuiWindowFlags_NavFlattened);
  std::string f = s.filter; for (auto& c : f) c = (char)tolower((unsigned char)c);
  int shown = 0;
  for (auto& n : s.names) {
    if (!f.empty() && n.find(f) == std::string::npos) continue;
    if (++shown > 600) { ImGui::TextDisabled("(type to find more)"); break; }
    if (ImGui::Selectable(n.c_str(), n == s.sel)) { select(L, n); if (e) e->play(n); }
  }
  ImGui::EndChild();
  ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::PushFont(L.fMid); ImGui::TextUnformatted(s.sel.empty() ? "Pick a sound, or bring one in" : s.sel.c_str()); ImGui::PopFont();
  if (s.dirty) shape();
  // its picture
  {
    const ImVec2 p = ImGui::GetCursorScreenPos(), sz(ImGui::GetContentRegionAvail().x, 90);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(20, 16, 14, 230), 3);
    dl->AddLine(ImVec2(p.x, p.y + sz.y / 2), ImVec2(p.x + sz.x, p.y + sz.y / 2), IM_COL32(255, 255, 255, 30));
    if (!s.out.empty()) {
      const int W = (int)sz.x; const size_t n = s.out.size();
      for (int x = 0; x < W; x++) {
        const size_t a = n * x / W, b = std::max(a + 1, n * (x + 1) / W);
        float lo = 0, hi = 0; for (size_t i = a; i < b && i < n; i++) { lo = std::min(lo, s.out[i]); hi = std::max(hi, s.out[i]); }
        dl->AddLine(ImVec2(p.x + x, p.y + sz.y / 2 - hi * sz.y * 0.48f), ImVec2(p.x + x, p.y + sz.y / 2 - lo * sz.y * 0.48f + 1), IM_COL32(255, 160, 70, 230));
      }
      char buf[64]; std::snprintf(buf, sizeof buf, "%.2f s", n / 48000.0 / s.pitch);
      dl->AddText(ImVec2(p.x + 6, p.y + 4), IM_COL32(255, 230, 200, 180), buf);
    }
    ImGui::Dummy(sz);
  }
  // the sandbox
  const float cw = 260;
  auto sl = [&](const char* id, float* v, float a, float b, const char* fmt) { ImGui::SetNextItemWidth(cw); if (ImGui::SliderFloat(id, v, a, b, fmt)) s.dirty = true; };
  sl("##pitch", &s.pitch, 0.5f, 2.f, "pitch %.2fx"); ImGui::SameLine(); sl("##gain", &s.gainDb, -24, 12, "gain %+.1f dB");
  sl("##ta", &s.trimA, 0, 1, "start %.2f"); ImGui::SameLine(); sl("##tb", &s.trimB, 0, 1, "end %.2f");
  sl("##fi", &s.fadeIn, 0, 1, "fade in %.2f s"); ImGui::SameLine(); sl("##fo", &s.fadeOut, 0, 1, "fade out %.2f s");
  sl("##lp", &s.lowpass, 0, 12000, s.lowpass > 0 ? "low-pass %.0f Hz" : "low-pass off");
  ImGui::SameLine();
  if (mcButton(L, "Play", ImVec2(90, 0), BtnKind::Go, !s.out.empty()) && e) e->playSamples(s.out, 1, s.pitch);
  ImGui::SameLine();
  if (mcButton(L, "Another take", ImVec2(0, 0), BtnKind::Grey, !s.sel.empty()) && e) { s.src = e->renderSound(s.sel, ++s.take, (u32)s.take * 7919u + 1); s.dirty = true; e->playSamples(s.src); }
  ImGui::SameLine();
  if (mcButton(L, "Normalise", ImVec2(0, 0), BtnKind::Grey, !s.out.empty())) {
    float pk = 0; for (float v : s.out) pk = std::max(pk, std::abs(v));
    if (pk > 1e-5f) { s.gainDb += 20 * std::log10(0.891f / pk); s.dirty = true; s.status = "Normalised to -1 dB"; }
  }
  // bringing one in, and saving it as the game's
  ImGui::SetNextItemWidth(340); ImGui::InputTextWithHint("##path", "A recording (WAV, MP3 or FLAC)", &s.path);
  ImGui::SameLine();
  if (mcButton(L, "Browse\xE2\x80\xA6", ImVec2(0, 0), BtnKind::Grey, !s.pick.busy())) s.pick.open("A recording", "wav");
  std::string picked; if (s.pick.take(picked) && !picked.empty()) s.path = picked;
  ImGui::SameLine();
  if (mcButton(L, "Bring it in", ImVec2(0, 0), BtnKind::Grey, !s.path.empty())) load(s.path);
  const bool can = !s.sel.empty() && !s.out.empty();
  if (mcButton(L, can ? ("Use as the game's \"" + s.sel + "\"").c_str() : "Use as the game's sound", ImVec2(0, 0), BtnKind::Go, can)) {
    const fs::path p = userDir() / "sounds" / (s.sel + ".wav");
    s.status = saveWav(p, s.out, s.pitch) ? "Saved " + p.string() + ": the game uses it from its next start" : "Couldn't write " + p.string();
  }
  { const fs::path p = userDir() / "sounds" / (s.sel + ".wav"); std::error_code ec;
    ImGui::SameLine();
    if (!s.sel.empty() && fs::exists(p, ec) && mcButton(L, "Put the original back", ImVec2(0, 0))) { fs::remove(p, ec); s.status = "Removed " + p.string(); } }
  ImGui::PushFont(L.fSmall);
  dimText("%s", s.status.empty() ? "Your sounds go in the sounds folder; a .wav named after a sound replaces it." : s.status.c_str());
  ImGui::PopFont();
  ImGui::EndGroup();
}

} // namespace dq
