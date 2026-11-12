// common/ui_scale.h — interface size for 1080p, 2K, 3K and 4K displays.
// settings.guiScale is shared by the launcher and the game: "auto", or a percentage
// ("100", "125", "150", "200", …). "small" / "normal" / "large" are the original names.
//
// Auto follows the monitor's shorter side (so an ultrawide stays with its height):
//   under 1300 px   100%   1080p and below
//   1300–1599       125%   1440p, "2K"
//   1600–1999       150%   1620p–1800p, "3K"
//   2000 and up     200%   2160p, "4K"
// The game then caps that by the window, so a small window does not overflow.
// Sizes are in the same units as SDL_GetWindowSize (points where the OS already
// scales, pixels otherwise). The framebuffer scale is only for sharp text.
#pragma once
#include <cstdlib>
#include <string>
#include <algorithm>
#include <cmath>

namespace dq {

constexpr int kUiFitW = 1280;   // a window this size holds interface zoom 1
constexpr int kUiFitH = 720;
constexpr int kLauncherDesignW = 1200;
constexpr int kLauncherDesignH = 760;

struct UiTier {
  const char* name;   // "1080p", "2K", "3K", "4K"
  float zoom;
};

constexpr UiTier uiTierForEdge(int edge) {
  if (edge >= 2000) return {"4K", 2.f};
  if (edge >= 1600) return {"3K", 1.5f};
  if (edge >= 1300) return {"2K", 1.25f};
  return {"1080p", 1.f};
}

inline bool uiScaleIsAuto(const std::string& guiScale) {
  return guiScale.empty() || guiScale == "auto" || guiScale == "Auto";
}

// Explicit percentages clamp to 50%..300%. Anything else (including "auto") uses autoZoom.
inline float uiZoomFromSetting(const std::string& guiScale, float autoZoom) {
  if (guiScale == "small") return 0.8f;
  if (guiScale == "normal") return 1.f;
  if (guiScale == "large") return 1.25f;
  if (!uiScaleIsAuto(guiScale)) {
    const char* s = guiScale.c_str();
    char* end = nullptr;
    float pct = std::strtof(s, &end);
    if (end != s && pct >= 50.f) return std::clamp(pct / 100.f, 0.5f, 3.f);
  }
  return autoZoom;
}

// Auto zoom for a window on a display whose shorter side is displayEdge.
// The monitor picks the tier; the window caps it (zoom 1 is authored for 1280×720).
constexpr float autoUiZoom(int winW, int winH, int displayEdge) {
  float tier = uiTierForEdge(std::max(0, displayEdge)).zoom;
  float view = uiTierForEdge(std::max(0, std::min(winW, winH))).zoom;
  float want = std::max(tier, view);
  float fit = 1.f;
  if (winW > 0 && winH > 0) fit = std::min(winW / (float)kUiFitW, winH / (float)kUiFitH);
  if (fit < 1.f) fit = 1.f;
  float z = want < fit ? want : fit;
  if (z < 1.f) z = 1.f;
  if (z > 3.f) z = 3.f;
  return z;
}

// Launcher window for a zoom, kept on the usable display (40 px margin, same as before).
inline void launcherWindowForZoom(float zoom, int dispW, int dispH, int& w, int& h) {
  if (zoom < 0.5f) zoom = 1.f;
  int maxW = dispW > 80 ? dispW - 40 : std::max(1, dispW);
  int maxH = dispH > 80 ? dispH - 40 : std::max(1, dispH);
  w = std::max(1, std::min((int)std::lround(kLauncherDesignW * zoom), maxW));
  h = std::max(1, std::min((int)std::lround(kLauncherDesignH * zoom), maxH));
}

// First game launch (the 1280×720 default, not a size the player chose): grow the
// window so the tier above fits. A 1080p display stays 1280×720.
inline void gameWindowForZoom(float zoom, int dispW, int dispH, int curW, int curH, int& w, int& h) {
  w = curW > 0 ? curW : kUiFitW;
  h = curH > 0 ? curH : kUiFitH;
  if (zoom <= 1.01f) return;
  if (curW != kUiFitW || curH != kUiFitH) return;
  int maxW = dispW > 80 ? dispW - 40 : dispW;
  int maxH = dispH > 80 ? dispH - 40 : dispH;
  int nw = std::min((int)std::lround(kUiFitW * zoom), std::max(1, maxW));
  int nh = std::min((int)std::lround(kUiFitH * zoom), std::max(1, maxH));
  if (nw >= 960 && nh >= 540) { w = nw; h = nh; }
}

static_assert(uiTierForEdge(1080).zoom == 1.f, "1080p");
static_assert(uiTierForEdge(1440).zoom == 1.25f, "2K");
static_assert(uiTierForEdge(1620).zoom == 1.5f, "3K");
static_assert(uiTierForEdge(1800).zoom == 1.5f, "3K 1800p");
static_assert(uiTierForEdge(2160).zoom == 2.f, "4K");
static_assert(autoUiZoom(1280, 720, 1080) == 1.f, "720p window");
static_assert(autoUiZoom(1920, 1080, 1080) == 1.f, "1080p window");
static_assert(autoUiZoom(2560, 1440, 1440) == 1.25f, "2K window");
static_assert(autoUiZoom(2880, 1620, 1620) == 1.5f, "3K window");
static_assert(autoUiZoom(3840, 2160, 2160) == 2.f, "4K window");
static_assert(autoUiZoom(1280, 720, 2160) == 1.f, "small window on a 4K display");
static_assert(autoUiZoom(1920, 1080, 2160) == 1.5f, "1080p window on a 4K display");
static_assert(autoUiZoom(3440, 1440, 1440) == 1.25f, "ultrawide follows its height");

} // namespace dq
