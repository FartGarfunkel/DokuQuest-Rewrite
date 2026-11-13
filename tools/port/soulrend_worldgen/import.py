#!/usr/bin/env python3
"""Soulrend's Overworld generation, as data, into data/soulrend_worldgen/ (git-ignored: Mojang's and the mods' data), read by
gen/density.cpp, gen/mc_biomes.cpp and gen/surface_rules.cpp for terrain version 15:
  python3 tools/port/soulrend_worldgen/import.py [prism instance dir] [data-dir]

What Soulrend's Overworld is made of (its mods and their configs, in the order Forge loads them):
  - Minecraft 1.20.1's noise router, with Expanded Ecosphere's own offset / factor / jaggedness splines (its ee_default pack:
    the "DEFAULT" mode Soulrend's config picks) -- the shape of the land;
  - the biomes: TerraBlender's region layer (overworld_region_size 3) choosing per area between region 0 (weight 10: the
    dimension's own list, which ee_default replaces with Expanded Ecosphere's 4589 entries, 240 biomes) and Nature's Spirit's
    five regions (weight 4 each: their OverworldBiomeBuilder copies, run here -- converted to JS, see j2js.py -- with every
    Nature's Spirit biome on, as Soulrend's natures_spirit-common.toml has it);
  - the surface: TerraBlender's namespaced rules (natures_spirit:* biomes Nature's Spirit's rules, minecraft:* Minecraft's
    own, climaterivers:* Climate Rivers') falling back to ee_default's surface rule (its wythers:* biomes);
  - the biomes' own data (colours, temperatures, features): vanilla's, then Nature's Spirit's, Expanded Ecosphere's and
    WWOO's (alphabetical by mod id: the last wins, so WWOO's versions of the vanilla biomes) -- for the features to come.
Needs: java (+ CFR, tools/cfr), node, the Prism instance (its mods) and Minecraft 1.20.1's client jar and mappings."""
import json, os, pathlib, re, shutil, struct, subprocess, sys, tempfile, zipfile

HERE = pathlib.Path(__file__).resolve().parent
PRISM = pathlib.Path.home() / '.local/share/PrismLauncher'
INSTANCE = PRISM / 'instances/Soulrend - Dark Fantasy RPG Soulslike'
CFR = pathlib.Path.home() / 'tools/cfr/usr/share/java/cfr/cfr.jar'
MCJAR = PRISM / 'libraries/com/mojang/minecraft/1.20.1/minecraft-1.20.1-client.jar'
MOJMAP = PRISM / 'libraries/net/minecraft/client/1.20.1-20230612.114412/client-1.20.1-20230612.114412-mappings.txt'
SRGMAP = PRISM / 'libraries/de/oceanlabs/mcp/mcp_config/1.20.1-20230612.114412/mcp_config-1.20.1-20230612.114412-mappings-merged.txt'
NS_CLASSES = ['net/hibiscus/naturespirit/terrablender/Terra%sParameters' % r for r in ('Ferax', 'Solaris', 'Flava', 'Mater', 'Laeta')]


def jar(mods, prefix):
    for f in sorted(os.listdir(mods)):
        if f.lower().startswith(prefix.lower()) and f.endswith('.jar'): return zipfile.ZipFile(mods / f)
    raise SystemExit('soulrend_worldgen: no %s*.jar in %s' % (prefix, mods))


def decompile(z, names, work, mapping=None):
    """the named classes (and their inner classes) from a jar, decompiled with CFR into work/src"""
    cj = work / ('c%d.jar' % len(os.listdir(work)))
    with zipfile.ZipFile(cj, 'w') as out:
        for n in z.namelist():
            if any(n == c + '.class' or n.startswith(c + '$') for c in names): out.writestr(n, z.read(n))
    cmd = ['java', '-jar', str(CFR), str(cj), '--outputdir', str(work / 'src'), '--silent', 'true']
    if mapping: cmd += ['--obfuscationpath', str(mapping)]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def copy_tree(z, prefix, dest, rename=None):
    """every .json under prefix in a jar, to dest (keeping the rest of the path); later copies overwrite (as Forge does)"""
    n = 0
    for name in z.namelist():
        if not name.startswith(prefix) or not name.endswith('.json'): continue
        rel = name[len(prefix):]
        f = dest / (rename(rel) if rename else rel)
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(z.read(name)); n += 1
    return n


def toml_bools(text):
    return {k: v == 'true' for k, v in re.findall(r'^\s*(\w+)\s*=\s*(true|false)\s*$', text, flags=re.M)}


def camel(k):   # has_snowy_fir_forest -> hasSnowyFirForest
    p = k.split('_'); return p[0] + ''.join(w.title() for w in p[1:])


def q(f):   # Climate.quantizeCoord: (long)(f * 10000.0f), in float
    f32 = lambda x: struct.unpack('f', struct.pack('f', x))[0]
    return int(f32(f32(f) * 10000.0))


# our natural ground, for the mods' ground blocks we don't have (matched by how they look, as tools/port/block_match.py does
# for the structures' building blocks)
GROUND = {   # (by kind: a sand for a sand, a soil for a soil, a rock for anything else)
    'sand': ['sand', 'red_sand', 'silver_sand', 'desert_sand', 'gravel'],
    'soil': ['dirt', 'coarse_dirt', 'podzol', 'rooted_dirt', 'mud', 'packed_mud', 'moss_block', 'mycelium', 'scorched_dirt', 'clay'],
    'rock': ['terracotta', 'white_terracotta', 'orange_terracotta', 'yellow_terracotta', 'red_terracotta', 'brown_terracotta', 'light_gray_terracotta',
             'pink_terracotta', 'stone', 'granite', 'diorite', 'andesite', 'calcite', 'tuff', 'dripstone_block', 'sandstone', 'red_sandstone',
             'smooth_sandstone', 'smooth_red_sandstone', 'desert_stone', 'desert_sandstone', 'basalt', 'smooth_basalt', 'blackstone', 'deepslate']}
def ground_kind(n):
    p = n.split(':')[-1]
    return 'sand' if p.endswith('sand') or p.endswith('gravel') else 'soil' if any(w in p for w in ('soil', 'dirt', 'moss', 'mud', 'podzol', 'mycelium')) else 'rock'


def ground_substitutes(out, d, jars):
    """data/soulrend_worldgen/block_substitutes.json: each block the surface rules lay that isn't ours, as the nearest of our ground"""
    sys.path.insert(0, str(HERE.parent)); import block_match as BM
    ours = {b['key'] for b in json.loads((d / 'blocks.json').read_text())['blocks']}
    names = sorted(set(re.findall(r'"Name":"([^"]+)"', (out / 'surface_rules.json').read_text())))
    todo = [n for n in names if not n.startswith('minecraft:')]
    if not todo: return
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp) / 'mods'
        for z in jars:   # (their block pictures, models and states, as block_match reads unpacked mods)
            r = root / str(len(os.listdir(root)) if root.exists() else 0); r.mkdir(parents=True)
            for nm in z.namelist():
                if not nm.endswith('/') and nm.startswith('assets/') and ('/textures/block/' in nm or '/models/block/' in nm or '/blockstates/' in nm):
                    f = r / nm; f.parent.mkdir(parents=True, exist_ok=True); f.write_bytes(z.read(nm))
        tex = BM.Tex(root)
        pack = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'
        cand = {}
        for kind, keys in GROUND.items():
            for k in keys:
                if k not in ours: continue
                png = (pack / (k + '.png')).read_bytes() if (pack / (k + '.png')).exists() else tex.block_texture('minecraft:' + k)
                f = BM.features(png) if png else None
                if f: cand.setdefault(kind, {})[k] = f
        res = {}
        for n in todo:
            png = tex.block_texture(n); f = BM.features(png) if png else None
            c = cand.get(ground_kind(n), {})
            if f and c: res[n] = min(c, key=lambda k: BM.dist(f, c[k]))
            else: print('soulrend_worldgen: no picture for', n)
    (out / 'block_substitutes.json').write_text(json.dumps(res, indent=0, sort_keys=True))
    print('soulrend_worldgen: ground blocks matched: ' + ', '.join('%s -> %s' % (k.split(':')[1], v) for k, v in res.items()))


def main(inst=INSTANCE, d='data'):
    inst = pathlib.Path(inst); out = pathlib.Path(d) / 'soulrend_worldgen'
    mods = inst / 'minecraft/mods'
    cfgdir = inst / 'minecraft/config'
    if out.exists(): shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / '.gitignore').write_text('*\n!.gitignore\n')
    mc = zipfile.ZipFile(MCJAR)
    ns, ee, ww, cr = jar(mods, 'natures_spirit'), jar(mods, 'expanded_ecosphere'), jar(mods, 'WWOO'), jar(mods, 'ClimateRivers')
    nscfg = toml_bools((cfgdir / 'natures_spirit-common.toml').read_text()) if (cfgdir / 'natures_spirit-common.toml').exists() else {}
    tbcfg = (cfgdir / 'terrablender.toml').read_text() if (cfgdir / 'terrablender.toml').exists() else ''
    size = int((re.search(r'overworld_region_size\s*=\s*(\d+)', tbcfg) or [0, 3])[1])
    vweight = int((re.search(r'vanilla_overworld_region_weight\s*=\s*(\d+)', tbcfg) or [0, 10])[1])
    nsweights = {r: int((re.search(r'terra_%s_weight\s*=\s*(\d+)' % r, (cfgdir / 'natures_spirit-common.toml').read_text() if nscfg else '') or [0, 4])[1])
                 for r in ('ferax', 'solaris', 'flava', 'mater', 'laeta')}

    # ---- the land: vanilla's router and noises, Expanded Ecosphere's splines and noise settings, Nature's Spirit's noises
    vprefix = 'data/minecraft/worldgen/'
    n = copy_tree(mc, vprefix + 'density_function/', out / 'density_function')
    n += copy_tree(mc, vprefix + 'noise/', out / 'noise')
    n += copy_tree(ee, vprefix + 'density_function/', out / 'density_function')
    n += copy_tree(ee, 'resources/ee_default/data/minecraft/worldgen/noise_settings/', out / 'noise_settings')
    n += copy_tree(ns, 'data/natures_spirit/worldgen/noise/', out / 'noise/natures_spirit')
    # ---- the biomes' data, in Forge's order (vanilla, then the mods by id: expanded_ecosphere, natures_spirit, wwoo_forge)
    for k in ('biome', 'placed_feature', 'configured_feature'):
        n += copy_tree(mc, vprefix + k + '/', out / k / 'minecraft')
        for z in (ee, ns, ww):
            for name in sorted({p.split('/')[1] for p in z.namelist() if p.startswith('data/') and p.count('/') > 2}):
                n += copy_tree(z, 'data/%s/worldgen/%s/' % (name, k), out / k / name)
        n += copy_tree(cr, 'data/climaterivers/worldgen/%s/' % k, out / k / 'climaterivers')

    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp); (work / 'src').mkdir()
        # ---- Nature's Spirit's regions (their code, run) and vanilla's underground biomes (which theirs call)
        mojnames = {l.split(' -> ')[0]: l.split(' -> ')[1].strip().rstrip(':') for l in open(MOJMAP) if not l.startswith((' ', '#'))}
        decompile(mc, [mojnames['net.minecraft.world.level.biome.OverworldBiomeBuilder'], mojnames['net.minecraft.world.level.biome.Climate']], work, MOJMAP)   # (Climate: for its names)
        decompile(ns, NS_CLASSES + ['net/hibiscus/naturespirit/world/NSSurfaceRules'], work)
        decompile(cr, ['fuzs/climaterivers/handler/SurfaceRuleBuilder'], work)
        subprocess.run([sys.executable, str(HERE / 'maps2.py'), str(work / 'srg.json'), 'net.minecraft.world.level.levelgen.SurfaceRules',
                        'net.minecraft.world.level.block.Blocks', 'net.minecraft.world.level.levelgen.Noises', 'net.minecraft.world.level.levelgen.VerticalAnchor',
                        'net.minecraft.world.level.levelgen.placement.CaveSurface', 'net.minecraft.world.level.biome.Biomes',
                        'net.minecraft.world.level.block.state.BlockBehaviour', 'net.minecraft.world.level.block.Block'], check=True, stdout=subprocess.DEVNULL)
        srg = json.load(open(work / 'srg.json'))
        sys.path.insert(0, str(HERE)); import j2js
        def conv(src, dst, static=False):
            s = open(src).read()
            s = re.sub(r'\b([mf]_\d+_)\b', lambda x: srg.get(x.group(1), x.group(1)), s)
            j2js.STATIC = static
            (work / dst).write_text(j2js.conv(s))
        conv(work / 'src/net/minecraft/world/level/biome/OverworldBiomeBuilder.java', 'van.js')
        for c in NS_CLASSES: conv(work / 'src' / (c + '.java'), 'ns_' + c.split('Terra')[1].replace('Parameters', '') + '.js')
        conv(work / 'src/net/hibiscus/naturespirit/world/NSSurfaceRules.java', 'nsrules.js', True)
        conv(work / 'src/fuzs/climaterivers/handler/SurfaceRuleBuilder.java', 'crrules.js', True)
        (work / 'nsconfig.json').write_text(json.dumps({camel(k): v for k, v in nscfg.items()}))
        subprocess.run(['node', str(HERE / 'regions.js'), str(work), str(work / 'regions.json')], check=True)
        subprocess.run(['node', str(HERE / 'rules.js'), str(work), str(work / 'rules.json')], check=True)
        nsreg = json.load(open(work / 'regions.json')); rules = json.load(open(work / 'rules.json'))

    # ---- region 0: Expanded Ecosphere's list (ee_default's dimension), quantized as Minecraft does
    dim = json.loads(ee.read('resources/ee_default/data/minecraft/dimension/overworld.json'))
    ee_list = []
    for e in dim['generator']['biome_source']['biomes']:
        p = e['parameters']; row = []
        for k in ('temperature', 'humidity', 'continentalness', 'erosion', 'depth', 'weirdness'):
            v = p[k]; a, b = (v, v) if isinstance(v, (int, float)) else v
            row += [q(a), q(b)]
        ee_list.append(row + [q(p.get('offset', 0)), e['biome']])
    regions = [{'name': 'minecraft:vanilla (expanded_ecosphere)', 'weight': vweight, 'entries': ee_list}]
    for r in ('ferax', 'solaris', 'flava', 'mater', 'laeta'):   # (TerraBlender's index order: Nature's Spirit registers them so)
        regions.append({'name': 'natures_spirit:terra_' + r, 'weight': nsweights[r], 'entries': nsreg['terra_' + r]})
    names = sorted({e[-1] for r in regions for e in r['entries']})
    idx = {b: i for i, b in enumerate(names)}
    for r in regions: r['entries'] = [e[:-1] + [idx[e[-1]]] for e in r['entries']]
    (out / 'biome_regions.json').write_text(json.dumps({'region_size': size, 'biomes': names, 'regions': regions}, separators=(',', ':')))
    # ---- surface rules: by the biome's namespace, then the noise settings' own
    base = json.load(open(out / 'noise_settings/overworld.json'))['surface_rule']
    vanilla_rule = json.loads(mc.read(vprefix + 'noise_settings/overworld.json'))['surface_rule']
    (out / 'surface_rules.json').write_text(json.dumps({'base': base, 'namespaces': {'minecraft': vanilla_rule, **rules}}, separators=(',', ':')))
    ground_substitutes(out, pathlib.Path(d), [ns, ee, ww, mc])
    missing = [b for b in names if not (out / 'biome' / b.split(':')[0] / (b.split(':')[1] + '.json')).exists()]
    print('soulrend_worldgen: %d files; %d regions (%s entries), %d biomes%s' % (n, len(regions), '/'.join(str(len(r['entries'])) for r in regions),
          len(names), '; no data for: ' + ', '.join(missing) if missing else ''))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else INSTANCE, sys.argv[2] if len(sys.argv) > 2 else 'data')
