// render/gpu_select.h — which graphics card the game draws with (Settings::gpu, launcher Options → Graphics Card).
// On a computer with two GPUs (a processor's built-in graphics and a dedicated card) OpenGL takes whichever one
// the driver calls primary, often the built-in one. "auto" picks the first dedicated card; a "vendor:device" id
// (hex, as listGpus() gives it) picks that card. Vulkan chooses its device itself (vk_backend.cpp asks chosenGpuId()).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dq {

struct GpuDevice {
  std::string name;        // "AMD Radeon RX 6950 XT (RADV NAVI21)"
  std::string id;          // "1002:73a5": PCI vendor:device in lower-case hex, the value Settings::gpu stores
  uint32_t vendor = 0, device = 0;
  bool discrete = false, integrated = false;
};

// Every card the Vulkan loader can see (asked once, then cached). Empty when there is no Vulkan loader.
const std::vector<GpuDevice>& listGpus();
// Points the OpenGL driver at the card `setting` names, before SDL makes a GL context. Call it in the game and the
// launcher (the launcher starts the game with its own environment). Linux: Mesa's DRI_PRIME and NVIDIA's PRIME
// offload variables. A DRI_PRIME the player set themselves is left alone. Returns the chosen card's id, or "".
std::string applyGpuChoice(const std::string& setting);
// The id applyGpuChoice chose ("" for no preference): Vulkan prefers that device.
const std::string& chosenGpuId();

} // namespace dq
