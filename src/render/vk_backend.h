// render/vk_backend.h — Vulkan as a third renderer beside Legacy and Modern OpenGL.
// The game keeps calling OpenGL. On this path those calls run on a Vulkan device.
#pragma once
#include <SDL.h>

namespace dq {

bool gfxInitVulkan(SDL_Window* window);
void gfxShutdownVulkan();
bool gfxIsVulkan();
void gfxVulkanNewFrame();
// Waits (at most 100 ms) for the GPU to finish the frame whose slot the next gfxVulkanNewFrame takes: called before the
// input is read, so the wait comes before the input instead of between reading it and drawing with it (input latency).
void gfxVulkanWaitFrame();
void gfxVulkanPresent(SDL_Window* window);
void gfxVulkanSetVsync(bool on);

} // namespace dq
