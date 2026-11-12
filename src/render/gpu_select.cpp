// render/gpu_select.cpp — see gpu_select.h.
#include "render/gpu_select.h"
#include "common/common.h"
#include <volk.h>
#include <cstdio>
#include <cstdlib>

namespace dq {

static std::string g_chosen;

static std::string hexId(uint32_t vendor, uint32_t device) {
  char b[24]; std::snprintf(b, sizeof b, "%04x:%04x", vendor & 0xffffu, device & 0xffffu);
  return b;
}

const std::vector<GpuDevice>& listGpus() {
  static std::vector<GpuDevice> out;
  static bool asked = false;
  if (asked) return out;
  asked = true;
  if (volkInitialize() != VK_SUCCESS) return out;
  // A throwaway instance with no extensions, its functions fetched into locals, so the volk globals the Vulkan
  // renderer loads for its own instance are never pointed at this one.
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "DokuQuest";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  VkInstance inst = VK_NULL_HANDLE;
  if (!vkCreateInstance || vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) return out;
  auto enumerate = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDevices");
  auto props = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceProperties");
  auto destroy = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(inst, "vkDestroyInstance");
  uint32_t n = 0;
  if (enumerate && props && enumerate(inst, &n, nullptr) == VK_SUCCESS && n) {
    std::vector<VkPhysicalDevice> devs(n);
    enumerate(inst, &n, devs.data());
    for (uint32_t i = 0; i < n; i++) {
      VkPhysicalDeviceProperties p{}; props(devs[i], &p);
      if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;   // llvmpipe: not a card anyone picks
      GpuDevice g;
      g.name = p.deviceName; g.vendor = p.vendorID; g.device = p.deviceID; g.id = hexId(p.vendorID, p.deviceID);
      g.discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
      g.integrated = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
      bool dup = false;
      for (auto& e : out) if (e.id == g.id) dup = true;   // (one card can show up twice through two drivers)
      if (!dup) out.push_back(g);
    }
  }
  if (destroy) destroy(inst, nullptr);
  return out;
}

const std::string& chosenGpuId() { return g_chosen; }

#if defined(__linux__)
static void setEnv(const char* k, const char* v) { setenv(k, v, 1); }
#endif

std::string applyGpuChoice(const std::string& setting) {
  g_chosen.clear();
  const auto& gpus = listGpus();
  const GpuDevice* pick = nullptr;
  if (!setting.empty() && setting != "auto")
    for (auto& g : gpus) if (g.id == setting) pick = &g;
  if (!pick) for (auto& g : gpus) if (g.discrete) { pick = &g; break; }   // auto (or a card that's gone): the dedicated one
  if (!pick) return g_chosen;
  g_chosen = pick->id;
  DQ_LOG("gpu: %s (%s)%s", pick->name.c_str(), pick->id.c_str(), setting == "auto" || setting.empty() ? ", picked automatically" : "");
#if defined(__linux__)
  if (gpus.size() < 2) return g_chosen;   // one card: nothing to steer
  const char* own = getenv("DRI_PRIME");
  if (own && *own && !getenv("DQ_GPU_PICKED")) { DQ_LOG("gpu: DRI_PRIME=%s was set outside the game, leaving it", own); return g_chosen; }
  // Mesa (AMD, Intel, nouveau): DRI_PRIME takes vendor:device. DQ_GPU_PICKED marks it as ours, so a game the
  // launcher starts can change it when the setting changed.
  setEnv("DRI_PRIME", pick->id.c_str());
  setEnv("DQ_GPU_PICKED", "1");
  // NVIDIA's own driver on a computer that also has another GPU (PRIME render offload)
  bool otherVendor = false;
  for (auto& g : gpus) if (g.vendor != 0x10de) otherVendor = true;
  if (pick->vendor == 0x10de && otherVendor) {
    setEnv("__NV_PRIME_RENDER_OFFLOAD", "1");
    setEnv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
    setEnv("__VK_LAYER_NV_optimus", "NVIDIA_only");
  } else if (pick->vendor != 0x10de) {   // another card picked: an earlier NVIDIA choice must not carry over
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    for (auto& g : gpus) if (g.vendor == 0x10de) { setEnv("__VK_LAYER_NV_optimus", "non_NVIDIA_only"); break; }
  }
#endif
  return g_chosen;
}

} // namespace dq
