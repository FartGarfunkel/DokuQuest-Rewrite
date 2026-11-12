// client/photo_mode.h — Better than Adventure's Photo Mode (the screen: ui/photo_mode.cpp). While it is open the camera
// leaves the player and flies on its own; the world stands still (in a world of your own), the HUD is hidden, and the
// panel's choices change only the picture: the time of day and the season shown, the fog, the tilt, a filter.
// Read by client/play.cpp (the camera), client/session.cpp (time, season, fog), render/char_layer.cpp (show the
// player) and render/colour_filter_layer.cpp (the filter).
#pragma once
#include "common/common.h"

namespace dq {
struct FrameCtx;

struct PhotoMode {
  bool active = false;
  glm::dvec3 pos{0};
  float yaw = 0, pitch = 0;
  float tilt = 0;          // degrees, the camera's roll
  float fovAdd = 0;        // on top of Options' field of view
  bool showPlayer = true, fog = true;
  bool setTime = false;    // show the time of day below instead of the world's own
  float hour = 12;         // 0..24 on the clock (6 is sunrise)
  int season = 0;          // 0 the world's own, 1 spring, 2 summer, 3 autumn, 4 winter
  int shader = 0;          // 0 none (render/colour_filter_layer.cpp lists them)
  float intensity = 1;
};
PhotoMode& photoMode();
void photoFrame(FrameCtx& f);   // after the sky has worked out the frame: the season and the fog as photo mode says
double photoDayTime();          // Level::info.time for the hour photo mode shows

} // namespace dq
