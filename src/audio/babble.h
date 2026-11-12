// audio/babble.h — the villagers' voice (villagers.js babble / npcVoice): one short vowel blip per letter, with a
// puff of noise for the hard consonants, at the villager's own pitch, so a line sounds like Animal Crossing or the
// traders of Vintage Story. Rendered like every other sound here (no audio files): a line is one buffer, started
// when the words start to appear and paced like them (26 letters a second).
#pragma once
#include "common/common.h"
#include <string>
#include <vector>

namespace dq {

class AudioEngine;

namespace snd {

struct BabbleVoice { float pitch = 220, formant = 1; bool triangle = false; int seed = 0; };   // sim NpcVoice, the same fields

// The cache key of a line (voice + pace + words), and the buffer for such a key (mono 48 kHz; `seconds` = how long
// the babble takes to say, villagers.js babble's return value). Safe on any thread.
std::string babbleKey(const std::string& text, const BabbleVoice& v, float rate, int maxBlips);
std::vector<float> renderBabbleKey(const std::string& key, double* seconds = nullptr);

} // namespace snd

// Say a line now — AudioEngine::play("babble:" + key), so dq-sfx can render and mix it by that name too (sfx volume; `volume` is villagers.js's vol: 0.5 for a line, 0.4 for "Thank you!"). Returns a
// handle for AudioEngine::stop, 0 when nothing plays (no sound card, muted).
int babble(AudioEngine& e, const std::string& text, const snd::BabbleVoice& v, float volume = 0.5f, float rate = 26, int maxBlips = 90);

} // namespace dq
