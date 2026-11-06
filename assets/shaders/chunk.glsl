// chunk.glsl — terrain sections. Every block face is a 32x32 grid of micro-voxels: the colour of
// each comes from the tile array (one layer per tile), its depth from the height array.
// A baked face (tile layer 4095) already carries that colour in the vertex, so it does not sample the array.
// Only the exposed face is in the mesh. The cubes you cannot see were never built.
//  MICRO_VOXELS 0: flat   1: shaded micro-voxels   2: relief — the face's height field is ray-marched one
//  micro-voxel cell at a time (a voxel parallax), so each micro-voxel shows real depth, side walls and
//  self-shadowing from the sun, fading to flat past uMicroDist. Legacy does not march: each nearby cell
//  is one sharp colour from the tile plus a lip, on solid faces and on plates (a door, a ladder, a flame).
//  Near water is the climate tint. Near lava is a hot crust, not the lava picture.
#include "common.glsl"

#ifdef VERTEX
layout(location = 0) in uvec4 aV;      // first 16 bytes, packed (render/mesher.h ChunkVertex):
//   x: x14 | y14 << 14 | normal3 << 28 | NoRelief << 31     y: z14 | tile12 << 14 | tint5 << 26 | Lava << 31
//   z: u14 | v14 << 14 | mat bits 0-3 << 28                 w: sky8 | block8 << 8 | shade8 << 16 | sway7 << 24 | Flowing << 31
layout(location = 1) in uint aLit;     // sun RGB then block RGB, 5 bits each (0..31)
uniform sampler2DArray uClimate;        // RG: temperature, humidity of each column, 1024 x 1024 wrapping, a layer per land
uniform sampler2DArray uTintLut;        // the tint of a climate: grass, foliage, water (256 x 256 by the climate bytes)
#ifdef DQ_MODERN
layout(location = 6) in vec3 aOrigin;   // per draw (multi-draw-indirect instance attribute)
#else
uniform vec3 uOrigin;                   // section origin relative to the camera's integer block
#endif
uniform mat4 uViewProj;
uniform vec3 uWorldOrigin;              // camera's integer block, wrapped (for wind/wave phase)
uniform vec2 uGrassFade;                // 3D grass: fully grown nearer than x, sunk into the ground past y

out vec3 vUV;
out vec4 vLight;                        // sky byte, block byte, shade (liquid tops: shore foam), unused
out vec3 vSun;                          // Cubyz sun channels, 0..31
out vec3 vBlock;                        // Cubyz block channels, 0..31
out vec3 vTint;
out vec3 vRel;                          // position relative to the camera block
out vec3 vWorld;                        // wrapped world position (patterns)
flat out int vNormal;
flat out uint vMat;

void main() {
#ifdef DQ_MODERN
  vec3 origin = aOrigin;
#else
  vec3 origin = uOrigin;
#endif
  vec3 p = vec3(float(aV.x & 16383u), float((aV.x >> 14u) & 16383u), float(aV.y & 16383u)) / 256.0 - 16.0;
  vec3 rel = origin + p;
  vec3 world = rel + uWorldOrigin;
  float sw = float((aV.w >> 24u) & 127u);
  // the tint: the climate where the vertex stands (filtered: a corner takes the four columns round it), then its colour
  uint tk = (aV.y >> 26u) & 31u;
  vec3 tint = vec3(1.0);
  if (tk >= 4u) {   // redstone dust by its power (mesh_shapes.cpp dustColor)
    float f = float(tk - 4u) / 15.0;
    tint = vec3(f * 0.6 + (f > 0.0 ? 0.4 : 0.3), clamp(f * f * 0.7 - 0.5, 0.0, 1.0), clamp(f * f * 0.6 - 0.7, 0.0, 1.0)) * (204.0 / 255.0);
  } else if (tk > 0u) {
    float land = clamp(floor((origin.y + uWorldOrigin.y + 8.0) / 256.0), 0.0, 3.0);   // the section's own land
    vec2 cl = textureLod(uClimate, vec3(world.xz / 1024.0, land), 0.0).rg;
    tint = textureLod(uTintLut, vec3((cl * 255.0 + 0.5) / 256.0, float(tk - 1u)), 0.0).rgb;
  }
  vec3 wo = waveOffset(sw, world);
  if (sw > 79.5) {
    // a grass blade's tip: sinks into the ground as you go away from it (gone well before the detail ring)
    float fade = smoothstep(uGrassFade.x, uGrassFade.y, length(rel));
    wo *= 1.0 - fade;
    rel.y -= (fract(world.y) + 0.04) * fade;
  }
  rel += wo;
  vRel = rel;
  vWorld = world;
  vUV = vec3(float(aV.z & 16383u) / 256.0 - 16.0, float((aV.z >> 14u) & 16383u) / 256.0 - 16.0, float((aV.y >> 14u) & 4095u));
  vTint = tint;
  vLight = vec4(float(aV.w & 255u) / 255.0, float((aV.w >> 8u) & 255u) / 255.0, float((aV.w >> 16u) & 255u) / 255.0, 0.0);
  vSun = vec3(float((aLit >> 25u) & 31u), float((aLit >> 20u) & 31u), float((aLit >> 15u) & 31u));
  vBlock = vec3(float((aLit >> 10u) & 31u), float((aLit >> 5u) & 31u), float(aLit & 31u));
  vNormal = int((aV.x >> 28u) & 7u);
  uint mat = ((aV.z >> 28u) & 15u) | (((aV.x >> 31u) & 1u) << 4u) | (((aV.y >> 31u) & 1u) << 5u) | (((aV.w >> 31u) & 1u) << 6u);
  if (tk >= 3u) mat |= 128u;   // water and dust take the whole picture (Mat::TintAll)
  vMat = mat;
  gl_Position = uViewProj * vec4(rel, 1.0);
}
#endif

#ifdef FRAGMENT
uniform vec3 uCamFrac;       // camera position inside its block
uniform int uPass;           // 0 opaque, 1 cutout, 2 translucent
in vec3 vUV;
in vec4 vLight;
in vec3 vSun;
in vec3 vBlock;
in vec3 vTint;
in vec3 vRel;
in vec3 vWorld;
flat in int vNormal;
flat in uint vMat;
out vec4 fragColor;

#ifdef DQ_MODERN
#define RELIEF_STEPS 24
#define RELIEF_SHADOW 1
#else
#define RELIEF_STEPS 10
#define RELIEF_SHADOW 0
#endif
const float VOX = 32.0;        // micro-voxels per block edge
const float MAXD = 4.0;        // deepest relief, in micro-voxels

float cellDepth(vec2 cell, float layer) {
  return floor(tileHeightAt(fract((cell + 0.5) / VOX), layer).r * MAXD + 0.5);
}
// the smooth sub-voxel normal of the surface at a point of the tile (baked once per tile set from the exact,
// blurred heights, tile_relief.cpp): in the face's tangent frame, x along T, y along B, z out of the face.
// `k` fades the tilt (distance, grazing angles).
vec3 smoothNormal(vec4 nm, float k) {
  vec2 xy = (nm.xy * 2.0 - 1.0) * k;
  return normalize(vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.01))));
}
// light on a tilted micro-voxel: the sun where the sky reaches, and a key light from above for everything
// (so the bumps read in a torch-lit cave too). Returns a multiplier for the face's colour.
float cubyzSky() {
  return max(vSun.r, max(vSun.g, vSun.b)) / 31.0 * max(uAmbient.r, max(uAmbient.g, uAmbient.b));
}
float bumpLight(vec3 nB, vec3 N, float skyLit) {
  const vec3 K = vec3(0.4423, 0.7372, 0.5108);   // normalize(0.6, 1.0, 0.7)
  float key = 1.0 + 0.55 * (dot(nB, K) - dot(N, K));
  float sun = 1.0 + 0.6 * skyLit * (max(dot(nB, uSunDir), 0.0) - max(dot(N, uSunDir), 0.0));
  return max(key * sun, 0.35);
}
vec3 snapAxis(vec3 v) {
  vec3 a = abs(v);
  if (a.x >= a.y && a.x >= a.z) return vec3(sign(v.x), 0.0, 0.0);
  if (a.y >= a.z) return vec3(0.0, sign(v.y), 0.0);
  return vec3(0.0, 0.0, sign(v.z));
}

void main() {
  vec2 uv = vUV.xy;
  float layer = vUV.z;
  vec2 gdx = dFdx(uv), gdy = dFdy(uv);
  vec3 dp1 = dFdx(vRel), dp2 = dFdy(vRel);
  vec3 viewVec = vRel - uCamFrac;
  float dist = length(viewVec);
  int n = vNormal;
  bool liquid = (vMat & 2u) != 0u, lava = (vMat & 32u) != 0u;
  vec3 N = faceNormal(n);
  float shade = (liquid && n == 2) ? 1.0 : vLight.z;
  float sunMod = 1.0;
  // Layer 4095: R, G, B live in the uv word (render/mesher.h). No tile, height or normal sample.
  bool baked = layer > 4094.5;
  vec4 col = vec4(1.0);
  if (baked) {
    float ru = (vUV.x + 16.0) * 256.0;
    float rv = (vUV.y + 16.0) * 256.0;
    uint ub = uint(clamp(floor(ru + 0.5), 0.0, 16383.0));
    uint vb = uint(clamp(floor(rv + 0.5), 0.0, 16383.0));
    uint bits = ub | (vb << 14u);
    col = vec4(float(bits & 255u), float((bits >> 8u) & 255u), float((bits >> 16u) & 255u), 255.0) / 255.0;
  } else {

#if MICRO_VOXELS >= 1 && defined(DQ_MODERN)
  // Legacy does not build this frame or read the normal map. The cell snap below is its whole
  // near look, one colour sample and a lip, which is what a small Intel GPU can draw.
  bool relief = n < 6 && (vMat & 16u) == 0u && dist < uMicroDist;
  if (relief) {
    float fade = 1.0 - smoothstep(uMicroDist * 0.6, uMicroDist, dist);
    // tangent frame from the derivatives, snapped to the block's axes
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = snapAxis(dp2perp * gdx.x + dp1perp * gdy.x);
    vec3 B = snapAxis(dp2perp * gdx.y + dp1perp * gdy.y);
#if MICRO_VOXELS >= 2 && defined(DQ_MODERN)
    // The 24-step height march stays on the modern renderer. Legacy never enters this block.
    vec3 V = viewVec / max(dist, 1e-4);
    vec3 Vt = vec3(dot(V, T), dot(V, B), dot(V, N));
    float scale = fade * smoothstep(0.04, 0.22, -Vt.z);      // flatten at grazing angles
    if (scale > 0.01) {
      // march the height field: cells of 1 micro-voxel, depth measured in micro-voxels
      vec2 p = uv * VOX;
      vec2 dir = Vt.xy / max(-Vt.z, 1e-3);                 // cells moved per micro-voxel of depth
      vec2 cell = floor(p);
      vec2 st = vec2(dir.x >= 0.0 ? 1.0 : -1.0, dir.y >= 0.0 ? 1.0 : -1.0);
      vec2 tDelta = vec2(dir.x != 0.0 ? abs(1.0 / dir.x) : 1e9, dir.y != 0.0 ? abs(1.0 / dir.y) : 1e9);
      vec2 tMax = vec2(dir.x != 0.0 ? ((cell.x + max(st.x, 0.0)) - p.x) / dir.x : 1e9, dir.y != 0.0 ? ((cell.y + max(st.y, 0.0)) - p.y) / dir.y : 1e9);
      float depthIn = 0.0, hitDepth = 0.0;
      vec3 nt = vec3(0.0, 0.0, 1.0);
      int last = -1;
      bool hit = false;
      vec2 hitCell = cell;
      for (int i = 0; i < RELIEF_STEPS; i++) {
        float cd = cellDepth(cell, layer) * scale;
        if (depthIn > cd + 1e-4) {
          // stepped into a shorter column: the wall belongs to the tall one we left, not the hole we entered
          hit = true; hitDepth = depthIn;
          if (last == 0) { nt = vec3(-st.x, 0.0, 0.0); hitCell = cell - vec2(st.x, 0.0); }
          else { nt = vec3(0.0, -st.y, 0.0); hitCell = cell - vec2(0.0, st.y); }
          break;
        }
        float tExit = min(tMax.x, tMax.y);
        if (tExit >= cd) { hit = true; hitDepth = cd; nt = vec3(0.0, 0.0, 1.0); hitCell = cell; break; }   // onto the column's top
        depthIn = tExit; hitDepth = depthIn;
        if (tMax.x < tMax.y) { cell.x += st.x; tMax.x += tDelta.x; last = 0; } else { cell.y += st.y; tMax.y += tDelta.y; last = 1; }
      }
      // a steep look runs out of steps before it lands. Snapping to that far cell samples a hole and the rung vanishes.
      if (hit) {
      uv = (hitCell + 0.5) / VOX;
      vec3 nW = T * nt.x + B * nt.y + N * nt.z;
      shade *= axisShade(nW) / axisShade(N);                   // the side walls of micro-voxels shade like block faces
      shade *= 1.0 - 0.05 * hitDepth;                         // deeper is darker
      float skyLit = cubyzSky();
      float sunN = max(dot(N, uSunDir), 0.0);
      sunMod = 1.0 + 0.22 * skyLit * (max(dot(nW, uSunDir), 0.0) - sunN);
      // the smooth surface where the ray lands: crevice darkening everywhere, and on a column's top the sub-voxel
      // normal (round stones, bevelled gems, soft mortar lips)
      vec2 hpT = clamp(p + dir * hitDepth, hitCell, hitCell + 1.0);
      vec4 sm = tileNormalAt(hpT / VOX, layer);
      shade *= mix(1.0, sm.z, scale);
      if (nt.z > 0.5) {
        vec3 bt = smoothNormal(sm, scale);
        sunMod *= mix(1.0, bumpLight(normalize(T * bt.x + B * bt.y + N * bt.z), N, skyLit), scale);
      }
#if RELIEF_SHADOW
      vec3 Lt = vec3(dot(uSunDir, T), dot(uSunDir, B), dot(uSunDir, N));
      if (Lt.z > 0.08 && skyLit > 0.01) {
        // walk toward the sun from the hit point: a taller column on the way casts a shadow
        vec2 hp = (nt.z > 0.5) ? p + dir * hitDepth : p + dir * hitDepth + nt.xy * 0.01;
        vec2 sd = Lt.xy / Lt.z;
        vec2 sc = floor(hp);
        vec2 ss = vec2(sd.x >= 0.0 ? 1.0 : -1.0, sd.y >= 0.0 ? 1.0 : -1.0);
        vec2 sDelta = vec2(sd.x != 0.0 ? abs(1.0 / sd.x) : 1e9, sd.y != 0.0 ? abs(1.0 / sd.y) : 1e9);
        vec2 sMax = vec2(sd.x != 0.0 ? ((sc.x + max(ss.x, 0.0)) - hp.x) / sd.x : 1e9, sd.y != 0.0 ? ((sc.y + max(ss.y, 0.0)) - hp.y) / sd.y : 1e9);
        float lit = 1.0;
        for (int i = 0; i < 6; i++) {
          float r = min(sMax.x, sMax.y);
          float dAt = hitDepth - r;                             // the ray's depth where it crosses into the next cell
          if (dAt <= 0.0) break;
          if (sMax.x < sMax.y) { sc.x += ss.x; sMax.x += sDelta.x; } else { sc.y += ss.y; sMax.y += sDelta.y; }
          if (cellDepth(sc, layer) * scale < dAt - 1e-3) { lit = 0.0; break; }
        }
        sunMod *= mix(1.0, 0.72, (1.0 - lit) * skyLit);
      }
#endif
      }
    }
#else
    // shaded micro-voxels (the Legacy-friendly look): no geometry, one smooth baked normal per pixel for the
    // lighting (stones bulge, mortar sinks) and the crevice darkening
    vec4 sm = tileNormalAt(uv, layer);
    vec3 bt = smoothNormal(sm, 1.0);
    float skyLit = cubyzSky();
    float bl = bumpLight(normalize(T * bt.x + B * bt.y + N * bt.z), N, skyLit);
    shade *= mix(1.0, (1.0 - sm.w * 0.22) * sm.z * bl, fade);
#endif
  }
#endif

  bool cellSnap = false;
#ifndef DQ_MODERN
#if MICRO_VOXELS >= 1
  // One colour per micro-voxel, and a thin darker lip so the cell reads as a cube.
  // Plates (NoRelief) get it too: a door, a ladder and a flame are already voxel geometry, and the
  // picture on them was still being filtered. Liquids have their own grid below. No height march.
  if (n < 6 && !liquid && dist < uMicroDist) {
    cellSnap = true;
    vec2 edge = fract(uv * VOX);
    float lip = min(min(edge.x, 1.0 - edge.x), min(edge.y, 1.0 - edge.y));
    shade *= mix(0.86, 1.0, smoothstep(0.0, 0.12, lip));
    uv = (floor(uv * VOX) + 0.5) / VOX;
  }
#endif
#endif
  vec2 fuv = fract(uv);
  // A cell you can see is the one texel in its middle. The filtered sample is for when that cell is
  // smaller than a pixel, where the sharp texel shimmers.
  float texel = max(length(gdx), length(gdy));
  bool crisp = cellSnap && texel * float(VOX) < 0.85;
  bool tintedWater = liquid && !lava && (vMat & 128u) != 0u;
  bool sheet = !(liquid && (lava || tintedWater) && dist < 32.0);
  if (!sheet && lava) col = vec4(1.0);
  else if (!sheet) col = vec4(vec3(0.62), 175.0 / 255.0);
  else col = crisp ? tileColorLod(fuv, layer, 0.0) : tileColorGrad(fuv, layer, gdx, gdy);
  if (uPass == 1) {
    // Looking along a cutout (a ladder, a leaf) the mip averages the holes into the solid texels and the
    // alpha test throws the rung away. The hole test stays on the sharp texel; the colour follows it when the mip has already gone clear.
    vec4 sharp = crisp ? col : tileColorLod(fuv, layer, 0.0);
    if (sharp.a < 0.5) discard;
    if (col.a < 0.5) col.rgb = sharp.rgb;
  }
  // A liquid face near the camera is a grid of eighths. Each eighth is one cell, with a lip, and the
  // grid drifts (downstream on a flowing face). Far water keeps the calm sheet. Water's colour is the
  // climate tint (the picture is grey-blue and the tint paints it). Lava's cells are a hot crust.
  if (liquid && dist < 64.0) {
    float drift = lava ? uTime * 0.45 : (((vMat & 64u) != 0u) ? uTime * 0.55 : uTime * 0.07);
    vec2 u2 = fract(uv + vec2(drift, drift * 0.37));
    vec2 cell = floor(u2 * 8.0);
    vec4 q;
    if (lava) {
      float h = hash12(cell + floor(vWorld.xy + vWorld.yz));
      vec3 hot = mix(vec3(0.42, 0.05, 0.01), vec3(1.0, 0.42, 0.02), smoothstep(0.2, 0.75, h));
      if (h > 0.9) hot = vec3(1.0, 0.86, 0.35);
      q = vec4(hot, 1.0);
    } else if (tintedWater) {
      q = vec4(vec3(0.62), 175.0 / 255.0);
    } else {
      q = vec4(col.rgb, col.a);
    }
    q.rgb *= mix(0.88, 1.06, hash12(cell + vec2(3.1, 1.7)));
    float near = 1.0 - smoothstep(32.0, 64.0, dist);
    col = mix(col, q, near);
    vec2 edge = fract(u2 * 8.0);
    float lip = min(min(edge.x, 1.0 - edge.x), min(edge.y, 1.0 - edge.y));
    shade *= mix(1.0, mix(0.78, 1.0, smoothstep(0.0, 0.16, lip)), near);
  }
  }
  // A baked grass or leaf cell carries the raw texel. Alpha 0 makes the tint below take the whole cell,
  // including the season. The opacity is put back after that multiply.
  if (baked && (vTint.r + vTint.g + vTint.b) < 2.99) col.a = 0.0;
  // climate tint: water and dust take it all over; tint-marked pixels (alpha 253) of grass and leaves take it too
  if ((vMat & 128u) != 0u) col.rgb *= vTint * 1.25;
  else if (vTint.r + vTint.g + vTint.b < 2.99) {
    float w = clamp((1.0 - col.a) * 127.5, 0.0, 1.0);
    vec3 tint = vTint * 1.3;
    if (uSeason.a > 0.0) tint = mix(tint, uSeason.rgb * (dot(tint, vec3(0.299, 0.587, 0.114)) * 1.6), uSeason.a);   // the season's colour
    col.rgb *= mix(vec3(1.0), tint, w);
  }
  if (uPass != 2) col.a = 1.0;
  else if (baked) col.a = 160.0 / 255.0;

  vec3 block31 = vBlock;
  if (uDynN > 0) block31 = max(block31, dynBlock31(vRel + N * 0.25, N));
  vec3 lc = (vMat & 1u) != 0u ? vec3(1.0) : cubyzCombine(vSun, block31);
  // Lava's light is the pulse below. The height map is a texture read a baked face does not use.
  float emis = (!baked && !lava) ? tileHeightAt(fract(uv), layer).g : 0.0;
  lc = max(lc, vec3(emis));
  if (lava) lc = vec3(1.0) * (0.92 + 0.16 * vnoise(vWorld.xz * 1.3 + vec2(uTime * 0.25, -uTime * 0.17)));
  vec3 c = col.rgb * lc * shade * sunMod;

  if (liquid && !lava) {
    // water: the face's own (wavy) normal for a sky reflection and a sun glint
    vec3 wn = normalize(cross(dp1, dp2));
    if (dot(wn, viewVec) > 0.0) wn = -wn;
    vec3 v = -viewVec / max(dist, 1e-4);
    float fres = pow(1.0 - clamp(dot(v, wn), 0.0, 1.0), 3.0);
    float skyLit = cubyzSky();
    // a rain puddle is water without the climate tint: darker, glossier, a stronger sky reflection
    float pud = (vMat & 128u) == 0u ? 1.0 : 0.0;
    c *= 1.0 - 0.2 * pud;
    c = mix(c, mix(uSkyColor, uFogColor, 0.35) * max(skyLit, uMinLight), (fres * 0.45 + pud * 0.25) * step(0.5, N.y));
    vec3 hv = normalize(v + uSunDir);
    c += pow(max(dot(hv, wn), 0.0), 140.0) * 0.55 * skyLit * step(0.5, N.y);
    if (n == 2 && pud < 0.5) {
      // shoreline foam: broken lines lapping where the water meets the land
      float sh = vLight.z;
      float nz = vnoise(vWorld.xz * 5.0 + vec2(uTime * 0.35, uTime * 0.2)) * 0.6 + vnoise(vWorld.xz * 11.0 - uTime * 0.5) * 0.4;
      float band = sh * 0.85 + 0.2 * sin(uTime * 1.4 + dot(vWorld.xz, vec2(1.3, 0.9))) - 0.3;
      float foam = smoothstep(0.6, 0.9, band + nz * 0.5) * step(0.01, sh);
      c = mix(c, vec3(0.93, 0.96, 1.0) * lc, foam * 0.7);
      col.a = max(col.a, foam * 0.8);
    }
    if ((vMat & 64u) != 0u && n != 2) {
      // a waterfall: white water streaming down its face
      float s = vnoise(vec2(dot(vWorld.xz, vec2(3.1, 2.3)), vWorld.y * 1.6 + uTime * 4.5));
      float w = smoothstep(0.62, 0.92, s);
      c = mix(c, vec3(0.9, 0.95, 1.0) * lc, w * 0.55);
      col.a = max(col.a, 0.55 + w * 0.35);
    }
  }
  c = applyFog(c, dist, viewVec);
  fragColor = vec4(DQ_GRADE(c), col.a);
}
#endif
