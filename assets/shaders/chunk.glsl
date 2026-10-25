// chunk.glsl — terrain sections. Every block face is a 32x32 grid of micro-voxels: the colour of
// each comes from the tile array (one layer per tile), its depth from the height array.
//  MICRO_VOXELS 0: flat   1: shaded micro-voxels   2: relief — the face's height field is ray-marched one
//  micro-voxel cell at a time (a voxel parallax), so each micro-voxel shows real depth, side walls and
//  self-shadowing from the sun, fading to flat past uMicroDist. The Legacy (GL 3.3) build takes fewer steps
//  and skips the shadow ray.
#include "common.glsl"

#ifdef VERTEX
layout(location = 0) in vec3 aPos;      // 1/256 block units, section-relative
layout(location = 1) in uint aTile;     // layer | normal << 12
layout(location = 2) in vec2 aUV;       // (tile units + 64) * 256
layout(location = 3) in uvec4 aLight;   // sway, sky, block, shade
layout(location = 4) in vec3 aTint;
layout(location = 5) in uint aMat;
#ifdef DQ_MODERN
layout(location = 6) in vec3 aOrigin;   // per draw (multi-draw-indirect instance attribute)
#else
uniform vec3 uOrigin;                   // section origin relative to the camera's integer block
#endif
uniform mat4 uViewProj;
uniform vec3 uWorldOrigin;              // camera's integer block, wrapped (for wind/wave phase)
uniform vec2 uGrassFade;                // 3D grass: fully grown nearer than x, sunk into the ground past y

out vec3 vUV;
out vec4 vLight;                        // sky, block, shade (liquid tops: shore foam), unused
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
  vec3 p = aPos / 256.0;
  vec3 rel = origin + p;
  vec3 world = rel + uWorldOrigin;
  float sw = float(aLight.x);
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
  vUV = vec3(aUV / 256.0 - 64.0, float(aTile & 4095u));
  vTint = aTint;
  vLight = vec4(float(aLight.y) / 255.0, float(aLight.z) / 255.0, float(aLight.w) / 255.0, 0.0);
  vNormal = int(aTile >> 12u) & 7;
  vMat = aMat;
  gl_Position = uViewProj * vec4(rel, 1.0);
}
#endif

#ifdef FRAGMENT
uniform vec3 uCamFrac;       // camera position inside its block
uniform int uPass;           // 0 opaque, 1 cutout, 2 translucent
in vec3 vUV;
in vec4 vLight;
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

#if MICRO_VOXELS >= 1
  bool relief = n < 6 && (vMat & 16u) == 0u && dist < uMicroDist;
  if (relief) {
    float fade = 1.0 - smoothstep(uMicroDist * 0.6, uMicroDist, dist);
    // tangent frame from the derivatives, snapped to the block's axes
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = snapAxis(dp2perp * gdx.x + dp1perp * gdy.x);
    vec3 B = snapAxis(dp2perp * gdx.y + dp1perp * gdy.y);
#if MICRO_VOXELS >= 2
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
      for (int i = 0; i < RELIEF_STEPS; i++) {
        float cd = cellDepth(cell, layer) * scale;
        if (depthIn > cd + 1e-4) { hitDepth = depthIn; nt = last == 0 ? vec3(-st.x, 0.0, 0.0) : vec3(0.0, -st.y, 0.0); break; }   // into a column's side wall
        float tExit = min(tMax.x, tMax.y);
        if (tExit >= cd) { hitDepth = cd; nt = vec3(0.0, 0.0, 1.0); break; }                                                   // onto the column's top
        depthIn = tExit; hitDepth = depthIn;
        if (tMax.x < tMax.y) { cell.x += st.x; tMax.x += tDelta.x; last = 0; } else { cell.y += st.y; tMax.y += tDelta.y; last = 1; }
      }
      uv = (cell + 0.5) / VOX;
      vec3 nW = T * nt.x + B * nt.y + N * nt.z;
      shade *= axisShade(nW) / axisShade(N);                   // the side walls of micro-voxels shade like block faces
      shade *= 1.0 - 0.05 * hitDepth;                         // deeper is darker
      float skyLit = smoothstep(0.45, 1.0, vLight.x) * uDaylight;
      float sunN = max(dot(N, uSunDir), 0.0);
      sunMod = 1.0 + 0.22 * skyLit * (max(dot(nW, uSunDir), 0.0) - sunN);
      // the smooth surface where the ray lands: crevice darkening everywhere, and on a column's top the sub-voxel
      // normal (round stones, bevelled gems, soft mortar lips)
      vec2 hpT = clamp(p + dir * hitDepth, cell, cell + 1.0);
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
#else
    // shaded micro-voxels (the Legacy-friendly look): no geometry, one smooth baked normal per pixel for the
    // lighting (stones bulge, mortar sinks) and the crevice darkening
    vec4 sm = tileNormalAt(uv, layer);
    vec3 bt = smoothNormal(sm, 1.0);
    float skyLit = smoothstep(0.45, 1.0, vLight.x) * uDaylight;
    float bl = bumpLight(normalize(T * bt.x + B * bt.y + N * bt.z), N, skyLit);
    shade *= mix(1.0, (1.0 - sm.w * 0.22) * sm.z * bl, fade);
#endif
  }
#endif

  vec4 col = tileColorGrad(fract(uv), layer, gdx, gdy);
  if (uPass == 1 && col.a < 0.5) discard;
  // climate tint: water and dust take it all over; tint-marked pixels (alpha 253) of grass and leaves take it too
  if ((vMat & 128u) != 0u) col.rgb *= vTint * 1.25;
  else if (vTint.r + vTint.g + vTint.b < 2.99) {
    float w = clamp((1.0 - col.a) * 127.5, 0.0, 1.0);
    col.rgb *= mix(vec3(1.0), vTint * 1.3, w);
  }
  if (uPass != 2) col.a = 1.0;

  vec3 lc = lighting(vLight.x, uDynN > 0 ? max(vLight.y, dynLight(vRel, N)) : vLight.y, (vMat & 1u) != 0u);
  float emis = tileHeightAt(fract(uv), layer).g;
  lc = max(lc, vec3(emis));
  if (lava) lc = vec3(1.0) * (0.92 + 0.16 * vnoise(vWorld.xz * 1.3 + vec2(uTime * 0.25, -uTime * 0.17)));
  vec3 c = col.rgb * lc * shade * sunMod;

  if (liquid && !lava) {
    // water: the face's own (wavy) normal for a sky reflection and a sun glint
    vec3 wn = normalize(cross(dp1, dp2));
    if (dot(wn, viewVec) > 0.0) wn = -wn;
    vec3 v = -viewVec / max(dist, 1e-4);
    float fres = pow(1.0 - clamp(dot(v, wn), 0.0, 1.0), 3.0);
    float skyLit = lightCurve(vLight.x) * uDaylight;
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
