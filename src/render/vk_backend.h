// render/vk_backend.h — Vulkan as a third renderer beside Legacy and Modern OpenGL.
// The game keeps calling OpenGL. On this path those calls run on a Vulkan device.
#pragma once
#include <SDL.h>

namespace dq {

bool gfxInitVulkan(SDL_Window* window);
void gfxShutdownVulkan();
bool gfxIsVulkan();
void gfxVulkanNewFrame();
void gfxVulkanPresent(SDL_Window* window);
void gfxVulkanSetVsync(bool on);

} // namespace dq
