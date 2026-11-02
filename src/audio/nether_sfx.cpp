// audio/nether_sfx.cpp — the sounds of the Nether's five lands (BTA 8.0's Nether, gen/nether_bta.h), made on the fly
// with the original's live synthesis ops (audio/dsp.cpp: "tone", "nh" noise hits, "voice", "swell"): the volcanoes'
// rumble and eruptions, the acid pools' bubbling, the Crystal Forest's chimes, the Old World's hollow wind and the
// Crag's falling stones. Added to the name table like every other sound (audio/ambience.cpp plays them).
#include "audio/sfxlib.h"

namespace dq::snd {

namespace {
json tone(double t, double f, double dur, const char* type, double gain, double slide = 0) { return json::array({"tone", t, f, dur, type, gain, slide}); }
json nh(double t, double dur, double f, double q, double gain, const char* type) { return json::array({"nh", t, dur, f, q, gain, type}); }
json swell(double dur, double f0, double f1, double q, double gain, double rise, double delay = 0) {
  return json::array({"swell", {{"dur", dur}, {"fr", json::array({json::array({0, f0}), json::array({1, f1})})}, {"q", q}, {"gain", gain}, {"rise", rise}, {"delay", delay}}});
}
json moan(std::initializer_list<std::pair<double, double>> f, double dur, double gain, double p, double delay = 0) {
  json pts = json::array(); for (auto& q : f) pts.push_back(json::array({q.first, q.second * p}));
  return json::array({"voice", {{"f", pts}, {"dur", dur}, {"v", "uoo"}, {"k", 1.0}, {"gain", gain}, {"noise", 0.55}, {"vib", json::array({2.5, 5 * p})},
                                {"lp", 650}, {"att", 1.1}, {"hold", 0.6}, {"delay", delay}}});
}
} // namespace

void addNetherSounds(json& table) {
  if (!table.is_object() || table.contains("nether_rumble")) return;   // (also the nightmare sting, below)
  const double P[3] = {1.0, 0.88, 1.13};
  json rumble = json::array(), erupt = json::array(), bubble = json::array(), chime = json::array(), ash = json::array(), crack = json::array();
  for (double p : P) {
    // the ground grumbling under a volcano: a slow swell of deep noise over a sinking hum
    rumble.push_back(json::array({swell(3.6, 140 * p, 60 * p, 0.8, 0.32, 0.4), tone(0.2, 46 * p, 3.2, "sine", 0.22, 31 * p), nh(0.6, 2.4, 90 * p, 0.7, 0.18, "lowpass")}));
    // an eruption: the blast, its fall to a growl, the roar of the fountain, the crackle of flung rock
    erupt.push_back(json::array({nh(0, 2.6, 120 * p, 0.8, 0.95, "lowpass"), tone(0, 58 * p, 2.4, "sine", 0.55, 26 * p), swell(6.5, 420 * p, 140 * p, 0.7, 0.26, 0.12, 0.2),
                                 nh(0.15, 1.6, 2300 * p, 1.4, 0.12, "bandpass"), nh(0.9, 1.2, 1800 * p, 1.2, 0.08, "bandpass"), nh(1.7, 0.9, 2600 * p, 1.5, 0.06, "bandpass")}));
    // acid bubbling up and popping in a sulfur pool
    bubble.push_back(json::array({tone(0, 360 * p, 0.07, "sine", 0.12, 820 * p), tone(0.13, 290 * p, 0.06, "sine", 0.08, 690 * p), tone(0.24, 420 * p, 0.05, "sine", 0.06, 940 * p),
                                  nh(0, 0.1, 1300 * p, 4, 0.05, "bandpass"), nh(0.22, 0.08, 1500 * p, 4, 0.035, "bandpass")}));
    // the crystals ringing to each other: a soft glassy chord that hangs on
    chime.push_back(json::array({tone(0, 1568 * p, 1.8, "sine", 0.05), tone(0.03, 2349 * p, 1.5, "sine", 0.03), tone(0.35, 3136 * p, 1.1, "triangle", 0.016),
                                 tone(0.6, 1976 * p, 1.3, "sine", 0.022)}));
    // the Old World's hollow wind through its ruins
    ash.push_back(json::array({moan({{0, 120}, {0.45, 168}, {1, 104}}, 3.8, 0.09, p), swell(3.6, 900 * p, 520 * p, 1.6, 0.06, 0.45, 0.3)}));
    // stones breaking off a crag and clattering down
    crack.push_back(json::array({nh(0, 0.05, 2600 * p, 1.2, 0.24, "bandpass"), tone(0, 190 * p, 0.2, "triangle", 0.12, 90 * p), nh(0.14, 0.04, 3100 * p, 1.5, 0.12, "bandpass"),
                                 nh(0.29, 0.035, 2800 * p, 1.5, 0.08, "bandpass"), nh(0.41, 0.03, 3300 * p, 1.6, 0.05, "bandpass")}));
  }
  // waking from a nightmare: a sharp low jolt, a dissonant cluster rising and a breath of noise
  json sting = json::array();
  for (double p : P) sting.push_back(json::array({nh(0, 0.35, 160 * p, 0.9, 0.7, "lowpass"), tone(0, 62 * p, 1.4, "sawtooth", 0.12, 40 * p),
                                                  tone(0.05, 233 * p, 1.6, "sine", 0.07, 247 * p), tone(0.05, 247 * p, 1.6, "sine", 0.06, 262 * p),
                                                  tone(0.1, 349 * p, 1.4, "triangle", 0.04, 370 * p), swell(1.8, 300 * p, 1400 * p, 1.4, 0.12, 0.6, 0.1)}));
  auto add = [&](const char* name, json takes, double gap) { table[name] = {{"live", takes}, {"gap", gap}}; };
  add("nightmare", sting, 2);
  add("nether_rumble", rumble, 3); add("eruption", erupt, 2); add("acid_bubble", bubble, 0.2); add("crystal_chime", chime, 0.8);
  add("ash_moan", ash, 4); add("crag_crack", crack, 1);
}

} // namespace dq::snd
