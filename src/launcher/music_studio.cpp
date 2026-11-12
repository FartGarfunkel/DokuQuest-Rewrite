// launcher/music_studio.cpp — the Music Studio (Mod Studio → Music): songs played by the game's own instruments on a
// piano roll. Write a piece to one of the game's moods, read a recording for its tempo, key and chords and write an
// original piece from them, bring MIDI in and send it out, and give a song a realm to have it play there now and then.
// Mouse: click an empty spot for a note, click a note to pick it, drag to move it, right-click or Delete to remove.
// Controller: A on the roll starts editing (d-pad moves the cursor, A sets or clears a note, LB/RB instrument, Y note
// length, X play/stop), B stops editing.
#include "launcher/launcher.h"
#include "audio/song.h"
#include "audio/audio.h"
#include "ui/file_pick.h"
#include "common/fs.h"
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>

namespace dq {
using namespace snd;

namespace {
ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 operator-(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
struct MS {
  bool init = false;
  Song song; std::vector<Song> list; int sel = -1;
  std::unique_ptr<AudioEngine> eng;
  bool playing = false; double t0 = 0; size_t next = 0; float fromBeat = 0;
  int ins = MI_PIANO, mood = 7, style = 0, realm = 0; u32 seed = 1;
  float grid = 0.5f, pxBeat = 40, rowH = 10;
  int selNote = -1; bool dragging = false; float dragDB = 0, dragDM = 0;
  bool padEdit = false; float curBeat = 0; int curMidi = 60;
  std::string status, recPath; SongAnalysis an;
  FilePicker pickMidi, pickRec;
};
MS& M() { static MS m; return m; }

const char* REALMS[] = {"No realm", "Overworld", "Nether", "End", "Skylands"};
const char* REALM_IDS[] = {"", "overworld", "nether", "end", "skylands"};
const char* STYLES[] = {"calm", "folk", "epic", "dreamy"};
const float GRIDS[] = {0.25f, 0.5f, 1, 2};

ImU32 insColour(int i) {
  const float h = std::fmod(i * 0.137f + 0.05f, 1.f);
  float r, g, b; ImGui::ColorConvertHSVtoRGB(h, 0.55f, 0.95f, r, g, b);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 235);
}
bool blackKey(int m) { const int k = ((m % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

AudioEngine* engine(Launcher& L) {
  MS& m = M();
  if (!m.eng) {
    m.eng = std::make_unique<AudioEngine>();
    if (!m.eng->init(L.settings)) { m.status = "No sound device: songs can still be written and saved"; }
    m.eng->setCategoryVolume("music", 1);
  }
  return m.eng->ok() ? m.eng.get() : nullptr;
}
void stop() { MS& m = M(); m.playing = false; if (m.eng && m.eng->ok()) m.eng->musicClear(); }
void play(Launcher& L, float fromBeat) {
  MS& m = M(); AudioEngine* e = engine(L);
  if (!e) return;
  stop();
  std::sort(m.song.notes.begin(), m.song.notes.end(), [](const SongNote& a, const SongNote& b) { return a.beat < b.beat; });
  m.selNote = -1;
  m.fromBeat = fromBeat; m.t0 = e->audioClock() + 0.15 - fromBeat * 60.0 / m.song.bpm; m.next = 0;
  while (m.next < m.song.notes.size() && m.song.notes[m.next].beat < fromBeat) m.next++;
  m.playing = true;
}
void pump() {
  MS& m = M();
  if (!m.playing || !m.eng || !m.eng->ok()) return;
  const double now = m.eng->audioClock(), spb = 60.0 / m.song.bpm;
  while (m.next < m.song.notes.size()) {
    const SongNote& n = m.song.notes[m.next];
    const double at = m.t0 + n.beat * spb;
    if (at > now + 1.5) break;
    m.eng->musicNote(n.ins, n.midi, (float)(n.len * spb), n.vel, std::max(now + 0.01, at));
    m.next++;
  }
  if (m.next >= m.song.notes.size() && now > m.t0 + (m.song.lengthBeats() + 1) * spb) m.playing = false;
}
float playBeat() { MS& m = M(); return m.playing && m.eng ? (float)((m.eng->audioClock() - m.t0) * m.song.bpm / 60.0) : -1; }
void reloadList() { MS& m = M(); m.list = loadUserSongs(); }
int realmIndex(const std::string& r) { for (int i = 0; i < 5; i++) if (r == REALM_IDS[i]) return i; return 0; }
void setSong(Song s) { MS& m = M(); stop(); m.song = std::move(s); m.realm = realmIndex(m.song.realm); m.selNote = -1; }

int noteAt(float beat, int midi) {
  MS& m = M();
  for (int i = (int)m.song.notes.size() - 1; i >= 0; i--) {
    const SongNote& n = m.song.notes[i];
    if ((int)std::lround(n.midi) == midi && beat >= n.beat && beat < n.beat + n.len) return i;
  }
  return -1;
}
void toggleNote(float beat, int midi) {
  MS& m = M();
  const int at = noteAt(beat + 0.001f, midi);
  if (at >= 0) { m.song.notes.erase(m.song.notes.begin() + at); m.selNote = -1; return; }
  m.song.notes.push_back({beat, M().grid, (u8)m.ins, (float)midi, 0.8f});
  m.selNote = (int)m.song.notes.size() - 1;
  if (m.eng && m.eng->ok()) m.eng->musicNote(m.ins, (float)midi, 0.4f, 0.8f, m.eng->audioClock() + 0.02);   // hear it
}

void toolbar(Launcher& L) {
  MS& m = M();
  // the song
  ImGui::SetNextItemWidth(220); ImGui::InputText("##name", &m.song.name);
  ImGui::SameLine(); ImGui::SetNextItemWidth(170); ImGui::SliderFloat("##bpm", &m.song.bpm, 40, 220, "%.0f beats a minute");
  ImGui::SameLine(); ImGui::SetNextItemWidth(130);
  if (ImGui::Combo("##realm", &m.realm, REALMS, 5)) m.song.realm = REALM_IDS[m.realm];
  ImGui::SameLine();
  if (mcButton(L, m.playing ? "Stop" : "Play", ImVec2(80, 0), BtnKind::Go)) { if (m.playing) stop(); else play(L, 0); }
  ImGui::SameLine();
  if (mcButton(L, "Save", ImVec2(80, 0))) { std::string err; if (saveUserSong(m.song, &err)) { m.status = "Saved to " + (userDir() / "music").string(); reloadList(); } else m.status = err; }
  ImGui::SameLine();
  if (mcButton(L, "New", ImVec2(70, 0))) { setSong(Song()); m.status.clear(); }
  // writing pieces
  const auto& moods = songMoods();
  std::vector<const char*> mn; for (auto& s : moods) mn.push_back(s.c_str());
  ImGui::SetNextItemWidth(140); ImGui::Combo("##mood", &m.mood, mn.data(), (int)mn.size());
  ImGui::SameLine();
  if (mcButton(L, "Write a piece to this mood", ImVec2(0, 0))) { m.seed = (u32)(ImGui::GetTime() * 1000) ^ (m.seed * 2654435761u); setSong(songFromMood(moods[m.mood], m.seed)); m.status = "A new " + moods[m.mood] + " piece"; }
  ImGui::SameLine();
  if (mcButton(L, "Import MIDI\xE2\x80\xA6", ImVec2(0, 0), BtnKind::Grey, !m.pickMidi.busy())) m.pickMidi.open("Import a MIDI file", "mid");
  ImGui::SameLine();
  if (mcButton(L, "Export MIDI", ImVec2(0, 0), BtnKind::Grey, !m.song.notes.empty())) {
    std::error_code ec; fs::create_directories(userDir() / "music", ec);
    fs::path p = userDir() / "music" / (m.song.name + ".mid");
    auto b = songToMidi(m.song);
    m.status = writeFileAtomic(p, b.data(), b.size()) ? "Exported " + p.string() : "Couldn't write " + p.string();
  }
  std::string picked;
  if (m.pickMidi.take(picked) && !picked.empty()) {
    auto txt = readFile(picked);
    Song s; std::string err;
    if (txt && songFromMidi(std::vector<u8>(txt->begin(), txt->end()), s, err)) { if (s.name == "Untitled") s.name = fs::path(picked).stem().string(); setSong(s); m.status = "Imported " + std::to_string(s.notes.size()) + " notes"; }
    else m.status = err.empty() ? "Couldn't read that file" : err;
  }
  // reading a recording
  ImGui::SetNextItemWidth(320); ImGui::InputTextWithHint("##rec", "A recording to read (WAV, MP3 or FLAC)", &m.recPath);
  ImGui::SameLine();
  if (mcButton(L, "Browse\xE2\x80\xA6", ImVec2(0, 0), BtnKind::Grey, !m.pickRec.busy())) m.pickRec.open("A recording to read", "mp3");
  if (m.pickRec.take(picked) && !picked.empty()) m.recPath = picked;
  ImGui::SameLine();
  if (mcButton(L, "Read it", ImVec2(0, 0), BtnKind::Grey, !m.recPath.empty())) {
    m.an = analyseRecording(m.recPath);
    m.status = m.an.ok ? "Read: " + std::to_string((int)m.an.bpm) + " beats a minute, " + keyName(m.an.key, m.an.minor) : m.an.err;
  }
  if (m.an.ok) {
    std::string ch; for (size_t i = 0; i < m.an.chords.size() && i < 16; i++) ch += (i ? " " : "") + m.an.chords[i];
    ImGui::PushFont(L.fSmall); dimText("%d beats a minute \xC2\xB7 %s \xC2\xB7 chords: %s", (int)m.an.bpm, keyName(m.an.key, m.an.minor).c_str(), ch.c_str()); ImGui::PopFont();
    ImGui::SetNextItemWidth(110); ImGui::Combo("##style", &m.style, STYLES, 4);
    ImGui::SameLine();
    if (mcButton(L, "Write an original piece from it", ImVec2(0, 0), BtnKind::Go)) { m.seed = m.seed * 1103515245u + 12345u; setSong(songFromAnalysis(m.an, STYLES[m.style], m.seed)); m.status = "Our own piece in its key, tempo and chords"; }
  }
  // the pen
  std::vector<const char*> insN; for (int i = 0; i < MI_COUNT; i++) insN.push_back(insName(i));
  ImGui::SetNextItemWidth(130); ImGui::Combo("##ins", &m.ins, insN.data(), MI_COUNT);
  ImGui::SameLine(); ImGui::SetNextItemWidth(110);
  int gi = 0; for (int i = 0; i < 4; i++) if (m.grid == GRIDS[i]) gi = i;
  const char* gn[] = {"1/4 beat", "1/2 beat", "1 beat", "2 beats"};
  if (ImGui::Combo("##grid", &gi, gn, 4)) m.grid = GRIDS[gi];
  ImGui::SameLine(); ImGui::SetNextItemWidth(140); ImGui::SliderFloat("##zoom", &m.pxBeat, 12, 120, "zoom %.0f");
  ImGui::SameLine();
  ImGui::PushFont(L.fSmall);
  dimText("%d notes \xC2\xB7 %.0f beats%s", (int)m.song.notes.size(), m.song.lengthBeats(), m.status.empty() ? "" : (" \xC2\xB7 " + m.status).c_str());
  ImGui::PopFont();
}

void roll(Launcher& L, ImVec2 size) {
  MS& m = M();
  const int LO = 24, HI = 96, ROWS = HI - LO + 1;
  const float lenB = std::max(32.f, m.song.lengthBeats() + 16), W = lenB * m.pxBeat, H = ROWS * m.rowH;
  ImGui::BeginChild("##roll", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | (m.padEdit ? ImGuiWindowFlags_NoNav : 0));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##grid", ImVec2(W, H), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hov = ImGui::IsItemHovered(), focused = ImGui::IsItemFocused();
  const ImVec2 cmin = ImGui::GetWindowPos(), cmax = cmin + ImGui::GetWindowSize();
  // rows and the beat grid
  for (int r = 0; r < ROWS; r++) {
    const int midi = HI - r; const float y = o.y + r * m.rowH;
    if (y + m.rowH < cmin.y || y > cmax.y) continue;
    dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + W, y + m.rowH), blackKey(midi) ? IM_COL32(26, 22, 20, 255) : IM_COL32(40, 34, 30, 255));
    if (midi % 12 == 0) dl->AddLine(ImVec2(o.x, y + m.rowH), ImVec2(o.x + W, y + m.rowH), IM_COL32(255, 255, 255, 40));
  }
  for (float b = 0; b <= lenB; b += m.grid) {
    const float x = o.x + b * m.pxBeat;
    if (x < cmin.x - 2 || x > cmax.x + 2) continue;
    const bool bar = std::fmod(b, 4.f) == 0, beat = std::fmod(b, 1.f) == 0;
    dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H), bar ? IM_COL32(255, 200, 140, 70) : beat ? IM_COL32(255, 255, 255, 28) : IM_COL32(255, 255, 255, 10));
  }
  // the notes
  for (int i = 0; i < (int)m.song.notes.size(); i++) {
    const SongNote& n = m.song.notes[i];
    const int r = HI - (int)std::lround(n.midi);
    if (r < 0 || r >= ROWS) continue;
    const ImVec2 a(o.x + n.beat * m.pxBeat, o.y + r * m.rowH), b(o.x + (n.beat + n.len) * m.pxBeat - 1, o.y + (r + 1) * m.rowH - 1);
    if (b.x < cmin.x || a.x > cmax.x || b.y < cmin.y || a.y > cmax.y) continue;
    dl->AddRectFilled(a, b, insColour(n.ins), 2);
    if (i == m.selNote) dl->AddRect(a - ImVec2(1, 1), b + ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 2, 0, 2);
  }
  // names of the Cs at the left edge
  for (int midi = LO; midi <= HI; midi += 12) {
    const float y = o.y + (HI - midi) * m.rowH;
    dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(cmin.x + 4, y - 2), IM_COL32(255, 230, 200, 160), ("C" + std::to_string(midi / 12 - 1)).c_str());
  }
  // the playhead
  const float pb = playBeat();
  if (pb >= 0) { const float x = o.x + pb * m.pxBeat; dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H), IM_COL32(255, 160, 60, 255), 2); if (x > cmax.x - 40 || x < cmin.x) ImGui::SetScrollX(std::max(0.f, pb * m.pxBeat - 80)); }
  // the mouse
  const ImVec2 mp = ImGui::GetIO().MousePos;
  const float mBeat = (mp.x - o.x) / m.pxBeat; const int mMidi = HI - (int)std::floor((mp.y - o.y) / m.rowH);
  if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const int at = noteAt(mBeat, mMidi);
    if (at >= 0) { m.selNote = at; m.dragging = true; m.dragDB = mBeat - m.song.notes[at].beat; m.dragDM = 0; }
    else if (mMidi >= LO && mMidi <= HI) { toggleNote(std::floor(mBeat / m.grid) * m.grid, mMidi); m.dragging = false; }
  }
  if (m.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m.selNote >= 0 && m.selNote < (int)m.song.notes.size()) {
    SongNote& n = m.song.notes[m.selNote];
    n.beat = std::max(0.f, std::round((mBeat - m.dragDB) / m.grid) * m.grid);
    n.midi = (float)std::clamp(mMidi, LO, HI);
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m.dragging = false;
  if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { const int at = noteAt(mBeat, mMidi); if (at >= 0) { m.song.notes.erase(m.song.notes.begin() + at); m.selNote = -1; } }
  if ((hov || focused) && m.selNote >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) { m.song.notes.erase(m.song.notes.begin() + m.selNote); m.selNote = -1; }
  if (hov && ImGui::IsKeyPressed(ImGuiKey_Space)) { if (m.playing) stop(); else play(L, 0); }
  // the controller: A on the roll starts editing, B stops
  if (!m.padEdit && focused && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) { m.padEdit = true; ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad; }
  else if (m.padEdit) {
    auto pr = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (pr(ImGuiKey_GamepadDpadLeft)) m.curBeat = std::max(0.f, m.curBeat - m.grid);
    if (pr(ImGuiKey_GamepadDpadRight)) m.curBeat += m.grid;
    if (pr(ImGuiKey_GamepadDpadUp)) m.curMidi = std::min(HI, m.curMidi + 1);
    if (pr(ImGuiKey_GamepadDpadDown)) m.curMidi = std::max(LO, m.curMidi - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) toggleNote(m.curBeat, m.curMidi);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) m.ins = (m.ins + MI_COUNT - 1) % MI_COUNT;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) m.ins = (m.ins + 1) % MI_COUNT;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp)) { int gi = 0; for (int i = 0; i < 4; i++) if (m.grid == GRIDS[i]) gi = i; m.grid = GRIDS[(gi + 1) % 4]; }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft)) { if (m.playing) stop(); else play(L, m.curBeat); }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) { m.padEdit = false; ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; }
    const ImVec2 a(o.x + m.curBeat * m.pxBeat, o.y + (HI - m.curMidi) * m.rowH);
    dl->AddRect(a, a + ImVec2(m.grid * m.pxBeat, m.rowH), IM_COL32(120, 255, 160, 255), 0, 0, 2);
    // keep the cursor in view
    const float cx = m.curBeat * m.pxBeat, cy = (HI - m.curMidi) * m.rowH;
    if (cx < ImGui::GetScrollX() + 20) ImGui::SetScrollX(std::max(0.f, cx - 20));
    if (cx > ImGui::GetScrollX() + size.x - 60) ImGui::SetScrollX(cx - size.x + 60);
    if (cy < ImGui::GetScrollY() + 10) ImGui::SetScrollY(std::max(0.f, cy - 10));
    if (cy > ImGui::GetScrollY() + size.y - 30) ImGui::SetScrollY(cy - size.y + 30);
  }
  if (!M().init) { ImGui::SetScrollY((HI - 72) * m.rowH); M().init = true; }
  ImGui::EndChild();
}
} // namespace

void drawMusicStudio(Launcher& L) {
  MS& m = M();
  static bool listed = false;
  if (!listed) { reloadList(); listed = true; }
  pump();
  const float listW = 190;
  ImGui::BeginGroup();
  ImGui::PushFont(L.fMid); ImGui::TextUnformatted("Your songs"); ImGui::PopFont();
  ImGui::BeginChild("##songs", ImVec2(listW, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders, ImGuiWindowFlags_NavFlattened);
  for (int i = 0; i < (int)m.list.size(); i++) {
    std::string lab = m.list[i].name + (m.list[i].realm.empty() ? "" : "  (" + m.list[i].realm + ")");
    if (ImGui::Selectable((lab + "##" + std::to_string(i)).c_str(), m.sel == i)) { m.sel = i; setSong(m.list[i]); m.status = "Opened " + m.list[i].name; }
  }
  if (m.list.empty()) { ImGui::PushFont(L.fSmall); dimText("Saved songs show here. A song with a realm plays in that land now and then."); ImGui::PopFont(); }
  ImGui::EndChild();
  ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  toolbar(L);
  ImGui::PushFont(L.fSmall);
  dimText(m.padEdit ? "Editing: d-pad moves \xC2\xB7 A note \xC2\xB7 LB/RB instrument (%s) \xC2\xB7 Y length \xC2\xB7 X play from here \xC2\xB7 B done"
                    : "Click for a note \xC2\xB7 drag to move \xC2\xB7 right-click or Delete removes \xC2\xB7 Space plays \xC2\xB7 controller: A on the roll to edit", insName(m.ins));
  ImGui::PopFont();
  roll(L, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
  ImGui::EndGroup();
}

} // namespace dq
