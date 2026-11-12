// audio/song.h — songs for the Music Studio (launcher) and the realms' own music: notes on a beat grid played by
// the game's music instruments (audio/music.h), saved as userDir()/music/<name>.json, Standard MIDI File in and out,
// a recording read for its tempo, key and chords, and new original pieces written to a mood or to what a recording
// was read as. A song given a realm plays now and then in that land (audio/music.cpp).
#pragma once
#include "audio/music.h"
#include "common/common.h"

namespace dq::snd {

struct SongNote { float beat = 0, len = 1; u8 ins = MI_PIANO; float midi = 60, vel = 0.8f; };
struct Song {
  std::string name = "Untitled";
  float bpm = 90;
  std::string realm;                 // "" none, overworld | nether | end | skylands
  std::vector<SongNote> notes;
  float lengthBeats() const;
};

const char* insName(int ins);
json songToJson(const Song& s);
Song songFromJson(const json& j);
std::vector<Song> loadUserSongs();                       // userDir()/music/*.json
bool saveUserSong(const Song& s, std::string* err = nullptr);

// Standard MIDI File (format 1 out, 0 or 1 in): instruments by General MIDI program, drums on channel 10
std::vector<u8> songToMidi(const Song& s);
bool songFromMidi(const std::vector<u8>& bytes, Song& out, std::string& err);

// a recording read by ear: tempo from the beat of its onsets, key from its pitch classes (major or minor), one chord
// per bar from triad templates
struct SongAnalysis { bool ok = false; float bpm = 0; int key = 0; bool minor = false; std::vector<std::string> chords; std::vector<int> chordRoots; std::vector<bool> chordMinor; std::string err; };
SongAnalysis analyseRecording(const std::string& path);
std::string keyName(int key, bool minor);

// new pieces: from one of the game's moods, or an original piece in a key, tempo and chord walk (our own composer)
Song songFromMood(const std::string& mood, u32 seed);
Song songFromAnalysis(const SongAnalysis& a, const std::string& style, u32 seed);
const std::vector<std::string>& songMoods();

} // namespace dq::snd
