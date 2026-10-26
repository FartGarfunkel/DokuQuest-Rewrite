// audio/hw_sfx.cpp — the voices of October's creatures (sim/hallowmobs.cpp), made on the fly with the original's own live
// synthesis (the "voice" and "swell" ops of audio/dsp.cpp). Added to the name table, so Audio::voice(...) finds them and
// its fallbacks (hurt = the call pitched up, death = pitched down) work as for every other creature.
#include "audio/sfxlib.h"

namespace dq::snd {

static json voiceOp(std::initializer_list<std::pair<double, double>> f, double dur, const char* v, double gain, double noise, double vibR, double vibD, double lp, double att, double hold, double pitch, double delay = 0, double amR = 0, double amD = 0) {
  json pts = json::array(); for (auto& p : f) pts.push_back(json::array({p.first, p.second * pitch}));
  json o = {{"f", pts}, {"dur", dur}, {"v", v}, {"k", 1.0}, {"gain", gain}, {"noise", noise}, {"vib", json::array({vibR, vibD * pitch})}, {"lp", lp}, {"att", att}, {"hold", hold}, {"delay", delay}};
  if (amR > 0) o["am"] = json::array({amR, amD});
  return json::array({"voice", o});
}
static json swellOp(double dur, double f0, double f1, double q, double gain, double delay) {
  return json::array({"swell", {{"dur", dur}, {"fr", json::array({json::array({0, f0}), json::array({1, f1})})}, {"q", q}, {"gain", gain}, {"rise", 0.35}, {"delay", delay}}});
}

void addHalloweenSounds(json& table) {
  if (!table.is_object() || table.contains("hw_ghost")) return;
  const double P[3] = {1.0, 0.9, 1.12};   // three takes, a little different each
  json ghost = json::array(), ghostHurt = json::array(), gourd = json::array(), gourdAngry = json::array(), straw = json::array(), strawAngry = json::array();
  for (double p : P) {
    // the ghost's moan: a slow "ooOOoo" with a wobble, and a thin second voice a fifth above, breathing in and out
    ghost.push_back(json::array({voiceOp({{0, 300}, {0.3, 430}, {0.65, 380}, {1, 230}}, 2.8, "uoo", 0.15, 0.12, 4.5, 16, 1500, 0.8, 0.55, p, 0, 2.6, 0.35),
                                 voiceOp({{0, 450}, {0.3, 640}, {0.65, 570}, {1, 345}}, 2.6, "ou", 0.05, 0.1, 5.5, 20, 1900, 0.9, 0.5, p, 0.2)}));
    ghostHurt.push_back(json::array({voiceOp({{0, 520}, {0.4, 760}, {1, 300}}, 0.9, "oa", 0.16, 0.1, 7, 30, 2200, 0.05, 0.4, p)}));
    // the pumpkin creature: a wet wooden creak that gurgles low; angry = a rasping roar from the hollow head
    gourd.push_back(json::array({voiceOp({{0, 110}, {0.5, 150}, {1, 80}}, 0.75, "oa", 0.17, 0.3, 16, 14, 1000, 0.04, 0.5, p),
                                 swellOp(0.5, 700, 260, 2.2, 0.07, 0.05)}));
    gourdAngry.push_back(json::array({voiceOp({{0, 90}, {0.3, 170}, {1, 60}}, 1.0, "aoa", 0.2, 0.5, 11, 18, 1300, 0.03, 0.6, p),
                                      swellOp(0.9, 500, 150, 1.6, 0.09, 0)}));
    // the scarecrow: dry rustling straw with a little creak; angry = a long hollow groan
    straw.push_back(json::array({swellOp(0.32, 2300 * p, 4600 * p, 1.2, 0.11, 0), swellOp(0.24, 3200 * p, 1900 * p, 1.5, 0.08, 0.17),
                                 voiceOp({{0, 125}, {1, 95}}, 0.3, "o", 0.04, 0.3, 0, 0, 600, 0.05, 0.5, p, 0.05)}));
    strawAngry.push_back(json::array({voiceOp({{0, 150}, {0.5, 105}, {1, 78}}, 1.1, "aoa", 0.14, 0.45, 5, 9, 750, 0.1, 0.6, p),
                                      swellOp(0.7, 1800, 3600, 1.0, 0.1, 0.1)}));
  }
  auto add = [&](const char* name, json takes, double gap) { table[name] = {{"live", takes}, {"gap", gap}, {"mob", 1}}; };
  add("hw_ghost", ghost, 1.5); add("hw_ghost_hurt", ghostHurt, 0.4);
  add("hw_gourd", gourd, 0.5); add("hw_gourd_angry", gourdAngry, 0.8);
  add("hw_straw", straw, 0.3); add("hw_straw_angry", strawAngry, 0.8);
}

} // namespace dq::snd
