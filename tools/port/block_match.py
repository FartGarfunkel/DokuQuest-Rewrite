#!/usr/bin/env python3
"""Every block the imported structures use that we don't have, matched to ours by how it looks: its own texture (from its
mod's assets, or Minecraft's jar) against our blocks' textures (the DokuQuest Base pack), within the same shape (a slab
for a slab, stairs for stairs, a full block for a full block). Functional things (machines, seats, food...) keep their
hand-picked stand-ins from tools/port/block_substitutes.py. Writes data/block_substitutes.json and a picture of every
match for checking by eye (out/block_match_*.png):
  python3 tools/port/block_match.py <unpacked mods dir> [data-dir] [out-dir]"""
import io, json, pathlib, re, sys, zipfile, glob
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import block_substitutes as BS
from poi_import import nbt

JAR = pathlib.Path.home() / '.local/share/PrismLauncher/libraries/com/mojang/minecraft/1.20.1/minecraft-1.20.1-client.jar'
SHAPES = ['_stairs', '_slab', '_wall', '_fence_gate', '_fence', '_trapdoor', '_door', '_pane', '_button', '_pressure_plate', '_carpet', '_vertical_slab']
NOT_MATERIAL = re.compile(r'ore|lamp|glow|command|spawner|tnt|sponge|slime|honey|_coral_block$|^coral|furnace|chest|barrel|crafting|table|anvil|beacon|'
                          r'portal|bedrock|leaves|log$|wood$|_stem|hyphae|mushroom_block|shroomlight|jack|pumpkin|melon|hay|target|observer|dispenser|'
                          r'dropper|piston|note|jukebox|loom|smoker|lectern|bookshelf|cauldron|composter|hive|nest|remains|infested|magma|crying|'
                          r'respawn|lodestone|sculk|reinforced|_ice$|^ice$|snow|powder|frost|grass|dirt|mud$|sand$|gravel|clay$|moss_block|farmland|path')


class Tex:
    """textures by resource name, from the unpacked mods and Minecraft's jar"""
    def __init__(self, mods):
        self.roots = [p for p in pathlib.Path(mods).iterdir() if (p / 'assets').is_dir()]
        self.jar = zipfile.ZipFile(JAR)
        self.jarnames = set(self.jar.namelist())
        self.ns = {}
        for r in self.roots:
            for a in (r / 'assets').iterdir(): self.ns.setdefault(a.name, []).append(r)
    def read(self, ns, path):   # path under assets/<ns>/
        if ns == 'minecraft':
            p = 'assets/minecraft/' + path
            if p in self.jarnames: return self.jar.read(p)
        for r in self.ns.get(ns, []):
            f = r / 'assets' / ns / path
            if f.exists(): return f.read_bytes()
        return None
    def json(self, ns, path):
        b = self.read(ns, path)
        try: return json.loads(b) if b else None
        except Exception: return None
    def model_textures(self, ref, depth=0):
        ns, _, p = ref.partition(':') if ':' in ref else ('minecraft', '', ref)
        m = self.json(ns, 'models/%s.json' % p)
        if not m or depth > 6: return {}
        t = {}
        if 'parent' in m: t.update(self.model_textures(m['parent'], depth + 1))
        t.update(m.get('textures', {}))
        return t
    def is_cube(self, name):   # a full block (its model comes from cube_* / leaves / glass), not a lantern, plant or machine
        ns, _, p = name.partition(':') if ':' in name else ('minecraft', '', name)
        bs = self.json(ns, 'blockstates/%s.json' % p)
        model = None
        if bs and bs.get('variants'):
            v = list(bs['variants'].values())[0]; v = v[0] if isinstance(v, list) else v; model = v.get('model')
        elif bs and bs.get('multipart'):
            a = bs['multipart'][0].get('apply'); a = a[0] if isinstance(a, list) else a; model = a and a.get('model')
        chain = []
        while model and len(chain) < 8:
            chain.append(model)
            mns, _, mp = model.partition(':') if ':' in model else ('minecraft', '', model)
            m = self.json(mns, 'models/%s.json' % mp)
            if not m: break
            if 'elements' in m and not any(c.split('/')[-1].startswith('cube') for c in chain[1:]): return False
            model = m.get('parent')
        return any(re.search(r'block/(cube|cube_all|cube_column|cube_bottom_top|cube_top|orientable|leaves|template_glazed)', c) for c in chain) if chain else True
    def block_texture(self, name):
        ns, _, p = name.partition(':') if ':' in name else ('minecraft', '', name)
        bs = self.json(ns, 'blockstates/%s.json' % p)
        model = None
        if bs:
            if 'variants' in bs and bs['variants']:
                v = list(bs['variants'].values())[0]; v = v[0] if isinstance(v, list) else v; model = v.get('model')
            elif 'multipart' in bs:
                for part in bs['multipart']:
                    a = part.get('apply'); a = a[0] if isinstance(a, list) else a
                    if a and a.get('model'): model = a['model']; break
        cands = []
        if model:
            t = self.model_textures(model)
            for k in ('all', 'side', 'texture', 'wall', 'end', 'top', 'particle', 'front', 'pane', 'cross', 'plant'):
                v = t.get(k)
                for _ in range(4):
                    if isinstance(v, str) and v.startswith('#'): v = t.get(v[1:])
                if isinstance(v, str) and not v.startswith('#'): cands.append(v)
        base = p
        for s in SHAPES:
            if base.endswith(s): base = base[:-len(s)]; break
        cands += ['%s:block/%s' % (ns, x) for x in (p, base, base + 's', base + '_bricks', base + '_planks', base.replace('brick', 'bricks'))]
        for c in cands:
            n2, _, q = c.partition(':') if ':' in c else ('minecraft', '', c)
            b = self.read(n2, 'textures/%s.png' % q)
            if b: return b
        return None


def features(png):
    try: im = Image.open(io.BytesIO(png)).convert('RGBA')
    except Exception: return None
    a = np.asarray(im)[:im.width, :im.width].astype(float)   # (the first frame of an animation)
    a = a.reshape(-1, 4); op = a[a[:, 3] > 128]
    if len(op) < 8: op = a[a[:, 3] > 24]   # (stained glass: all half see-through)
    if len(op) < 8: return None
    rgb = op[:, :3] / 255.0
    def lab(c):   # sRGB -> CIELAB (D65)
        c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
        X = c @ np.array([0.4124, 0.3576, 0.1805]) / 0.95047; Y = c @ np.array([0.2126, 0.7152, 0.0722]); Z = c @ np.array([0.0193, 0.1192, 0.9505]) / 1.08883
        f = lambda t: np.where(t > 0.008856, np.cbrt(t), 7.787 * t + 16 / 116)
        return np.stack([116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z))], -1)
    L = lab(rgb)
    return {'lab': L.mean(0), 'std': L[:, 0].std(), 'alpha': len(op) / len(a), 'img': im.resize((16, 16), Image.NEAREST)}


def dist(a, b):
    d = np.linalg.norm(a['lab'] - b['lab']) + 0.6 * abs(a['std'] - b['std'])
    if (a['alpha'] < 0.9) != (b['alpha'] < 0.9): d += 60   # see-through only for see-through
    return d


def by_kind(p, ours):
    m = re.match(r'(?:vertical_)?(\w+?)_planks$', p)   # Quark's vertical planks: the planks
    if m and (m.group(1) + '_planks') in ours: return m.group(1) + '_planks'
    if m and m.group(1) == 'oak': return 'planks'
    m = re.match(r'(?:stripped_)?(\w+?)_post$', p)      # a post: that wood's fence
    if m: return m.group(1) + '_fence' if m.group(1) + '_fence' in ours else 'oak_fence'
    m = re.match(r'(\w+?)_chest$', p)
    if m: return 'chest'
    for words, k in ((('grass_block',), 'grass'), (('coffin', 'sarcophagus'), 'chest'), (('gravestone', 'tombstone', 'grave'), 'cobblestone_wall'),
                     (('urn', 'vase'), 'decorated_pot'), (('statue', 'pedestal'), 'chiseled_stone_bricks'), (('book_pile',), 'bookshelf'),
                     (('girder',), 'iron_bars'), (('pipe',), 'chain'), (('spike',), 'pointed_dripstone'), (('item_shelf', 'shelf'), 'oak_slab'),
                     (('skull', '_head'), 'mob_head'),(('lantern',), 'lantern'), (('candle',), 'candle'), (('hedge',), 'leaves'), (('chain', 'rope', 'shaft'), 'chain'),
                     (('bars',), 'iron_bars'), (('crate', 'sack', 'basket', 'box'), 'barrel'), (('bookshelf',), 'bookshelf'), (('jar', 'pot'), 'flower_pot'),
                     (('torch', 'sconce'), 'torch'), (('ladder',), 'ladder'), (('scaffold',), 'scaffolding'), (('cobweb', 'web'), 'cobweb'), (('vine',), 'vine'),
                     (('leaves',), 'leaves'), (('log',), 'log'), (('glass',), 'glass'), (('lever', 'crank'), 'lever'), (('cauldron',), 'cauldron'),
                     (('table',), 'crafting_table'), (('seat', 'stool'), 'oak_slab'), (('sign',), 'oak_sign')):
        if any(w in p for w in words) and k in ours: return k
    return 'air'   # (a plant, a gadget: nothing rather than the wrong thing)


# never stand-ins for a building block: the land, ores, cloth, raw colour and treasure
NOT_BUILDING = re.compile(r'soil|nylium|mycelium|podzol|dirt|grass|^sand$|_sand$|gravel|_ore$|ore_|debris|wool|concrete|ruby|silver_block|gold_block|'
                          r'iron_block|mithril|sapphire|bronze|diamond|emerald_block|coal_block|lapis_block|netherite|copper_block|ice|snow|slime|honey|'
                          r'sponge|_glazed_terracotta|glazed|bone_block|amethyst|crystal|shale|skystone$|activator|conveyor|harvester|scaffold|quest|'
                          r'tinted|ward_stone|bulb|block_of|_block_of|magma|sculk|obsidian|crying|purpur_pillar|chiseled|cracked|infested|moss_block|clay$|terracotta_light')
KIND = [('glass', r'glass|window'), ('wood', r'plank|wood|log|stem|hyphae|bamboo|cabinet|crate|shelf|board'), ('shingle', r'shingle|roof'),
        ('brick', r'brick'), ('tile', r'tile|checker'), ('cobble', r'cobble|rubble'), ('pillar', r'pillar'), ('thatch', r'thatch|straw|hay'),
        ('plaster', r'daub|plaster|calcite|limestone|chalk'), ('stone', r'.')]
def kind_of(name):
    p = name.split(':')[-1]
    for k, rx in KIND:
        if re.search(rx, p): return k
    return 'stone'
# a roof shingle with no shingles of ours: tiles (terracotta) or bricks do
KIND_FALLBACK = {'shingle': ['tile', 'brick', 'stone'], 'tile': ['brick', 'stone'], 'cobble': ['stone'], 'pillar': ['stone'], 'plaster': ['stone'],
                 'thatch': ['wood'], 'brick': ['tile', 'stone']}


def shape_of(name):
    for s in SHAPES:
        if name.endswith(s): return s
    return ''


def main(mods, d='data', out='.'):
    d, out = pathlib.Path(d), pathlib.Path(out)
    bj = json.loads((d / 'blocks.json').read_text())
    ours = {b['key']: b for b in bj['blocks']}
    src = (pathlib.Path(__file__).parents[2] / 'src/gen/village_pieces.cpp').read_text()
    i = src.index('NAMES() {'); names = dict(re.findall(r'\{"([a-z0-9_]+)", "([a-z0-9_]+)"\}', src[i:src.index('return M;', i)]))
    tex = Tex(mods)
    # every block name the structures use
    used = set()
    for f in list(glob.glob(str(d / 'pois/**/*.nbt'), recursive=True)) + list(glob.glob(str(d / 'villages/*/*/*.nbt'))):
        try: t = nbt(open(f, 'rb').read())
        except Exception: continue
        for pal in ([t['palette']] if 'palette' in t else t.get('palettes', [])[:1]):
            for e in pal: used.add(e['Name'])
        for b in t.get('blocks', []):
            fs = (b.get('nbt') or {}).get('final_state')
            if isinstance(fs, str): used.add(fs.split('[')[0])
    def handled(n):
        ns, p = n.split(':', 1) if ':' in n else ('minecraft', n)
        if p in ('air', 'cave_air', 'void_air', 'structure_void', 'jigsaw'): return True
        for w in ('_wall_sign', '_wall_banner', '_wall_torch', '_wall_head', '_wall_skull'):
            if w in p: return True
        if p.startswith('potted_') and ns == 'minecraft': return True
        k = names.get(p, p)
        return ns == 'minecraft' and k in ours
    todo = sorted(n for n in used if not handled(n))
    # our blocks' looks, by shape
    ourfeat = {}
    pack = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'
    PLAIN = {'id', 'render', 'solid', 'opaque', 'replaceable', 'hardness', 'tool', 'tier', 'drop', 'light', 'lightAtt', 'sound', 'key', 'name', 'tex',
             'shapeKind', 'fuel', 'wlog', 'base', 'family', 'shape', 'betterEnd', 'itemIcon', 'color', 'rs', 'axisBlock', 'bta', 'wood', 'painted',
             'paintOf', 'translucent', 'isLog', 'texUpper', 'upperTile', 'selfCull', 'trapdoor', 'door', 'plate', 'cloth', 'clothTex', 'wooden',
             'aether', 'strip', 'xp', 'dropCount', 'hash', 'tint', 'betternether', 'facingBlock', 'needsSupport', 'sink', 'facingModel', 'attached'}
    for k, b in ours.items():
        if set(b) - PLAIN or b.get('light', 0) > 0 or NOT_BUILDING.search(k): continue   # (only plain building blocks stand in for building blocks)
        if shape_of(k) == '' and (b.get('render') != 1 or not b.get('opaque') or not b.get('solid')) and not ('glass' in k and b.get('render') in (1, 2)): continue
        if b.get('render') not in (1, 2, 7) or (b.get('render') == 1 and not b.get('opaque', True) and shape_of(k) == ''): continue
        if NOT_MATERIAL.search(k) and shape_of(k) == '': continue
        png = None
        tx = b.get('tex'); tx = tx if isinstance(tx, str) else (tx.get('side') or tx.get('all') or next(iter(tx.values()), None) if isinstance(tx, dict) else None)
        for t in (tx, k, 'oak_' + k, k.replace('slate', 'deepslate')):
            if not isinstance(t, str): continue
            if t and (pack / (t + '.png')).exists(): png = (pack / (t + '.png')).read_bytes(); break
        if png is None: png = tex.block_texture('minecraft:' + k)
        if png is None: continue
        f = features(png)
        if f: ourfeat[k] = f
    byshape = {}
    for k in ourfeat: byshape.setdefault((shape_of(k), kind_of(k)), []).append(k)
    explicit = {}
    for k, ch in BS.T.items():
        n = k if ':' in k else 'minecraft:' + k
        pick = next((c for c in ch if c == 'air' or c in ours), None)
        if pick: explicit[n] = pick
    res, rows, nomatch = {}, [], []
    key = lambda n: n.split(':', 1)[1] if n.startswith('minecraft:') else n   # (the game drops only Minecraft's own prefix)
    for n in todo:
        p = n.split(':', 1)[1] if ':' in n else n
        # a hand-picked "nothing" for a full block is never right: builders lay anything as floor (the castle's 821 notice boards)
        if n in explicit and not BS.is_material(n) and not (explicit[n] == 'air' and shape_of(p) == '' and n != 'quark:monster_box' and tex.is_cube(n)): res[key(n)] = explicit[n]; continue
        sh = shape_of(p)
        if sh == '' and not tex.is_cube(n):   # not a block of stuff: what it is, by name
            res[key(n)] = explicit[n] if n in explicit else by_kind(p, ours); continue
        png = tex.block_texture(n)
        f = features(png) if png else None
        kd = kind_of(n); cands = []
        for k2 in [kd] + KIND_FALLBACK.get(kd, []):
            cands = byshape.get((sh, k2), [])
            if cands: break
        if not f or not cands:
            if n in explicit: res[key(n)] = explicit[n]
            else: nomatch.append(n)
            continue
        if kd == 'glass':   # coloured glass: our stained glass of that colour (the pane of it for a pane)
            # (our stained glass pictures are one clear frame, tinted when drawn: by Minecraft's dye colours)
            DYE = {'white': (249, 255, 254), 'orange': (249, 128, 29), 'magenta': (199, 78, 189), 'light_blue': (58, 179, 218), 'yellow': (254, 216, 61),
                   'lime': (128, 199, 31), 'pink': (243, 139, 170), 'gray': (71, 79, 82), 'light_gray': (157, 157, 151), 'cyan': (22, 156, 156),
                   'purple': (137, 50, 184), 'blue': (60, 68, 170), 'brown': (131, 84, 50), 'green': (94, 124, 22), 'red': (176, 46, 38), 'black': (29, 29, 33)}
            im = np.asarray(f['img'].convert('RGBA')).reshape(-1, 4).astype(float); px = im[im[:, 3] > 24][:, :3]
            sat = px.max(1) - px.min(1)
            col = px[sat > 30].mean(0) if (sat > 30).sum() > 6 else None   # (the tinted panes, not the frame)
            if col is None: best = 'glass'
            else:
                c = min(DYE, key=lambda d: np.linalg.norm(np.array(DYE[d]) - col))
                best = c + '_stained_glass' if c + '_stained_glass' in ours else 'glass'
            if sh == '_pane': best = best + '_pane' if best + '_pane' in ours else 'glass_pane'
            res[key(n)] = best; rows.append((n, f, best if best in ourfeat else best[:-5], 0)); continue
        best = min(cands, key=lambda k: dist(f, ourfeat[k]))
        res[key(n)] = best; rows.append((n, f, best, dist(f, ourfeat[best])))
    # (the C++ looks names up without their mod: keep the first for a clash, and say so)
    (d / 'block_substitutes.json').write_text(json.dumps(res, indent=0, sort_keys=True))
    print('block_match: %d names not ours; %d matched by look, %d by hand, %d with nothing' % (len(todo), len(rows), len(res) - len(rows), len(nomatch)))
    for n in nomatch[:60]: print('   no match:', n)
    # the picture: theirs | ours, a row each
    out.mkdir(parents=True, exist_ok=True)
    rows.sort(key=lambda r: -r[3])
    per = 60
    for s in range(0, len(rows), per):
        chunk = rows[s:s + per]
        img = Image.new('RGB', (1500, 34 * ((len(chunk) + 1) // 2) + 4), (20, 20, 20)); dr = ImageDraw.Draw(img)
        for j, (n, f, b, dd) in enumerate(chunk):
            x, y = (j % 2) * 750, (j // 2) * 34 + 2
            img.paste(f['img'].convert('RGB').resize((32, 32), Image.NEAREST), (x + 4, y))
            img.paste(ourfeat[b]['img'].convert('RGB').resize((32, 32), Image.NEAREST), (x + 40, y))
            dr.text((x + 80, y + 10), '%s -> %s  (%.0f)' % (n, b, dd), fill=(255, 255, 0) if dd > 25 else (220, 220, 220))
        img.save(out / ('block_match_%02d.png' % (s // per)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'data', sys.argv[3] if len(sys.argv) > 3 else '.')
