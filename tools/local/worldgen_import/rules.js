// Nature's Spirit's and Climate Rivers' surface rules (their code, decompiled and converted by j2js.py) built against stand-ins
// that write Minecraft's own surface-rule JSON.   node rules.js <work dir> <out.json>
const fs = require('fs'), path = require('path');
const W = process.argv[2];
const cfg = JSON.parse(fs.readFileSync(path.join(W, 'nsconfig.json')));
const lc = k => k.toLowerCase();
const id = (ns, k) => ns + ':' + lc(k);
const state = bs => ({ type: 'minecraft:block', result_state: { Name: bs } });
const depth = (offset, add, a, b) => b === undefined ? { type: 'minecraft:stone_depth', offset, add_surface_depth: add, secondary_depth_range: 0, surface_type: a } : { type: 'minecraft:stone_depth', offset, add_surface_depth: add, secondary_depth_range: a, surface_type: b };
global.CaveSurface = { FLOOR: 'floor', CEILING: 'ceiling' };
global.SurfaceRules = {
  state, ifTrue: (c, r) => ({ type: 'minecraft:condition', if_true: c, then_run: r }),
  sequence: (...a) => ({ type: 'minecraft:sequence', sequence: a.length === 1 && Array.isArray(a[0]) ? a[0] : a }),
  not: c => ({ type: 'minecraft:not', invert: c }),
  waterBlockCheck: (offset, m) => ({ type: 'minecraft:water', offset, surface_depth_multiplier: m, add_stone_depth: false }),
  waterStartCheck: (offset, m) => ({ type: 'minecraft:water', offset, surface_depth_multiplier: m, add_stone_depth: true }),
  yBlockCheck: (anchor, m) => ({ type: 'minecraft:y_above', anchor, surface_depth_multiplier: m, add_stone_depth: false }),
  yStartCheck: (anchor, m) => ({ type: 'minecraft:y_above', anchor, surface_depth_multiplier: m, add_stone_depth: true }),
  isBiome: (...a) => ({ type: 'minecraft:biome', biome_is: a.length === 1 && Array.isArray(a[0]) ? a[0] : a }),
  hole: () => ({ type: 'minecraft:hole' }), steep: () => ({ type: 'minecraft:steep' }), temperature: () => ({ type: 'minecraft:temperature' }),
  abovePreliminarySurface: () => ({ type: 'minecraft:above_preliminary_surface' }), bandlands: () => ({ type: 'minecraft:bandlands' }),
  noiseCondition: (noise, min, max) => ({ type: 'minecraft:noise_threshold', noise, min_threshold: min, max_threshold: max === undefined ? 1.7976931348623157e308 : max }),
  stoneDepthCheck: depth,
  verticalGradient: (random_name, true_at_and_below, false_at_and_above) => ({ type: 'minecraft:vertical_gradient', random_name, true_at_and_below, false_at_and_above }),
  ON_FLOOR: depth(0, false, 'floor'), UNDER_FLOOR: depth(0, true, 'floor'), DEEP_UNDER_FLOOR: depth(0, true, 6, 'floor'), VERY_DEEP_UNDER_FLOOR: depth(0, true, 30, 'floor'),
  ON_CEILING: depth(0, false, 'ceiling'), UNDER_CEILING: depth(0, true, 'ceiling'),
};
global.VerticalAnchor = { absolute: y => ({ absolute: y }), aboveBottom: y => ({ above_bottom: y }), belowTop: y => ({ below_top: y }) };
global.ImmutableList = { builder: () => { const a = []; return { add(x) { a.push(x); return this; }, build: () => ({ toArray: () => a }) }; } };
global.Double = { MAX_VALUE: 1.7976931348623157e308 };
const blk = n => ({ defaultBlockState: () => n, get: () => blk(n), getBase: () => blk(n) });
global.Blocks = new Proxy({}, { get: (_, k) => blk(id('minecraft', k)) });
global.NSBlocks = new Proxy({}, { get: (_, k) => blk(id('natures_spirit', k)) });
global.Noises = new Proxy({}, { get: (_, k) => id('minecraft', k) });
global.NSWorldGen = new Proxy({}, { get: (_, k) => id('natures_spirit', k) });
global.Biomes = new Proxy({}, { get: (_, k) => id('minecraft', k) });
global.NSBiomes = new Proxy({}, { get: (_, k) => id('natures_spirit', k) });
global.ModBiomes = new Proxy({}, { get: (_, k) => id('climaterivers', k.replace(/_BIOME$/, '')) });
global.NSConfig = new Proxy({}, { get: (_, k) => cfg[k] === undefined ? true : cfg[k] });
const load = f => eval('(' + fs.readFileSync(path.join(W, f), 'utf8') + ')');
const NS = load('nsrules.js'), CRB = load('crrules.js');
const out = { natures_spirit: NS.makeRules(), climaterivers: CRB.overworldLike() };
fs.writeFileSync(process.argv[3], JSON.stringify(out));
