#!/usr/bin/env python3
# Neutral namespaces in the structure and terrain data (idempotent): the base set's "base:", a few imported sets our own words.
# Tracked: data/villages. On this machine only: data/pois (with its block_substitutes.json), data/worldgen, data/worldgen_base.
import sys, re, json, pathlib, shutil
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import nbt_strings
root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.')
NS = {'minecraft': 'main', 'natures_spirit': 'nature', 'quark': 'rare', 'dungeons_arise': 'dungeons', 'graveyard': 'graves', 'idas': 'halls'}
pat = re.compile(r'(?<![a-z_0-9])(' + '|'.join(NS) + r'):')
dirpat = re.compile(r'"(' + '|'.join(k for k in NS if k != 'minecraft') + r')/')
keypat = re.compile(r'"(' + '|'.join(NS) + r')"(\s*:)')   # (a namespace's own key: the surface rules' per-namespace sets)
def conv(s): s = keypat.sub(lambda m: '"' + NS[m.group(1)] + '"' + m.group(2), s); return dirpat.sub(lambda m: '"' + NS[m.group(1)] + '/', pat.sub(lambda m: NS[m.group(1)] + ':', s))
def fnv(s):
    h = 1469598103934665603
    for ch in s.encode(): h = ((h ^ ch) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h
def salted(s):   # a surface gradient's random_name keeps the salt its name gave it
    return re.sub(r'"random_name"(\s*):(\s*)"([^"]*)"(?!,\s*"random_salt")', lambda m: '"random_name"%s:%s"%s","random_salt":"%d"' % (m.group(1), m.group(2), m.group(3), fnv(m.group(3))), s)
n = {'nbt': 0, 'json': 0, 'dirs': 0}
# the noises named in an imported set keep the name their seed was made from (so the land stays as it was)
for d in ('data/worldgen', 'data/worldgen_base'):
    for ns in [k for k in NS if k != 'minecraft']:
        for p in (root / d / 'noise' / ns).glob('*.json') if (root / d / 'noise' / ns).exists() else []:
            j = json.loads(p.read_text()); j.setdefault('seedName', ns + ':' + p.stem); p.write_text(json.dumps(j))
for d in ('data/villages', 'data/pois', 'data/worldgen', 'data/worldgen_base'):
    base = root / d
    if not base.exists(): continue
    for p in base.rglob('*.nbt'):
        out, ch = nbt_strings.transform(p.read_bytes(), conv)
        if ch: p.write_bytes(out); n['nbt'] += 1
    for p in base.rglob('*.json'):
        s = p.read_text()
        # (a noise's seedName stays as it was made)
        keep = re.findall(r'"seedName": *"[^"]*"', s)
        t = conv(salted(s))
        for k in keep: t = re.sub(r'"seedName": *"[^"]*"', lambda m, k=k: k, t, count=1) if False else t
        if keep:
            j = json.loads(t); j['seedName'] = json.loads('{' + keep[0] + '}')['seedName']; t = json.dumps(j)
        if t != s: p.write_text(t); n['json'] += 1
    for p in sorted(base.rglob('*'), key=lambda q: -len(q.parts)):
        if p.is_dir() and p.name in NS:
            dst = p.with_name(NS[p.name])
            if dst.exists():
                for c in p.iterdir(): shutil.move(str(c), str(dst / c.name))
                p.rmdir()
            else: p.rename(dst)
            n['dirs'] += 1
print(n)
