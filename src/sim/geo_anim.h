// sim/geo_anim.h — imported keyframe models' clips on the "geo" rig (sim/geo_anim.cpp)
#pragma once
#include <string>

namespace dq {
void registerGeoAnim();                                                  // (from initCore's sim registrations)
float geoClipLength(const std::string& model, const std::string& clip);  // seconds; 0 if there's no such clip
} // namespace dq
