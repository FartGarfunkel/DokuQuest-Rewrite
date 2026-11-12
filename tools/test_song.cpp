// tools/test_song.cpp — the Music Studio's song core (headless): read a recording, write a piece from it, MIDI there
// and back. Run: build/test_song <recording.wav>
#include "audio/song.h"
#include <cstdio>

using namespace dq::snd;

int main(int argc, char** argv) {
  int fails = 0;
  if (argc > 1) {
    SongAnalysis a = analyseRecording(argv[1]);
    std::printf("analysis ok=%d bpm=%.1f key=%s chords:", a.ok, a.bpm, keyName(a.key, a.minor).c_str());
    for (size_t i = 0; i < a.chords.size() && i < 12; i++) std::printf(" %s", a.chords[i].c_str());
    std::printf("%s\n", a.err.c_str());
    Song s = songFromAnalysis(a, "folk", 7);
    std::printf("composed %zu notes, %.0f beats, %s\n", s.notes.size(), s.lengthBeats(), s.name.c_str());
    if (s.notes.size() < 100) fails++;
  }
  Song m = songFromMood("forest", 3);
  auto bytes = songToMidi(m);
  Song back; std::string err;
  bool ok = songFromMidi(bytes, back, err);
  std::printf("mood piece %zu notes -> MIDI %zu bytes -> %zu notes (%s) bpm %.1f\n", m.notes.size(), bytes.size(), back.notes.size(), ok ? "ok" : err.c_str(), back.bpm);
  if (!ok || back.notes.size() != m.notes.size()) fails++;
  Song j = songFromJson(songToJson(m));
  if (j.notes.size() != m.notes.size()) fails++;
  std::printf("%s (%d)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
