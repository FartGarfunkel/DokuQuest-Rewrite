// render/tile_box.h — boxes drawn from the block tiles with the content kit (render/content_draw.h): a door swinging,
// a decorated tree. A tile as a little 2D texture of its own (cached), and a box of six pictures.
#pragma once
#include "render/content_draw.h"

namespace dq {

class App;
GLuint tileTexture(App& a, int layer);                     // 0 if there is no such tile
GLuint tileTexture(App& a, const std::string& name);
// a box from a to b (in 1/16 block when sixteenths, else blocks) under m; side tile on the four sides, top/bottom on the
// ends; colour multiplies (leaves' green); lit = shaded by the world's light.
// uvMode 0 shares one rectangle on both faces of an axis (logs, gates, props).
// uvMode 1 is a door, 2 a trapdoor: the two big faces show one picture, handle on the same edge, and the
// thin edges follow the mesher so the swing meets the resting block.
void drawTileBox(ContentDraw& cd, const mat4& m, vec3 a, vec3 b, GLuint side, GLuint top, GLuint bot, vec2 light, vec3 color = vec3(1),
                 bool sixteenths = true, bool lit = true, vec3 cutTint = vec3(1), int uvMode = 0);

} // namespace dq
