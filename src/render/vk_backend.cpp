// Vulkan renderer. The game still calls OpenGL (glad function pointers); this file is those calls.
//
// Images are stored with OpenGL's origin: row 0 is the bottom (texture v = 0, framebuffer y = 0).
// The swapchain's origin is the top of the screen, so presenting copies the frame with a Y flip.
// Vertex shaders get a clip-space Z fix so OpenGL's -1..1 depth lands on Vulkan's 0..1.
#include "render/vk_backend.h"
#include "render/gl.h"
#include "common/common.h"

// volk loads libvulkan at runtime, so the game links without a Vulkan SDK (CI, and PCs that have none).
#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <SDL_vulkan.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dq {

static bool g_on = false;
static SDL_Window* g_window = nullptr;

struct GpuBuf {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  void* map = nullptr;
  VkDeviceSize size = 0;
};

struct Buf {
  bool alive = false;
  bool referenced = false;
  bool dirty = false;
  GpuBuf gpu;
};

struct Tex {
  bool alive = false;
  bool depth = false;
  bool defined = false;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;       // sampling: one mip until glGenerateMipmap, then the chain
  VkImageView attachView = VK_NULL_HANDLE; // color attachment: mip 0 only. A multi-mip view is illegal here.
  VkImageView depthStencilView = VK_NULL_HANDLE; // depth+stencil, for the attachment only
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  int w = 1, h = 1, d = 1, layers = 1, mips = 1;
  GLenum target = GL_TEXTURE_2D;
  GLenum minF = GL_NEAREST, magF = GL_NEAREST;
  GLenum wrapS = GL_REPEAT, wrapT = GL_REPEAT, wrapR = GL_REPEAT;
  int maxLevel = 0;
  // OpenGL sampler2D on a depth texture returns the depth in .r. Vulkan leaves that
  // sample undefined on a depth view, and this GPU returns 0, so border fog never starts.
  // The colour twin is an R32F copy that sampler2D can read. Shadow compares stay on the depth.
  VkImage depthColor = VK_NULL_HANDLE;
  VkDeviceMemory depthColorMem = VK_NULL_HANDLE;
  VkImageView depthColorView = VK_NULL_HANDLE;
  VkImageLayout depthColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  int depthColorW = 0, depthColorH = 0;
  bool depthColorDirty = true;
};

struct Attrib {
  bool on = false;
  bool integer = false;
  bool norm = false;
  int size = 4;
  GLenum type = GL_FLOAT;
  GLsizei stride = 0;
  uintptr_t offset = 0;
  GLuint buffer = 0;
  GLuint divisor = 0;
};

struct Vao {
  bool alive = false;
  Attrib a[16];
  GLuint elem = 0;
};

struct ShaderObj {
  bool alive = false;
  GLenum type = 0;
  bool compiled = false;
  std::string src, log;
};

struct Uni {
  std::string name;
  GLenum type = 0;
  int offset = -1;
  int stride = 0;
  int count = 1;
  bool sampler = false;
  int samplerIndex = -1;
};

struct Samp {
  int binding = 0;
  GLenum type = GL_SAMPLER_2D;
  int unit = 0;
  std::string name;
};

struct Prog {
  bool alive = false;
  bool linked = false;
  GLuint vs = 0, fs = 0;
  std::string log;
  VkShaderModule modVS = VK_NULL_HANDLE, modFS = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
  int uboBinding = -1;
  int uboSize = 0;
  std::vector<uint8_t> ubo;
  std::vector<Uni> unis;
  std::vector<Samp> samplers;
  struct SsboRef { int glIndex = 0; int binding = 0; };
  std::vector<SsboRef> ssbos;
  int colorOuts = 0; // highest fragment output location + 1
  std::unordered_map<std::string, int> uniName;
  std::unordered_map<std::string, int> attrName;
  std::vector<int> inputs;
};

struct Fbo {
  bool alive = false;
  GLuint color = 0;
  GLuint colorAtt[8]{};
  int nDraw = 1;
  int drawAtt[8]{};
  GLuint depthRb = 0;
  GLuint depthTex = 0;
  int w = 0, h = 0;
};

struct PipeKey {
  std::array<uint32_t, 96> w{};
  bool operator<(const PipeKey& o) const { return w < o.w; }
};

static VkInstance instance = VK_NULL_HANDLE;
static VkPhysicalDevice phys = VK_NULL_HANDLE;
static VkDevice device = VK_NULL_HANDLE;
static VkQueue queue = VK_NULL_HANDLE;
static uint32_t queueFamily = 0;
static VkSurfaceKHR surface = VK_NULL_HANDLE;
static VkSwapchainKHR swapchain = VK_NULL_HANDLE;
static VkFormat swapFmt = VK_FORMAT_R8G8B8A8_UNORM;
static VkFormat depthFmt = VK_FORMAT_D24_UNORM_S8_UINT;
static VkColorSpaceKHR swapSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
static VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
static bool wantVsync = true;
static std::vector<VkImage> swapImages;
static std::vector<VkImageView> swapViews;
static std::vector<VkImageLayout> swapLayout;
static uint32_t swapIndex = 0;
static int swapW = 0, swapH = 0;

static VkCommandPool cmdPool = VK_NULL_HANDLE;
static VkCommandBuffer cmd = VK_NULL_HANDLE;
static VkSemaphore imageReady = VK_NULL_HANDLE;
static VkSemaphore renderDone = VK_NULL_HANDLE;
static VkFence frameFence = VK_NULL_HANDLE;
static bool recording = false;
static bool frameOpen = false;
static bool skipFrame = false;
static VkPhysicalDeviceMemoryProperties memProps{};
static VkPhysicalDeviceLimits limits{};

static Tex* fbColor = nullptr;
static Tex* fbDepth = nullptr;

static std::vector<Buf> bufs;
static std::vector<Tex> texs;
static std::vector<Tex> rbs;
static std::vector<Vao> vaos;
static std::vector<ShaderObj> shaders;
static std::vector<Prog> progs;
static std::vector<Fbo> fbos;
static std::vector<GLuint> freeBuf, freeTex, freeRb, freeVao, freeSh, freeProg, freeFbo;

static std::vector<GpuBuf> retiredBuf;
static std::vector<VkImage> retiredImg;
static std::vector<VkDeviceMemory> retiredMem;
static std::vector<VkImageView> retiredView;
static std::vector<VkSampler> retiredSamp;
// Shader programs deleted mid-frame (a shader pack reload) are still named by the
// command buffer recording this frame. They go here and die after the frame's fence.
struct RetiredGpu {
  VkPipeline pipe = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkShaderModule mod = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
};
static std::vector<RetiredGpu> retiredGpu;
static void resetDesc();
static void retireGpu(RetiredGpu g) {
  if (g.pipe || g.layout || g.mod || g.dsl) retiredGpu.push_back(g);
}

struct Block { VkBuffer buf; VkDeviceMemory mem; uint8_t* map; VkDeviceSize cap, used; };
static std::vector<Block> bumps;
static int bumpIndex = 0;

static std::vector<VkDescriptorPool> descPools;
static int descPoolCur = -1;

static std::map<PipeKey, VkPipeline> pipelines;
static std::unordered_map<uint32_t, VkSampler> samplers;

static GLuint arrayBuf = 0, copyRead = 0, copyWrite = 0, indirectBuf = 0, pixelUnpack = 0, ssboBuf = 0;
static GLuint ssboBase[16]{};
static GLuint drawFbo = 0, readFbo = 0;
static GLuint curProg = 0;
static GLuint curVao = 0;
static int activeUnit = 0;
static GLuint bind2D[32]{}, bindArr[32]{}, bind3D[32]{};
static float curAttr[16][4]{};

static bool depthTest = false, depthWrite = true, cullOn = false, blendOn = false;
static uint8_t blendMask = 0;   // bit i: blend enabled for color attachment i
static bool scissorOn = false, stencilOn = false, polyOffset = false, primRestart = false;
static GLenum depthFuncE = GL_LESS, cullFaceE = GL_BACK, frontFaceE = GL_CCW;
static GLenum blendSrc = GL_ONE, blendDst = GL_ZERO, blendSrcA = GL_ONE, blendDstA = GL_ZERO;
static GLenum blendEq = GL_FUNC_ADD, blendEqA = GL_FUNC_ADD;
static GLboolean colorMask[4] = {1, 1, 1, 1};
static GLenum stenFunc = GL_ALWAYS, stenSFail = GL_KEEP, stenDPFail = GL_KEEP, stenDPPass = GL_KEEP;
static GLint stenRef = 0, stenCmpMask = 0xFF, stenWriteMask = 0xFF;
static float clearR = 0, clearG = 0, clearB = 0, clearA = 0, clearDepth = 1.f;
static float polyFactor = 0, polyUnits = 0;
static GLint vpX = 0, vpY = 0, vpW = 1, vpH = 1;
static GLint scX = 0, scY = 0, scW = 1, scH = 1;
static GLint unpackAlign = 4, unpackRowLength = 0;
static GLenum lastErr = GL_NO_ERROR;
static bool rendering = false;
static GLuint renderTarget = 0xFFFFFFFFu;
static int renderColors = -1;
static VkImage renderDepth = VK_NULL_HANDLE;
static GLuint white2D = 0, black2D = 0, whiteArr = 0, white3D = 0;

static bool glslangOn = false;

static void setErr(GLenum e) { if (lastErr == GL_NO_ERROR) lastErr = e; }
static bool vkFail(VkResult r, const char* what) {
  if (r == VK_SUCCESS) return false;
  DQ_ERR("vulkan: %s failed (%d)", what, (int)r);
  return true;
}

static uint32_t findMem(uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) return i;
  return 0xFFFFFFFFu;
}

static void destroyGpuBuf(GpuBuf& b) {
  if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
  if (b.mem) vkFreeMemory(device, b.mem, nullptr);
  b = {};
}

static bool allocHostBuf(VkDeviceSize size, VkBufferUsageFlags usage, GpuBuf& out) {
  out = {};
  if (size == 0) return true;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkFail(vkCreateBuffer(device, &bi, nullptr, &out.buffer), "vkCreateBuffer")) return false;
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device, out.buffer, &req);
  uint32_t mt = findMem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mt == 0xFFFFFFFFu) mt = findMem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  if (mt == 0xFFFFFFFFu) { destroyGpuBuf(out); setErr(GL_OUT_OF_MEMORY); return false; }
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = mt;
  if (vkFail(vkAllocateMemory(device, &ai, nullptr, &out.mem), "vkAllocateMemory")) { destroyGpuBuf(out); return false; }
  vkBindBufferMemory(device, out.buffer, out.mem, 0);
  vkMapMemory(device, out.mem, 0, req.size, 0, &out.map);
  out.size = size;
  return true;
}

static void retireGpuBuf(GpuBuf b) { if (b.buffer || b.mem) retiredBuf.push_back(b); }

static void freeRetired() {
  resetDesc();
  for (auto& b : retiredBuf) destroyGpuBuf(b);
  retiredBuf.clear();
  for (auto v : retiredView) vkDestroyImageView(device, v, nullptr);
  retiredView.clear();
  for (auto im : retiredImg) vkDestroyImage(device, im, nullptr);
  retiredImg.clear();
  for (auto m : retiredMem) vkFreeMemory(device, m, nullptr);
  retiredMem.clear();
  for (auto s : retiredSamp) if (s) vkDestroySampler(device, s, nullptr);
  retiredSamp.clear();
  for (auto& g : retiredGpu) if (g.pipe) vkDestroyPipeline(device, g.pipe, nullptr);
  for (auto& g : retiredGpu) if (g.layout) vkDestroyPipelineLayout(device, g.layout, nullptr);
  for (auto& g : retiredGpu) if (g.mod) vkDestroyShaderModule(device, g.mod, nullptr);
  for (auto& g : retiredGpu) if (g.dsl) vkDestroyDescriptorSetLayout(device, g.dsl, nullptr);
  retiredGpu.clear();
}

static void resetBumps() {
  for (auto& b : bumps) b.used = 0;
  bumpIndex = 0;
}

static bool addBump(VkDeviceSize cap) {
  Block b{};
  b.cap = cap;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = cap;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (vkFail(vkCreateBuffer(device, &bi, nullptr, &b.buf), "bump buffer")) return false;
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device, b.buf, &req);
  uint32_t mt = findMem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mt == 0xFFFFFFFFu) return false;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size; ai.memoryTypeIndex = mt;
  if (vkFail(vkAllocateMemory(device, &ai, nullptr, &b.mem), "bump memory")) return false;
  vkBindBufferMemory(device, b.buf, b.mem, 0);
  vkMapMemory(device, b.mem, 0, req.size, 0, (void**)&b.map);
  bumps.push_back(b);
  return true;
}

struct Alloc { VkBuffer buf; VkDeviceSize off; uint8_t* ptr; };
static Alloc bumpAlloc(VkDeviceSize n, VkDeviceSize align) {
  if (align == 0) align = 1;
  if (bumps.empty()) addBump(std::max(n + align, (VkDeviceSize)(8u << 20)));
  for (int guard = 0; guard < 4; guard++) {
    if (bumpIndex >= (int)bumps.size()) { if (!addBump(std::max(n + align, (VkDeviceSize)(8u << 20)))) return {}; bumpIndex = (int)bumps.size() - 1; }
    Block& b = bumps[bumpIndex];
    VkDeviceSize off = (b.used + align - 1) & ~(align - 1);
    if (off + n <= b.cap) { b.used = off + n; return {b.buf, off, b.map + off}; }
    bumpIndex++;
  }
  return {};
}

static const VkBufferUsageFlags kBufUse =
  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
  VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

static Buf* bufOf(GLuint id) { return id && id < bufs.size() && bufs[id].alive ? &bufs[id] : nullptr; }
static Tex* texOf(GLuint id) { return id && id < texs.size() && texs[id].alive ? &texs[id] : nullptr; }
static Tex* rbOf(GLuint id) { return id && id < rbs.size() && rbs[id].alive ? &rbs[id] : nullptr; }
static Vao* vaoOf(GLuint id) { return id < vaos.size() && vaos[id].alive ? &vaos[id] : nullptr; }
static ShaderObj* shOf(GLuint id) { return id && id < shaders.size() && shaders[id].alive ? &shaders[id] : nullptr; }
static Prog* progOf(GLuint id) { return id && id < progs.size() && progs[id].alive ? &progs[id] : nullptr; }
static Fbo* fboOf(GLuint id) { return id && id < fbos.size() && fbos[id].alive ? &fbos[id] : nullptr; }

template <class T>
static GLuint allocId(std::vector<T>& tab, std::vector<GLuint>& freeIds) {
  if (!freeIds.empty()) { GLuint id = freeIds.back(); freeIds.pop_back(); tab[id] = T{}; tab[id].alive = true; return id; }
  tab.push_back(T{});
  tab.back().alive = true;
  return (GLuint)tab.size() - 1;
}

static void beginOneShot() {
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  recording = true;
}
static void submitOneShot() {
  if (!recording) return;
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);
  recording = false;
  freeRetired();
  resetBumps();
}

struct GpuScope {
  bool outer = false;
  GpuScope() {
    if (recording || frameOpen) return;
    beginOneShot();
    outer = true;
  }
  ~GpuScope() { if (outer && recording) submitOneShot(); }
};

static void hostBarrier(VkBuffer buffer, VkAccessFlags dstAccess, VkPipelineStageFlags dstStage) {
  VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  b.dstAccessMask = dstAccess;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.buffer = buffer;
  b.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, dstStage, 0, 0, nullptr, 1, &b, 0, nullptr);
}

static bool depthHasStencil(VkFormat f) {
  return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}
static VkImageAspectFlags aspectOf(const Tex& t) {
  if (!t.depth) return VK_IMAGE_ASPECT_COLOR_BIT;
  return depthHasStencil(t.format) ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT;
}
static VkImageAspectFlags copyAspect(const Tex& t) {
  return t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

static void transitionRaw(Tex& t, VkImageLayout next, VkImageAspectFlags aspect, int baseMip, int mipCount) {
  if (!t.image || t.layout == next) return;
  if (!recording) return;
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = t.layout;
  b.newLayout = next;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = t.image;
  b.subresourceRange.aspectMask = aspect;
  b.subresourceRange.baseMipLevel = (uint32_t)baseMip;
  b.subresourceRange.levelCount = (uint32_t)mipCount;
  b.subresourceRange.layerCount = t.target == GL_TEXTURE_3D ? 1u : (uint32_t)std::max(1, t.layers);
  auto ends = [](VkImageLayout l, VkPipelineStageFlags& st, VkAccessFlags& ac) {
    switch (l) {
      case VK_IMAGE_LAYOUT_UNDEFINED: st = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; ac = 0; break;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: st = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; ac = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: st = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; ac = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: st = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; ac = VK_ACCESS_SHADER_READ_BIT; break;
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: st = VK_PIPELINE_STAGE_TRANSFER_BIT; ac = VK_ACCESS_TRANSFER_READ_BIT; break;
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: st = VK_PIPELINE_STAGE_TRANSFER_BIT; ac = VK_ACCESS_TRANSFER_WRITE_BIT; break;
      default: st = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; ac = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; break;
    }
  };
  VkPipelineStageFlags ss, ds; VkAccessFlags sa, da;
  ends(t.layout, ss, sa); ends(next, ds, da);
  if (t.layout == VK_IMAGE_LAYOUT_UNDEFINED) sa = 0;
  b.srcAccessMask = sa; b.dstAccessMask = da;
  vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
  if (baseMip == 0 && mipCount >= t.mips) t.layout = next;
}

static void endRendering();
static void transitionAll(Tex& t, VkImageLayout next) {
  if (rendering) endRendering();
  transitionRaw(t, next, aspectOf(t), 0, t.mips);
  t.layout = next;
}

static int targetW(GLuint fbo);
static int targetH(GLuint fbo);
static Tex* colorOf(GLuint fbo);
static Tex* depthOf(GLuint fbo);

struct ColorAtt { Tex* tex = nullptr; VkFormat format = VK_FORMAT_UNDEFINED; };

// Draw buffers select the color attachments. A shader that writes more locations than the
// framebuffer has gets a null attachment for the extra ones, so the real targets keep their slots.
static void fboColors(GLuint fbo, const Prog* p, ColorAtt* out, int& n) {
  n = 0;
  if (fbo == 0) {
    if (!fbColor) return;
    out[0] = {fbColor, fbColor->format};
    n = 1;
    return;
  }
  Fbo* f = fboOf(fbo);
  if (!f) return;
  int want = f->nDraw > 0 ? f->nDraw : (f->color ? 1 : 0);
  want = std::clamp(want, 0, 8);
  if (p && p->colorOuts > want) want = std::min(p->colorOuts, 8);
  if (limits.maxColorAttachments) want = std::min(want, (int)limits.maxColorAttachments);
  bool any = false;
  for (int i = 0; i < want; i++) {
    GLuint id = 0;
    if (i < f->nDraw) {
      int a = f->drawAtt[i];
      if (a >= 0 && a < 8) id = a == 0 ? (f->color ? f->color : f->colorAtt[0]) : f->colorAtt[a];
    }
    Tex* t = id ? texOf(id) : nullptr;
    out[i].tex = t;
    out[i].format = t ? t->format : VK_FORMAT_R8G8B8A8_UNORM;
    if (t) any = true;
  }
  if (!any) {
    if (f->color) {
      if (Tex* t = texOf(f->color)) { out[0] = {t, t->format}; n = 1; }
    }
    return;
  }
  n = want;
}

static void endRendering() {
  if (!rendering) return;
  vkCmdEndRendering(cmd);
  rendering = false;
  renderTarget = 0xFFFFFFFFu;
  renderColors = -1;
  renderDepth = VK_NULL_HANDLE;
}

static void beginRendering(const Prog* p) {
  if (skipFrame || !recording) return;
  ColorAtt cols[8];
  int nCol = 0;
  fboColors(drawFbo, p, cols, nCol);
  // The depth image is part of the pass. A water draw that only swaps the depth
  // attachment must not keep the terrain pass open: that pass still tests the old image.
  Tex* depth = depthOf(drawFbo);
  VkImage depthImg = depth && depth->image ? depth->image : VK_NULL_HANDLE;
  if (rendering && renderTarget == drawFbo && renderColors == nCol && renderDepth == depthImg) return;
  if (rendering) endRendering();
  if (nCol <= 0 && !depth) return;
  int w = targetW(drawFbo), h = targetH(drawFbo);
  if (w <= 0 || h <= 0) return;
  for (int i = 0; i < nCol; i++) if (cols[i].tex)
    transitionRaw(*cols[i].tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, cols[i].tex->mips);
  if (depth) transitionRaw(*depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspectOf(*depth), 0, depth->mips);
  VkRenderingAttachmentInfo ca[8]{};
  VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.layerCount = 1;
  ri.renderArea = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
  if (nCol > 0) {
    for (int i = 0; i < nCol; i++) {
      ca[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      ca[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      VkImageView colorView = cols[i].tex ? (cols[i].tex->attachView ? cols[i].tex->attachView : cols[i].tex->view) : VK_NULL_HANDLE;
      if (cols[i].tex && colorView) {
        ca[i].imageView = colorView;
        ca[i].loadOp = cols[i].tex->defined ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ca[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        cols[i].tex->defined = true;
      } else {
        ca[i].imageView = VK_NULL_HANDLE;
        ca[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ca[i].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      }
    }
    ri.colorAttachmentCount = (uint32_t)nCol;
    ri.pColorAttachments = ca;
  }
  if (depth) {
    da.imageView = depth->depthStencilView ? depth->depthStencilView : depth->view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = depth->defined ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ri.pDepthAttachment = &da;
    // A depth-only view is not a legal stencil attachment. Portals write stencil, so the
    // combined view is used when the format has one. Sampling stays on the depth-only view.
    if (depth->depthStencilView) ri.pStencilAttachment = &da;
    depth->defined = true;
  }
  vkCmdBeginRendering(cmd, &ri);
  rendering = true;
  renderTarget = drawFbo;
  renderColors = nCol;
  renderDepth = depthImg;
}

static void syncGpu() {
  if (!recording) return;
  endRendering();
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);
  recording = false;
  freeRetired();
  if (!frameOpen) { resetBumps(); return; }
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  recording = true;
}

static bool supported(VkFormat fmt, VkFormatFeatureFlags need) {
  VkFormatProperties p{};
  vkGetPhysicalDeviceFormatProperties(phys, fmt, &p);
  return (p.optimalTilingFeatures & need) == need;
}

static VkFormat pickDepth() {
  VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if (supported(VK_FORMAT_D24_UNORM_S8_UINT, need)) return VK_FORMAT_D24_UNORM_S8_UINT;
  if (supported(VK_FORMAT_D32_SFLOAT_S8_UINT, need)) return VK_FORMAT_D32_SFLOAT_S8_UINT;
  return VK_FORMAT_D32_SFLOAT;
}

static void freeDepthColor(Tex& t) {
  if (t.depthColorView) retiredView.push_back(t.depthColorView);
  if (t.depthColor) retiredImg.push_back(t.depthColor);
  if (t.depthColorMem) retiredMem.push_back(t.depthColorMem);
  t.depthColorView = VK_NULL_HANDLE;
  t.depthColor = VK_NULL_HANDLE;
  t.depthColorMem = VK_NULL_HANDLE;
  t.depthColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  t.depthColorW = t.depthColorH = 0;
  t.depthColorDirty = true;
}
static void destroyTexGpu(Tex& t) {
  freeDepthColor(t);
  if (t.depthStencilView) retiredView.push_back(t.depthStencilView);
  if (t.attachView) retiredView.push_back(t.attachView);
  if (t.view) retiredView.push_back(t.view);
  if (t.image) retiredImg.push_back(t.image);
  if (t.mem) retiredMem.push_back(t.mem);
  t.view = VK_NULL_HANDLE; t.attachView = VK_NULL_HANDLE; t.depthStencilView = VK_NULL_HANDLE;
  t.image = VK_NULL_HANDLE; t.mem = VK_NULL_HANDLE;
  t.layout = VK_IMAGE_LAYOUT_UNDEFINED; t.defined = false;
}

// A color attachment view has to be one mip. The sample view starts as mip 0 as well: the image
// keeps a full chain for glGenerateMipmap, but the levels above 0 are undefined until that runs,
// and sampling a view that includes them made shader-pack texelFetch come back white.
// The sample view starts as mip 0. The image keeps a full chain for glGenerateMipmap, but the
// levels above 0 are undefined until that runs, and sampling them made texelFetch come back white.
// The attachment view stays mip 0 even after the sample view grows to the whole chain.
static void retireViews(Tex& t) {
  if (t.depthStencilView) { retiredView.push_back(t.depthStencilView); t.depthStencilView = VK_NULL_HANDLE; }
  if (t.attachView) { retiredView.push_back(t.attachView); t.attachView = VK_NULL_HANDLE; }
  if (t.view) { retiredView.push_back(t.view); t.view = VK_NULL_HANDLE; }
}
static bool makeViews(Tex& t, int sampleLevels) {
  retireViews(t);
  if (!t.image) return false;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = t.image;
  vi.viewType = t.target == GL_TEXTURE_3D ? VK_IMAGE_VIEW_TYPE_3D : t.target == GL_TEXTURE_2D_ARRAY ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  vi.format = t.format;
  vi.subresourceRange.aspectMask = t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  vi.subresourceRange.levelCount = (uint32_t)std::clamp(sampleLevels, 1, std::max(1, t.mips));
  vi.subresourceRange.layerCount = (uint32_t)(t.target == GL_TEXTURE_3D ? 1 : std::max(1, t.layers));
  if (vkFail(vkCreateImageView(device, &vi, nullptr, &t.view), "image view")) return false;
  // Sampling may see the whole chain after glGenerateMipmap. A color attachment may not:
  // levelCount > 1 is invalid, and this GPU then paints a half-size copy into the top-left.
  if (!t.depth) {
    vi.subresourceRange.baseMipLevel = 0;
    vi.subresourceRange.levelCount = 1;
    if (vkFail(vkCreateImageView(device, &vi, nullptr, &t.attachView), "color attachment view")) return false;
  }
  if (t.depth && t.format != VK_FORMAT_D32_SFLOAT) {
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    vi.subresourceRange.baseMipLevel = 0;
    vi.subresourceRange.levelCount = 1;
    if (vkFail(vkCreateImageView(device, &vi, nullptr, &t.depthStencilView), "depth stencil view")) return false;
  }
  return true;
}
static bool makeImage(Tex& t) {
  destroyTexGpu(t);
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = t.target == GL_TEXTURE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
  ii.format = t.format;
  ii.extent = {(uint32_t)std::max(1, t.w), (uint32_t)std::max(1, t.h), (uint32_t)(t.target == GL_TEXTURE_3D ? std::max(1, t.d) : 1)};
  ii.mipLevels = (uint32_t)std::max(1, t.mips);
  ii.arrayLayers = (uint32_t)(t.target == GL_TEXTURE_3D ? 1 : std::max(1, t.layers));
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (t.depth) ii.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  else if (supported(t.format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) ii.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkFail(vkCreateImage(device, &ii, nullptr, &t.image), "vkCreateImage")) return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device, t.image, &req);
  uint32_t mt = findMem(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mt == 0xFFFFFFFFu) mt = findMem(req.memoryTypeBits, 0);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size; ai.memoryTypeIndex = mt;
  if (vkFail(vkAllocateMemory(device, &ai, nullptr, &t.mem), "image memory")) return false;
  vkBindImageMemory(device, t.image, t.mem, 0);
  t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  t.defined = false;
  return makeViews(t, 1);
}

static int mipCountOf(int w, int h, int d) {
  int n = 1, m = std::max(w, std::max(h, d));
  while (m > 1) { n++; m >>= 1; }
  return n;
}

static bool colorFmtOk(VkFormat fmt) {
  VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
    VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
    VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
  return supported(fmt, need);
}
static VkFormat glFormat(GLenum internal, GLenum format) {
  GLenum f = internal ? internal : format;
  if (f == GL_R8 || f == GL_RED) return VK_FORMAT_R8_UNORM;
  if (f == GL_RG8 || f == GL_RG) return VK_FORMAT_R8G8_UNORM;
  if (f == GL_R32F) return VK_FORMAT_R32_SFLOAT;
  if (f == GL_DEPTH_COMPONENT || f == GL_DEPTH_COMPONENT16 || f == GL_DEPTH_COMPONENT24 || f == GL_DEPTH_COMPONENT32) return depthFmt;
  // Pack colortex are RGBA16F. Storing them as unorm clamps the lighting before the tonemap.
  if (f == GL_RGBA16F || f == GL_RGB16F || f == GL_RGBA32F) {
    VkFormat hf = f == GL_RGBA32F ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
    if (colorFmtOk(hf)) return hf;
    static bool warned = false;
    if (!warned) { warned = true; DQ_WARN("vulkan: float colour buffers are not available, shader packs stay 8-bit"); }
  }
  return VK_FORMAT_R8G8B8A8_UNORM;
}
static int compOf(VkFormat f) {
  if (f == VK_FORMAT_R8_UNORM || f == VK_FORMAT_R32_SFLOAT || f == VK_FORMAT_R16_SFLOAT) return 1;
  if (f == VK_FORMAT_R8G8_UNORM || f == VK_FORMAT_R16G16_SFLOAT) return 2;
  return 4;
}
static bool halfFmt(VkFormat f) {
  return f == VK_FORMAT_R16_SFLOAT || f == VK_FORMAT_R16G16_SFLOAT || f == VK_FORMAT_R16G16B16A16_SFLOAT;
}
static int bppOf(VkFormat f) {
  if (f == VK_FORMAT_R8_UNORM) return 1;
  if (f == VK_FORMAT_R8G8_UNORM || f == VK_FORMAT_R16_SFLOAT) return 2;
  if (f == VK_FORMAT_R16G16_SFLOAT || f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_R32_SFLOAT) return 4;
  if (f == VK_FORMAT_R16G16B16A16_SFLOAT) return 8;
  if (f == VK_FORMAT_R32G32B32A32_SFLOAT) return 16;
  return 4;
}
static uint16_t f32to16(float v) {
  uint32_t x; std::memcpy(&x, &v, 4);
  uint32_t sign = (x >> 16) & 0x8000u;
  int32_t exp = (int32_t)((x >> 23) & 0xff) - 127 + 15;
  uint32_t mant = x & 0x7fffffu;
  if (((x >> 23) & 0xff) == 0xff) return (uint16_t)(sign | 0x7c00u | (mant ? 0x200u : 0));
  if (exp <= 0) {
    if (exp < -10) return (uint16_t)sign;
    mant |= 0x800000u;
    uint32_t t = mant >> (uint32_t)(1 - exp + 13);
    if ((mant >> (uint32_t)(1 - exp + 12)) & 1u) t++;
    return (uint16_t)(sign | t);
  }
  if (exp >= 31) return (uint16_t)(sign | 0x7c00u);
  uint16_t h = (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
  if (mant & 0x1000u) h++;
  return h;
}
static float f16to32(uint16_t h) {
  uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
  int exp = (h >> 10) & 0x1f;
  int mant = h & 0x3ff;
  uint32_t out;
  if (exp == 0) {
    if (!mant) out = sign;
    else {
      exp = 127 - 15 + 1;
      while ((mant & 0x400) == 0) { mant <<= 1; exp--; }
      mant &= 0x3ff;
      out = sign | ((uint32_t)exp << 23) | ((uint32_t)mant << 13);
    }
  } else if (exp == 31) out = sign | 0x7f800000u | ((uint32_t)mant << 13);
  else out = sign | ((uint32_t)(exp + 127 - 15) << 23) | ((uint32_t)mant << 13);
  float f; std::memcpy(&f, &out, 4); return f;
}

static void uploadImage(Tex& t, int level, int x, int y, int z, int w, int h, int d, const void* data, GLenum srcType) {
  if (!data || !t.image || t.depth) return;
  GpuScope gpu;
  if (!recording) return;
  int bpp = bppOf(t.format);
  const bool toHalf = srcType == GL_FLOAT && halfFmt(t.format);
  const int srcComp = compOf(t.format);
  const int srcBpp = toHalf ? srcComp * 4 : bpp;
  int rowBytes = w * bpp;
  int srcRow = w * srcBpp;
  int srcStride = unpackRowLength > 0 ? unpackRowLength * srcBpp : (srcRow + unpackAlign - 1) & ~(unpackAlign - 1);
  int stride = rowBytes;
  VkDeviceSize total = (VkDeviceSize)stride * h * std::max(1, d);
  auto a = bumpAlloc(total, 256);
  if (!a.ptr) return;
  if (!toHalf && srcStride == rowBytes) memcpy(a.ptr, data, (size_t)total);
  else {
    const char* src = (const char*)data;
    char* dst = (char*)a.ptr;
    for (int sliceI = 0; sliceI < std::max(1, d); sliceI++)
      for (int row = 0; row < h; row++) {
        const char* srow = src + ((size_t)sliceI * h + row) * srcStride;
        char* drow = dst + ((size_t)sliceI * h + row) * stride;
        if (!toHalf) memcpy(drow, srow, rowBytes);
        else {
          auto* out = reinterpret_cast<uint16_t*>(drow);
          for (int col = 0; col < w * srcComp; col++) {
            float v; std::memcpy(&v, srow + (size_t)col * 4, 4);
            out[col] = f32to16(v);
          }
        }
      }
  }
  if (rendering) endRendering();
  transitionRaw(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, t.mips);
  t.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  hostBarrier(a.buf, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy c{};
  c.bufferOffset = a.off;
  c.bufferRowLength = (stride == rowBytes) ? 0u : (uint32_t)(stride / bpp);
  c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  c.imageSubresource.mipLevel = (uint32_t)level;
  c.imageSubresource.baseArrayLayer = t.target == GL_TEXTURE_3D ? 0u : (uint32_t)z;
  c.imageSubresource.layerCount = t.target == GL_TEXTURE_3D ? 1u : (uint32_t)std::max(1, d);
  c.imageOffset = {x, y, t.target == GL_TEXTURE_3D ? z : 0};
  c.imageExtent = {(uint32_t)w, (uint32_t)h, (uint32_t)(t.target == GL_TEXTURE_3D ? std::max(1, d) : 1)};
  vkCmdCopyBufferToImage(cmd, a.buf, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
  transitionRaw(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, t.mips);
  t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  t.defined = true;
}

static void defineTex(Tex& t, GLenum target, int level, GLenum internal, int w, int h, int d, int layers, GLenum format, GLenum type, const void* data) {
  if (level > 0) { uploadImage(t, level, 0, 0, 0, w, h, std::max(1, d), data, type); return; }
  t.target = target;
  t.w = std::max(1, w); t.h = std::max(1, h);
  t.d = target == GL_TEXTURE_3D ? std::max(1, d) : 1;
  t.layers = target == GL_TEXTURE_2D_ARRAY ? std::max(1, layers) : 1;
  t.format = glFormat(internal, format);
  t.depth = t.format == depthFmt;
  t.mips = mipCountOf(t.w, t.h, t.d);
  t.maxLevel = 0;
  t.minF = GL_NEAREST; t.magF = GL_NEAREST;
  if (!makeImage(t)) return;
  if (data && !t.depth) uploadImage(t, 0, 0, 0, 0, t.w, t.h, target == GL_TEXTURE_3D ? t.d : t.layers, data, type);
}

static Tex* boundTex(GLenum target) {
  GLuint id = target == GL_TEXTURE_2D_ARRAY ? bindArr[activeUnit] : target == GL_TEXTURE_3D ? bind3D[activeUnit] : bind2D[activeUnit];
  return texOf(id);
}

static uint32_t sampKey(const Tex& t, bool shadow) {
  uint32_t k = 0;
  k |= (t.magF == GL_LINEAR ? 1u : 0u);
  k |= (t.minF == GL_LINEAR || t.minF == GL_LINEAR_MIPMAP_NEAREST || t.minF == GL_LINEAR_MIPMAP_LINEAR ? 2u : 0u);
  bool mip = t.minF == GL_NEAREST_MIPMAP_NEAREST || t.minF == GL_LINEAR_MIPMAP_NEAREST || t.minF == GL_NEAREST_MIPMAP_LINEAR || t.minF == GL_LINEAR_MIPMAP_LINEAR;
  bool mipLinear = t.minF == GL_NEAREST_MIPMAP_LINEAR || t.minF == GL_LINEAR_MIPMAP_LINEAR;
  k |= mip ? 4u : 0u;
  k |= mipLinear ? 8u : 0u;
  auto enc = [](GLenum w) { return w == GL_CLAMP_TO_EDGE ? 1u : w == GL_MIRRORED_REPEAT ? 2u : 0u; };
  k |= enc(t.wrapS) << 4;
  k |= enc(t.wrapT) << 6;
  k |= enc(t.wrapR) << 8;
  k |= (uint32_t)std::min(t.maxLevel, 15) << 10;
  if (shadow) k |= 1u << 16;
  return k;
}
static VkSamplerAddressMode addrMode(GLenum w) {
  if (w == GL_CLAMP_TO_EDGE) return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (w == GL_MIRRORED_REPEAT) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}
static VkSampler samplerFor(const Tex& t, bool shadow) {
  uint32_t key = sampKey(t, shadow);
  auto it = samplers.find(key);
  if (it != samplers.end()) return it->second;
  bool magL = t.magF == GL_LINEAR;
  bool minL = t.minF == GL_LINEAR || t.minF == GL_LINEAR_MIPMAP_NEAREST || t.minF == GL_LINEAR_MIPMAP_LINEAR;
  bool mip = t.minF == GL_NEAREST_MIPMAP_NEAREST || t.minF == GL_LINEAR_MIPMAP_NEAREST || t.minF == GL_NEAREST_MIPMAP_LINEAR || t.minF == GL_LINEAR_MIPMAP_LINEAR;
  bool mipL = t.minF == GL_NEAREST_MIPMAP_LINEAR || t.minF == GL_LINEAR_MIPMAP_LINEAR;
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = magL ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  si.minFilter = minL ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  si.mipmapMode = (mip && mipL) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = addrMode(t.wrapS);
  si.addressModeV = addrMode(t.wrapT);
  si.addressModeW = addrMode(t.wrapR);
  si.maxLod = mip ? (float)std::max(0, t.maxLevel) : 0.f;
  // GL shadow maps are GL_LEQUAL. LESS treats an equal depth as shadowed and speckles every lit face.
  if (shadow) { si.compareEnable = VK_TRUE; si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; }
  VkSampler s = VK_NULL_HANDLE;
  if (vkFail(vkCreateSampler(device, &si, nullptr, &s), "sampler")) return VK_NULL_HANDLE;
  samplers[key] = s;
  return s;
}

static Tex* dummyFor(GLenum type) {
  if (type == GL_SAMPLER_2D_ARRAY || type == GL_SAMPLER_2D_ARRAY_SHADOW) return texOf(whiteArr);
  if (type == GL_SAMPLER_3D) return texOf(white3D);
  return texOf(white2D);
}

static void ensureDescPool() {
  if (descPoolCur >= 0) return;
  VkDescriptorPoolSize sizes[3]{};
  sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; sizes[0].descriptorCount = 2000;
  sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[1].descriptorCount = 2000 * 16;
  sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sizes[2].descriptorCount = 2000 * 8;
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets = 2000;
  pi.poolSizeCount = 3;
  pi.pPoolSizes = sizes;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (vkFail(vkCreateDescriptorPool(device, &pi, nullptr, &pool), "descriptor pool")) return;
  descPools.push_back(pool);
  descPoolCur = (int)descPools.size() - 1;
}
static void resetDesc() {
  for (auto p : descPools) vkResetDescriptorPool(device, p, 0);
  descPoolCur = descPools.empty() ? -1 : 0;
}

static VkBlendFactor blendFactor(GLenum f) {
  switch (f) {
    case GL_ZERO: return VK_BLEND_FACTOR_ZERO;
    case GL_ONE: return VK_BLEND_FACTOR_ONE;
    case GL_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case GL_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case GL_SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default: return VK_BLEND_FACTOR_ONE;
  }
}
static VkBlendOp blendOp(GLenum e) {
  if (e == GL_FUNC_SUBTRACT) return VK_BLEND_OP_SUBTRACT;
  if (e == GL_FUNC_REVERSE_SUBTRACT) return VK_BLEND_OP_REVERSE_SUBTRACT;
  if (e == GL_MIN) return VK_BLEND_OP_MIN;
  if (e == GL_MAX) return VK_BLEND_OP_MAX;
  return VK_BLEND_OP_ADD;
}
static VkCompareOp cmpOp(GLenum f) {
  switch (f) {
    case GL_NEVER: return VK_COMPARE_OP_NEVER;
    case GL_LESS: return VK_COMPARE_OP_LESS;
    case GL_EQUAL: return VK_COMPARE_OP_EQUAL;
    case GL_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case GL_GREATER: return VK_COMPARE_OP_GREATER;
    case GL_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
    case GL_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case GL_ALWAYS: return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_LESS_OR_EQUAL;
  }
}
static VkStencilOp stenOp(GLenum o) {
  switch (o) {
    case GL_ZERO: return VK_STENCIL_OP_ZERO;
    case GL_REPLACE: return VK_STENCIL_OP_REPLACE;
    case GL_INCR: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case GL_DECR: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case GL_INCR_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case GL_DECR_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    case GL_INVERT: return VK_STENCIL_OP_INVERT;
    default: return VK_STENCIL_OP_KEEP;
  }
}
static VkPrimitiveTopology topoOf(GLenum m) {
  if (m == GL_LINES) return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  if (m == GL_LINE_STRIP) return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  if (m == GL_TRIANGLE_STRIP) return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  if (m == GL_TRIANGLE_FAN) return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}
static VkFormat attribFormat(const Attrib& a) {
  // Three packed bytes (particle tint). R8G8B8 is not a required vertex format; R8G8B8A8
  // is, and a vec3 shader keeps the first three components. The extra byte is the next
  // field in the vertex and is also read by its own attribute.
  if (a.integer) {
    if (a.type == GL_UNSIGNED_INT) return a.size == 4 ? VK_FORMAT_R32G32B32A32_UINT : a.size == 3 ? VK_FORMAT_R32G32B32_UINT : a.size == 2 ? VK_FORMAT_R32G32_UINT : VK_FORMAT_R32_UINT;
    if (a.type == GL_INT) return a.size == 4 ? VK_FORMAT_R32G32B32A32_SINT : a.size == 3 ? VK_FORMAT_R32G32B32_SINT : a.size == 2 ? VK_FORMAT_R32G32_SINT : VK_FORMAT_R32_SINT;
    if (a.type == GL_UNSIGNED_SHORT) return a.size >= 3 ? VK_FORMAT_R16G16B16A16_UINT : a.size == 2 ? VK_FORMAT_R16G16_UINT : VK_FORMAT_R16_UINT;
    if (a.type == GL_SHORT) return a.size >= 3 ? VK_FORMAT_R16G16B16A16_SINT : a.size == 2 ? VK_FORMAT_R16G16_SINT : VK_FORMAT_R16_SINT;
    if (a.type == GL_UNSIGNED_BYTE) return a.size >= 3 ? VK_FORMAT_R8G8B8A8_UINT : a.size == 2 ? VK_FORMAT_R8G8_UINT : VK_FORMAT_R8_UINT;
    if (a.type == GL_BYTE) return a.size >= 3 ? VK_FORMAT_R8G8B8A8_SINT : a.size == 2 ? VK_FORMAT_R8G8_SINT : VK_FORMAT_R8_SINT;
  }
  if (a.type == GL_UNSIGNED_BYTE || a.type == GL_BYTE) {
    bool sign = a.type == GL_BYTE;
    if (a.size >= 3) return a.norm ? (sign ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_UNORM) : (sign ? VK_FORMAT_R8G8B8A8_SSCALED : VK_FORMAT_R8G8B8A8_USCALED);
    if (a.size == 2) return a.norm ? (sign ? VK_FORMAT_R8G8_SNORM : VK_FORMAT_R8G8_UNORM) : (sign ? VK_FORMAT_R8G8_SSCALED : VK_FORMAT_R8G8_USCALED);
    return a.norm ? (sign ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_UNORM) : (sign ? VK_FORMAT_R8_SSCALED : VK_FORMAT_R8_USCALED);
  }
  if (a.type == GL_FLOAT) return a.size == 4 ? VK_FORMAT_R32G32B32A32_SFLOAT : a.size == 3 ? VK_FORMAT_R32G32B32_SFLOAT : a.size == 2 ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R32_SFLOAT;
  // Three unsigned shorts (voxel positions) and one signed short (distant height).
  // R16G16B16 is not a required vertex format; a vec3 shader reads the first three of four.
  if (a.type == GL_UNSIGNED_SHORT || a.type == GL_SHORT) {
    bool sign = a.type == GL_SHORT;
    if (a.size >= 3) return a.norm ? (sign ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_UNORM) : (sign ? VK_FORMAT_R16G16B16A16_SSCALED : VK_FORMAT_R16G16B16A16_USCALED);
    if (a.size == 2) return a.norm ? (sign ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_UNORM) : (sign ? VK_FORMAT_R16G16_SSCALED : VK_FORMAT_R16G16_USCALED);
    return a.norm ? (sign ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_UNORM) : (sign ? VK_FORMAT_R16_SSCALED : VK_FORMAT_R16_USCALED);
  }
  return VK_FORMAT_R32G32B32A32_SFLOAT;
}

static void clearReferenced() {
  for (auto& b : bufs) b.referenced = false;
}

static void reallocBuf(Buf& b, VkDeviceSize n, bool keep) {
  GpuBuf old = b.gpu;
  bool had = old.buffer != VK_NULL_HANDLE;
  allocHostBuf(n, kBufUse, b.gpu);
  if (keep && had && old.map && b.gpu.map) memcpy(b.gpu.map, old.map, (size_t)std::min(old.size, n));
  if (had) { if (b.referenced) retireGpuBuf(old); else destroyGpuBuf(old); }
  b.referenced = false;
  b.dirty = true;
}

// ---------------------------------------------------------------- shaders

static bool isSamplerType(GLenum t) {
  return t == GL_SAMPLER_2D || t == GL_SAMPLER_3D || t == GL_SAMPLER_CUBE || t == GL_SAMPLER_2D_ARRAY ||
         t == GL_SAMPLER_2D_SHADOW || t == GL_SAMPLER_2D_ARRAY_SHADOW || t == GL_SAMPLER_CUBE_SHADOW ||
         t == GL_INT_SAMPLER_2D || t == GL_INT_SAMPLER_3D || t == GL_UNSIGNED_INT_SAMPLER_2D || t == GL_UNSIGNED_INT_SAMPLER_3D ||
         t == GL_UNSIGNED_INT_SAMPLER_2D_ARRAY || t == GL_SAMPLER_2D_RECT || t == GL_SAMPLER_BUFFER;
}
static int glTypeBytes(GLenum t) {
  switch (t) {
    case GL_FLOAT: case GL_INT: case GL_BOOL: case GL_UNSIGNED_INT: return 4;
    case GL_FLOAT_VEC2: case GL_INT_VEC2: case GL_UNSIGNED_INT_VEC2: return 8;
    case GL_FLOAT_VEC3: case GL_INT_VEC3: return 12;
    case GL_FLOAT_VEC4: case GL_INT_VEC4: return 16;
    case GL_FLOAT_MAT4: return 64;
    case GL_FLOAT_MAT3: return 48;
    default: return 0;
  }
}

// Defined in post_layer.cpp so Legacy and Modern get the same LOD 0 data taps.
std::string lod0ForDataTaps(const std::string& s);

static std::string prepGlsl(std::string s, bool vertex) {
  // GLSL does not splice backslash newlines. Packs still end banner lines and macros with one,
  // and glslang then reports a broken comment. Join those lines before anything else.
  {
    std::string joined;
    joined.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
      if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '\n' || s[i + 1] == '\r')) {
        i += (s[i + 1] == '\r' && i + 2 < s.size() && s[i + 2] == '\n') ? 3 : 2;
        continue;
      }
      joined.push_back(s[i++]);
    }
    s.swap(joined);
  }
  auto vpos = s.find("#version");
  if (vpos != std::string::npos) {
    auto eol = s.find('\n', vpos);
    if (eol == std::string::npos) eol = s.size();
    // Vulkan layouts (set, binding) need 450. Packs arrive as 120 or 330.
    s.replace(vpos, eol - vpos, "#version 450");
  }
  for (size_t i = 0; (i = s.find("gl_VertexID", i)) != std::string::npos; ) {
    bool left = i == 0 || !(isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_');
    bool right = i + 11 >= s.size() || !(isalnum((unsigned char)s[i + 11]) || s[i + 11] == '_');
    if (left && right) { s.replace(i, 11, "gl_VertexIndex"); i += 14; }
    else i += 11;
  }
  if (!vertex) return lod0ForDataTaps(s);
  if (std::getenv("DQ_VK_NOZ")) return s;
  std::string out; out.reserve(s.size() + 80);
  for (size_t pos = 0; pos < s.size(); ) {
    size_t i = s.find("void", pos);
    if (i == std::string::npos) { out += s.substr(pos); break; }
    out.append(s, pos, i - pos);
    size_t j = i + 4;
    while (j < s.size() && isspace((unsigned char)s[j])) j++;
    if (s.compare(j, 4, "main") == 0) {
      size_t k = j + 4;
      while (k < s.size() && isspace((unsigned char)s[k])) k++;
      if (k < s.size() && s[k] == '(') { out += "void dq_main"; pos = j + 4; continue; }
    }
    out += "void";
    pos = i + 4;
  }
  out += "\nvoid main(){dq_main();gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;}\n";
  return out;
}

static void setupShader(glslang::TShader& sh, EShLanguage stage, bool relaxed) {
  sh.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
  sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
  sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
  if (relaxed) sh.setEnvInputVulkanRulesRelaxed();
  sh.setAutoMapBindings(true);
  sh.setAutoMapLocations(true);
  sh.setShiftBinding(glslang::EResTexture, 1);
  sh.setShiftBinding(glslang::EResSampler, 1);
  if (relaxed) {
    sh.setGlobalUniformBlockName("DQBlock");
    sh.setGlobalUniformSet(0);
    sh.setGlobalUniformBinding(0);
  }
}

static bool parseOne(const std::string& src, EShLanguage stage, std::string& log) {
  if (!glslangOn) { glslang::InitializeProcess(); glslangOn = true; }
  const char* p = src.c_str();
  glslang::TShader sh(stage);
  sh.setStrings(&p, 1);
  setupShader(sh, stage, true);
  EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
  bool ok = sh.parse(GetDefaultResources(), 450, ENoProfile, false, false, msg);
  if (!ok) log = sh.getInfoLog() ? sh.getInfoLog() : "compile failed";
  return ok;
}

static void destroyProgGpu(Prog& p) {
  retireGpu({.mod = p.modVS});
  retireGpu({.mod = p.modFS});
  retireGpu({.layout = p.pipeLayout});
  retireGpu({.dsl = p.dsl});
  p.modVS = p.modFS = VK_NULL_HANDLE;
  p.pipeLayout = VK_NULL_HANDLE;
  p.dsl = VK_NULL_HANDLE;
}

static VkShaderModule makeModule(const std::vector<uint32_t>& spv) {
  if (spv.empty()) return VK_NULL_HANDLE;
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mi.codeSize = spv.size() * 4;
  mi.pCode = spv.data();
  VkShaderModule m = VK_NULL_HANDLE;
  if (vkFail(vkCreateShaderModule(device, &mi, nullptr, &m), "shader module")) return VK_NULL_HANDLE;
  return m;
}

struct SampDecl { std::string name; GLenum type; int binding; };
static GLenum samplerEnum(const std::string& t) {
  if (t == "sampler2DArray") return GL_SAMPLER_2D_ARRAY;
  if (t == "sampler2DArrayShadow") return GL_SAMPLER_2D_ARRAY_SHADOW;
  if (t == "sampler3D") return GL_SAMPLER_3D;
  if (t == "samplerCube") return GL_SAMPLER_CUBE;
  if (t == "sampler2DShadow") return GL_SAMPLER_2D_SHADOW;
  return GL_SAMPLER_2D;
}
static std::vector<SampDecl> collectSamplers(const std::string& src) {
  std::vector<SampDecl> out;
  for (size_t i = 0; i < src.size(); ) {
    size_t p = src.find("uniform", i);
    if (p == std::string::npos) break;
    size_t j = p + 7;
    while (j < src.size() && isspace((unsigned char)src[j])) j++;
    if (src.compare(j, 7, "sampler") != 0) { i = p + 7; continue; }
    size_t k = j;
    while (k < src.size() && (isalnum((unsigned char)src[k]) || src[k] == '_')) k++;
    std::string ty = src.substr(j, k - j);
    while (k < src.size() && isspace((unsigned char)src[k])) k++;
    size_t ns = k;
    while (k < src.size() && (isalnum((unsigned char)src[k]) || src[k] == '_')) k++;
    if (ns == k) { i = p + 7; continue; }
    SampDecl d; d.name = src.substr(ns, k - ns); d.type = samplerEnum(ty); d.binding = 0;
    bool seen = false;
    for (auto& e : out) if (e.name == d.name) seen = true;
    if (!seen) out.push_back(d);
    i = k;
  }
  return out;
}
static void rewriteSamplers(std::string& s, const std::vector<SampDecl>& decls) {
  std::string out; size_t i = 0;
  while (i < s.size()) {
    size_t p = s.find("uniform", i);
    if (p == std::string::npos) { out += s.substr(i); break; }
    size_t j = p + 7;
    while (j < s.size() && isspace((unsigned char)s[j])) j++;
    if (s.compare(j, 7, "sampler") != 0) { out.append(s, i, p + 7 - i); i = p + 7; continue; }
    size_t k = j;
    while (k < s.size() && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
    while (k < s.size() && isspace((unsigned char)s[k])) k++;
    size_t ns = k;
    while (k < s.size() && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
    std::string name = s.substr(ns, k - ns);
    bool laid = false;
    if (p > 0) {
      size_t b = s.rfind("layout", p);
      if (b != std::string::npos && p - b < 64 && s.find(';', b) > p) laid = true;
    }
    out.append(s, i, p - i);
    if (!laid) for (auto& d : decls) if (d.name == name) { out += "layout(set=0, binding=" + std::to_string(d.binding) + ") "; break; }
    out.append(s, p, k - p);
    i = k;
  }
  s.swap(out);
}
// Enough of a #if expression for shader packs: defined(name), comparisons, && and ||.
// A condition that only kept the first name turned `defined(A) || B == 4` into `defined(A)`,
// so a later use of a variable stayed while its declaration was dropped.
static int ifEval(const std::unordered_map<std::string, int>& defined, const std::string& d, size_t p) {
  struct P {
    const std::unordered_map<std::string, int>& defined;
    const std::string& d;
    size_t p;
    void skip() { while (p < d.size() && std::isspace((unsigned char)d[p])) p++; }
    std::string word() {
      skip();
      size_t b = p;
      while (p < d.size() && (std::isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
      return d.substr(b, p - b);
    }
    int primary() {
      skip();
      if (p >= d.size()) return 0;
      if (d[p] == '(') { p++; int v = orExpr(); skip(); if (p < d.size() && d[p] == ')') p++; return v; }
      if (d[p] == '-' ) { p++; return -unary(); }
      if (d[p] == '+' ) { p++; return unary(); }
      if (std::isdigit((unsigned char)d[p])) {
        int n = std::atoi(d.c_str() + p);
        while (p < d.size() && std::isdigit((unsigned char)d[p])) p++;
        return n;
      }
      std::string name = word();
      if (name == "defined") {
        skip();
        bool paren = p < d.size() && d[p] == '(';
        if (paren) p++;
        name = word();
        if (paren) { skip(); if (p < d.size() && d[p] == ')') p++; }
        return defined.count(name) ? 1 : 0;
      }
      auto it = defined.find(name);
      return it == defined.end() ? 0 : it->second;
    }
    int unary() { skip(); if (p < d.size() && d[p] == '!') { p++; return !unary(); } return primary(); }
    int cmp() {
      int v = unary();
      skip();
      auto op2 = [&](char a, char b) { return p + 1 < d.size() && d[p] == a && d[p + 1] == b; };
      if (op2('=', '=')) { p += 2; return v == unary(); }
      if (op2('!', '=')) { p += 2; return v != unary(); }
      if (op2('>', '=')) { p += 2; return v >= unary(); }
      if (op2('<', '=')) { p += 2; return v <= unary(); }
      if (p < d.size() && d[p] == '>') { p++; return v > unary(); }
      if (p < d.size() && d[p] == '<') { p++; return v < unary(); }
      return v;
    }
    int andExpr() {
      int v = cmp();
      skip();
      if (p + 1 < d.size() && d[p] == '&' && d[p + 1] == '&') {
        p += 2;
        int r = andExpr();
        return (v && r) ? 1 : 0;
      }
      return v ? 1 : 0;
    }
    int orExpr() {
      int v = andExpr();
      skip();
      if (p + 1 < d.size() && d[p] == '|' && d[p + 1] == '|') {
        p += 2;
        int r = orExpr();
        return (v || r) ? 1 : 0;
      }
      return v ? 1 : 0;
    }
  };
  return P{defined, d, p}.orExpr();
}
static void resolveIfdefs(std::string& s) {
  std::unordered_map<std::string, int> defined;
  struct Frame { bool parent; bool taken; };
  std::vector<Frame> stack;
  bool active = true;
  std::string out;
  size_t i = 0;
  auto word = [](const std::string& d, size_t& p) {
    size_t b = p;
    while (p < d.size() && (isalnum((unsigned char)d[p]) || d[p] == '_')) p++;
    return d.substr(b, p - b);
  };
  auto evalCond = [&](const std::string& d, size_t p, const std::string& kw) {
    if (kw == "if" || kw == "elif") return ifEval(defined, d, p) != 0;
    std::string name = word(d, p);
    return (defined.count(name) != 0) == (kw == "ifdef");
  };
  while (i < s.size()) {
    size_t eol = s.find('\n', i);
    if (eol == std::string::npos) eol = s.size();
    std::string line = s.substr(i, eol - i);
    i = eol < s.size() ? eol + 1 : eol;
    size_t h = 0;
    while (h < line.size() && isspace((unsigned char)line[h])) h++;
    if (h < line.size() && line[h] == '#') {
      std::string d = line.substr(h + 1);
      size_t p = 0;
      while (p < d.size() && isspace((unsigned char)d[p])) p++;
      std::string kw = word(d, p);
      while (p < d.size() && isspace((unsigned char)d[p])) p++;
      if (kw == "define" && active) {
        std::string name = word(d, p);
        // glslang rejects a second #define with a different body. Iris lets the later
        // one win, so drop the previous body before writing this one.
        if (!name.empty() && defined.count(name)) out += "#undef " + name + "\n";
        // Function-like macros stay in the text for glslang. Object-like ones feed #if.
        if (p < d.size() && d[p] == '(') {
          if (!name.empty()) defined[name] = 1;
          out += line + "\n";
          continue;
        }
        while (p < d.size() && isspace((unsigned char)d[p])) p++;
        int val = 1;
        if (p < d.size() && (isdigit((unsigned char)d[p]) || d[p] == '-')) val = std::atoi(d.c_str() + p);
        else {
          std::string other = word(d, p);
          auto it = defined.find(other);
          if (it != defined.end()) val = it->second;
        }
        if (!name.empty()) defined[name] = val;
        out += line + "\n";
        continue;
      }
      // A skipped #undef left NETHER_STORM defined in the overworld, and that storm
      // mixes the picture toward white. Only an active undef takes effect, and the
      // directive has to stay in the text or glslang keeps the old body.
      if (kw == "undef" && active) {
        std::string name = word(d, p);
        if (!name.empty()) defined.erase(name);
        out += line + "\n";
        continue;
      }
      if (kw == "ifdef" || kw == "ifndef" || kw == "if") {
        bool cond = evalCond(d, p, kw);
        stack.push_back({active, false});
        active = active && cond;
        if (active) stack.back().taken = true;
        continue;
      }
      if (kw == "elif" && !stack.empty()) {
        bool cond = evalCond(d, p, kw);
        active = stack.back().parent && !stack.back().taken && cond;
        if (active) stack.back().taken = true;
        continue;
      }
      if (kw == "else" && !stack.empty()) {
        active = stack.back().parent && !stack.back().taken;
        if (active) stack.back().taken = true;
        continue;
      }
      if (kw == "endif" && !stack.empty()) { active = stack.back().parent; stack.pop_back(); continue; }
    }
    if (active) out += line + "\n";
  }
  s.swap(out);
}
// Comments, strings, and preprocessor lines are not braces. A `{` inside a pack comment
// used to swallow the rest of the file, including main(), and the strip then deleted it.
static bool skipShaderTrivia(const std::string& s, size_t& i) {
  if (i >= s.size()) return false;
  if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
    while (i < s.size() && s[i] != '\n') i++;
    return true;
  }
  if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
    i += 2;
    while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
    if (i + 1 < s.size()) i += 2;
    return true;
  }
  if (s[i] == '"' || s[i] == '\'') {
    char q = s[i++];
    while (i < s.size() && s[i] != q && s[i] != '\n') {
      if (s[i] == '\\' && i + 1 < s.size()) i += 2;
      else i++;
    }
    if (i < s.size() && s[i] == q) i++;
    return true;
  }
  size_t line = i;
  while (line > 0 && s[line - 1] != '\n') line--;
  size_t h = line;
  while (h < s.size() && (s[h] == ' ' || s[h] == '\t')) h++;
  if (i == h && h < s.size() && s[h] == '#') {
    while (i < s.size() && s[i] != '\n') {
      if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '\n' || s[i + 1] == '\r')) i += 2;
      else i++;
    }
    return true;
  }
  return false;
}
static bool hasShaderMain(const std::string& s) {
  auto hit = [&](const char* name) {
    const size_t n = std::strlen(name);
    for (size_t p = 0; (p = s.find(name, p)) != std::string::npos; p += n) {
      if (p > 0 && (std::isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_')) continue;
      if (p + n < s.size() && (std::isalnum((unsigned char)s[p + n]) || s[p + n] == '_')) continue;
      size_t b = p;
      while (b > 0 && std::isspace((unsigned char)s[b - 1])) b--;
      if (b < 4 || s.compare(b - 4, 4, "void") != 0) continue;
      if (b > 4 && (std::isalnum((unsigned char)s[b - 5]) || s[b - 5] == '_')) continue;
      size_t a = p + n;
      while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
      if (a < s.size() && s[a] == '(') return true;
    }
    return false;
  };
  // prepGlsl renames the vertex entry point to dq_main. A strip that deletes it
  // has to be undone, or the vertex stage links with no entry point.
  return hit("main") || hit("dq_main");
}
static void stripUnusedFunctions(std::string& s) {
  struct Fn { size_t begin, end; std::string name; };
  std::vector<Fn> fns;
  bool inBlock = false;
  for (size_t i = 0; i < s.size(); ) {
    if (inBlock) {
      if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') { inBlock = false; i += 2; continue; }
      i++;
      continue;
    }
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') { inBlock = true; i += 2; continue; }
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      while (i < s.size() && s[i] != '\n') i++;
      continue;
    }
    bool lineStart = i == 0 || s[i - 1] == '\n';
    if (!lineStart) { i++; continue; }
    size_t line = i;
    while (line < s.size() && (s[line] == ' ' || s[line] == '\t')) line++;
    if (line < s.size() && s[line] == '#') {
      while (i < s.size() && s[i] != '\n') i++;
      continue;
    }
    auto wordAt = [&](size_t at, size_t& end) {
      while (at < s.size() && isspace((unsigned char)s[at])) at++;
      end = at;
      while (end < s.size() && (isalnum((unsigned char)s[end]) || s[end] == '_')) end++;
      return s.substr(at, end - at);
    };
    size_t typeEnd = line;
    std::string ty = wordAt(line, typeEnd);
    // Interpolation and memory qualifiers may sit in front of the return type (`flat vec3 f(`).
    auto qual = [](const std::string& w) {
      return w == "flat" || w == "smooth" || w == "noperspective" || w == "centroid" || w == "invariant" || w == "precise" ||
             w == "const" || w == "highp" || w == "mediump" || w == "lowp" || w == "coherent" || w == "volatile" ||
             w == "restrict" || w == "readonly" || w == "writeonly";
    };
    // Keywords are not return types. `return raytrace(` must not become a function, or the real
    // raytrace is never kept and its callees (handleVoxel) are stripped.
    auto keyword = [](const std::string& w) {
      return w == "if" || w == "while" || w == "for" || w == "do" || w == "switch" || w == "case" || w == "default" ||
             w == "return" || w == "break" || w == "continue" || w == "discard" || w == "else" || w == "struct" ||
             w == "layout" || w == "uniform" || w == "buffer" || w == "shared" || w == "in" || w == "out" || w == "inout" ||
             w == "attribute" || w == "varying" || w == "precision" || w == "subroutine" || w == "patch" ||
             w == "true" || w == "false" || w == "sample";
    };
    for (int q = 0; q < 6 && qual(ty); q++) ty = wordAt(typeEnd, typeEnd);
    // A user struct (`ray_hit_t raytrace`) is a real function. Leaving it untracked dropped handleVoxel.
    if (ty.empty() || keyword(ty) || qual(ty)) { i = s.find('\n', i); if (i == std::string::npos) break; i++; continue; }
    size_t j = typeEnd;
    while (j < s.size() && isspace((unsigned char)s[j])) j++;
    size_t ns = j;
    while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
    if (ns == j || j >= s.size() || s[j] != '(') { i = s.find('\n', i); if (i == std::string::npos) break; i++; continue; }
    std::string name = s.substr(ns, j - ns);
    int paren = 0; size_t k = j; bool saw = false;
    while (k < s.size()) {
      if (skipShaderTrivia(s, k)) continue;
      if (s[k] == '(') { paren++; saw = true; k++; continue; }
      if (s[k] == ')') { paren--; k++; if (saw && paren == 0) break; continue; }
      if (s[k] == ';' && paren == 0) { k = std::string::npos; break; }
      k++;
    }
    if (k == std::string::npos || k >= s.size()) { i = s.find('\n', i); if (i == std::string::npos) break; i++; continue; }
    while (k < s.size()) {
      if (skipShaderTrivia(s, k)) continue;
      if (!isspace((unsigned char)s[k])) break;
      k++;
    }
    if (k >= s.size() || s[k] != '{') { i = s.find('\n', i); if (i == std::string::npos) break; i++; continue; }
    int brace = 0; size_t e = k;
    while (e < s.size()) {
      if (skipShaderTrivia(s, e)) continue;
      if (s[e] == '{') { brace++; e++; continue; }
      if (s[e] == '}') { brace--; e++; if (brace == 0) break; continue; }
      e++;
    }
    fns.push_back({line, e, name});
    i = e;
  }
  if (fns.empty()) return;
  auto bodyCalls = [&](size_t b, size_t e, const std::string& name) {
    std::string key = name + "(";
    if (b >= s.size() || b >= e) return false;
    for (size_t p = b; p < e && (p = s.find(key, p)) != std::string::npos && p < e; p += key.size()) {
      if (p > b && (isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_')) continue;
      return true;
    }
    return false;
  };
  std::vector<char> keep(fns.size(), 0);
  for (size_t i = 0; i < fns.size(); i++) if (fns[i].name == "main" || fns[i].name == "dq_main" || fns[i].name == "dqPackMain") keep[i] = 1;
  // A macro such as Bayer8() calls Bayer2(), but that call is not inside a function
  // body, so the function looked unused and was deleted. Calls outside every function
  // are roots. The signature itself sits inside the function's span and is skipped.
  {
    std::vector<char> inFn(s.size(), 0);
    for (const Fn& fn : fns)
      for (size_t p = fn.begin; p < fn.end && p < inFn.size(); ++p) inFn[p] = 1;
    for (size_t fi = 0; fi < fns.size(); fi++) {
      if (keep[fi]) continue;
      const std::string key = fns[fi].name + "(";
      for (size_t p = 0; (p = s.find(key, p)) != std::string::npos; p += key.size()) {
        if (p < inFn.size() && inFn[p]) continue;
        if (p > 0 && (std::isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_')) continue;
        keep[fi] = 1;
        break;
      }
    }
  }
  // `#define tonemap tonemap_lottes` then `tonemap(color)` calls the function, but the
  // source never contains `tonemap_lottes(`. Only a macro that is actually invoked counts.
  // Keeping every name in every #define pulled dead image code back in and the draw faulted.
  {
    struct Mac { std::string name, repl; size_t lo, hi; };
    std::vector<Mac> macs;
    for (size_t i = 0; i < s.size(); ) {
      size_t eol = s.find('\n', i);
      if (eol == std::string::npos) eol = s.size();
      size_t h = i;
      while (h < eol && (s[h] == ' ' || s[h] == '\t')) h++;
      if (h < eol && s[h] == '#') {
        std::string line = s.substr(h, eol - h);
        auto cmt = line.find("//");
        if (cmt != std::string::npos) line.resize(cmt);
        auto blk = line.find("/*");
        if (blk != std::string::npos) line.resize(blk);
        size_t p = 0;
        while (p < line.size() && (line[p] == '#' || std::isspace((unsigned char)line[p]))) p++;
        if (line.compare(p, 6, "define") == 0 && (p + 6 >= line.size() || std::isspace((unsigned char)line[p + 6]))) {
          p += 6;
          while (p < line.size() && std::isspace((unsigned char)line[p])) p++;
          size_t ns = p;
          while (p < line.size() && (std::isalnum((unsigned char)line[p]) || line[p] == '_')) p++;
          if (p > ns) {
            std::string name = line.substr(ns, p - ns);
            if (p < line.size() && line[p] == '(') {
              int d = 0;
              do {
                if (line[p] == '(') d++;
                else if (line[p] == ')') d--;
                p++;
              } while (p < line.size() && d > 0);
            }
            while (p < line.size() && std::isspace((unsigned char)line[p])) p++;
            macs.push_back({name, line.substr(p), i, eol});
          }
        }
      }
      i = eol < s.size() ? eol + 1 : eol;
    }
    auto invoked = [&](const Mac& m) {
      const std::string key = m.name + "(";
      for (size_t p = 0; (p = s.find(key, p)) != std::string::npos; p += key.size()) {
        if (p >= m.lo && p < m.hi) continue;
        if (p > 0 && (std::isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_')) continue;
        return true;
      }
      return false;
    };
    std::unordered_set<std::string> seen;
    std::function<void(const std::string&)> walk = [&](const std::string& repl) {
      for (size_t i = 0; i < repl.size(); ) {
        if (!(std::isalpha((unsigned char)repl[i]) || repl[i] == '_')) { i++; continue; }
        size_t j = i;
        while (j < repl.size() && (std::isalnum((unsigned char)repl[j]) || repl[j] == '_')) j++;
        std::string w = repl.substr(i, j - i);
        i = j;
        if (!seen.insert(w).second) continue;
        for (size_t fi = 0; fi < fns.size(); fi++) if (fns[fi].name == w) keep[fi] = 1;
        for (const Mac& o : macs) if (o.name == w) walk(o.repl);
      }
    };
    for (const Mac& m : macs) if (invoked(m)) walk(m.repl);
  }
  bool grew = true;
  while (grew) {
    grew = false;
    for (size_t i = 0; i < fns.size(); i++) if (!keep[i]) {
      for (size_t k = 0; k < fns.size(); k++) if (keep[k] && bodyCalls(fns[k].begin, fns[k].end, fns[i].name)) {
        keep[i] = 1; grew = true; break;
      }
    }
  }
  std::string out; size_t cursor = 0;
  for (size_t i = 0; i < fns.size(); i++) {
    if (keep[i]) continue;
    out.append(s, cursor, fns[i].begin - cursor);
    cursor = fns[i].end;
  }
  out.append(s, cursor, std::string::npos);
  s.swap(out);
  // Drop sampler declarations nothing samples. Intel's driver faults while compiling a pipeline
  // whose SPIR-V still declares those images.
  std::string kept;
  for (size_t i = 0; i < s.size(); ) {
    size_t eol = s.find('\n', i);
    if (eol == std::string::npos) eol = s.size();
    std::string line = s.substr(i, eol - i);
    size_t u = line.find("uniform");
    bool drop = false;
    size_t kind = std::string::npos;
    if (u != std::string::npos) {
      if (line.find("sampler") != std::string::npos) kind = line.find("sampler");
      else if (line.find("image2D") != std::string::npos || line.find("image3D") != std::string::npos ||
               line.find("imageCube") != std::string::npos || line.find("imageBuffer") != std::string::npos)
        kind = line.find("image");
    }
    if (kind != std::string::npos) {
      size_t j = kind;
      while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
      while (j < line.size() && isspace((unsigned char)line[j])) j++;
      size_t ns = j;
      while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
      std::string name = line.substr(ns, j - ns);
      if (!name.empty()) {
        int hits = 0;
        for (size_t p = 0; (p = s.find(name, p)) != std::string::npos; p += name.size()) {
          bool left = p > 0 && (isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_');
          bool right = p + name.size() < s.size() && (isalnum((unsigned char)s[p + name.size()]) || s[p + name.size()] == '_');
          if (!left && !right) hits++;
        }
        drop = hits <= 1;
      }
    }
    if (!drop) kept += line + (eol < s.size() ? "\n" : "");
    i = eol < s.size() ? eol + 1 : eol;
  }
  s.swap(kept);
}
static std::vector<std::string> looseUniforms(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    size_t eol = s.find('\n', i);
    if (eol == std::string::npos) eol = s.size();
    std::string line = s.substr(i, eol - i);
    i = eol < s.size() ? eol + 1 : eol;
    size_t h = 0;
    while (h < line.size() && isspace((unsigned char)line[h])) h++;
    if (line.compare(h, 8, "uniform ") != 0) continue;
    if (line.find("sampler") != std::string::npos || line.find('{') != std::string::npos) continue;
    std::string decl = line.substr(h + 8);
    while (!decl.empty() && isspace((unsigned char)decl.back())) decl.pop_back();
    if (!decl.empty()) out.push_back(decl);
  }
  return out;
}
static void stripLooseUniforms(std::string& s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    size_t eol = s.find('\n', i);
    if (eol == std::string::npos) eol = s.size();
    std::string line = s.substr(i, eol - i);
    size_t h = 0;
    while (h < line.size() && isspace((unsigned char)line[h])) h++;
    bool drop = line.compare(h, 8, "uniform ") == 0 && line.find("sampler") == std::string::npos && line.find('{') == std::string::npos;
    if (!drop) out += line + "\n";
    i = eol < s.size() ? eol + 1 : eol;
  }
  s.swap(out);
}
static bool ioWord(const std::string& s, size_t i, const char* w) {
  size_t n = std::strlen(w);
  if (i + n > s.size() || s.compare(i, n, w) != 0) return false;
  auto idch = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
  if (i > 0 && idch((unsigned char)s[i - 1])) return false;
  if (i + n < s.size() && idch((unsigned char)s[i + n])) return false;
  return true;
}

// Vulkan's shader interface requires a Location on every user input and output.
// Varyings are one interface, so the same name gets the same location in both stages.
// A location the shader already wrote is left alone, and a new one takes the next free number.
// Handing out 0, 1, 2 in order collided with a pack that had already used one of those.
static int layoutNumber(const std::string& line) {
  auto p = line.find("location");
  if (p == std::string::npos) return -1;
  auto eq = line.find('=', p);
  if (eq == std::string::npos) return -1;
  return std::atoi(line.c_str() + eq + 1);
}
static bool ioDecl(const std::string& line, const char*& kw, size_t& kwAt) {
  size_t h = 0;
  while (h < line.size() && std::isspace((unsigned char)line[h])) h++;
  // a comment can contain the word "in" and a semicolon ("a torch in the hand; ...") and is not an input
  if (h >= line.size() || line[h] == '#' || line[h] == '/' || line.find("uniform") != std::string::npos || line.find(';') == std::string::npos) return false;
  kw = nullptr;
  kwAt = std::string::npos;
  int depth = 0;
  bool inStr = false;
  for (size_t p = h; p < line.size(); ++p) {
    // "ivec2 charPos; // ... in the text" is a struct member. The word in the
    // comment is not an input, and a location on it is illegal inside the struct.
    if (!inStr && line[p] == '/' && p + 1 < line.size() && line[p + 1] == '/') break;
    if (!inStr && line[p] == '/' && p + 1 < line.size() && line[p + 1] == '*') {
      p += 2;
      while (p + 1 < line.size() && !(line[p] == '*' && line[p + 1] == '/')) p++;
      if (p + 1 < line.size()) p++;
      continue;
    }
    if (line[p] == '"') { inStr = !inStr; continue; }
    if (inStr) continue;
    if (line[p] == '(') { depth++; continue; }
    if (line[p] == ')') { if (depth) depth--; continue; }
    if (depth) continue;
    if (ioWord(line, p, "out")) { kw = "out"; kwAt = p; break; }
    if (ioWord(line, p, "in")) { kw = "in"; kwAt = p; break; }
  }
  return kw != nullptr;
}
// `flat out vec3 upVec, sunVec;` is two outputs. One location on that line is applied to both,
// which glslang rejects as an overlap. Split them so each name gets its own location.
static std::vector<std::string> splitIoDecls(const std::string& line, size_t kwAt) {
  size_t semi = line.find(';', kwAt);
  if (semi == std::string::npos) return {line};
  std::string indent = line.substr(0, line.find_first_not_of(" \t"));
  std::string body = line.substr(kwAt, semi - kwAt);
  std::vector<std::string> parts;
  std::string cur;
  int depth = 0;
  for (char c : body) {
    if (c == '(') depth++;
    else if (c == ')' && depth) depth--;
    if (c == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  parts.push_back(cur);
  if (parts.size() < 2) return {line};
  auto prefixOf = [](const std::string& p) {
    size_t e = p.size();
    while (e && std::isspace((unsigned char)p[e - 1])) e--;
    while (e && p[e - 1] == ']') {
      size_t b = p.rfind('[', e - 1);
      if (b == std::string::npos) break;
      e = b;
      while (e && std::isspace((unsigned char)p[e - 1])) e--;
    }
    size_t b = e;
    while (b && (std::isalnum((unsigned char)p[b - 1]) || p[b - 1] == '_')) b--;
    return p.substr(0, b);
  };
  const std::string prefix = prefixOf(parts[0]);
  std::vector<std::string> decls;
  for (size_t i = 0; i < parts.size(); i++) {
    std::string one = (i == 0 ? parts[0] : prefix + parts[i]);
    size_t a = 0;
    while (a < one.size() && std::isspace((unsigned char)one[a])) a++;
    decls.push_back(indent + one.substr(a) + ";");
  }
  return decls;
}
static std::string ioNameAt(const std::string& line, size_t kwAt) {
  size_t semi = line.find(';', kwAt);
  size_t e = semi == std::string::npos ? line.size() : semi;
  while (e > kwAt && std::isspace((unsigned char)line[e - 1])) e--;
  size_t b = e;
  while (b > kwAt && (std::isalnum((unsigned char)line[b - 1]) || line[b - 1] == '_')) b--;
  if (b >= e) return {};
  std::string name = line.substr(b, e - b);
  if (name.compare(0, 3, "gl_") == 0) return {};
  return name;
}
// A user struct varying occupies one location per member. Counting it as one slot
// put the next varying on top of it (Photon fog_params overlapped sky_sh at location 6,
// and clouds_params overlapped the next output at location 9).
static bool structQual(const std::string& w) {
  return w == "const" || w == "in" || w == "out" || w == "flat" || w == "smooth" || w == "noperspective" ||
         w == "centroid" || w == "sample" || w == "highp" || w == "mediump" || w == "lowp" || w == "invariant" ||
         w == "precise" || w == "coherent" || w == "volatile" || w == "restrict" || w == "readonly" || w == "writeonly";
}
static int primLocationCols(const std::string& ty) {
  if (ty == "mat4" || ty == "mat4x4" || ty == "mat4x3" || ty == "mat4x2") return 4;
  if (ty == "mat3" || ty == "mat3x4" || ty == "mat3x3" || ty == "mat3x2") return 3;
  if (ty == "mat2" || ty == "mat2x4" || ty == "mat2x3" || ty == "mat2x2") return 2;
  if (ty == "dmat4" || ty == "dmat4x4" || ty == "dmat4x3" || ty == "dmat4x2") return 8;
  if (ty == "dmat3" || ty == "dmat3x4" || ty == "dmat3x3" || ty == "dmat3x2" || ty == "dmat2x4" || ty == "dmat2x3") return 6;
  if (ty == "dmat2" || ty == "dmat2x2" || ty == "dvec3" || ty == "dvec4" || ty == "double") return 2;
  if (ty == "float" || ty == "int" || ty == "uint" || ty == "bool" || ty == "vec2" || ty == "vec3" || ty == "vec4" ||
      ty == "ivec2" || ty == "ivec3" || ty == "ivec4" || ty == "uvec2" || ty == "uvec3" || ty == "uvec4" ||
      ty == "bvec2" || ty == "bvec3" || ty == "bvec4" || ty == "dvec2")
    return 1;
  return -1;
}
static void collectStructSpans(const std::string& src, std::unordered_map<std::string, int>& spans) {
  std::unordered_map<std::string, std::vector<std::string>> members;
  for (size_t i = 0; i < src.size(); ) {
    if (skipShaderTrivia(src, i)) continue;
    if (!ioWord(src, i, "struct")) { i++; continue; }
    i += 6;
    while (i < src.size() && std::isspace((unsigned char)src[i])) i++;
    size_t ns = i;
    while (i < src.size() && (std::isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
    if (ns == i) continue;
    std::string name = src.substr(ns, i - ns);
    while (i < src.size() && std::isspace((unsigned char)src[i])) i++;
    if (i >= src.size() || src[i] != '{') continue;
    i++;
    std::string body;
    int depth = 1;
    while (i < src.size() && depth) {
      if (skipShaderTrivia(src, i)) continue;
      char c = src[i];
      if (c == '{') depth++;
      else if (c == '}') {
        depth--;
        if (!depth) { i++; break; }
      }
      body.push_back(c);
      i++;
    }
    std::vector<std::string> mem;
    std::string cur;
    int pdepth = 0;
    for (char c : body) {
      if (c == '(') pdepth++;
      else if (c == ')' && pdepth) pdepth--;
      if (c == ';' && pdepth == 0) { mem.push_back(cur); cur.clear(); }
      else cur.push_back(c);
    }
    members[name] = std::move(mem);
  }
  std::unordered_map<std::string, int> memo;
  std::unordered_set<std::string> visiting;
  std::function<int(const std::string&)> spanOf = [&](const std::string& name) -> int {
    auto known = memo.find(name);
    if (known != memo.end()) return known->second;
    auto body = members.find(name);
    if (body == members.end()) return 1;
    if (!visiting.insert(name).second) return 1;
    int total = 0;
    for (const std::string& mem : body->second) {
      struct Tok { bool bracket; int n; std::string w; };
      std::vector<Tok> toks;
      for (size_t k = 0; k < mem.size(); ) {
        if (mem[k] == '/' && k + 1 < mem.size() && mem[k + 1] == '/') break;
        if (std::isspace((unsigned char)mem[k])) { k++; continue; }
        if (mem[k] == '[') {
          int n = std::atoi(mem.c_str() + k + 1);
          if (n < 1) n = 1;
          toks.push_back({true, n, {}});
          while (k < mem.size() && mem[k] != ']') k++;
          if (k < mem.size()) k++;
          continue;
        }
        if (std::isalpha((unsigned char)mem[k]) || mem[k] == '_') {
          size_t j = k;
          while (j < mem.size() && (std::isalnum((unsigned char)mem[j]) || mem[j] == '_')) j++;
          toks.push_back({false, 0, mem.substr(k, j - k)});
          k = j;
          continue;
        }
        k++;
      }
      size_t t = 0;
      while (t < toks.size() && !toks[t].bracket && structQual(toks[t].w)) t++;
      if (t >= toks.size() || toks[t].bracket) continue;
      int cols = primLocationCols(toks[t].w);
      if (cols < 0) cols = spanOf(toks[t].w);
      if (cols < 1) cols = 1;
      t++;
      while (t < toks.size() && toks[t].bracket) { cols *= toks[t].n; t++; }
      int names = 0;
      while (t < toks.size()) {
        if (toks[t].bracket) { t++; continue; }
        t++;
        names++;
        int arr = 1;
        while (t < toks.size() && toks[t].bracket) { arr *= toks[t].n; t++; }
        total += cols * arr;
      }
      if (!names) total += cols;
    }
    visiting.erase(name);
    if (total < 1) total = 1;
    memo[name] = total;
    return total;
  };
  for (const auto& kv : members) spans[kv.first] = spanOf(kv.first);
}
// `layout(` / `location = 0` / `) out vec4 name` is one declaration. Putting another
// layout in front of the `)` lands inside the still-open parenthesis.
static bool layoutCloser(const std::string& line) {
  size_t h = 0;
  while (h < line.size() && std::isspace((unsigned char)line[h])) h++;
  return h < line.size() && line[h] == ')';
}
// A mat4 varying occupies one location per column. Giving the next matrix location+1
// overlaps (Rethinking Voxels prepare: in mat4 unprojectionMatrix, projectionMatrix).
static int ioLocationSpan(const std::string& decl, const std::unordered_map<std::string, int>& structs) {
  std::string bare = decl;
  auto cmt = bare.find("//");
  if (cmt != std::string::npos) bare.resize(cmt);
  size_t bestAt = std::string::npos;
  int bestCols = 1;
  for (const auto& kv : structs) {
    for (size_t p = 0; (p = bare.find(kv.first, p)) != std::string::npos; p += kv.first.size()) {
      bool left = p > 0 && (std::isalnum((unsigned char)bare[p - 1]) || bare[p - 1] == '_');
      bool right = p + kv.first.size() < bare.size() && (std::isalnum((unsigned char)bare[p + kv.first.size()]) || bare[p + kv.first.size()] == '_');
      if (left || right) continue;
      if (p < bestAt) { bestAt = p; bestCols = kv.second; }
      break;
    }
  }
  if (bestAt != std::string::npos) {
    int arr = 1;
    for (size_t b = bestAt; (b = bare.find('[', b)) != std::string::npos; ++b) {
      int n = std::atoi(bare.c_str() + b + 1);
      if (n > 1) arr *= n;
    }
    int span = bestCols * arr;
    if (span < 1) span = 1;
    if (span > 64) span = 64;
    return span;
  }
  auto has = [&](const char* w) {
    const size_t n = std::strlen(w);
    for (size_t p = 0; (p = decl.find(w, p)) != std::string::npos; p += n) {
      bool left = p > 0 && (std::isalnum((unsigned char)decl[p - 1]) || decl[p - 1] == '_');
      bool right = p + n < decl.size() && (std::isalnum((unsigned char)decl[p + n]) || decl[p + n] == '_');
      if (!left && !right) return true;
    }
    return false;
  };
  int cols = 1;
  if (has("dmat4") || has("dmat4x4") || has("dmat4x3") || has("dmat4x2")) cols = 8;
  else if (has("dmat3") || has("dmat3x4") || has("dmat3x3") || has("dmat3x2") || has("dmat2x4") || has("dmat2x3")) cols = 6;
  else if (has("dmat2") || has("dmat2x2") || has("dvec3") || has("dvec4")) cols = 2;
  else if (has("mat4x3") || has("mat4x2") || has("mat4x4") || has("mat4")) cols = 4;
  else if (has("mat3x4") || has("mat3x3") || has("mat3x2") || has("mat3")) cols = 3;
  else if (has("mat2x4") || has("mat2x3") || has("mat2x2") || has("mat2")) cols = 2;
  int arr = 1;
  for (size_t b = 0; (b = decl.find('[', b)) != std::string::npos; ++b) {
    int n = std::atoi(decl.c_str() + b + 1);
    if (n > 1) arr *= n;
  }
  int span = cols * arr;
  if (span < 1) span = 1;
  if (span > 16) span = 16;
  return span;
}
static void assignIoLocations(std::string& vs, std::string& fs) {
  std::unordered_map<std::string, int> structs;
  collectStructSpans(vs, structs);
  collectStructSpans(fs, structs);
  std::unordered_map<std::string, int> vary;
  std::set<int> usedVary, usedAttr, usedColor;
  auto scan = [&](const std::string& s, bool vertex) {
    for (size_t i = 0; i < s.size(); ) {
      size_t eol = s.find('\n', i);
      if (eol == std::string::npos) eol = s.size();
      std::string line = s.substr(i, eol - i);
      i = eol < s.size() ? eol + 1 : eol;
      if (layoutCloser(line) || line.find("layout") == std::string::npos) continue;
      const char* kw = nullptr;
      size_t kwAt = 0;
      if (!ioDecl(line, kw, kwAt)) continue;
      int loc = layoutNumber(line);
      if (loc < 0) continue;
      bool isOut = kw[0] == 'o';
      int span = ioLocationSpan(line, structs);
      auto reserve = [&](std::set<int>& used) { for (int k = 0; k < span; k++) used.insert(loc + k); };
      if (vertex && !isOut) reserve(usedAttr);
      else if (!vertex && isOut) reserve(usedColor);
      else {
        reserve(usedVary);
        std::string name = ioNameAt(line, kwAt);
        if (!name.empty()) vary[name] = loc;
      }
    }
  };
  scan(vs, true);
  scan(fs, false);
  auto allocN = [](std::set<int>& used, int n) {
    int loc = 0;
    for (;;) {
      bool ok = true;
      for (int k = 0; k < n; k++) if (used.count(loc + k)) { ok = false; break; }
      if (ok) break;
      if (++loc > 128) break;
    }
    for (int k = 0; k < n; k++) used.insert(loc + k);
    return loc;
  };
  auto rewrite = [&](std::string& s, bool vertex) {
    std::string out;
    for (size_t i = 0; i < s.size(); ) {
      size_t eol = s.find('\n', i);
      if (eol == std::string::npos) eol = s.size();
      std::string line = s.substr(i, eol - i);
      i = eol < s.size() ? eol + 1 : eol;
      const char* kw = nullptr;
      size_t kwAt = 0;
      if (!layoutCloser(line) && line.find("layout") == std::string::npos && line.find('=') == std::string::npos && ioDecl(line, kw, kwAt)) {
        auto place = [&](std::string decl) {
          const char* dkw = nullptr;
          size_t dAt = 0;
          if (!ioDecl(decl, dkw, dAt)) { out += decl; out += '\n'; return; }
          std::string name = ioNameAt(decl, dAt);
          if (name.empty()) { out += decl; out += '\n'; return; }
          int loc;
          bool isOut = dkw[0] == 'o';
          int span = ioLocationSpan(decl, structs);
          if (vertex && !isOut) loc = allocN(usedAttr, span);
          else if (!vertex && isOut) loc = allocN(usedColor, span);
          else {
            auto it = vary.find(name);
            if (it == vary.end()) loc = vary.emplace(name, allocN(usedVary, span)).first->second;
            else {
              loc = it->second;
              for (int k = 0; k < span; k++) usedVary.insert(loc + k);
            }
          }
          size_t h = 0;
          while (h < decl.size() && std::isspace((unsigned char)decl[h])) h++;
          decl.insert(h, "layout(location=" + std::to_string(loc) + ") ");
          out += decl;
          out += '\n';
        };
        for (const std::string& decl : splitIoDecls(line, kwAt)) place(decl);
      } else {
      out += line;
      out += '\n';
      }
      continue;
    }
    s.swap(out);
  };
  rewrite(vs, true);
  rewrite(fs, false);
}
static std::string declKey(std::string d) {
  auto c = d.find("//");
  if (c != std::string::npos) d.resize(c);
  while (!d.empty() && std::isspace((unsigned char)d.back())) d.pop_back();
  // `uniform float x = 1.0;` is legal at global scope and illegal inside a block.
  auto eq = d.find('=');
  if (eq != std::string::npos) {
    d.resize(eq);
    while (!d.empty() && std::isspace((unsigned char)d.back())) d.pop_back();
    if (!d.empty() && d.back() != ';') d += ';';
  }
  return d;
}
static bool glslTypeWord(const std::string& w) {
  static const char* types[] = {
    "float", "int", "uint", "bool", "double",
    "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4",
    "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4"
  };
  for (const char* t : types) if (w == t) return true;
  return false;
}
static bool glslQualWord(const std::string& w) {
  return w == "highp" || w == "mediump" || w == "lowp" || w == "coherent" || w == "readonly" || w == "writeonly" || w == "restrict" || w == "volatile";
}
// `uniform float viewWidth;` and `uniform float viewWidth, viewHeight;` are legal as two
// global declarations. Inside one block the second viewWidth is a duplicate member.
static void addLooseMembers(std::vector<std::string>& out, std::unordered_set<std::string>& seen, const std::string& d) {
  std::string k = declKey(d);
  size_t i = 0;
  auto skip = [&] { while (i < k.size() && std::isspace((unsigned char)k[i])) i++; };
  auto wordAt = [&](size_t at) {
    size_t e = at;
    while (e < k.size() && (std::isalnum((unsigned char)k[e]) || k[e] == '_')) e++;
    return k.substr(at, e - at);
  };
  std::string type;
  while (true) {
    skip();
    std::string w = wordAt(i);
    if (w.empty()) return;
    if (glslQualWord(w)) { i += w.size(); continue; }
    if (glslTypeWord(w)) {
      if (!type.empty()) type += " ";
      type += w;
      i += w.size();
      continue;
    }
    if (type.empty()) { type = w; i += w.size(); }
    break;
  }
  if (type.empty()) return;
  while (i < k.size()) {
    skip();
    if (i >= k.size() || k[i] == ';') break;
    std::string name = wordAt(i);
    if (name.empty()) break;
    i += name.size();
    skip();
    std::string arr;
    if (i < k.size() && k[i] == '[') {
      size_t b = i;
      int depth = 0;
      do {
        if (k[i] == '[') depth++;
        else if (k[i] == ']') depth--;
        i++;
      } while (i < k.size() && depth > 0);
      arr = k.substr(b, i - b);
    }
    if (seen.insert(name).second) out.push_back(type + " " + name + arr + ";");
    skip();
    if (i < k.size() && k[i] == ',') i++;
    else break;
  }
}
// Desktop GL uses `uniform float cloudHeight = 192.0` when the game never writes it.
// That initializer is illegal in the block, so it is stripped and the zeroed UBO would
// leave the value at 0. Complementary then puts the cloud slab under the camera.
struct UniInit {
  bool integral = false;
  bool matrix = false;
  int rows = 1;
  int cols = 1;
  std::vector<double> v;
};
struct GlslShape {
  bool known = false;
  bool integral = false;
  bool matrix = false;
  int rows = 1;
  int cols = 1;
};
static GlslShape glslShape(const std::string& t) {
  GlslShape s;
  auto set = [&](int rows, int cols, bool integral, bool matrix) {
    s.known = true; s.rows = rows; s.cols = cols; s.integral = integral; s.matrix = matrix;
  };
  if (t == "float" || t == "double") { set(1, 1, false, false); return s; }
  if (t == "int" || t == "uint" || t == "bool") { set(1, 1, true, false); return s; }
  if (t == "vec2") { set(2, 1, false, false); return s; }
  if (t == "vec3") { set(3, 1, false, false); return s; }
  if (t == "vec4") { set(4, 1, false, false); return s; }
  if (t == "ivec2" || t == "uvec2" || t == "bvec2") { set(2, 1, true, false); return s; }
  if (t == "ivec3" || t == "uvec3" || t == "bvec3") { set(3, 1, true, false); return s; }
  if (t == "ivec4" || t == "uvec4" || t == "bvec4") { set(4, 1, true, false); return s; }
  if (t == "mat2" || t == "mat2x2") { set(2, 2, false, true); return s; }
  if (t == "mat3" || t == "mat3x3") { set(3, 3, false, true); return s; }
  if (t == "mat4" || t == "mat4x4") { set(4, 4, false, true); return s; }
  if (t == "mat2x3") { set(3, 2, false, true); return s; }
  if (t == "mat2x4") { set(4, 2, false, true); return s; }
  if (t == "mat3x2") { set(2, 3, false, true); return s; }
  if (t == "mat3x4") { set(4, 3, false, true); return s; }
  if (t == "mat4x2") { set(2, 4, false, true); return s; }
  if (t == "mat4x3") { set(3, 4, false, true); return s; }
  return s;
}
// std140 stores a matrix as an array of column vectors, and an array stride rounds
// up to a vec4. Every column therefore starts 16 bytes after the previous one.
static bool std140Shape(GLenum t, int& rows, int& cols, bool& matrix, bool& integral) {
  rows = 1; cols = 1; matrix = false; integral = false;
  switch (t) {
    case GL_FLOAT: return true;
    case GL_FLOAT_VEC2: rows = 2; return true;
    case GL_FLOAT_VEC3: rows = 3; return true;
    case GL_FLOAT_VEC4: rows = 4; return true;
    case GL_INT: case GL_BOOL: case GL_UNSIGNED_INT: integral = true; return true;
    case GL_INT_VEC2: case GL_UNSIGNED_INT_VEC2: case GL_BOOL_VEC2: rows = 2; integral = true; return true;
    case GL_INT_VEC3: case GL_UNSIGNED_INT_VEC3: case GL_BOOL_VEC3: rows = 3; integral = true; return true;
    case GL_INT_VEC4: case GL_UNSIGNED_INT_VEC4: case GL_BOOL_VEC4: rows = 4; integral = true; return true;
    case GL_FLOAT_MAT2: matrix = true; cols = 2; rows = 2; return true;
    case GL_FLOAT_MAT3: matrix = true; cols = 3; rows = 3; return true;
    case GL_FLOAT_MAT4: matrix = true; cols = 4; rows = 4; return true;
    case GL_FLOAT_MAT2x3: matrix = true; cols = 2; rows = 3; return true;
    case GL_FLOAT_MAT2x4: matrix = true; cols = 2; rows = 4; return true;
    case GL_FLOAT_MAT3x2: matrix = true; cols = 3; rows = 2; return true;
    case GL_FLOAT_MAT3x4: matrix = true; cols = 3; rows = 4; return true;
    case GL_FLOAT_MAT4x2: matrix = true; cols = 4; rows = 2; return true;
    case GL_FLOAT_MAT4x3: matrix = true; cols = 4; rows = 3; return true;
    default: return false;
  }
}
static std::unordered_map<std::string, UniInit> collectUniformDefaults(const std::string& s) {
  struct Val {
    bool ok = false;
    std::vector<double> v;
  };
  struct Parser {
    const std::string& s;
    size_t i = 0;
    std::unordered_map<std::string, UniInit> out;
    void skipComment() {
      if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
        i += 2; while (i < s.size() && s[i] != '\n') i++;
      } else if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
        i += 2;
        while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
        if (i + 1 < s.size()) i += 2;
      }
    }
    void skipWs() {
      for (;;) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
        if (i + 1 < s.size() && s[i] == '/' && (s[i + 1] == '/' || s[i + 1] == '*')) { skipComment(); continue; }
        break;
      }
    }
    bool commentHere() const {
      return i + 1 < s.size() && s[i] == '/' && (s[i + 1] == '/' || s[i + 1] == '*');
    }
    std::string readWord() {
      skipWs();
      if (i >= s.size() || !(std::isalpha((unsigned char)s[i]) || s[i] == '_')) return {};
      size_t b = i;
      while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
      return s.substr(b, i - b);
    }
    void skipBalanced(char open, char close) {
      skipWs();
      if (i >= s.size() || s[i] != open) return;
      int d = 0;
      do {
        if (commentHere()) { skipComment(); continue; }
        if (s[i] == open) d++;
        else if (s[i] == close) d--;
        i++;
      } while (i < s.size() && d > 0);
    }
    bool parseNumber(double& out) {
      skipWs();
      size_t b = i;
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        if (i + 1 >= s.size() || !(std::isdigit((unsigned char)s[i + 1]) || s[i + 1] == '.')) return false;
        i++;
      }
      bool any = false;
      auto digits = [&] {
        bool d = false;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) { d = true; i++; }
        return d;
      };
      if (digits()) any = true;
      if (i < s.size() && s[i] == '.') {
        i++;
        if (digits()) any = true;
      }
      if (!any) { i = b; return false; }
      if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t e = i++;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
        if (!digits()) i = e;
      }
      if (i < s.size() && (s[i] == 'f' || s[i] == 'F')) i++;
      if (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) { i = b; return false; }
      char* end = nullptr;
      out = std::strtod(s.c_str() + b, &end);
      if (end == s.c_str() + b) { i = b; return false; }
      return true;
    }
    Val fromShape(const GlslShape& sh, const std::vector<Val>& args) {
      Val o;
      if (!sh.known) return o;
      int need = sh.matrix ? sh.cols * sh.rows : sh.rows;
      if (sh.matrix && args.size() == 1 && args[0].ok && args[0].v.size() == 1) {
        o.ok = true;
        o.v.assign((size_t)need, 0.0);
        double d = args[0].v[0];
        int n = std::min(sh.cols, sh.rows);
        for (int c = 0; c < n; c++) o.v[(size_t)(c * sh.rows + c)] = d;
        return o;
      }
      std::vector<double> flat;
      for (const Val& a : args) {
        if (!a.ok) return {};
        flat.insert(flat.end(), a.v.begin(), a.v.end());
      }
      if ((int)flat.size() == 1 && need > 1 && !sh.matrix) flat.assign((size_t)need, flat[0]);
      if ((int)flat.size() != need) return {};
      o.ok = true;
      o.v = std::move(flat);
      return o;
    }
    Val parsePrimary() {
      skipWs();
      Val none;
      if (i >= s.size()) return none;
      if (s[i] == '+' || s[i] == '-' || s[i] == '.' || std::isdigit((unsigned char)s[i])) {
        double n = 0;
        if (!parseNumber(n)) return none;
        Val v; v.ok = true; v.v = {n}; return v;
      }
      std::string w = readWord();
      if (w == "true" || w == "false") {
        Val v; v.ok = true; v.v = {w == "true" ? 1.0 : 0.0}; return v;
      }
      if (w.empty()) return none;
      skipWs();
      if (i >= s.size() || s[i] != '(') return none;
      i++;
      std::vector<Val> args;
      skipWs();
      if (i < s.size() && s[i] == ')') { i++; return fromShape(glslShape(w), args); }
      for (;;) {
        Val a = parsePrimary();
        if (!a.ok) return none;
        args.push_back(std::move(a));
        skipWs();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == ')') { i++; break; }
        return none;
      }
      return fromShape(glslShape(w), args);
    }
    void skipJunkToDelim() {
      int pd = 0;
      while (i < s.size()) {
        if (commentHere()) { skipComment(); continue; }
        if (s[i] == '(') pd++;
        else if (s[i] == ')' && pd) pd--;
        else if (pd == 0 && (s[i] == ',' || s[i] == ';')) break;
        i++;
      }
    }
    bool parseUniform() {
      std::string type;
      for (;;) {
        skipWs();
        if (i >= s.size()) return false;
        if (s[i] == '(') { skipBalanced('(', ')'); continue; }
        size_t at = i;
        std::string w = readWord();
        if (w.empty()) return false;
        if (w == "layout") {
          skipWs();
          if (i < s.size() && s[i] == '(') skipBalanced('(', ')');
          continue;
        }
        if (glslQualWord(w)) continue;
        if (glslTypeWord(w)) { type = w; break; }
        i = at;
        return false;
      }
      GlslShape sh = glslShape(type);
      while (i < s.size()) {
        std::string name = readWord();
        if (name.empty()) return false;
        skipWs();
        bool isArray = false;
        if (i < s.size() && s[i] == '[') { isArray = true; skipBalanced('[', ']'); }
        skipWs();
        if (i < s.size() && s[i] == '=') {
          i++;
          Val v = parsePrimary();
          int need = sh.matrix ? sh.cols * sh.rows : sh.rows;
          if (v.ok && !isArray && sh.known && (int)v.v.size() == need && !out.count(name)) {
            UniInit u; u.integral = sh.integral; u.matrix = sh.matrix; u.rows = sh.rows; u.cols = sh.cols; u.v = std::move(v.v);
            out.emplace(name, std::move(u));
          } else if (!v.ok) skipJunkToDelim();
        }
        skipWs();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == ';') { i++; return true; }
        return false;
      }
      return false;
    }
    void run() {
      int depth = 0;
      while (i < s.size()) {
        if (commentHere()) { skipComment(); continue; }
        char c = s[i];
        if (c == '{') { depth++; i++; continue; }
        if (c == '}') { if (depth) depth--; i++; continue; }
        if (std::isspace((unsigned char)c)) { i++; continue; }
        if (c == '#') { while (i < s.size() && s[i] != '\n') i++; continue; }
        if (depth == 0 && (std::isalpha((unsigned char)c) || c == '_')) {
          std::string w = readWord();
          if (w == "uniform") {
            size_t before = i;
            if (!parseUniform()) {
              if (i == before && i < s.size()) i++;
              int pd = 0;
              while (i < s.size()) {
                if (commentHere()) { skipComment(); continue; }
                if (s[i] == '(') pd++;
                else if (s[i] == ')' && pd) pd--;
                else if (pd == 0 && (s[i] == ';' || s[i] == '{' || s[i] == '\n')) break;
                i++;
              }
              if (i < s.size() && (s[i] == ';' || s[i] == '\n')) i++;
            }
          }
          continue;
        }
        i++;
      }
    }
  };
  Parser p{s};
  p.run();
  return std::move(p.out);
}
static void applyUniformDefaults(Prog& p, const std::unordered_map<std::string, UniInit>& defs) {
  if (defs.empty() || p.ubo.empty()) return;
  auto put4 = [&](int off, bool integral, double value) {
    if (off < 0 || off + 4 > (int)p.ubo.size()) return;
    if (integral) {
      int iv = (int)value;
      std::memcpy(p.ubo.data() + off, &iv, 4);
    } else {
      float fv = (float)value;
      std::memcpy(p.ubo.data() + off, &fv, 4);
    }
  };
  for (const Uni& u : p.unis) {
    if (u.sampler || u.offset < 0) continue;
    std::string name = u.name;
    auto dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(dot + 1);
    if (name.size() > 3 && name.ends_with("[0]")) name.resize(name.size() - 3);
    auto it = defs.find(name);
    if (it == defs.end()) continue;
    int rows = 1, cols = 1;
    bool matrix = false, integral = false;
    if (!std140Shape(u.type, rows, cols, matrix, integral)) continue;
    const UniInit& d = it->second;
    int need = matrix ? cols * rows : rows;
    if ((int)d.v.size() != need) continue;
    if (matrix) {
      for (int c = 0; c < cols; c++)
        for (int r = 0; r < rows; r++)
          put4(u.offset + c * 16 + r * 4, integral, d.v[(size_t)(c * rows + r)]);
    } else {
      for (int r = 0; r < rows; r++) put4(u.offset + r * 4, integral, d.v[(size_t)r]);
    }
    if (name == "cloudHeight" && d.v.size() == 1) {
      static int once = 0;
      if (once++ == 0) DQ_LOG("vulkan: cloudHeight default %.0f", d.v[0]);
    }
  }
}
static void wrapLooseUniforms(std::string& vs, std::string& fs) {
  // The same uniform in both stages is one block member. A comment on only one
  // of the lines (clouds.glsl uShell) used to leave both in and fail the link.
  // A comma list plus a second declaration of the same name (Rethinking Voxels
  // viewWidth) is one member too.
  std::vector<std::string> decls;
  std::unordered_set<std::string> seen;
  for (auto& d : looseUniforms(vs)) addLooseMembers(decls, seen, d);
  for (auto& d : looseUniforms(fs)) addLooseMembers(decls, seen, d);
  if (decls.empty()) return;
  std::string block = "layout(std140, set = 0, binding = 0) uniform DQBlock {\n";
  for (auto& d : decls) block += "  " + d + "\n";
  block += "};\n";
  stripLooseUniforms(vs);
  stripLooseUniforms(fs);
  auto insert = [&](std::string& s) {
    size_t v = s.find("#version");
    size_t at = 0;
    if (v != std::string::npos) {
      at = s.find('\n', v);
      at = at == std::string::npos ? s.size() : at + 1;
    }
    s.insert(at, block);
  };
  insert(vs);
  insert(fs);
}
// Pack storage blocks use binding 0, which is also the uniform block. Move them above the
// samplers and remember the original index so glBindBufferBase still selects the buffer.
static void rewriteStorageBlocks(std::string& s, int base, std::vector<int>& glIndices) {
  std::string out;
  size_t i = 0;
  auto ident = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
  while (i < s.size()) {
    size_t p = s.find("layout", i);
    if (p == std::string::npos) { out.append(s, i, std::string::npos); break; }
    if (p > 0 && ident((unsigned char)s[p - 1])) { out.append(s, i, p + 6 - i); i = p + 6; continue; }
    size_t paren = p + 6;
    while (paren < s.size() && std::isspace((unsigned char)s[paren])) paren++;
    if (paren >= s.size() || s[paren] != '(') { out.append(s, i, p + 6 - i); i = p + 6; continue; }
    int depth = 0;
    size_t q = paren;
    for (; q < s.size(); q++) {
      if (s[q] == '(') depth++;
      else if (s[q] == ')') { depth--; if (depth == 0) { q++; break; } }
    }
    if (q >= s.size() || depth != 0) { out.append(s, i, std::string::npos); break; }
    // Packs put a macro (WRITE_TO_SSBOS) or a memory qualifier between layout() and buffer.
    // Missing that left the block at binding 0, the same slot as the uniform block, and its
    // members were reflected into the uniform block on top of dqMVP.
    size_t k = q;
    auto wordAt = [&](size_t at, const char* w) {
      size_t n = std::strlen(w);
      if (at + n > s.size() || s.compare(at, n, w) != 0) return false;
      return at + n == s.size() || !ident((unsigned char)s[at + n]);
    };
    bool foundBuffer = false;
    for (int guard = 0; guard < 8 && !foundBuffer; guard++) {
      while (k < s.size() && std::isspace((unsigned char)s[k])) k++;
      if (wordAt(k, "buffer")) { foundBuffer = true; break; }
      const char* quals[] = {"coherent", "volatile", "restrict", "readonly", "writeonly"};
      bool skipped = false;
      for (const char* w : quals) if (wordAt(k, w)) { k += std::strlen(w); skipped = true; break; }
      if (skipped) continue;
      if (k < s.size() && (std::isalpha((unsigned char)s[k]) || s[k] == '_')) {
        while (k < s.size() && ident((unsigned char)s[k])) k++;
        continue;
      }
      break;
    }
    if (!foundBuffer) {
      out.append(s, i, q - i);
      i = q;
      continue;
    }
    std::string inner = s.substr(paren + 1, (q - 1) - (paren + 1));
    int binding = 0;
    auto bpos = inner.find("binding");
    auto eq = bpos == std::string::npos ? std::string::npos : inner.find('=', bpos);
    if (bpos != std::string::npos && eq != std::string::npos) {
      binding = std::atoi(inner.c_str() + eq + 1);
      if (binding < 0) binding = 0;
      size_t num = eq + 1;
      while (num < inner.size() && std::isspace((unsigned char)inner[num])) num++;
      size_t numEnd = num;
      if (numEnd < inner.size() && (inner[numEnd] == '-' || inner[numEnd] == '+')) numEnd++;
      while (numEnd < inner.size() && std::isdigit((unsigned char)inner[numEnd])) numEnd++;
      inner.replace(num, numEnd - num, std::to_string(base + binding));
    } else {
      if (!inner.empty() && inner.find_first_not_of(" \t") != std::string::npos && inner.back() != ',') inner += ", ";
      inner += "binding=" + std::to_string(base + binding);
    }
    if (inner.find("set") == std::string::npos) inner = "set=0, " + inner;
    glIndices.push_back(binding);
    out.append(s, i, p - i);
    out += "layout(" + inner + ")";
    i = q;
  }
  s.swap(out);
}
static bool linkProg(Prog& p) {
  GLuint self = (GLuint)(&p - progs.data());
  std::vector<PipeKey> drop;
  for (auto& kv : pipelines) if (kv.first.w[0] == self) drop.push_back(kv.first);
  for (auto& k : drop) { retireGpu({.pipe = pipelines[k]}); pipelines.erase(k); }
  destroyProgGpu(p);
  p.unis.clear(); p.samplers.clear(); p.ssbos.clear(); p.uniName.clear(); p.attrName.clear(); p.inputs.clear();
  p.ubo.clear(); p.uboSize = 0; p.uboBinding = -1; p.colorOuts = 0; p.linked = false; p.log.clear();
  ShaderObj* vs = shOf(p.vs); ShaderObj* fs = shOf(p.fs);
  if (!vs || !fs || !vs->compiled || !fs->compiled) { p.log = "missing shader"; return false; }
  if (!glslangOn) { glslang::InitializeProcess(); glslangOn = true; }
  std::string vsrc = prepGlsl(vs->src, true), fsrc = prepGlsl(fs->src, false);
  {
    std::string vkeep = vsrc, fkeep = fsrc;
    resolveIfdefs(vsrc); resolveIfdefs(fsrc);
    if (!hasShaderMain(vsrc) && hasShaderMain(vkeep)) vsrc = vkeep;
    if (!hasShaderMain(fsrc) && hasShaderMain(fkeep)) fsrc = fkeep;
    vkeep = vsrc; fkeep = fsrc;
    stripUnusedFunctions(vsrc); stripUnusedFunctions(fsrc);
    if (!hasShaderMain(vsrc) && hasShaderMain(vkeep)) vsrc = std::move(vkeep);
    if (!hasShaderMain(fsrc) && hasShaderMain(fkeep)) fsrc = std::move(fkeep);
  }
  std::unordered_map<std::string, UniInit> uniDefaults = collectUniformDefaults(vsrc);
  for (auto& kv : collectUniformDefaults(fsrc)) uniDefaults.emplace(kv.first, kv.second);
  std::vector<SampDecl> decls = collectSamplers(vsrc);
  for (auto& d : collectSamplers(fsrc)) {
    bool seen = false; for (auto& e : decls) if (e.name == d.name) seen = true;
    if (!seen) decls.push_back(d);
  }
  for (int i = 0; i < (int)decls.size(); i++) decls[i].binding = i + 1;
  rewriteSamplers(vsrc, decls); rewriteSamplers(fsrc, decls);
  if (std::getenv("DQ_VK_MINSKY") && (vsrc.find("vNdc") != std::string::npos || fsrc.find("vNdc") != std::string::npos)) {
    vsrc = "#version 450\nlayout(location=0) out vec2 vNdc;\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2, gl_VertexIndex&2)*2.0-1.0; vNdc=p; gl_Position=vec4(p,0.9999,1.0); gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;}\n";
    fsrc = "#version 450\nlayout(location=0) in vec2 vNdc;\nlayout(location=0) out vec4 fragColor;\nvoid main(){fragColor=vec4(vNdc*0.5+0.5,0.3,1.0);}\n";
    decls.clear();
  }
  // A fragment that reads gl_FogFragCoord needs the vertex to write the same varying.
  {
    const bool fin = fsrc.find("in float dqFogFragCoord") != std::string::npos;
    const bool vout = vsrc.find("out float dqFogFragCoord") != std::string::npos;
    if (fin && !vout) {
      auto nl = vsrc.find('\n');
      vsrc.insert(nl == std::string::npos ? vsrc.size() : nl + 1, "out float dqFogFragCoord;\n");
      auto m = vsrc.find("void main()");
      auto b = m == std::string::npos ? std::string::npos : vsrc.find('{', m);
      if (b != std::string::npos) vsrc.insert(b + 1, "dqFogFragCoord=0.0;");
    }
  }
  assignIoLocations(vsrc, fsrc);
  wrapLooseUniforms(vsrc, fsrc);
  {
    std::vector<int> ssboGl;
    int base = (int)decls.size() + 1;
    rewriteStorageBlocks(vsrc, base, ssboGl);
    rewriteStorageBlocks(fsrc, base, ssboGl);
    for (int g : ssboGl) {
      if (g < 0 || g >= 16) continue;
      bool seen = false;
      for (auto& e : p.ssbos) if (e.glIndex == g) seen = true;
      if (!seen) p.ssbos.push_back({g, base + g});
    }
  }
  if (std::getenv("DQ_VK_DUMP") && (vsrc.find("vNdc") != std::string::npos || fsrc.find("vNdc") != std::string::npos)) {
    FILE* vf = fopen("/tmp/dq-sky.vert", "w"); if (vf) { fwrite(vsrc.data(), 1, vsrc.size(), vf); fclose(vf); }
    FILE* ff = fopen("/tmp/dq-sky.frag", "w"); if (ff) { fwrite(fsrc.data(), 1, fsrc.size(), ff); fclose(ff); }
  }
  const char* vp = vsrc.c_str(); const char* fp = fsrc.c_str();
  glslang::TShader vsh(EShLangVertex), fsh(EShLangFragment);
  vsh.setStrings(&vp, 1); fsh.setStrings(&fp, 1);
  setupShader(vsh, EShLangVertex, false); setupShader(fsh, EShLangFragment, false);
  EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
  if (!vsh.parse(GetDefaultResources(), 450, ENoProfile, false, false, msg)) { p.log = vsh.getInfoLog(); return false; }
  if (!fsh.parse(GetDefaultResources(), 450, ENoProfile, false, false, msg)) { p.log = fsh.getInfoLog(); return false; }
  glslang::TProgram program;
  program.addShader(&vsh); program.addShader(&fsh);
  if (!program.link(msg)) {
    p.log = program.getInfoLog() ? program.getInfoLog() : "link failed";
    {
      FILE* df = fopen("/tmp/dq-vk-linkfail.txt", "w");
      if (df) {
        fprintf(df, "frag main %d len %zu\nvert main %d len %zu\n%s\n", hasShaderMain(fsrc) ? 1 : 0, fsrc.size(), hasShaderMain(vsrc) ? 1 : 0, vsrc.size(), p.log.c_str());
        // The reported line is in the fragment that glslang just rejected.
        // glslang prints "ERROR: 0:727: ...". The first colon is the one in "ERROR:", so skip
        // a zero and take the first ":N:" whose N is the source line.
        int line = 1;
        const std::string& lg = p.log;
        for (size_t c = 0; c + 2 < lg.size(); c++) {
          if (lg[c] != ':' || !std::isdigit((unsigned char)lg[c + 1])) continue;
          int n = std::atoi(lg.c_str() + c + 1);
          size_t j = c + 1;
          while (j < lg.size() && std::isdigit((unsigned char)lg[j])) j++;
          if (n > 0 && j < lg.size() && lg[j] == ':') { line = n; break; }
        }
        int cur = 1;
        size_t i = 0;
        while (cur + 40 < line && i < fsrc.size()) { if (fsrc[i++] == '\n') cur++; }
        size_t begin = i;
        int endLine = line + 40;
        while (cur < endLine && i < fsrc.size()) { if (fsrc[i++] == '\n') cur++; }
        fprintf(df, "---- fragment lines %d..%d ----\n", cur > 80 ? line - 40 : 1, cur);
        fwrite(fsrc.data() + begin, 1, i - begin, df);
        fclose(df);
      }
    }
    return false;
  }
  // SeparateBuffers keeps shader-storage members out of the uniform list. Their offsets are
  // relative to the SSBO, and writing them into the uniform block replaced dqMVP (final drew
  // the whole frame into one quadrant) and gbufferModelView.
  int opts = EShReflectionBasicArraySuffix | EShReflectionAllBlockVariables | EShReflectionAllIOVariables | EShReflectionSeparateBuffers;
  program.buildReflection(opts);
  glslang::SpvOptions opt; opt.disableOptimizer = true;
  std::vector<uint32_t> vspv, fspv;
  if (!program.getIntermediate(EShLangVertex) || !program.getIntermediate(EShLangFragment)) { p.log = "no intermediate"; return false; }
  glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), vspv, &opt);
  glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), fspv, &opt);
  if (std::getenv("DQ_VK_DUMP")) {
    static int dumpN = 0;
    if (dumpN < 4) {
      auto save = [&](const char* tag, const std::vector<uint32_t>& spv) {
        std::string path = "/tmp/dq-spv-" + std::to_string(dumpN) + "-" + tag + ".spv";
        FILE* f = fopen(path.c_str(), "wb"); if (!f) return; fwrite(spv.data(), 4, spv.size(), f); fclose(f);
      };
      save("vs", vspv); save("fs", fspv); dumpN++;
    }
  }
  int blocks = program.getNumUniformBlocks();
  for (int i = 0; i < blocks; i++) {
    const auto& b = program.getUniformBlock(i);
    if (p.uboBinding < 0 || std::string(b.name) == "DQBlock") {
      p.uboBinding = b.getBinding() < 0 ? 0 : b.getBinding();
      p.uboSize = std::max(p.uboSize, b.size);
    }
  }
  int nu = program.getNumUniformVariables();
  for (int i = 0; i < nu; i++) {
    const auto& u = program.getUniform(i);
    GLenum gt = (GLenum)u.glDefineType;
    Uni uni; uni.name = u.name; uni.type = gt; uni.offset = u.offset; uni.stride = u.arrayStride; uni.count = std::max(1, u.size);
    if (isSamplerType(gt)) {
      uni.sampler = true;
      Samp s; s.binding = u.getBinding(); s.type = gt; s.unit = 0; s.name = u.name;
      if (s.name.size() > 3 && s.name.ends_with("[0]")) s.name.resize(s.name.size() - 3);
      uni.samplerIndex = (int)p.samplers.size();
      p.samplers.push_back(s);
    } else if (uni.offset >= 0) {
      int bytes = glTypeBytes(gt);
      int end = uni.offset + std::max(bytes, uni.stride > 0 ? uni.stride * uni.count : bytes);
      p.uboSize = std::max(p.uboSize, end);
    }
    int loc = (int)p.unis.size();
    p.uniName[uni.name] = loc;
    if (uni.name.size() > 3 && uni.name.ends_with("[0]")) p.uniName[uni.name.substr(0, uni.name.size() - 3)] = loc;
    p.unis.push_back(uni);
  }
  if (p.uboSize > 0) {
    p.uboSize = (p.uboSize + 15) & ~15;
    p.ubo.assign((size_t)p.uboSize, 0);
    if (p.uboBinding < 0) p.uboBinding = 0;
    applyUniformDefaults(p, uniDefaults);
  }
  int ni = program.getNumPipeInputs();
  for (int i = 0; i < ni; i++) {
    const auto& in = program.getPipeInput(i);
    unsigned loc = in.layoutLocation();
    if (loc == 0xFFFFFFFFu || loc > 64) continue;
    p.attrName[in.name] = (int)loc;
    p.inputs.push_back((int)loc);
  }
  int nout = program.getNumPipeOutputs();
  int maxLoc = 0;
  for (int i = 0; i < nout; i++) {
    const auto& o = program.getPipeOutput(i);
    if (o.name.rfind("gl_", 0) == 0) continue;
    unsigned loc = o.layoutLocation();
    if (loc == 0xFFFFFFFFu || loc >= 8) continue;
    maxLoc = std::max(maxLoc, (int)loc + 1);
  }
  p.colorOuts = maxLoc;
  if (limits.maxPerStageDescriptorSamplers && decls.size() > limits.maxPerStageDescriptorSamplers) {
    p.log = "too many samplers for this GPU"; return false;
  }
  if (p.uboSize > 0 && limits.maxUniformBufferRange && (uint32_t)p.uboSize > limits.maxUniformBufferRange) {
    p.log = "uniform block is larger than this GPU allows"; return false;
  }
  if (vspv.empty() || fspv.empty()) { p.log = "empty spir-v"; return false; }
  auto moduleOk = [](const std::vector<uint32_t>& s) {
    if (s.size() < 6 || s[0] != 0x07230203u) return false;
    for (uint32_t i = 5; i < s.size(); ) {
      uint32_t wc = s[i] >> 16;
      if (wc == 0 || i + wc > s.size()) return false;
      i += wc;
    }
    return true;
  };
  if (!moduleOk(vspv) || !moduleOk(fspv)) { p.log = "broken spir-v"; return false; }
  p.modVS = makeModule(vspv); p.modFS = makeModule(fspv);
  if (!p.modVS || !p.modFS) { p.log = "shader module"; return false; }
  std::vector<VkDescriptorSetLayoutBinding> binds;
  if (p.uboSize > 0) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = (uint32_t)p.uboBinding;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds.push_back(b);
  }
  for (auto& d : decls) {
    bool seen = false;
    for (auto& s : p.samplers) if (s.name == d.name || s.name == d.name + "[0]") {
      s.binding = d.binding; s.type = d.type; seen = true;
    }
    if (!seen) { Samp s; s.binding = d.binding; s.type = d.type; s.unit = 0; s.name = d.name; p.samplers.push_back(s); }
  }
  for (auto& s : p.samplers) {
    if (s.binding < 0) s.binding = 1 + (int)(&s - p.samplers.data());
    VkDescriptorSetLayoutBinding b{};
    b.binding = (uint32_t)s.binding;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds.push_back(b);
  }
  for (auto& s : p.ssbos) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = (uint32_t)s.binding;
    b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds.push_back(b);
  }
  if (!binds.empty()) {
    VkDescriptorSetLayoutCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    di.bindingCount = (uint32_t)binds.size();
    di.pBindings = binds.data();
    if (vkFail(vkCreateDescriptorSetLayout(device, &di, nullptr, &p.dsl), "set layout")) return false;
  }
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (p.dsl) { li.setLayoutCount = 1; li.pSetLayouts = &p.dsl; }
  if (vkFail(vkCreatePipelineLayout(device, &li, nullptr, &p.pipeLayout), "pipeline layout")) return false;
  p.linked = true;
  if (std::getenv("DQ_VK_DEBUG")) {
    DQ_LOG("link ubo binding %d size %d samplers %zu unis %zu inputs %zu", p.uboBinding, p.uboSize, p.samplers.size(), p.unis.size(), p.inputs.size());
    for (auto& s : p.samplers) DQ_LOG("  sampler %s bind %d", s.name.c_str(), s.binding);
  }
  return true;
}

// ---------------------------------------------------------------- draw

static void markBuf(GLuint id) {
  if (Buf* b = bufOf(id)) { b->referenced = true; if (b->dirty && b->gpu.buffer && recording) { hostBarrier(b->gpu.buffer, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT); b->dirty = false; } }
}

static VkShaderModule tinyModule(bool vertex) {
  static VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
  if (vertex && vs) return vs;
  if (!vertex && fs) return fs;
  const char* src = vertex
    ? "#version 450\nvoid main(){gl_Position=vec4(0.0,0.0,0.0,1.0);}\n"
    : "#version 450\nlayout(location=0) out vec4 o;\nvoid main(){o=vec4(1.0,0.0,0.0,1.0);}\n";
  EShLanguage stage = vertex ? EShLangVertex : EShLangFragment;
  const char* ps = src;
  glslang::TShader sh(stage);
  sh.setStrings(&ps, 1);
  setupShader(sh, stage, false);
  EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
  if (!sh.parse(GetDefaultResources(), 450, ENoProfile, false, false, msg)) { DQ_ERR("tiny parse %s", sh.getInfoLog()); return VK_NULL_HANDLE; }
  glslang::TProgram program; program.addShader(&sh);
  if (!program.link(msg)) { DQ_ERR("tiny link %s", program.getInfoLog()); return VK_NULL_HANDLE; }
  std::vector<uint32_t> spv; glslang::SpvOptions opt; opt.disableOptimizer = true;
  glslang::GlslangToSpv(*program.getIntermediate(stage), spv, &opt);
  VkShaderModule m = makeModule(spv);
  if (vertex) vs = m; else fs = m;
  return m;
}
static VkPipeline pipelineFor(Prog& p, const PipeKey& key, const std::vector<VkVertexInputBindingDescription>& binds, const std::vector<VkVertexInputAttributeDescription>& attrs) {
  auto it = pipelines.find(key);
  if (it != pipelines.end()) return it->second;
  bool tiny = std::getenv("DQ_VK_TINY") != nullptr;
  bool simple = std::getenv("DQ_VK_SIMPLE") != nullptr || tiny;
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = tiny ? tinyModule(true) : p.modVS; stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = tiny ? tinyModule(false) : p.modFS; stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = (uint32_t)binds.size();
  vi.pVertexBindingDescriptions = binds.empty() ? nullptr : binds.data();
  vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
  vi.pVertexAttributeDescriptions = attrs.empty() ? nullptr : attrs.data();
  if (tiny) { vi.vertexBindingDescriptionCount = 0; vi.pVertexBindingDescriptions = nullptr; vi.vertexAttributeDescriptionCount = 0; vi.pVertexAttributeDescriptions = nullptr; }
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1; vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rs.lineWidth = 1.f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  ds.minDepthBounds = 0; ds.maxDepthBounds = 1;
  VkPipelineColorBlendAttachmentState cb{};
  cb.colorWriteMask = (uint32_t)key.w[4];
  cb.srcColorBlendFactor = (VkBlendFactor)(key.w[5]);
  cb.dstColorBlendFactor = (VkBlendFactor)(key.w[6]);
  cb.colorBlendOp = (VkBlendOp)(key.w[7]);
  cb.srcAlphaBlendFactor = (VkBlendFactor)(key.w[8]);
  cb.dstAlphaBlendFactor = (VkBlendFactor)(key.w[9]);
  cb.alphaBlendOp = (VkBlendOp)(key.w[10]);
  int colorCount = std::clamp((int)key.w[1], 0, 8);
  VkFormat cfs[8];
  for (int i = 0; i < colorCount; i++) cfs[i] = (VkFormat)key.w[11 + i];
  // key.w[3] is a mask, one bit per attachment. gbuffers_water turns blending off for some colortex.
  std::vector<VkPipelineColorBlendAttachmentState> atts((size_t)colorCount);
  for (int i = 0; i < colorCount; i++) { atts[i] = cb; atts[i].blendEnable = (key.w[3] >> i) & 1u; }
  VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbs.attachmentCount = (uint32_t)colorCount;
  cbs.pAttachments = atts.empty() ? nullptr : atts.data();
  VkDynamicState dynFull[] = {
    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_LINE_WIDTH,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
    VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE
  };
  int dynCount = (int)(sizeof(dynFull) / sizeof(dynFull[0]));
  if (simple) dynCount = 2;
  if (const char* dl = std::getenv("DQ_VK_DYN")) dynCount = std::max(1, std::min(dynCount, std::atoi(dl)));
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = (uint32_t)dynCount;
  dy.pDynamicStates = dynFull;
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  VkFormat df = (VkFormat)key.w[2];
  if (colorCount > 0) { rendering.colorAttachmentCount = (uint32_t)colorCount; rendering.pColorAttachmentFormats = cfs; }
  if (df) { rendering.depthAttachmentFormat = df; if (df != VK_FORMAT_D32_SFLOAT) rendering.stencilAttachmentFormat = df; }
  VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pi.pNext = &rendering;
  pi.stageCount = 2; pi.pStages = stages;
  pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
  pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds;
  pi.pColorBlendState = &cbs; pi.pDynamicState = &dy;
  static VkPipelineLayout emptyLayout = VK_NULL_HANDLE;
  if (tiny && !emptyLayout) {
    VkPipelineLayoutCreateInfo eli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(device, &eli, nullptr, &emptyLayout);
  }
  pi.layout = tiny ? emptyLayout : p.pipeLayout;
  VkPipeline pipe = VK_NULL_HANDLE;
  if (vkFail(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe), "pipeline")) return VK_NULL_HANDLE;
  pipelines[key] = pipe;
  return pipe;
}

static void applyDynamic(GLenum mode) {
  int fh = targetH(drawFbo);
  VkViewport v{};
  v.x = (float)vpX; v.y = (float)vpY; v.width = (float)std::max(0, vpW); v.height = (float)std::max(0, vpH);
  v.minDepth = 0; v.maxDepth = 1;
  vkCmdSetViewport(cmd, 0, 1, &v);
  VkRect2D sc{};
  if (scissorOn) { sc.offset = {scX, scY}; sc.extent = {(uint32_t)std::max(0, scW), (uint32_t)std::max(0, scH)}; }
  else { sc.extent = {(uint32_t)std::max(0, targetW(drawFbo)), (uint32_t)std::max(0, fh)}; }
  vkCmdSetScissor(cmd, 0, 1, &sc);
  vkCmdSetPrimitiveTopology(cmd, topoOf(mode));
  vkCmdSetPrimitiveRestartEnable(cmd, primRestart);
  VkCullModeFlags cm = VK_CULL_MODE_NONE;
  if (cullOn) cm = cullFaceE == GL_FRONT ? VK_CULL_MODE_FRONT_BIT : cullFaceE == GL_FRONT_AND_BACK ? VK_CULL_MODE_FRONT_AND_BACK : VK_CULL_MODE_BACK_BIT;
  vkCmdSetCullMode(cmd, cm);
  // Vulkan's area is the negation of OpenGL's, so a GL counter-clockwise face
  // has negative area here and was being culled. The present blit flips Y for
  // display only; it does not change which faces already survived the test.
  vkCmdSetFrontFace(cmd, frontFaceE == GL_CW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
  vkCmdSetDepthTestEnable(cmd, depthTest);
  vkCmdSetDepthWriteEnable(cmd, depthWrite);
  vkCmdSetDepthCompareOp(cmd, cmpOp(depthFuncE));
  vkCmdSetDepthBiasEnable(cmd, polyOffset);
  vkCmdSetDepthBias(cmd, polyUnits, 0.f, polyFactor);
  vkCmdSetLineWidth(cmd, 1.f);
  vkCmdSetStencilTestEnable(cmd, stencilOn);
  vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, stenOp(stenSFail), stenOp(stenDPPass), stenOp(stenDPFail), cmpOp(stenFunc));
  vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)stenCmpMask);
  vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)stenWriteMask);
  vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)stenRef);
}

// sampler2D on a depth image is undefined in Vulkan and comes back 0 here, so a pack's
// border fog (lViewPos from depthtex) never starts. Copy the depth into R32F and bind that.
static void allowDepthSample(std::vector<uint32_t>& spv) {
  if (spv.size() < 6 || spv[0] != 0x07230203u) return;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t count = spv[i] >> 16;
    uint32_t op = spv[i] & 0xFFFFu;
    if (count == 0 || i + count > spv.size()) break;
    // OpTypeImage: depth is word 4, sampled-usage is word 7. 2 means "depth or colour".
    if (op == 25 && count >= 9 && spv[i + 4] == 0 && spv[i + 7] == 1) spv[i + 4] = 2;
    i += count;
  }
}
struct DepthCopyGpu {
  VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipe = VK_NULL_HANDLE;
  VkSampler samp = VK_NULL_HANDLE;
  bool failed = false;
};
static DepthCopyGpu g_depthCopy;
static int g_depthAsColor = -1; // -1 untested, 0 raw depth, 1 R32F copy
static int g_depthUnpack = 0;   // 0 GPU sample, 1 upload D24, 2 upload D32
static int g_depthConstLogs = 0;
static void destroyDepthCopy() {
  if (device) {
    if (g_depthCopy.pipe) vkDestroyPipeline(device, g_depthCopy.pipe, nullptr);
    if (g_depthCopy.layout) vkDestroyPipelineLayout(device, g_depthCopy.layout, nullptr);
    if (g_depthCopy.dsl) vkDestroyDescriptorSetLayout(device, g_depthCopy.dsl, nullptr);
    if (g_depthCopy.vs) vkDestroyShaderModule(device, g_depthCopy.vs, nullptr);
    if (g_depthCopy.fs) vkDestroyShaderModule(device, g_depthCopy.fs, nullptr);
    if (g_depthCopy.samp) vkDestroySampler(device, g_depthCopy.samp, nullptr);
  }
  g_depthCopy = {};
  g_depthAsColor = -1;
  g_depthUnpack = 0;
  g_depthConstLogs = 0;
}
static std::vector<uint32_t> compileDepthSpv(const char* src, EShLanguage stage, bool depthSample) {
  if (!glslangOn) { glslang::InitializeProcess(); glslangOn = true; }
  const char* ps = src;
  glslang::TShader sh(stage);
  sh.setStrings(&ps, 1);
  sh.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
  sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
  sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
  EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
  if (!sh.parse(GetDefaultResources(), 450, ENoProfile, false, false, msg)) {
    DQ_WARN("vulkan: depth copy shader: %s", sh.getInfoLog() ? sh.getInfoLog() : "");
    return {};
  }
  glslang::TProgram program;
  program.addShader(&sh);
  if (!program.link(msg)) {
    DQ_WARN("vulkan: depth copy link: %s", program.getInfoLog() ? program.getInfoLog() : "");
    return {};
  }
  if (!program.getIntermediate(stage)) return {};
  std::vector<uint32_t> spv;
  glslang::SpvOptions opt; opt.disableOptimizer = true;
  glslang::GlslangToSpv(*program.getIntermediate(stage), spv, &opt);
  if (depthSample) allowDepthSample(spv);
  return spv;
}
static bool ensureDepthCopyPipe(VkFormat fmt) {
  if (g_depthCopy.pipe) return true;
  if (g_depthCopy.failed) return false;
  const char* vsrc =
    "#version 450\n"
    "void main(){vec2 p=vec2((gl_VertexIndex<<1)&2, gl_VertexIndex&2); gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";
  const char* fsrc =
    "#version 450\n"
    "layout(set=0, binding=0) uniform sampler2D tex;\n"
    "layout(location=0) out float color;\n"
    "void main(){color=texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r;}\n";
  auto vspv = compileDepthSpv(vsrc, EShLangVertex, false);
  auto fspv = compileDepthSpv(fsrc, EShLangFragment, true);
  g_depthCopy.vs = makeModule(vspv);
  g_depthCopy.fs = makeModule(fspv);
  if (!g_depthCopy.vs || !g_depthCopy.fs) { g_depthCopy.failed = true; return false; }
  VkDescriptorSetLayoutBinding b{};
  b.binding = 0;
  b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  b.descriptorCount = 1;
  b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  di.bindingCount = 1; di.pBindings = &b;
  if (vkFail(vkCreateDescriptorSetLayout(device, &di, nullptr, &g_depthCopy.dsl), "depth copy set")) { g_depthCopy.failed = true; return false; }
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  li.setLayoutCount = 1; li.pSetLayouts = &g_depthCopy.dsl;
  if (vkFail(vkCreatePipelineLayout(device, &li, nullptr, &g_depthCopy.layout), "depth copy layout")) { g_depthCopy.failed = true; return false; }
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (vkFail(vkCreateSampler(device, &si, nullptr, &g_depthCopy.samp), "depth copy sampler")) { g_depthCopy.failed = true; return false; }
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = g_depthCopy.vs; stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = g_depthCopy.fs; stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1; vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendAttachmentState cb{};
  cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
  VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbs.attachmentCount = 1; cbs.pAttachments = &cb;
  VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &fmt;
  VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pi.pNext = &rendering;
  pi.stageCount = 2; pi.pStages = stages;
  pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
  pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds;
  pi.pColorBlendState = &cbs; pi.pDynamicState = &dy;
  pi.layout = g_depthCopy.layout;
  if (vkFail(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &g_depthCopy.pipe), "depth copy pipeline")) {
    g_depthCopy.failed = true; return false;
  }
  return true;
}
static void imageBarrier(VkImage image, VkImageLayout& layout, VkImageLayout next, VkImageAspectFlags aspect) {
  if (!image || layout == next || !recording) return;
  if (rendering) endRendering();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = layout; b.newLayout = next;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange.aspectMask = aspect;
  b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
  auto ends = [](VkImageLayout l, VkPipelineStageFlags& st, VkAccessFlags& ac) {
    switch (l) {
      case VK_IMAGE_LAYOUT_UNDEFINED: st = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; ac = 0; break;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: st = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; ac = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: st = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; ac = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: st = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; ac = VK_ACCESS_SHADER_READ_BIT; break;
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: st = VK_PIPELINE_STAGE_TRANSFER_BIT; ac = VK_ACCESS_TRANSFER_READ_BIT; break;
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: st = VK_PIPELINE_STAGE_TRANSFER_BIT; ac = VK_ACCESS_TRANSFER_WRITE_BIT; break;
      default: st = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; ac = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; break;
    }
  };
  VkPipelineStageFlags ss, ds; VkAccessFlags sa, da;
  ends(layout, ss, sa); ends(next, ds, da);
  if (layout == VK_IMAGE_LAYOUT_UNDEFINED) sa = 0;
  b.srcAccessMask = sa; b.dstAccessMask = da;
  vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
  layout = next;
}
static bool ensureDepthColorStore(Tex& t, VkFormat fmt) {
  if (t.depthColor && t.depthColorW == t.w && t.depthColorH == t.h && t.depthColorView) return true;
  freeDepthColor(t);
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt;
  ii.extent = {(uint32_t)std::max(1, t.w), (uint32_t)std::max(1, t.h), 1};
  ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (vkFail(vkCreateImage(device, &ii, nullptr, &t.depthColor), "depth colour image")) return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device, t.depthColor, &req);
  uint32_t mt = findMem(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mt == 0xFFFFFFFFu) mt = findMem(req.memoryTypeBits, 0);
  if (mt == 0xFFFFFFFFu) { freeDepthColor(t); return false; }
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size; ai.memoryTypeIndex = mt;
  if (vkFail(vkAllocateMemory(device, &ai, nullptr, &t.depthColorMem), "depth colour memory")) { freeDepthColor(t); return false; }
  vkBindImageMemory(device, t.depthColor, t.depthColorMem, 0);
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = t.depthColor; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
  vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
  if (vkFail(vkCreateImageView(device, &vi, nullptr, &t.depthColorView), "depth colour view")) { freeDepthColor(t); return false; }
  t.depthColorW = t.w; t.depthColorH = t.h;
  t.depthColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  t.depthColorDirty = true;
  return true;
}
static bool copyImageToHost(VkImage image, VkImageLayout& layout, VkImageAspectFlags aspect, int w, int h, void* dst) {
  VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
  auto a = bumpAlloc(bytes, 256);
  if (!a.ptr || !recording) return false;
  imageBarrier(image, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aspect);
  VkBufferImageCopy c{};
  c.bufferOffset = a.off;
  c.imageSubresource.aspectMask = aspect; c.imageSubresource.layerCount = 1;
  c.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
  vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, a.buf, 1, &c);
  VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  hb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hb.buffer = a.buf; hb.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0, nullptr);
  syncGpu();
  if (!recording) return false;
  memcpy(dst, a.ptr, (size_t)bytes);
  return true;
}
static bool depthSmall(const Tex& t) {
  return t.w >= 32 && t.h >= 32 && (size_t)t.w * (size_t)t.h * 4u <= (4u << 20);
}
static float depthWord01(const Tex& t, uint32_t w) {
  if (t.format == VK_FORMAT_D32_SFLOAT || t.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
    float f; std::memcpy(&f, &w, 4);
    return f == f ? f : 0.f;
  }
  return (float)(w & 0xFFFFFFu) / 16777215.f;
}
static bool readDepthAspect(Tex& t, std::vector<uint8_t>& raw) {
  if (!recording || t.w <= 0 || t.h <= 0 || !depthSmall(t)) return false;
  size_t bytes = (size_t)t.w * (size_t)t.h * 4u;
  raw.assign(bytes, 0);
  if (rendering) endRendering();
  transitionAll(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  auto a = bumpAlloc((VkDeviceSize)bytes, 256);
  if (!a.ptr || !recording) return false;
  VkBufferImageCopy c{};
  c.bufferOffset = a.off;
  c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; c.imageSubresource.layerCount = 1;
  c.imageExtent = {(uint32_t)t.w, (uint32_t)t.h, 1};
  vkCmdCopyImageToBuffer(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, a.buf, 1, &c);
  VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  hb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hb.buffer = a.buf; hb.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0, nullptr);
  syncGpu();
  if (!recording) return false;
  std::memcpy(raw.data(), a.ptr, bytes);
  transitionAll(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  return true;
}
static bool uploadFloatImage(Tex& t, const float* src, int n) {
  if (!recording || n <= 0) return false;
  auto a = bumpAlloc((VkDeviceSize)n * 4, 256);
  if (!a.ptr) return false;
  std::memcpy(a.ptr, src, (size_t)n * 4);
  hostBarrier(a.buf, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  imageBarrier(t.depthColor, t.depthColorLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
  VkBufferImageCopy c{};
  c.bufferOffset = a.off;
  c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1;
  c.imageExtent = {(uint32_t)t.w, (uint32_t)t.h, 1};
  vkCmdCopyBufferToImage(cmd, a.buf, t.depthColor, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
  imageBarrier(t.depthColor, t.depthColorLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
  return true;
}
static bool cpuDepthRefresh(Tex& t) {
  std::vector<uint8_t> raw;
  if (!readDepthAspect(t, raw)) return false;
  int n = t.w * t.h;
  std::vector<float> pix((size_t)n);
  auto* words = reinterpret_cast<const uint32_t*>(raw.data());
  for (int i = 0; i < n; i++) pix[i] = depthWord01(t, words[i]);
  if (!uploadFloatImage(t, pix.data(), n)) return false;
  t.depthColorDirty = false;
  return true;
}
static int depthMismatch(const std::vector<float>& mem, const std::vector<float>& twin, int w, int h) {
  int bad = 0, n = 0;
  int sx = std::max(1, w / 8), sy = std::max(1, h / 8);
  for (int y = 0; y < h; y += sy)
    for (int x = 0; x < w; x += sx) {
      float d = mem[(size_t)y * w + x] - twin[(size_t)y * w + x];
      if (d < 0) d = -d;
      n++;
      if (d > 0.05f) bad++;
    }
  return n ? bad : 1000;
}
// True when sampler2D should bind the R32F copy. The first window-sized depth is checked
// against the image itself; a copy that does not match stays off so packs keep the raw depth.
static bool depthColorForSample(Tex& t) {
  if (!t.depth || !t.image || !t.defined || t.w <= 0 || t.h <= 0) return false;
  // The R32F copy matches this image (a cleared depth is 1, and a real frame was 0.962..1
  // with no mismatches). Binding it made Complementary's horizon darker than sampling the
  // depth image, so the pack keeps the depth view.
  return false;
  if (g_depthAsColor == 0) return false;
  VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if (!supported(VK_FORMAT_R32_SFLOAT, need)) {
    g_depthAsColor = 0;
    DQ_WARN("vulkan: R32F is not a colour target, depth stays a depth image");
    return false;
  }
  if (g_depthUnpack != 0) {
    if (!depthSmall(t)) return false;
    if (!ensureDepthColorStore(t, VK_FORMAT_R32_SFLOAT)) return false;
    if (t.depthColorDirty && !cpuDepthRefresh(t)) return false;
    return true;
  }
  if (!ensureDepthCopyPipe(VK_FORMAT_R32_SFLOAT) || !ensureDepthColorStore(t, VK_FORMAT_R32_SFLOAT)) return false;
  bool refreshed = false;
  if (t.depthColorDirty) {
    if (!recording) return false;
    if (rendering) endRendering();
    transitionAll(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    imageBarrier(t.depthColor, t.depthColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    ensureDescPool();
    if (descPoolCur < 0) return false;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPools[descPoolCur]; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_depthCopy.dsl;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) return false;
    VkDescriptorImageInfo ii{};
    ii.sampler = g_depthCopy.samp; ii.imageView = t.view; ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = t.depthColorView; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {(uint32_t)t.w, (uint32_t)t.h}};
    ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_depthCopy.pipe);
    VkViewport v{}; v.width = (float)t.w; v.height = (float)t.h; v.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &v);
    VkRect2D sc{{0, 0}, {(uint32_t)t.w, (uint32_t)t.h}};
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_depthCopy.layout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    imageBarrier(t.depthColor, t.depthColorLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    t.depthColorDirty = false;
    refreshed = true;
  }
  if (g_depthAsColor < 0 && refreshed && depthSmall(t)) {
    int n = t.w * t.h;
    std::vector<uint8_t> raw, copied((size_t)n * 4);
    // A cleared depth is all 1s and matches a sample of 1. Wait for geometry before deciding.
    if (!readDepthAspect(t, raw)) { g_depthAsColor = 0; DQ_WARN("vulkan: depth-as-colour readback failed"); return false; }
    bool gotC = copyImageToHost(t.depthColor, t.depthColorLayout, VK_IMAGE_ASPECT_COLOR_BIT, t.w, t.h, copied.data());
    imageBarrier(t.depthColor, t.depthColorLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!gotC) { g_depthAsColor = 0; DQ_WARN("vulkan: depth-as-colour readback failed"); return false; }
    auto* words = reinterpret_cast<const uint32_t*>(raw.data());
    auto* cf = reinterpret_cast<const float*>(copied.data());
    std::vector<float> truth((size_t)n), twin(cf, cf + n);
    float mn = 1.f, mx = 0.f, tmn = 1.f, tmx = 0.f;
    for (int i = 0; i < n; i++) {
      truth[i] = depthWord01(t, words[i]);
      if (truth[i] < mn) mn = truth[i];
      if (truth[i] > mx) mx = truth[i];
      if (twin[i] < tmn) tmn = twin[i];
      if (twin[i] > tmx) tmx = twin[i];
    }
    if (mx - mn < 0.02f) {
      if (g_depthConstLogs++ == 0)
        DQ_LOG("vulkan: depth-as-colour pending, depth is flat %.3f..%.3f", mn, mx);
      if (g_depthConstLogs == 8 && fbDepth && fbDepth != &t && fbDepth->image && depthSmall(*fbDepth)) {
        std::vector<uint8_t> win;
        if (readDepthAspect(*fbDepth, win)) {
          float wmn = 1.f, wmx = 0.f;
          auto* ww = reinterpret_cast<const uint32_t*>(win.data());
          int wn = fbDepth->w * fbDepth->h;
          for (int i = 0; i < wn; i++) {
            float d = depthWord01(*fbDepth, ww[i]);
            if (d < wmn) wmn = d;
            if (d > wmx) wmx = d;
          }
          DQ_LOG("vulkan: window depth %.3f..%.3f while depthtex stays flat", wmn, wmx);
        }
      }
      return false;
    }
    int bad = depthMismatch(truth, twin, t.w, t.h);
    if (bad <= 3) {
      g_depthAsColor = 1;
      DQ_LOG("vulkan: depth-as-colour gpu mem %.3f..%.3f copy %.3f..%.3f mismatches %d", mn, mx, tmn, tmx, bad);
    } else if (uploadFloatImage(t, truth.data(), n)) {
      g_depthUnpack = (t.format == VK_FORMAT_D32_SFLOAT || t.format == VK_FORMAT_D32_SFLOAT_S8_UINT) ? 2 : 1;
      g_depthAsColor = 1;
      DQ_LOG("vulkan: depth-as-colour cpu mem %.3f..%.3f copy %.3f..%.3f mismatches %d raw %08x %08x %08x",
             mn, mx, tmn, tmx, bad, words[0], words[n / 2], words[n - 1]);
    } else {
      g_depthAsColor = 0;
      DQ_WARN("vulkan: depth-as-colour upload failed");
      return false;
    }
  }
  return g_depthAsColor == 1;
}

static void bindDescriptors(Prog& p) {
  if (!p.dsl) return;
  ensureDescPool();
  if (descPoolCur < 0) return;
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = descPools[descPoolCur];
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &p.dsl;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) {
    descPoolCur = -1; ensureDescPool();
    if (descPoolCur < 0) return;
    ai.descriptorPool = descPools[descPoolCur];
    if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) return;
  }
  VkDescriptorBufferInfo bi{};
  Alloc uboAlloc{};
  if (p.uboSize > 0) {
    VkDeviceSize align = std::max<VkDeviceSize>(limits.minUniformBufferOffsetAlignment, 16);
    uboAlloc = bumpAlloc((VkDeviceSize)p.uboSize, align);
    if (uboAlloc.ptr) {
      memcpy(uboAlloc.ptr, p.ubo.data(), p.ubo.size());
      hostBarrier(uboAlloc.buf, VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      bi.buffer = uboAlloc.buf; bi.offset = uboAlloc.off; bi.range = (VkDeviceSize)p.uboSize;
    }
  }
  std::vector<VkDescriptorImageInfo> iis(p.samplers.size());
  std::vector<VkWriteDescriptorSet> writes;
  if (p.uboSize > 0 && uboAlloc.buf) {
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set; w.dstBinding = (uint32_t)p.uboBinding; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.pBufferInfo = &bi;
    writes.push_back(w);
  }
  for (size_t i = 0; i < p.samplers.size(); i++) {
    Samp& s = p.samplers[i];
    int unit = std::clamp(s.unit, 0, 31);
    GLenum target = (s.type == GL_SAMPLER_2D_ARRAY || s.type == GL_SAMPLER_2D_ARRAY_SHADOW) ? GL_TEXTURE_2D_ARRAY : s.type == GL_SAMPLER_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    GLuint id = target == GL_TEXTURE_2D_ARRAY ? bindArr[unit] : target == GL_TEXTURE_3D ? bind3D[unit] : bind2D[unit];
    Tex* tex = texOf(id);
    if (!tex) tex = dummyFor(s.type);
    bool shadow = s.type == GL_SAMPLER_2D_SHADOW || s.type == GL_SAMPLER_2D_ARRAY_SHADOW;
    if (tex) {
      // The R32F copy is refreshed before this draw records a pipeline. Binding it here
      // must not submit the command buffer, or that pipeline bind is dropped.
      VkImageView view = tex->view;
      if (tex->depth && !shadow && g_depthAsColor == 1 && tex->depthColorView && !tex->depthColorDirty)
        view = tex->depthColorView;
      else if (tex->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) transitionAll(*tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      iis[i].sampler = samplerFor(*tex, shadow);
      iis[i].imageView = view;
      iis[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set; w.dstBinding = (uint32_t)s.binding; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &iis[i];
    writes.push_back(w);
  }
  static GpuBuf dummyStorage;
  static bool dummyBarried = false;
  std::vector<VkDescriptorBufferInfo> sbi(p.ssbos.size());
  for (size_t i = 0; i < p.ssbos.size(); i++) {
    int gi = p.ssbos[i].glIndex;
    Buf* b = (gi >= 0 && gi < 16) ? bufOf(ssboBase[gi]) : nullptr;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceSize range = 0;
    if (b && b->gpu.buffer) {
      if (b->dirty) {
        if (rendering) endRendering();
        hostBarrier(b->gpu.buffer, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        b->dirty = false;
      }
      b->referenced = true;
      buf = b->gpu.buffer;
      range = b->gpu.size;
    } else {
      if (!dummyStorage.buffer) {
        allocHostBuf(256, kBufUse, dummyStorage);
        if (dummyStorage.map) memset(dummyStorage.map, 0, 256);
      }
      if (!dummyStorage.buffer) continue;
      if (!dummyBarried) {
        if (rendering) endRendering();
        hostBarrier(dummyStorage.buffer, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        dummyBarried = true;
      }
      buf = dummyStorage.buffer;
      range = dummyStorage.size;
    }
    sbi[i].buffer = buf;
    sbi[i].offset = 0;
    sbi[i].range = range ? range : VK_WHOLE_SIZE;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set; w.dstBinding = (uint32_t)p.ssbos[i].binding; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &sbi[i];
    writes.push_back(w);
  }
  if (!writes.empty()) vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeLayout, 0, 1, &set, 0, nullptr);
  if (std::getenv("DQ_UBO_DUMP")) {
    static std::set<GLuint> dumped;
    bool want = false;
    for (auto& u : p.unis) if (u.name == "gbufferProjectionInverse") want = true;
    if (want && dumped.insert(curProg).second && dumped.size() <= 8) {
      DQ_LOG("ubo prog %u size %d ssbos %zu", (unsigned)curProg, p.uboSize, p.ssbos.size());
      for (auto& u : p.unis) {
        if (u.sampler || u.offset < 0) continue;
        if (u.name == "near" || u.name == "far" || u.name == "renderDistance") {
          if (u.offset + 4 > (int)p.ubo.size()) continue;
          float v; std::memcpy(&v, p.ubo.data() + u.offset, 4);
          DQ_LOG("  %s off %d %.4f", u.name.c_str(), u.offset, v);
          continue;
        }
        if (u.name != "gbufferProjection" && u.name != "gbufferProjectionInverse" && u.name != "gbufferModelView") continue;
        if (u.offset + 64 > (int)p.ubo.size()) { DQ_LOG("  %s off %d OOB", u.name.c_str(), u.offset); continue; }
        const float* m = reinterpret_cast<const float*>(p.ubo.data() + u.offset);
        DQ_LOG("  %s off %d [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]",
               u.name.c_str(), u.offset,
               m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
      }
    }
  }
}

static void drawCommon(GLenum mode, bool indexed, GLsizei count, GLenum indexType, const void* indices, GLint baseVertex, GLsizei instanceCount, GLuint indirect, GLsizei indirectCount, GLsizei indirectStride) {
  if (std::getenv("DQ_VK_NODRAW")) return;
  if (skipFrame || !frameOpen || !recording || count < 0) return;
  Prog* p = progOf(curProg);
  if (std::getenv("DQ_UBO_DUMP")) {
    static std::set<GLuint> reached;
    if (reached.insert(curProg).second && reached.size() <= 40)
      DQ_LOG("draw reach prog %u linked %d vao %u outs %d indirect %u", (unsigned)curProg, p && p->linked ? 1 : 0, (unsigned)curVao, p ? p->colorOuts : -1, (unsigned)indirect);
  }
  if (!p || !p->linked) return;
  Vao* vao = vaoOf(curVao);
  if (!vao) return;
  ColorAtt cols[8];
  int nCol = 0;
  fboColors(drawFbo, p, cols, nCol);
  for (auto& s : p->samplers) {
    int unit = std::clamp(s.unit, 0, 31);
    GLenum target = (s.type == GL_SAMPLER_2D_ARRAY || s.type == GL_SAMPLER_2D_ARRAY_SHADOW) ? GL_TEXTURE_2D_ARRAY : s.type == GL_SAMPLER_3D ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    GLuint id = target == GL_TEXTURE_2D_ARRAY ? bindArr[unit] : target == GL_TEXTURE_3D ? bind3D[unit] : bind2D[unit];
    if (Tex* t = texOf(id)) {
      // Before beginRendering. The self-test submits the command buffer, and a pipeline
      // bound earlier in this draw would not survive that.
      bool shadow = s.type == GL_SAMPLER_2D_SHADOW || s.type == GL_SAMPLER_2D_ARRAY_SHADOW;
      if (t->depth && !shadow) depthColorForSample(*t);
      bool isTarget = drawFbo == 0 ? t == fbColor : false;
      if (drawFbo != 0) for (int ci = 0; ci < nCol; ci++) if (cols[ci].tex == t) isTarget = true;
      if (t->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && isTarget) transitionAll(*t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }
  beginRendering(p);
  if (!rendering) return;

  struct Bnd { GLuint buf; VkDeviceSize off; uint32_t stride; VkVertexInputRate rate; bool konst; };
  std::vector<Bnd> bnds;
  std::vector<VkVertexInputAttributeDescription> attrs;
  auto bindingFor = [&](GLuint buffer, VkDeviceSize off, uint32_t stride, uint32_t div, bool konst) -> uint32_t {
    for (uint32_t i = 0; i < bnds.size(); i++)
      if (bnds[i].buf == buffer && bnds[i].off == off && bnds[i].stride == stride && bnds[i].konst == konst && ((bnds[i].rate == VK_VERTEX_INPUT_RATE_INSTANCE) == (div > 0))) return i;
    bnds.push_back({buffer, off, stride, div ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX, konst});
    return (uint32_t)bnds.size() - 1;
  };
  std::set<int> seen;
  for (int loc : p->inputs) {
    if (loc < 0 || loc >= 16 || seen.count(loc)) continue;
    seen.insert(loc);
    Attrib* a = &vao->a[loc];
    VkVertexInputAttributeDescription ad{};
    ad.location = (uint32_t)loc;
    if (a->on && bufOf(a->buffer)) {
      uint32_t stride = (uint32_t)(a->stride ? a->stride : 16);
      VkDeviceSize base = stride ? (VkDeviceSize)(a->offset / stride) * stride : 0;
      uint32_t rel = (uint32_t)(a->offset - base);
      ad.binding = bindingFor(a->buffer, base, a->divisor ? stride : stride, a->divisor, false);
      ad.format = attribFormat(*a);
      ad.offset = rel;
    } else {
      ad.binding = bindingFor(0, 0, 0, 0, true);
      ad.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      ad.offset = (uint32_t)loc * 16;
    }
    attrs.push_back(ad);
  }
  Alloc konst{};
  bool needKonst = false;
  for (auto& b : bnds) if (b.konst) needKonst = true;
  if (needKonst) {
    konst = bumpAlloc(16 * 16, 256);
    if (konst.ptr) {
      memcpy(konst.ptr, curAttr, sizeof(curAttr));
      hostBarrier(konst.buf, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
    }
  }
  std::vector<VkVertexInputBindingDescription> vb(bnds.size());
  std::vector<VkBuffer> vbuf(bnds.size());
  std::vector<VkDeviceSize> voff(bnds.size());
  for (size_t i = 0; i < bnds.size(); i++) {
    vb[i].binding = (uint32_t)i;
    vb[i].stride = bnds[i].konst ? 0 : bnds[i].stride;
    vb[i].inputRate = bnds[i].rate;
    if (bnds[i].konst) { vbuf[i] = konst.buf; voff[i] = konst.off; }
    else if (Buf* b = bufOf(bnds[i].buf)) { markBuf(bnds[i].buf); vbuf[i] = b->gpu.buffer; voff[i] = bnds[i].off; }
  }
  // stride 0 is rejected by some drivers: give constants a real stride and a per-vertex repeat via a 1-vertex instance rate
  for (size_t i = 0; i < bnds.size(); i++) if (bnds[i].konst) { vb[i].stride = 256; vb[i].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE; }

  Tex* depth = depthOf(drawFbo);
  if (depthTest && !depth) {
    static int missDepth = 0;
    if (missDepth++ < 3) DQ_LOG("vulkan: depth test enabled but this framebuffer has no depth image");
  }
  uint32_t mask = 0;
  if (colorMask[0]) mask |= VK_COLOR_COMPONENT_R_BIT;
  if (colorMask[1]) mask |= VK_COLOR_COMPONENT_G_BIT;
  if (colorMask[2]) mask |= VK_COLOR_COMPONENT_B_BIT;
  if (colorMask[3]) mask |= VK_COLOR_COMPONENT_A_BIT;
  PipeKey key{};
  key.w[0] = curProg;
  key.w[1] = (uint32_t)nCol;
  key.w[2] = depth ? (uint32_t)depth->format : 0;
  key.w[3] = blendMask;
  key.w[4] = mask;
  key.w[5] = (uint32_t)blendFactor(blendSrc);
  key.w[6] = (uint32_t)blendFactor(blendDst);
  key.w[7] = (uint32_t)blendOp(blendEq);
  key.w[8] = (uint32_t)blendFactor(blendSrcA);
  key.w[9] = (uint32_t)blendFactor(blendDstA);
  key.w[10] = (uint32_t)blendOp(blendEqA);
  for (int i = 0; i < 8; i++) key.w[11 + i] = i < nCol ? (uint32_t)cols[i].format : 0;
  uint32_t n = 19;
  key.w[n++] = (uint32_t)attrs.size();
  key.w[n++] = (uint32_t)vb.size();
  for (auto& a : attrs) { if (n + 4 >= key.w.size()) break; key.w[n++] = a.location; key.w[n++] = a.binding; key.w[n++] = (uint32_t)a.format; key.w[n++] = a.offset; }
  for (auto& b : vb) { if (n + 2 >= key.w.size()) break; key.w[n++] = b.stride; key.w[n++] = (uint32_t)b.inputRate; }

  // A sampler still attached (the shadow map the gbuffer is about to read) leaves
  // attachment layout here. That ends the pass. Depth test, compare, and cull are
  // dynamic, and the pipeline's static depth test is off, so they have to be set
  // again inside the pass that records the draw. Otherwise a gbuffer's GL_EQUAL
  // test is dropped and every face is painted over the scene.
  VkPipeline pipe = pipelineFor(*p, key, vb, attrs);
  if (!pipe) return;
  if (std::getenv("DQ_VK_PIPEONLY")) return;
  bindDescriptors(*p);
  if (!rendering) beginRendering(p);
  if (!rendering) return;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  applyDynamic(mode);
  if (!vbuf.empty()) vkCmdBindVertexBuffers(cmd, 0, (uint32_t)vbuf.size(), vbuf.data(), voff.data());
  if (indexed && !indirect) {
    Buf* ib = bufOf(vao->elem);
    if (!ib || !ib->gpu.buffer) return;
    markBuf(vao->elem);
    VkIndexType it = indexType == GL_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd, ib->gpu.buffer, 0, it);
    uint32_t indexSize = it == VK_INDEX_TYPE_UINT16 ? 2u : 4u;
    uint32_t first = (uint32_t)((uintptr_t)indices / indexSize);
    vkCmdDrawIndexed(cmd, (uint32_t)count, (uint32_t)std::max(1, instanceCount), first, baseVertex, 0);
  } else if (indirect) {
    Buf* ib = bufOf(vao->elem);
    Buf* db = bufOf(indirect);
    if (!ib || !db) return;
    markBuf(vao->elem); markBuf(indirect);
    vkCmdBindIndexBuffer(cmd, ib->gpu.buffer, 0, indexType == GL_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    uint32_t stride = indirectStride ? (uint32_t)indirectStride : (uint32_t)sizeof(VkDrawIndexedIndirectCommand);
    vkCmdDrawIndexedIndirect(cmd, db->gpu.buffer, (VkDeviceSize)(uintptr_t)indices, (uint32_t)indirectCount, stride);
  } else {
    vkCmdDraw(cmd, (uint32_t)count, (uint32_t)std::max(1, instanceCount), (uint32_t)baseVertex, 0);
  }
  if (depthWrite) if (Tex* dw = depthOf(drawFbo)) dw->depthColorDirty = true;
}

// ---------------------------------------------------------------- framebuffer size

static void fitFbo(Fbo& f) {
  f.w = f.h = 0;
  auto take = [&](GLuint id) {
    if (f.w > 0 || !id) return;
    if (Tex* c = texOf(id)) { f.w = c->w; f.h = c->h; }
  };
  take(f.color);
  for (int i = 0; i < 8; i++) take(f.colorAtt[i]);
  if (f.w > 0) return;
  if (Tex* d = rbOf(f.depthRb)) { f.w = d->w; f.h = d->h; }
  else if (Tex* d = texOf(f.depthTex)) { f.w = d->w; f.h = d->h; }
}
static Tex* colorOf(GLuint fbo) {
  if (fbo == 0) return fbColor;
  Fbo* f = fboOf(fbo);
  return f ? texOf(f->color) : nullptr;
}
static Tex* depthOf(GLuint fbo) {
  if (fbo == 0) return fbDepth;
  Fbo* f = fboOf(fbo);
  if (!f) return nullptr;
  if (Tex* d = rbOf(f->depthRb)) return d;
  return texOf(f->depthTex);
}
static int targetW(GLuint fbo) { if (fbo == 0) return swapW; Fbo* f = fboOf(fbo); return f ? f->w : 0; }
static int targetH(GLuint fbo) { if (fbo == 0) return swapH; Fbo* f = fboOf(fbo); return f ? f->h : 0; }

static void destroyViewList(std::vector<VkImageView>& v) { for (auto i : v) vkDestroyImageView(device, i, nullptr); v.clear(); }

static bool createOffscreen() {
  auto make = [](Tex*& slot, bool depth) {
    if (!slot) slot = new Tex();
    destroyTexGpu(*slot);
    slot->alive = true;
    slot->w = std::max(1, swapW); slot->h = std::max(1, swapH); slot->d = 1; slot->layers = 1; slot->mips = 1;
    slot->target = GL_TEXTURE_2D;
    slot->depth = depth;
    slot->format = depth ? depthFmt : swapFmt;
    return makeImage(*slot);
  };
  return make(fbColor, false) && make(fbDepth, true);
}

static void destroySwapchain() {
  destroyViewList(swapViews);
  swapImages.clear(); swapLayout.clear();
  if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
}

static bool createSwapchain(SDL_Window* window) {
  destroySwapchain();
  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
  uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nf, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(nf);
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nf, formats.data());
  swapFmt = VK_FORMAT_B8G8R8A8_UNORM; swapSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  for (auto& f : formats) if (f.format == VK_FORMAT_R8G8B8A8_UNORM) { swapFmt = f.format; swapSpace = f.colorSpace; break; }
  if (swapFmt == VK_FORMAT_B8G8R8A8_UNORM) for (auto& f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { swapSpace = f.colorSpace; break; }
  DQ_LOG("swapchain %s", swapFmt == VK_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : swapFmt == VK_FORMAT_R8G8B8A8_UNORM ? "RGBA8" : "other");
  uint32_t np = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &np, nullptr);
  std::vector<VkPresentModeKHR> modes(np);
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &np, modes.data());
  presentMode = VK_PRESENT_MODE_FIFO_KHR;
  if (!wantVsync) {
    for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) presentMode = m;
    if (presentMode == VK_PRESENT_MODE_FIFO_KHR) for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
  }
  int dw = 0, dh = 0; SDL_GL_GetDrawableSize(window, &dw, &dh);
  if (caps.currentExtent.width != 0xFFFFFFFFu) { swapW = (int)caps.currentExtent.width; swapH = (int)caps.currentExtent.height; }
  else { swapW = std::clamp(dw, (int)caps.minImageExtent.width, (int)caps.maxImageExtent.width); swapH = std::clamp(dh, (int)caps.minImageExtent.height, (int)caps.maxImageExtent.height); }
  if (swapW <= 0 || swapH <= 0) return false;
  uint32_t count = std::max(2u, caps.minImageCount);
  if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface;
  sci.minImageCount = count;
  sci.imageFormat = swapFmt;
  sci.imageColorSpace = swapSpace;
  sci.imageExtent = {(uint32_t)swapW, (uint32_t)swapH};
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) sci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  }
  sci.presentMode = presentMode;
  sci.clipped = VK_TRUE;
  if (vkFail(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain), "swapchain")) return false;
  uint32_t ni = 0; vkGetSwapchainImagesKHR(device, swapchain, &ni, nullptr);
  swapImages.resize(ni);
  vkGetSwapchainImagesKHR(device, swapchain, &ni, swapImages.data());
  swapViews.resize(ni); swapLayout.assign(ni, VK_IMAGE_LAYOUT_UNDEFINED);
  for (uint32_t i = 0; i < ni; i++) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = swapImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = swapFmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &vi, nullptr, &swapViews[i]);
  }
  depthFmt = pickDepth();
  if (!createOffscreen()) return false;
  vpW = swapW; vpH = swapH; scW = swapW; scH = swapH;
  return true;
}

// ---------------------------------------------------------------- GL entry points

static void GLAD_API_PTR dq_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { vpX = x; vpY = y; vpW = w; vpH = h; }
static void GLAD_API_PTR dq_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { scX = x; scY = y; scW = w; scH = h; }
static void GLAD_API_PTR dq_glEnable(GLenum c) {
  if (c == GL_DEPTH_TEST) depthTest = true;
  else if (c == GL_CULL_FACE) cullOn = true;
  else if (c == GL_BLEND) { blendOn = true; blendMask = 0xFFu; }
  else if (c == GL_SCISSOR_TEST) scissorOn = true;
  else if (c == GL_STENCIL_TEST) stencilOn = true;
  else if (c == GL_POLYGON_OFFSET_FILL) polyOffset = true;
  else if (c == GL_PRIMITIVE_RESTART) primRestart = true;
}
static void GLAD_API_PTR dq_glDisable(GLenum c) {
  if (c == GL_DEPTH_TEST) depthTest = false;
  else if (c == GL_CULL_FACE) cullOn = false;
  else if (c == GL_BLEND) { blendOn = false; blendMask = 0; }
  else if (c == GL_SCISSOR_TEST) scissorOn = false;
  else if (c == GL_STENCIL_TEST) stencilOn = false;
  else if (c == GL_POLYGON_OFFSET_FILL) polyOffset = false;
  else if (c == GL_PRIMITIVE_RESTART) primRestart = false;
}
static GLboolean GLAD_API_PTR dq_glIsEnabled(GLenum c) {
  if (c == GL_DEPTH_TEST) return depthTest;
  if (c == GL_CULL_FACE) return cullOn;
  if (c == GL_BLEND) return blendOn;
  if (c == GL_SCISSOR_TEST) return scissorOn;
  if (c == GL_STENCIL_TEST) return stencilOn;
  if (c == GL_POLYGON_OFFSET_FILL) return polyOffset;
  if (c == GL_PRIMITIVE_RESTART) return primRestart;
  return GL_FALSE;
}
static void GLAD_API_PTR dq_glDepthFunc(GLenum f) { depthFuncE = f; }
static void GLAD_API_PTR dq_glDepthMask(GLboolean m) { depthWrite = m == GL_TRUE; }
static void GLAD_API_PTR dq_glCullFace(GLenum f) { cullFaceE = f; }
static void GLAD_API_PTR dq_glFrontFace(GLenum f) { frontFaceE = f; }
static void GLAD_API_PTR dq_glEnablei(GLenum c, GLuint i) {
  if (c == GL_BLEND && i < 8) { blendMask = (uint8_t)(blendMask | (uint8_t)(1u << i)); blendOn = blendMask != 0; }
}
static void GLAD_API_PTR dq_glDisablei(GLenum c, GLuint i) {
  if (c == GL_BLEND && i < 8) { blendMask = (uint8_t)(blendMask & (uint8_t)~(1u << i)); blendOn = blendMask != 0; }
}
static void GLAD_API_PTR dq_glBlendFunc(GLenum s, GLenum d) { blendSrc = blendSrcA = s; blendDst = blendDstA = d; }
static void GLAD_API_PTR dq_glBlendFuncSeparate(GLenum s, GLenum d, GLenum sa, GLenum da) { blendSrc = s; blendDst = d; blendSrcA = sa; blendDstA = da; }
static void GLAD_API_PTR dq_glBlendEquation(GLenum e) { blendEq = blendEqA = e; }
static void GLAD_API_PTR dq_glBlendEquationSeparate(GLenum rgb, GLenum a) { blendEq = rgb; blendEqA = a; }
static void GLAD_API_PTR dq_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { colorMask[0] = r; colorMask[1] = g; colorMask[2] = b; colorMask[3] = a; }
static void GLAD_API_PTR dq_glPolygonOffset(GLfloat factor, GLfloat units) { polyFactor = factor; polyUnits = units; }
static void GLAD_API_PTR dq_glPolygonMode(GLenum, GLenum) {}
static void GLAD_API_PTR dq_glStencilFunc(GLenum func, GLint ref, GLuint mask) { stenFunc = func; stenRef = ref; stenCmpMask = (GLint)mask; }
static void GLAD_API_PTR dq_glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) { stenSFail = sfail; stenDPFail = dpfail; stenDPPass = dppass; }
static void GLAD_API_PTR dq_glStencilMask(GLuint mask) { stenWriteMask = (GLint)mask; }
static void GLAD_API_PTR dq_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { clearR = r; clearG = g; clearB = b; clearA = a; }
static void GLAD_API_PTR dq_glClearDepth(GLdouble d) { clearDepth = (float)d; }
static void GLAD_API_PTR dq_glClearDepthf(GLfloat d) { clearDepth = d; }
static void GLAD_API_PTR dq_glClear(GLbitfield mask) {
  if (!recording) return;
  // A pack program's extra outputs must not widen this clear. The draw that follows
  // restarts the pass if it needs more attachments, and LOAD keeps the cleared texels.
  beginRendering(nullptr);
  if (!rendering) return;
  VkClearAttachment at[9]{};
  uint32_t n = 0;
  if (mask & GL_COLOR_BUFFER_BIT) {
    ColorAtt cols[8];
    int nCol = 0;
    fboColors(drawFbo, nullptr, cols, nCol);
    for (int i = 0; i < nCol && n < 8; i++) {
      if (!cols[i].tex) continue;
      at[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      at[n].colorAttachment = (uint32_t)i;
      at[n].clearValue.color.float32[0] = clearR; at[n].clearValue.color.float32[1] = clearG;
      at[n].clearValue.color.float32[2] = clearB; at[n].clearValue.color.float32[3] = clearA;
      n++;
    }
  }
  if (depthOf(drawFbo) && (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))) {
    if (mask & GL_DEPTH_BUFFER_BIT) at[n].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if ((mask & GL_STENCIL_BUFFER_BIT) && (aspectOf(*depthOf(drawFbo)) & VK_IMAGE_ASPECT_STENCIL_BIT)) at[n].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    at[n].clearValue.depthStencil.depth = clearDepth;
    at[n].clearValue.depthStencil.stencil = 0;
    if (at[n].aspectMask) {
      if (Tex* dw = depthOf(drawFbo)) dw->depthColorDirty = true;
      n++;
    }
  }
  if (!n) return;
  VkClearRect rc{};
  rc.layerCount = 1;
  if (scissorOn) { rc.rect.offset = {scX, scY}; rc.rect.extent = {(uint32_t)std::max(0, scW), (uint32_t)std::max(0, scH)}; }
  else { rc.rect.extent = {(uint32_t)std::max(1, targetW(drawFbo)), (uint32_t)std::max(1, targetH(drawFbo))}; }
  vkCmdClearAttachments(cmd, n, at, 1, &rc);
}
static void GLAD_API_PTR dq_glActiveTexture(GLenum t) { activeUnit = (int)(t - GL_TEXTURE0); if (activeUnit < 0 || activeUnit > 31) activeUnit = 0; }
static void GLAD_API_PTR dq_glBindTexture(GLenum target, GLuint id) {
  if (target == GL_TEXTURE_2D_ARRAY) bindArr[activeUnit] = id;
  else if (target == GL_TEXTURE_3D) bind3D[activeUnit] = id;
  else bind2D[activeUnit] = id;
}
static void GLAD_API_PTR dq_glBindSampler(GLuint, GLuint) {}
static void GLAD_API_PTR dq_glPixelStorei(GLenum pname, GLint param) {
  if (pname == GL_UNPACK_ALIGNMENT) unpackAlign = param > 0 ? param : 4;
  else if (pname == GL_UNPACK_ROW_LENGTH) unpackRowLength = param;
}
static void GLAD_API_PTR dq_glGenTextures(GLsizei n, GLuint* out) { for (GLsizei i = 0; i < n; i++) out[i] = allocId(texs, freeTex); }
static void GLAD_API_PTR dq_glDeleteTextures(GLsizei n, const GLuint* ids) {
  for (GLsizei i = 0; i < n; i++) {
    if (Tex* t = texOf(ids[i])) { destroyTexGpu(*t); *t = {}; freeTex.push_back(ids[i]); }
    for (int u = 0; u < 32; u++) { if (bind2D[u] == ids[i]) bind2D[u] = 0; if (bindArr[u] == ids[i]) bindArr[u] = 0; if (bind3D[u] == ids[i]) bind3D[u] = 0; }
  }
}
static void GLAD_API_PTR dq_glTexParameteri(GLenum target, GLenum pname, GLint param) {
  Tex* t = boundTex(target); if (!t) return;
  if (pname == GL_TEXTURE_MIN_FILTER) t->minF = (GLenum)param;
  else if (pname == GL_TEXTURE_MAG_FILTER) t->magF = (GLenum)param;
  else if (pname == GL_TEXTURE_WRAP_S) t->wrapS = (GLenum)param;
  else if (pname == GL_TEXTURE_WRAP_T) t->wrapT = (GLenum)param;
  else if (pname == GL_TEXTURE_WRAP_R) t->wrapR = (GLenum)param;
  else if (pname == GL_TEXTURE_MAX_LEVEL) t->maxLevel = param;
  else if (pname == GL_TEXTURE_BASE_LEVEL) { /* base 0 */ }
}
static void GLAD_API_PTR dq_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint, GLenum format, GLenum type, const void* data) {
  Tex* t = boundTex(target == GL_TEXTURE_2D_ARRAY ? GL_TEXTURE_2D : target);
  if (target != GL_TEXTURE_2D && target != GL_PROXY_TEXTURE_2D) t = boundTex(GL_TEXTURE_2D);
  if (!t) return;
  defineTex(*t, GL_TEXTURE_2D, level, (GLenum)internal, w, h, 1, 1, format, type, data);
}
static void GLAD_API_PTR dq_glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d, GLint, GLenum format, GLenum type, const void* data) {
  Tex* t = boundTex(target); if (!t) return;
  if (target == GL_TEXTURE_2D_ARRAY) defineTex(*t, GL_TEXTURE_2D_ARRAY, level, (GLenum)internal, w, h, 1, d, format, type, data);
  else defineTex(*t, GL_TEXTURE_3D, level, (GLenum)internal, w, h, d, 1, format, type, data);
}
static void GLAD_API_PTR dq_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum, GLenum type, const void* data) {
  Tex* t = boundTex(target); if (!t) return;
  uploadImage(*t, level, x, y, 0, w, h, 1, data, type);
}
static void GLAD_API_PTR dq_glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum, GLenum type, const void* data) {
  Tex* t = boundTex(target); if (!t) return;
  uploadImage(*t, level, x, y, z, w, h, d, data, type);
}
static void GLAD_API_PTR dq_glGenerateMipmap(GLenum target) {
  Tex* t = boundTex(target); if (!t || !t->image || t->depth || t->mips <= 1) return;
  GpuScope gpu; if (!recording) return;
  if (rendering) endRendering();
  auto barrierMip = [&](int mip, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t->image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = (uint32_t)mip; b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = t->target == GL_TEXTURE_3D ? 1u : (uint32_t)std::max(1, t->layers);
    VkPipelineStageFlags ss = VK_PIPELINE_STAGE_TRANSFER_BIT, ds = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) { b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; ss = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; }
    else if (from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    else if (from == VK_IMAGE_LAYOUT_UNDEFINED) b.srcAccessMask = 0;
    else b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = to == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
  };
  // Upload leaves every mip in t->layout (usually shader-read). Those levels are real
  // subresources; calling them UNDEFINED discards the blit on some drivers, so distant
  // tiles sample an empty mip and the land breaks up.
  VkImageLayout cur = t->layout;
  for (int mip = 1; mip < t->mips; mip++) {
    barrierMip(mip - 1, mip == 1 ? cur : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrierMip(mip, cur, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    int layers = t->target == GL_TEXTURE_2D_ARRAY ? t->layers : 1;
    int depth = t->target == GL_TEXTURE_3D ? std::max(1, t->d >> (mip - 1)) : 1;
    for (int layer = 0; layer < layers; layer++) {
      VkImageBlit blit{};
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = (uint32_t)(mip - 1);
      blit.srcSubresource.baseArrayLayer = (uint32_t)layer; blit.srcSubresource.layerCount = 1;
      blit.srcOffsets[1] = {std::max(1, t->w >> (mip - 1)), std::max(1, t->h >> (mip - 1)), std::max(1, depth)};
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = (uint32_t)mip;
      blit.dstSubresource.baseArrayLayer = (uint32_t)layer; blit.dstSubresource.layerCount = 1;
      blit.dstOffsets[1] = {std::max(1, t->w >> mip), std::max(1, t->h >> mip), std::max(1, t->target == GL_TEXTURE_3D ? t->d >> mip : 1)};
      vkCmdBlitImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }
  }
  for (int mip = 0; mip < t->mips; mip++) {
    VkImageLayout from = mip == t->mips - 1 ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t->image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = (uint32_t)mip; b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = t->target == GL_TEXTURE_3D ? 1u : (uint32_t)std::max(1, t->layers);
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  t->maxLevel = t->mips - 1;
  t->defined = true;
  makeViews(*t, t->mips);
}
static void GLAD_API_PTR dq_glGenRenderbuffers(GLsizei n, GLuint* out) { for (GLsizei i = 0; i < n; i++) out[i] = allocId(rbs, freeRb); }
static void GLAD_API_PTR dq_glDeleteRenderbuffers(GLsizei n, const GLuint* ids) {
  for (GLsizei i = 0; i < n; i++) if (Tex* t = rbOf(ids[i])) { destroyTexGpu(*t); *t = {}; freeRb.push_back(ids[i]); }
}
static GLuint curRb = 0;
static void GLAD_API_PTR dq_glBindRenderbuffer(GLenum, GLuint id) { curRb = id; }
static void GLAD_API_PTR dq_glRenderbufferStorage(GLenum, GLenum internal, GLsizei w, GLsizei h) {
  Tex* t = rbOf(curRb); if (!t) return;
  defineTex(*t, GL_TEXTURE_2D, 0, internal, w, h, 1, 1, internal, GL_UNSIGNED_BYTE, nullptr);
}
static void GLAD_API_PTR dq_glGenFramebuffers(GLsizei n, GLuint* out) { for (GLsizei i = 0; i < n; i++) out[i] = allocId(fbos, freeFbo); }
static void GLAD_API_PTR dq_glDeleteFramebuffers(GLsizei n, const GLuint* ids) {
  for (GLsizei i = 0; i < n; i++) if (Fbo* f = fboOf(ids[i])) { *f = {}; freeFbo.push_back(ids[i]); if (drawFbo == ids[i]) drawFbo = 0; if (readFbo == ids[i]) readFbo = 0; }
}
static void GLAD_API_PTR dq_glBindFramebuffer(GLenum target, GLuint id) {
  if (rendering && drawFbo != id && (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)) endRendering();
  if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) drawFbo = id;
  if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) readFbo = id;
}
static void GLAD_API_PTR dq_glFramebufferTexture2D(GLenum, GLenum attachment, GLenum, GLuint tex, GLint) {
  Fbo* f = fboOf(drawFbo); if (!f) return;
  if (rendering && renderTarget == drawFbo) endRendering();
  if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 8) {
    int i = (int)(attachment - GL_COLOR_ATTACHMENT0);
    f->colorAtt[i] = tex;
    if (i == 0) f->color = tex;
  } else if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
    f->depthTex = tex; f->depthRb = 0;
  }
  fitFbo(*f);
}
static void GLAD_API_PTR dq_glFramebufferRenderbuffer(GLenum, GLenum attachment, GLenum, GLuint rb) {
  Fbo* f = fboOf(drawFbo); if (!f) return;
  if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) { f->depthRb = rb; f->depthTex = 0; }
  fitFbo(*f);
}
static GLenum GLAD_API_PTR dq_glCheckFramebufferStatus(GLenum) {
  if (drawFbo == 0) return GL_FRAMEBUFFER_COMPLETE;
  Fbo* f = fboOf(drawFbo);
  if (!f) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  bool color = f->color != 0;
  if (!color) for (int i = 0; i < 8; i++) if (f->colorAtt[i]) color = true;
  if (!color && !f->depthRb && !f->depthTex) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  return f->w > 0 && f->h > 0 ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
}
static void GLAD_API_PTR dq_glDrawBuffers(GLsizei n, const GLenum* bufs) {
  if (drawFbo == 0) return;
  Fbo* f = fboOf(drawFbo);
  if (!f) return;
  if (rendering && renderTarget == drawFbo) endRendering();
  int count = std::clamp((int)n, 0, 8);
  f->nDraw = 0;
  if (!bufs) return;
  for (int i = 0; i < count; i++) {
    if (bufs[i] >= GL_COLOR_ATTACHMENT0 && bufs[i] < GL_COLOR_ATTACHMENT0 + 8)
      f->drawAtt[f->nDraw++] = (int)(bufs[i] - GL_COLOR_ATTACHMENT0);
    else
      f->drawAtt[f->nDraw++] = -1;
  }
}
static void GLAD_API_PTR dq_glDrawBuffer(GLenum buf) { dq_glDrawBuffers(1, &buf); }

static void copyToTex(Tex& dst, int dx, int dy, int sx, int sy, int w, int h) {
  Tex* src = colorOf(readFbo);
  if (dst.depth) src = depthOf(readFbo);
  if (!src || !src->image || !dst.image || w <= 0 || h <= 0) return;
  GpuScope gpu; if (!recording) return;
  if (rendering) endRendering();
  // A second copy of an image already in transfer-src used to skip the barrier.
  // The source was then read with no dependency on the first copy, and the
  // translucent depth came back clear. LEQUAL treated that as "far" and drew water over land.
  const bool srcHeld = src->layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  transitionAll(*src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  if (srcHeld && src->layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = src->image;
    b.subresourceRange.aspectMask = aspectOf(*src);
    b.subresourceRange.levelCount = (uint32_t)std::max(1, src->mips);
    b.subresourceRange.layerCount = src->target == GL_TEXTURE_3D ? 1u : (uint32_t)std::max(1, src->layers);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  transitionAll(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  // A copy keeps the bits and ignores the format, so a BGRA frame dropped into an RGBA
  // texture comes back with red and blue swapped. Blit converts the components.
  if (!src->depth && !dst.depth && src->format != dst.format) {
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; blit.srcSubresource.layerCount = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; blit.dstSubresource.layerCount = 1;
    blit.srcOffsets[0] = {sx, sy, 0}; blit.srcOffsets[1] = {sx + w, sy + h, 1};
    blit.dstOffsets[0] = {dx, dy, 0}; blit.dstOffsets[1] = {dx + w, dy + h, 1};
    vkCmdBlitImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
  } else {
    VkImageCopy c{};
    c.srcSubresource.aspectMask = copyAspect(*src); c.srcSubresource.layerCount = 1;
    c.dstSubresource.aspectMask = copyAspect(dst); c.dstSubresource.layerCount = 1;
    c.srcOffset = {sx, sy, 0}; c.dstOffset = {dx, dy, 0};
    c.extent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
  }
  transitionAll(dst, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  dst.defined = true;
  if (dst.depth) dst.depthColorDirty = true;
}
static void GLAD_API_PTR dq_glCopyTexSubImage2D(GLenum target, GLint, GLint xoff, GLint yoff, GLint x, GLint y, GLsizei w, GLsizei h) {
  Tex* t = boundTex(target); if (!t) return;
  copyToTex(*t, xoff, yoff, x, y, w, h);
}
static void GLAD_API_PTR dq_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
  Tex* src = (mask & GL_DEPTH_BUFFER_BIT) && !(mask & GL_COLOR_BUFFER_BIT) ? depthOf(readFbo) : colorOf(readFbo);
  Tex* dst = (mask & GL_DEPTH_BUFFER_BIT) && !(mask & GL_COLOR_BUFFER_BIT) ? depthOf(drawFbo) : colorOf(drawFbo);
  if (!src || !dst || !src->image || !dst->image) return;
  GpuScope gpu; if (!recording) return;
  if (rendering) endRendering();
  transitionAll(*src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  transitionAll(*dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkImageBlit blit{};
  blit.srcSubresource.aspectMask = copyAspect(*src); blit.srcSubresource.layerCount = 1;
  blit.dstSubresource.aspectMask = copyAspect(*dst); blit.dstSubresource.layerCount = 1;
  blit.srcOffsets[0] = {sx0, sy0, 0}; blit.srcOffsets[1] = {sx1, sy1, 1};
  blit.dstOffsets[0] = {dx0, dy0, 0}; blit.dstOffsets[1] = {dx1, dy1, 1};
  vkCmdBlitImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter == GL_LINEAR && !src->depth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
  transitionAll(*dst, dst == fbColor || dst == fbDepth ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  if (dst->depth && (dst == fbDepth || rbOf(drawFbo ? fboOf(drawFbo)->depthRb : 0) == dst)) dst->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  dst->defined = true;
  if (dst->depth) dst->depthColorDirty = true;
}
static void readback(Tex& t, void* dst, int bytes) {
  if (!t.image || !dst || bytes <= 0) return;
  GpuScope gpu; if (!recording) return;
  if (rendering) endRendering();
  transitionAll(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  auto a = bumpAlloc((VkDeviceSize)bytes, 256);
  if (!a.ptr) return;
  VkBufferImageCopy c{};
  c.bufferOffset = a.off;
  c.imageSubresource.aspectMask = copyAspect(t); c.imageSubresource.layerCount = 1;
  c.imageExtent = {(uint32_t)t.w, (uint32_t)t.h, 1};
  vkCmdCopyImageToBuffer(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, a.buf, 1, &c);
  VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.buffer = a.buf; b.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
  syncGpu();
  int have = t.w * t.h * bppOf(t.format);
  memcpy(dst, a.ptr, (size_t)std::min(bytes, have));
}
static void GLAD_API_PTR dq_glGetTexImage(GLenum target, GLint, GLenum, GLenum type, void* pixels) {
  Tex* t = boundTex(target); if (!t || !pixels) return;
  // Callers that ask for bytes (the pack dump) get unorm. The image itself stays float.
  if (type == GL_UNSIGNED_BYTE && halfFmt(t->format)) {
    int n = t->w * t->h;
    int c = compOf(t->format);
    std::vector<uint8_t> raw((size_t)n * (size_t)bppOf(t->format));
    readback(*t, raw.data(), (int)raw.size());
    auto* src = reinterpret_cast<const uint16_t*>(raw.data());
    auto* dst = (uint8_t*)pixels;
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < 4; k++) {
        float v = k < c ? f16to32(src[i * c + k]) : (k == 3 ? 1.f : 0.f);
        if (v < 0.f) v = 0.f; else if (v > 1.f) v = 1.f;
        dst[i * 4 + k] = (uint8_t)(v * 255.f + 0.5f);
      }
    }
    return;
  }
  readback(*t, pixels, t->w * t->h * bppOf(t->format));
}
static void GLAD_API_PTR dq_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum, GLenum, void* pixels) {
  Tex* t = colorOf(readFbo); if (!t || !pixels || w <= 0 || h <= 0) return;
  std::vector<uint8_t> full((size_t)t->w * t->h * 4);
  readback(*t, full.data(), (int)full.size());
  int bpp = 4;
  for (int row = 0; row < h; row++) {
    int sy = y + row;
    if (sy < 0 || sy >= t->h) continue;
    int copyW = std::min((int)w, t->w - x);
    if (x < 0 || copyW <= 0) continue;
    uint8_t* dst = (uint8_t*)pixels + (size_t)row * w * bpp;
    const uint8_t* src = full.data() + ((size_t)sy * t->w + x) * bpp;
    if (t->format == VK_FORMAT_B8G8R8A8_UNORM) {
      for (int col = 0; col < copyW; col++) {
        dst[col * 4 + 0] = src[col * 4 + 2];
        dst[col * 4 + 1] = src[col * 4 + 1];
        dst[col * 4 + 2] = src[col * 4 + 0];
        dst[col * 4 + 3] = src[col * 4 + 3];
      }
    } else {
      memcpy(dst, src, (size_t)copyW * bpp);
    }
  }
}

static void GLAD_API_PTR dq_glGenBuffers(GLsizei n, GLuint* out) { for (GLsizei i = 0; i < n; i++) out[i] = allocId(bufs, freeBuf); }
static void GLAD_API_PTR dq_glDeleteBuffers(GLsizei n, const GLuint* ids) {
  for (GLsizei i = 0; i < n; i++) {
    if (Buf* b = bufOf(ids[i])) {
      if (b->gpu.buffer) { if (b->referenced) retireGpuBuf(b->gpu); else destroyGpuBuf(b->gpu); }
      *b = {}; freeBuf.push_back(ids[i]);
    }
    if (ssboBuf == ids[i]) ssboBuf = 0;
    for (int s = 0; s < 16; s++) if (ssboBase[s] == ids[i]) ssboBase[s] = 0;
  }
}
static void GLAD_API_PTR dq_glBindBuffer(GLenum target, GLuint id) {
  if (target == GL_ARRAY_BUFFER) arrayBuf = id;
  else if (target == GL_ELEMENT_ARRAY_BUFFER) { if (Vao* v = vaoOf(curVao)) v->elem = id; }
  else if (target == GL_COPY_READ_BUFFER) copyRead = id;
  else if (target == GL_COPY_WRITE_BUFFER) copyWrite = id;
  else if (target == GL_DRAW_INDIRECT_BUFFER) indirectBuf = id;
  else if (target == GL_PIXEL_UNPACK_BUFFER) pixelUnpack = id;
  else if (target == GL_SHADER_STORAGE_BUFFER) ssboBuf = id;
}
static void GLAD_API_PTR dq_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  if (target != GL_SHADER_STORAGE_BUFFER || index >= 16) return;
  ssboBase[index] = buffer;
  ssboBuf = buffer;
}
static GLuint boundBuf(GLenum target) {
  if (target == GL_ARRAY_BUFFER) return arrayBuf;
  if (target == GL_ELEMENT_ARRAY_BUFFER) { Vao* v = vaoOf(curVao); return v ? v->elem : 0; }
  if (target == GL_COPY_READ_BUFFER) return copyRead;
  if (target == GL_COPY_WRITE_BUFFER) return copyWrite;
  if (target == GL_DRAW_INDIRECT_BUFFER) return indirectBuf;
  if (target == GL_PIXEL_UNPACK_BUFFER) return pixelUnpack;
  if (target == GL_SHADER_STORAGE_BUFFER) return ssboBuf;
  return arrayBuf;
}
static void GLAD_API_PTR dq_glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum) {
  Buf* b = bufOf(boundBuf(target)); if (!b) return;
  reallocBuf(*b, (VkDeviceSize)std::max<GLsizeiptr>(0, size), false);
  if (data && b->gpu.map && size > 0) memcpy(b->gpu.map, data, (size_t)size);
}
static void GLAD_API_PTR dq_glBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, const void* data) {
  Buf* b = bufOf(boundBuf(target)); if (!b || !data || size <= 0) return;
  if (b->referenced) reallocBuf(*b, b->gpu.size, true);
  if (b->gpu.map && (VkDeviceSize)off + (VkDeviceSize)size <= b->gpu.size) memcpy((uint8_t*)b->gpu.map + off, data, (size_t)size);
  b->dirty = true;
}
static void GLAD_API_PTR dq_glCopyBufferSubData(GLenum readT, GLenum writeT, GLintptr roff, GLintptr woff, GLsizeiptr size) {
  Buf* r = bufOf(boundBuf(readT)); Buf* w = bufOf(boundBuf(writeT));
  if (!r || !w || !r->gpu.map || size <= 0) return;
  if (w->referenced) reallocBuf(*w, w->gpu.size, true);
  if (w->gpu.map) memcpy((uint8_t*)w->gpu.map + woff, (uint8_t*)r->gpu.map + roff, (size_t)size);
  w->dirty = true;
}
static void GLAD_API_PTR dq_glGenVertexArrays(GLsizei n, GLuint* out) { for (GLsizei i = 0; i < n; i++) out[i] = allocId(vaos, freeVao); }
static void GLAD_API_PTR dq_glDeleteVertexArrays(GLsizei n, const GLuint* ids) {
  for (GLsizei i = 0; i < n; i++) if (ids[i] && vaoOf(ids[i])) { vaos[ids[i]] = {}; freeVao.push_back(ids[i]); if (curVao == ids[i]) curVao = 0; }
}
static void GLAD_API_PTR dq_glBindVertexArray(GLuint id) { curVao = id; }
static void GLAD_API_PTR dq_glEnableVertexAttribArray(GLuint i) { if (i < 16) if (Vao* v = vaoOf(curVao)) v->a[i].on = true; }
static void GLAD_API_PTR dq_glDisableVertexAttribArray(GLuint i) { if (i < 16) if (Vao* v = vaoOf(curVao)) v->a[i].on = false; }
static int typeBytes(GLenum t) {
  if (t == GL_UNSIGNED_BYTE || t == GL_BYTE) return 1;
  if (t == GL_UNSIGNED_SHORT || t == GL_SHORT) return 2;
  return 4;
}
static void setAttrib(GLuint index, GLint size, GLenum type, bool norm, GLsizei stride, const void* pointer, bool integer) {
  if (index >= 16) return;
  Vao* v = vaoOf(curVao); if (!v) return;
  Attrib& a = v->a[index];
  a.size = size; a.type = type; a.norm = norm; a.integer = integer;
  a.offset = (uintptr_t)pointer; a.buffer = arrayBuf;
  a.stride = stride ? stride : size * typeBytes(type);
}
static void GLAD_API_PTR dq_glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* p) { setAttrib(i, size, type, norm == GL_TRUE, stride, p, false); }
static void GLAD_API_PTR dq_glVertexAttribIPointer(GLuint i, GLint size, GLenum type, GLsizei stride, const void* p) { setAttrib(i, size, type, false, stride, p, true); }
static void GLAD_API_PTR dq_glVertexAttribDivisor(GLuint i, GLuint div) { if (i < 16) if (Vao* v = vaoOf(curVao)) v->a[i].divisor = div; }
static void GLAD_API_PTR dq_glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y) { if (i < 16) { curAttr[i][0] = x; curAttr[i][1] = y; curAttr[i][2] = curAttr[i][3] = 0; } }

static GLuint querySerial = 1;
static void GLAD_API_PTR dq_glDeleteQueries(GLsizei, const GLuint*) {}
static void GLAD_API_PTR dq_glBeginQuery(GLenum, GLuint) {}
static void GLAD_API_PTR dq_glEndQuery(GLenum) {}
static void GLAD_API_PTR dq_glGetQueryObjectiv(GLuint, GLenum pname, GLint* out) { if (out && pname == GL_QUERY_RESULT_AVAILABLE) *out = GL_TRUE; else if (out) *out = 0; }
static void GLAD_API_PTR dq_glGetQueryObjectui64v(GLuint, GLenum, GLuint64* out) { if (out) *out = 0; }

static GLuint GLAD_API_PTR dq_glCreateShader(GLenum type) { GLuint id = allocId(shaders, freeSh); if (ShaderObj* s = shOf(id)) s->type = type; return id; }
static void GLAD_API_PTR dq_glDeleteShader(GLuint id) { if (ShaderObj* s = shOf(id)) { *s = {}; freeSh.push_back(id); } }
static void GLAD_API_PTR dq_glShaderSource(GLuint id, GLsizei count, const GLchar* const* str, const GLint* len) {
  ShaderObj* s = shOf(id); if (!s) return;
  s->src.clear();
  for (GLsizei i = 0; i < count; i++) {
    if (!str[i]) continue;
    if (len && len[i] >= 0) s->src.append(str[i], str[i] + len[i]);
    else s->src += str[i];
  }
}
static void GLAD_API_PTR dq_glCompileShader(GLuint id) {
  ShaderObj* s = shOf(id); if (!s) return;
  std::string src = prepGlsl(s->src, s->type == GL_VERTEX_SHADER);
  s->log.clear();
  s->compiled = parseOne(src, s->type == GL_VERTEX_SHADER ? EShLangVertex : EShLangFragment, s->log);
  if (!s->compiled) DQ_ERR("vulkan shader compile failed:\n%s", s->log.c_str());
}
static void GLAD_API_PTR dq_glGetShaderiv(GLuint id, GLenum pname, GLint* out) {
  ShaderObj* s = shOf(id); if (!out) return;
  if (pname == GL_COMPILE_STATUS) *out = s && s->compiled ? GL_TRUE : GL_FALSE;
  else if (pname == GL_INFO_LOG_LENGTH) *out = s ? (GLint)s->log.size() + 1 : 1;
  else *out = 0;
}
static void GLAD_API_PTR dq_glGetShaderInfoLog(GLuint id, GLsizei max, GLsizei* len, GLchar* log) {
  ShaderObj* s = shOf(id);
  const std::string& t = s ? s->log : std::string();
  GLsizei n = (GLsizei)std::min((size_t)std::max(0, max - 1), t.size());
  if (log && max > 0) { if (n) memcpy(log, t.data(), n); log[n] = 0; }
  if (len) *len = n;
}
static GLuint GLAD_API_PTR dq_glCreateProgram() { return allocId(progs, freeProg); }
static void GLAD_API_PTR dq_glDeleteProgram(GLuint id) {
  if (Prog* p = progOf(id)) {
    std::vector<PipeKey> drop;
    for (auto& kv : pipelines) if (kv.first.w[0] == id) drop.push_back(kv.first);
    for (auto& k : drop) { retireGpu({.pipe = pipelines[k]}); pipelines.erase(k); }
    destroyProgGpu(*p);
    *p = {}; freeProg.push_back(id); if (curProg == id) curProg = 0;
  }
}
static void GLAD_API_PTR dq_glDetachShader(GLuint, GLuint) {}
static void GLAD_API_PTR dq_glAttachShader(GLuint prog, GLuint sh) {
  Prog* p = progOf(prog); ShaderObj* s = shOf(sh); if (!p || !s) return;
  if (s->type == GL_VERTEX_SHADER) p->vs = sh; else p->fs = sh;
}
static void GLAD_API_PTR dq_glLinkProgram(GLuint id) {
  Prog* p = progOf(id); if (!p) return;
  if (!linkProg(*p)) DQ_ERR("vulkan shader link failed:\n%s", p->log.c_str());
}
static void GLAD_API_PTR dq_glUseProgram(GLuint id) { curProg = id; }
static void GLAD_API_PTR dq_glGetProgramiv(GLuint id, GLenum pname, GLint* out) {
  Prog* p = progOf(id); if (!out) return;
  if (pname == GL_LINK_STATUS) *out = p && p->linked ? GL_TRUE : GL_FALSE;
  else if (pname == GL_INFO_LOG_LENGTH) *out = p ? (GLint)p->log.size() + 1 : 1;
  else if (pname == GL_ACTIVE_UNIFORMS) *out = p ? (GLint)p->unis.size() : 0;
  else if (pname == GL_ACTIVE_ATTRIBUTES) *out = p ? (GLint)p->inputs.size() : 0;
  else *out = 0;
}
static void GLAD_API_PTR dq_glGetProgramInfoLog(GLuint id, GLsizei max, GLsizei* len, GLchar* log) {
  Prog* p = progOf(id);
  const std::string& t = p ? p->log : std::string();
  GLsizei n = (GLsizei)std::min((size_t)std::max(0, max - 1), t.size());
  if (log && max > 0) { if (n) memcpy(log, t.data(), n); log[n] = 0; }
  if (len) *len = n;
}
static GLint GLAD_API_PTR dq_glGetUniformLocation(GLuint id, const GLchar* name) {
  Prog* p = progOf(id); if (!p || !name) return -1;
  auto it = p->uniName.find(name);
  if (it != p->uniName.end()) return it->second;
  std::string n(name);
  if (n.size() > 3 && n.ends_with("[0]")) {
    auto j = p->uniName.find(n.substr(0, n.size() - 3));
    if (j != p->uniName.end()) return j->second;
  } else {
    auto j = p->uniName.find(n + "[0]");
    if (j != p->uniName.end()) return j->second;
  }
  return -1;
}
static GLint GLAD_API_PTR dq_glGetAttribLocation(GLuint id, const GLchar* name) {
  Prog* p = progOf(id); if (!p || !name) return -1;
  auto it = p->attrName.find(name);
  return it == p->attrName.end() ? -1 : it->second;
}
static void GLAD_API_PTR dq_glGetActiveUniform(GLuint id, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
  Prog* p = progOf(id);
  if (!p || index >= p->unis.size()) { if (length) *length = 0; return; }
  const Uni& u = p->unis[index];
  if (size) *size = u.count;
  if (type) *type = u.type;
  GLsizei n = (GLsizei)std::min((size_t)std::max(0, bufSize - 1), u.name.size());
  if (name && bufSize > 0) { if (n) memcpy(name, u.name.data(), n); name[n] = 0; }
  if (length) *length = n;
}
static GLboolean GLAD_API_PTR dq_glIsProgram(GLuint id) { Prog* p = progOf(id); return p && p->linked ? GL_TRUE : GL_FALSE; }

static void writeUniform(GLint loc, const void* data, int count, int elemBytes) {
  Prog* p = progOf(curProg); if (!p || loc < 0 || loc >= (int)p->unis.size() || !data) return;
  Uni& u = p->unis[loc];
  if (u.sampler) return;
  int elem = elemBytes > 0 ? elemBytes : glTypeBytes(u.type);
  if (elem <= 0 || u.offset < 0) return;
  int stride = u.stride > elem ? u.stride : elem;
  for (int i = 0; i < count; i++) {
    int off = u.offset + i * stride;
    if (off < 0 || off + elem > (int)p->ubo.size()) break;
    memcpy(p->ubo.data() + off, (const char*)data + (size_t)i * elem, (size_t)elem);
  }
}
static void GLAD_API_PTR dq_glUniform1i(GLint loc, GLint v) {
  Prog* p = progOf(curProg); if (!p || loc < 0 || loc >= (int)p->unis.size()) return;
  Uni& u = p->unis[loc];
  if (u.sampler && u.samplerIndex >= 0 && u.samplerIndex < (int)p->samplers.size()) { p->samplers[u.samplerIndex].unit = v; return; }
  writeUniform(loc, &v, 1, 4);
}
static void GLAD_API_PTR dq_glUniform1f(GLint loc, GLfloat v) { writeUniform(loc, &v, 1, 4); }
static void GLAD_API_PTR dq_glUniform2f(GLint loc, GLfloat x, GLfloat y) { float a[2] = {x, y}; writeUniform(loc, a, 1, 8); }
static void GLAD_API_PTR dq_glUniform3f(GLint loc, GLfloat x, GLfloat y, GLfloat z) { float a[3] = {x, y, z}; writeUniform(loc, a, 1, 12); }
static void GLAD_API_PTR dq_glUniform2fv(GLint loc, GLsizei n, const GLfloat* v) { writeUniform(loc, v, n, 8); }
static void GLAD_API_PTR dq_glUniform2i(GLint loc, GLint x, GLint y) { int a[2] = {x, y}; writeUniform(loc, a, 1, 8); }
static void GLAD_API_PTR dq_glUniform3fv(GLint loc, GLsizei n, const GLfloat* v) { writeUniform(loc, v, n, 12); }
static void GLAD_API_PTR dq_glUniform4fv(GLint loc, GLsizei n, const GLfloat* v) { writeUniform(loc, v, n, 16); }
static void GLAD_API_PTR dq_glUniformMatrix4fv(GLint loc, GLsizei n, GLboolean, const GLfloat* v) { writeUniform(loc, v, n, 64); }

static void GLAD_API_PTR dq_glDrawArrays(GLenum mode, GLint first, GLsizei count) { drawCommon(mode, false, count, 0, nullptr, first, 1, 0, 0, 0); }
static void GLAD_API_PTR dq_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) { drawCommon(mode, true, count, type, indices, 0, 1, 0, 0, 0); }
static void GLAD_API_PTR dq_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base) { drawCommon(mode, true, count, type, indices, base, 1, 0, 0, 0); }
static void GLAD_API_PTR dq_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei prim) { drawCommon(mode, true, count, type, indices, 0, prim, 0, 0, 0); }
static void GLAD_API_PTR dq_glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect, GLsizei drawCount, GLsizei stride) {
  drawCommon(mode, false, 0, type, indirect, 0, 1, indirectBuf, drawCount, stride);
}

static GLenum GLAD_API_PTR dq_glGetError() { GLenum e = lastErr; lastErr = GL_NO_ERROR; return e; }
static const GLubyte* GLAD_API_PTR dq_glGetString(GLenum name) {
  if (name == GL_VENDOR) return (const GLubyte*)gfx.vendor.c_str();
  if (name == GL_RENDERER) return (const GLubyte*)gfx.renderer.c_str();
  if (name == GL_VERSION) return (const GLubyte*)"4.6.0 Vulkan";
  if (name == GL_SHADING_LANGUAGE_VERSION) return (const GLubyte*)"4.60 Vulkan";
  return (const GLubyte*)"";
}
static const GLubyte* GLAD_API_PTR dq_glGetStringi(GLenum, GLuint) { return nullptr; }
static void GLAD_API_PTR dq_glGetIntegerv(GLenum pname, GLint* out) {
  if (!out) return;
  switch (pname) {
    case GL_MAJOR_VERSION: *out = 4; break;
    case GL_MINOR_VERSION: *out = 6; break;
    case GL_CONTEXT_PROFILE_MASK: *out = GL_CONTEXT_CORE_PROFILE_BIT; break;
    case GL_CONTEXT_FLAGS: *out = 0; break;
    case GL_NUM_EXTENSIONS: *out = 0; break;
    case GL_TEXTURE_BINDING_2D: *out = (GLint)bind2D[activeUnit]; break;
    case GL_ACTIVE_TEXTURE: *out = GL_TEXTURE0 + activeUnit; break;
    case GL_CURRENT_PROGRAM: *out = (GLint)curProg; break;
    case GL_ARRAY_BUFFER_BINDING: *out = (GLint)arrayBuf; break;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: *out = (GLint)(vaoOf(curVao) ? vaoOf(curVao)->elem : 0); break;
    case GL_VERTEX_ARRAY_BINDING: *out = (GLint)curVao; break;
    case GL_FRAMEBUFFER_BINDING: *out = (GLint)drawFbo; break;
    case GL_DEPTH_FUNC: *out = (GLint)depthFuncE; break;
    case GL_READ_FRAMEBUFFER_BINDING: *out = (GLint)readFbo; break;
    case GL_VIEWPORT: out[0] = vpX; out[1] = vpY; out[2] = vpW; out[3] = vpH; break;
    case GL_SCISSOR_BOX: out[0] = scX; out[1] = scY; out[2] = scW; out[3] = scH; break;
    case GL_BLEND_SRC_RGB: *out = (GLint)blendSrc; break;
    case GL_BLEND_DST_RGB: *out = (GLint)blendDst; break;
    case GL_BLEND_SRC_ALPHA: *out = (GLint)blendSrcA; break;
    case GL_BLEND_DST_ALPHA: *out = (GLint)blendDstA; break;
    case GL_BLEND_EQUATION_RGB: *out = (GLint)blendEq; break;
    case GL_BLEND_EQUATION_ALPHA: *out = (GLint)blendEqA; break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: *out = gfx.maxArrayLayers; break;
    case GL_PIXEL_UNPACK_BUFFER_BINDING: *out = (GLint)pixelUnpack; break;
    case GL_SAMPLER_BINDING: *out = 0; break;
    case GL_POLYGON_MODE: out[0] = out[1] = GL_FILL; break;
    case GL_CLIP_ORIGIN: *out = (GLint)GL_LOWER_LEFT; break;
    default: *out = 0; break;
  }
}
static void GLAD_API_PTR dq_glGetFloatv(GLenum pname, GLfloat* out) {
  if (!out) return;
  if (pname == GL_MAX_TEXTURE_MAX_ANISOTROPY) *out = gfx.maxAniso;
  else *out = 0;
}
static void GLAD_API_PTR dq_glGetBooleanv(GLenum pname, GLboolean* out) {
  if (!out) return;
  if (pname == GL_DEPTH_WRITEMASK) *out = depthWrite ? GL_TRUE : GL_FALSE;
  else *out = GL_FALSE;
}
static void GLAD_API_PTR dq_glDebugMessageCallback(GLDEBUGPROC, const void*) {}

// The fixed-signature query generator above was split; wire the real one.
static void GLAD_API_PTR dq_glGenQueriesReal(GLsizei n, GLuint* ids) { for (GLsizei i = 0; i < n; i++) ids[i] = querySerial++; }

static void installHooks() {
  glad_glViewport = dq_glViewport;
  glad_glScissor = dq_glScissor;
  glad_glEnable = dq_glEnable;
  glad_glDisable = dq_glDisable;
  glad_glEnablei = dq_glEnablei;
  glad_glDisablei = dq_glDisablei;
  glad_glIsEnabled = dq_glIsEnabled;
  glad_glDepthFunc = dq_glDepthFunc;
  glad_glDepthMask = dq_glDepthMask;
  glad_glCullFace = dq_glCullFace;
  glad_glFrontFace = dq_glFrontFace;
  glad_glBlendFunc = dq_glBlendFunc;
  glad_glBlendFuncSeparate = dq_glBlendFuncSeparate;
  glad_glBlendEquation = dq_glBlendEquation;
  glad_glBlendEquationSeparate = dq_glBlendEquationSeparate;
  glad_glColorMask = dq_glColorMask;
  glad_glPolygonOffset = dq_glPolygonOffset;
  glad_glPolygonMode = dq_glPolygonMode;
  glad_glStencilFunc = dq_glStencilFunc;
  glad_glStencilOp = dq_glStencilOp;
  glad_glStencilMask = dq_glStencilMask;
  glad_glClearColor = dq_glClearColor;
  glad_glClearDepth = dq_glClearDepth;
  glad_glClearDepthf = dq_glClearDepthf;
  glad_glClear = dq_glClear;
  glad_glActiveTexture = dq_glActiveTexture;
  glad_glBindTexture = dq_glBindTexture;
  glad_glBindSampler = dq_glBindSampler;
  glad_glPixelStorei = dq_glPixelStorei;
  glad_glGenTextures = dq_glGenTextures;
  glad_glDeleteTextures = dq_glDeleteTextures;
  glad_glTexParameteri = dq_glTexParameteri;
  glad_glTexImage2D = dq_glTexImage2D;
  glad_glTexImage3D = dq_glTexImage3D;
  glad_glTexSubImage2D = dq_glTexSubImage2D;
  glad_glTexSubImage3D = dq_glTexSubImage3D;
  glad_glGenerateMipmap = dq_glGenerateMipmap;
  glad_glGenRenderbuffers = dq_glGenRenderbuffers;
  glad_glDeleteRenderbuffers = dq_glDeleteRenderbuffers;
  glad_glBindRenderbuffer = dq_glBindRenderbuffer;
  glad_glRenderbufferStorage = dq_glRenderbufferStorage;
  glad_glGenFramebuffers = dq_glGenFramebuffers;
  glad_glDeleteFramebuffers = dq_glDeleteFramebuffers;
  glad_glBindFramebuffer = dq_glBindFramebuffer;
  glad_glFramebufferTexture2D = dq_glFramebufferTexture2D;
  glad_glFramebufferRenderbuffer = dq_glFramebufferRenderbuffer;
  glad_glCheckFramebufferStatus = dq_glCheckFramebufferStatus;
  glad_glDrawBuffers = dq_glDrawBuffers;
  glad_glDrawBuffer = dq_glDrawBuffer;
  glad_glCopyTexSubImage2D = dq_glCopyTexSubImage2D;
  glad_glBlitFramebuffer = dq_glBlitFramebuffer;
  glad_glGetTexImage = dq_glGetTexImage;
  glad_glReadPixels = dq_glReadPixels;
  glad_glGenBuffers = dq_glGenBuffers;
  glad_glDeleteBuffers = dq_glDeleteBuffers;
  glad_glBindBuffer = dq_glBindBuffer;
  glad_glBindBufferBase = dq_glBindBufferBase;
  glad_glBufferData = dq_glBufferData;
  glad_glBufferSubData = dq_glBufferSubData;
  glad_glCopyBufferSubData = dq_glCopyBufferSubData;
  glad_glGenVertexArrays = dq_glGenVertexArrays;
  glad_glDeleteVertexArrays = dq_glDeleteVertexArrays;
  glad_glBindVertexArray = dq_glBindVertexArray;
  glad_glEnableVertexAttribArray = dq_glEnableVertexAttribArray;
  glad_glDisableVertexAttribArray = dq_glDisableVertexAttribArray;
  glad_glVertexAttribPointer = dq_glVertexAttribPointer;
  glad_glVertexAttribIPointer = dq_glVertexAttribIPointer;
  glad_glVertexAttribDivisor = dq_glVertexAttribDivisor;
  glad_glVertexAttrib2f = dq_glVertexAttrib2f;
  glad_glGenQueries = dq_glGenQueriesReal;
  glad_glDeleteQueries = dq_glDeleteQueries;
  glad_glBeginQuery = dq_glBeginQuery;
  glad_glEndQuery = dq_glEndQuery;
  glad_glGetQueryObjectiv = dq_glGetQueryObjectiv;
  glad_glGetQueryObjectui64v = dq_glGetQueryObjectui64v;
  glad_glCreateShader = dq_glCreateShader;
  glad_glDeleteShader = dq_glDeleteShader;
  glad_glShaderSource = dq_glShaderSource;
  glad_glCompileShader = dq_glCompileShader;
  glad_glGetShaderiv = dq_glGetShaderiv;
  glad_glGetShaderInfoLog = dq_glGetShaderInfoLog;
  glad_glCreateProgram = dq_glCreateProgram;
  glad_glDeleteProgram = dq_glDeleteProgram;
  glad_glDetachShader = dq_glDetachShader;
  glad_glAttachShader = dq_glAttachShader;
  glad_glLinkProgram = dq_glLinkProgram;
  glad_glUseProgram = dq_glUseProgram;
  glad_glGetProgramiv = dq_glGetProgramiv;
  glad_glGetProgramInfoLog = dq_glGetProgramInfoLog;
  glad_glGetUniformLocation = dq_glGetUniformLocation;
  glad_glGetAttribLocation = dq_glGetAttribLocation;
  glad_glGetActiveUniform = dq_glGetActiveUniform;
  glad_glIsProgram = dq_glIsProgram;
  glad_glUniform1i = dq_glUniform1i;
  glad_glUniform1f = dq_glUniform1f;
  glad_glUniform2f = dq_glUniform2f;
  glad_glUniform3f = dq_glUniform3f;
  glad_glUniform2fv = dq_glUniform2fv;
  glad_glUniform2i = dq_glUniform2i;
  glad_glUniform3fv = dq_glUniform3fv;
  glad_glUniform4fv = dq_glUniform4fv;
  glad_glUniformMatrix4fv = dq_glUniformMatrix4fv;
  glad_glDrawArrays = dq_glDrawArrays;
  glad_glDrawElements = dq_glDrawElements;
  glad_glDrawElementsBaseVertex = dq_glDrawElementsBaseVertex;
  glad_glDrawElementsInstanced = dq_glDrawElementsInstanced;
  glad_glMultiDrawElementsIndirect = dq_glMultiDrawElementsIndirect;
  glad_glGetError = dq_glGetError;
  glad_glGetString = dq_glGetString;
  glad_glGetStringi = dq_glGetStringi;
  glad_glGetIntegerv = dq_glGetIntegerv;
  glad_glGetFloatv = dq_glGetFloatv;
  glad_glGetBooleanv = dq_glGetBooleanv;
  glad_glDebugMessageCallback = dq_glDebugMessageCallback;
}

static void makeDummyTextures() {
  auto solid = [](GLenum target, int layers, GLuint& id, u8 r, u8 g, u8 b, u8 a) {
    dq_glGenTextures(1, &id);
    dq_glBindTexture(target, id);
    std::vector<u8> px((size_t)std::max(1, layers) * 4, 255);
    for (int i = 0; i < std::max(1, layers); i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a; }
    if (target == GL_TEXTURE_2D) dq_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    else if (target == GL_TEXTURE_2D_ARRAY) dq_glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    else dq_glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    dq_glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    dq_glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    dq_glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    dq_glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  solid(GL_TEXTURE_2D, 1, white2D, 255, 255, 255, 255);
  solid(GL_TEXTURE_2D, 1, black2D, 0, 0, 0, 0);
  solid(GL_TEXTURE_2D_ARRAY, 1, whiteArr, 255, 255, 255, 255);
  solid(GL_TEXTURE_3D, 1, white3D, 255, 255, 255, 255);
  dq_glBindTexture(GL_TEXTURE_2D, 0);
}

static void shutdownDevice() {
  if (device) vkDeviceWaitIdle(device);
  destroyDepthCopy();
  recording = false; frameOpen = false; rendering = false;
  for (auto& kv : pipelines) vkDestroyPipeline(device, kv.second, nullptr);
  pipelines.clear();
  for (auto& kv : samplers) vkDestroySampler(device, kv.second, nullptr);
  samplers.clear();
  for (auto& p : progs) if (p.alive) destroyProgGpu(p);
  for (auto& t : texs) if (t.alive) destroyTexGpu(t);
  for (auto& t : rbs) if (t.alive) destroyTexGpu(t);
  if (fbColor) { destroyTexGpu(*fbColor); delete fbColor; fbColor = nullptr; }
  if (fbDepth) { destroyTexGpu(*fbDepth); delete fbDepth; fbDepth = nullptr; }
  freeRetired();
  for (auto& b : bufs) if (b.alive) destroyGpuBuf(b.gpu);
  for (auto& b : bumps) { if (b.buf) vkDestroyBuffer(device, b.buf, nullptr); if (b.mem) vkFreeMemory(device, b.mem, nullptr); }
  bumps.clear();
  for (auto p : descPools) vkDestroyDescriptorPool(device, p, nullptr);
  descPools.clear(); descPoolCur = -1;
  destroySwapchain();
  if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
  if (imageReady) vkDestroySemaphore(device, imageReady, nullptr);
  if (renderDone) vkDestroySemaphore(device, renderDone, nullptr);
  if (frameFence) vkDestroyFence(device, frameFence, nullptr);
  cmdPool = VK_NULL_HANDLE; cmd = VK_NULL_HANDLE; imageReady = renderDone = VK_NULL_HANDLE; frameFence = VK_NULL_HANDLE;
  if (device) vkDestroyDevice(device, nullptr);
  if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
  if (instance) vkDestroyInstance(instance, nullptr);
  device = VK_NULL_HANDLE; instance = VK_NULL_HANDLE; surface = VK_NULL_HANDLE; phys = VK_NULL_HANDLE;
  if (glslangOn) { glslang::FinalizeProcess(); glslangOn = false; }
  g_on = false;
}

bool gfxIsVulkan() { return g_on; }

bool gfxInitVulkan(SDL_Window* window) {
  if (g_on) return true;
  if (volkInitialize() != VK_SUCCESS) { DQ_ERR("vulkan: loader not found"); return false; }
  g_window = window;
  bufs.clear(); texs.clear(); rbs.clear(); vaos.clear(); shaders.clear(); progs.clear(); fbos.clear();
  bufs.push_back({}); texs.push_back({}); rbs.push_back({}); shaders.push_back({}); progs.push_back({}); fbos.push_back({});
  vaos.push_back({}); vaos[0].alive = true;
  unsigned nExt = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(window, &nExt, nullptr)) { DQ_ERR("vulkan: SDL instance extensions: %s", SDL_GetError()); return false; }
  std::vector<const char*> exts(nExt);
  SDL_Vulkan_GetInstanceExtensions(window, &nExt, exts.data());
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "DokuQuest";
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = (uint32_t)exts.size();
  ici.ppEnabledExtensionNames = exts.data();
  if (vkFail(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance")) return false;
  volkLoadInstance(instance);
  if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) { DQ_ERR("vulkan: surface: %s", SDL_GetError()); shutdownDevice(); return false; }
  uint32_t nd = 0; vkEnumeratePhysicalDevices(instance, &nd, nullptr);
  if (!nd) { DQ_ERR("vulkan: no device"); shutdownDevice(); return false; }
  std::vector<VkPhysicalDevice> devs(nd);
  vkEnumeratePhysicalDevices(instance, &nd, devs.data());
  int bestScore = -1;
  for (auto d : devs) {
    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(d, &props);
    if (VK_API_VERSION_MAJOR(props.apiVersion) < 1 || (VK_API_VERSION_MAJOR(props.apiVersion) == 1 && VK_API_VERSION_MINOR(props.apiVersion) < 3)) continue;
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qf(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qf.data());
    for (uint32_t i = 0; i < nq; i++) {
      if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
      if (!present) continue;
      int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 1 : 0;
      if (score > bestScore) { bestScore = score; phys = d; queueFamily = i; }
    }
  }
  if (!phys) { DQ_ERR("vulkan: no Vulkan 1.3 device can draw to this window"); shutdownDevice(); return false; }
  VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(phys, &props);
  limits = props.limits;
  vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
  VkPhysicalDeviceFeatures2 feat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
  VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT};
  feat.pNext = &v13; v13.pNext = &eds; eds.pNext = &eds2;
  vkGetPhysicalDeviceFeatures2(phys, &feat);
  if (!v13.dynamicRendering || !eds.extendedDynamicState || !eds2.extendedDynamicState2) {
    DQ_ERR("vulkan: this driver is missing dynamic rendering");
    shutdownDevice(); return false;
  }
  VkPhysicalDeviceFeatures2 enable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan13Features ev13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eeds{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
  VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eeds2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT};
  enable.pNext = &ev13; ev13.pNext = &eeds; eeds.pNext = &eeds2;
  ev13.dynamicRendering = VK_TRUE;
  eeds.extendedDynamicState = VK_TRUE;
  eeds2.extendedDynamicState2 = VK_TRUE;
  if (feat.features.multiDrawIndirect) enable.features.multiDrawIndirect = VK_TRUE;
  if (feat.features.drawIndirectFirstInstance) enable.features.drawIndirectFirstInstance = VK_TRUE;
  if (feat.features.samplerAnisotropy) enable.features.samplerAnisotropy = VK_TRUE;
  if (feat.features.independentBlend) enable.features.independentBlend = VK_TRUE;
  if (feat.features.fillModeNonSolid) enable.features.fillModeNonSolid = VK_TRUE;
  // A pack indexes voxel SSBOs this renderer has not filled. Robust access returns zero
  // instead of faulting the GPU when that read runs past the buffer.
  if (feat.features.robustBufferAccess) enable.features.robustBufferAccess = VK_TRUE;
  float pri = 1.f;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = queueFamily; qi.queueCount = 1; qi.pQueuePriorities = &pri;
  const char* devExt[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.pNext = &enable;
  di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
  di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = devExt;
  if (vkFail(vkCreateDevice(phys, &di, nullptr, &device), "vkCreateDevice")) { shutdownDevice(); return false; }
  volkLoadDevice(device);
  vkGetDeviceQueue(device, queueFamily, 0, &queue);
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pi.queueFamilyIndex = queueFamily;
  pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkFail(vkCreateCommandPool(device, &pi, nullptr, &cmdPool), "command pool")) { shutdownDevice(); return false; }
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = cmdPool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
  if (vkFail(vkAllocateCommandBuffers(device, &cai, &cmd), "command buffer")) { shutdownDevice(); return false; }
  VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  vkCreateSemaphore(device, &semi, nullptr, &imageReady);
  vkCreateSemaphore(device, &semi, nullptr, &renderDone);
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vkCreateFence(device, &fi, nullptr, &frameFence);
  if (!createSwapchain(window)) { DQ_ERR("vulkan: swapchain"); shutdownDevice(); return false; }
  gfx.major = 4; gfx.minor = 6;
  gfx.modern = true;
  gfx.vulkan = true;
  gfx.mdi = enable.features.multiDrawIndirect && enable.features.drawIndirectFirstInstance;
  gfx.bufferStorage = false;
  gfx.anisotropic = enable.features.samplerAnisotropy == VK_TRUE;
  gfx.maxAniso = gfx.anisotropic ? limits.maxSamplerAnisotropy : 1.f;
  gfx.maxArrayLayers = (int)limits.maxImageArrayLayers;
  gfx.vendor = "Vulkan";
  gfx.renderer = props.deviceName;
  gfx.version = "Vulkan 1.3";
  g_on = true;
  installHooks();
  makeDummyTextures();
  DQ_LOG("Vulkan 1.3 (%s) — %s%s", gfx.mdi ? "multi-draw" : "single-draw", gfx.renderer.c_str(), presentMode == VK_PRESENT_MODE_FIFO_KHR ? "" : "");
  return true;
}

void gfxShutdownVulkan() {
  if (!instance && !device) return;
  shutdownDevice();
  gfx.vulkan = false;
}

void gfxVulkanSetVsync(bool on) {
  if (wantVsync == on) return;
  wantVsync = on;
  if (!g_on) return;
  vkDeviceWaitIdle(device);
  swapW = 0;
}

static void submitFence(VkSemaphore waitSem, VkSemaphore signalSem) {
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  if (waitSem) { si.waitSemaphoreCount = 1; si.pWaitSemaphores = &waitSem; si.pWaitDstStageMask = &stage; }
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  if (signalSem) { si.signalSemaphoreCount = 1; si.pSignalSemaphores = &signalSem; }
  vkQueueSubmit(queue, 1, &si, frameFence);
}
static void submitEmpty() {
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  vkEndCommandBuffer(cmd);
  submitFence(VK_NULL_HANDLE, VK_NULL_HANDLE);
}

static bool recreateSwapchain() {
  if (!g_window) return false;
  vkDeviceWaitIdle(device);
  return createSwapchain(g_window);
}

void gfxVulkanNewFrame() {
  if (!g_on) return;
  vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &frameFence);
  freeRetired();
  resetBumps();
  clearReferenced();
  rendering = false;
  skipFrame = false;
  frameOpen = false;
  recording = false;
  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
  int nw = swapW, nh = swapH;
  if (caps.currentExtent.width != 0xFFFFFFFFu) { nw = (int)caps.currentExtent.width; nh = (int)caps.currentExtent.height; }
  else if (g_window) SDL_GL_GetDrawableSize(g_window, &nw, &nh);
  if (nw <= 0 || nh <= 0) { skipFrame = true; frameOpen = true; submitEmpty(); return; }
  if (nw != swapW || nh != swapH || !swapchain) {
    if (!recreateSwapchain()) { skipFrame = true; frameOpen = true; submitEmpty(); return; }
  }
  VkResult ar = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageReady, VK_NULL_HANDLE, &swapIndex);
  if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
    if (!recreateSwapchain()) { skipFrame = true; frameOpen = true; submitEmpty(); return; }
    ar = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageReady, VK_NULL_HANDLE, &swapIndex);
  }
  if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
    DQ_WARN("vulkan: acquire failed (%d)", (int)ar);
    skipFrame = true; frameOpen = true; submitEmpty(); return;
  }
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  recording = true;
  frameOpen = true;
}

void gfxVulkanPresent(SDL_Window* window) {
  if (window) g_window = window;
  if (!g_on) return;
  if (skipFrame || !recording) { frameOpen = false; recording = false; skipFrame = false; return; }
  endRendering();
  if (fbColor && swapIndex < swapImages.size() && fbColor->image) {
    transitionAll(*fbColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = swapLayout[swapIndex];
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = swapImages[swapIndex];
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.layerCount = 1;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; blit.srcSubresource.layerCount = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; blit.dstSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, swapH, 0};
    blit.srcOffsets[1] = {swapW, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {swapW, swapH, 1};
    vkCmdBlitImage(cmd, fbColor->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImages[swapIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = swapImages[swapIndex];
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.layerCount = 1;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
    swapLayout[swapIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }
  vkEndCommandBuffer(cmd);
  recording = false;
  submitFence(imageReady, renderDone);
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &renderDone;
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain;
  pi.pImageIndices = &swapIndex;
  VkResult pr = vkQueuePresentKHR(queue, &pi);
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) swapW = 0;
  frameOpen = false;
}

} // namespace dq
