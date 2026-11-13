// Nature's Spirit's five TerraBlender regions, their own code (decompiled, converted by j2js.py) run against stand-ins for the
// few Minecraft types it uses: every (temperature, humidity, continentalness, erosion, depth, weirdness, offset) box and its
// biome, quantized as Minecraft does.   node regions.js <work dir> <out.json>
const fs = require('fs'), path = require('path');
const W = process.argv[2];
const srg = JSON.parse(fs.readFileSync(path.join(W, 'srg.json')));
const cfg = JSON.parse(fs.readFileSync(path.join(W, 'nsconfig.json')));
const q = f => Math.trunc(Math.fround(Math.fround(f) * 10000));   // Climate.quantizeCoord: (long)(f * 10000.0f)
class Parameter { constructor(a, b) { this.lo = a; this.hi = b; } min() { return this.lo; } max() { return this.hi; } f_186814_() { return this.lo; } f_186815_() { return this.hi; } }
const span = (a, b) => typeof a === 'number' ? new Parameter(q(a), q(b)) : new Parameter(Math.min(a.lo, b.lo), Math.max(a.hi, b.hi));
const point = f => new Parameter(q(f), q(f));
const params = (t, h, c, e, d, w, off) => ({ p: [t, h, c, e, d, w], off: q(off) });
global.Climate = { Parameter: { span, point, m_186822_: span, m_186829_: span, m_186820_: point }, parameters: params, m_186798_: params, quantizeCoord: q };
global.Pair = { of: (a, b) => ({ a, b }) };
global.SharedConstants = { debugGenerateSquareTerrainWithoutNoise: false };
global.Biomes = new Proxy({}, { get: (_, k) => 'minecraft:' + (srg[k] || k).toLowerCase() });
global.NSBiomes = new Proxy({}, { get: (_, k) => 'natures_spirit:' + k.toLowerCase() });
global.NSConfig = new Proxy({}, { get: (_, k) => cfg[k] === undefined ? true : cfg[k] });
const load = f => eval('(' + fs.readFileSync(path.join(W, f), 'utf8') + ')');
const Vanilla = load('van.js');
const dump = (obj, m) => { const out = []; obj[m]({ accept: p => out.push([...p.a.p.flatMap(x => [x.lo, x.hi]), p.a.off, p.b]) }); return out; };
const res = {};
for (const [n, f] of [['terra_ferax', 'Ferax'], ['terra_solaris', 'Solaris'], ['terra_flava', 'Flava'], ['terra_mater', 'Mater'], ['terra_laeta', 'Laeta']]) {
  const C = load('ns_' + f + '.js');
  C.prototype.m_187226_ = function (c) { new Vanilla().addUndergroundBiomes(c); };   // (inherited: OverworldBiomeBuilder's)
  res[n] = dump(new C(), 'm_187175_');
  console.error(n, res[n].length, 'entries,', new Set(res[n].map(e => e[13])).size, 'biomes');
}
fs.writeFileSync(process.argv[3], JSON.stringify(res));
