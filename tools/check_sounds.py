#!/usr/bin/env python3
# tools/check_sounds.py — every sound name the code plays must be one the game can make: an entry in data/sfx.json
# "names", an fx./vx. recipe, a pack event in data/sounds.json, or one the code adds itself (audio/*_sfx.cpp add("..")).
# A name that's none of these plays nothing. Prints them and exits 1 if there are any.   python3 tools/check_sounds.py
import glob, json, re, sys, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
used = {}
pats = [r'fx->sound\("([a-z0-9_.\-]+)"', r'\.sound\("([a-z0-9_.\-]+)"', r'soundUi\([^,]*,\s*"([a-z0-9_.\-]+)"', r'sfxPlay\("([a-z0-9_.\-]+)"']
added = set()
for f in glob.glob('src/**/*.cpp', recursive=True):
    s = open(f, errors='ignore').read()
    for p in pats:
        for m in re.finditer(p, s): used.setdefault(m.group(1), f)
    if f.startswith('src/audio/'):
        added.update(re.findall(r'\badd\("([a-z0-9_.\-]+)"', s))
sfx = json.load(open('data/sfx.json'))
table, rec = sfx.get('names', {}), sfx.get('recipes', {})
ev = json.load(open('data/sounds.json')).get('events', {})
special = {'note'}   # audio.cpp plays note blocks by pitch
def ok(n):
    return n in table or n in added or n in special or any(k in rec for k in ('fx.' + n, 'vx.' + n)) or any(k in ev for k in ('mob.' + n, 'fx.' + n))
bad = sorted((n, f) for n, f in used.items() if not ok(n))
print(f"sounds: {len(used)} names played, {len(bad)} that play nothing")
for n, f in bad: print(f"SILENT {n}  ({f})")
sys.exit(1 if bad else 0)
