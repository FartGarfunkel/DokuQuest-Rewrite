#!/usr/bin/env python3
"""Write data/sounds/: one <sound name>.txt placeholder per sound the game plays, sorted into blocks/<material>/,
mobs/<creature>/ and effects/. Put a recording named <sound name>.wav (or .mp3 / .flac) anywhere under data/sounds/
(or <user folder>/sounds/) and the game plays it instead of its synthesised sound (audio/sfxlib.cpp)."""
import json, os, re, sys
root = sys.argv[1] if len(sys.argv) > 1 else '.'
data = os.path.join(root, 'data')
sfx = json.load(open(os.path.join(data, 'sfx.json')))
mobs = sorted(json.load(open(os.path.join(data, 'mobs.json'))).keys(), key=len, reverse=True)
names = set(sfx.get('names', {}).keys())
names |= set(re.findall(r'"(hw_[a-z0-9_]+)"', open(os.path.join(root, 'src/audio/hw_sfx.cpp')).read()))
mats = {k for k in sfx.get('recipes', {}) if re.fullmatch(r'[a-z_]+\.[a-z_]+', k)}
out = os.path.join(data, 'sounds')
def slot(rel, name, what):
    p = os.path.join(out, rel, name + '.txt'); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'w').write(f"{what}\nReplace this file with {name}.wav (or {name}.mp3 / {name}.flac) to use your own recording.\n")
for k in sorted(mats):
    m, kind = k.split('.', 1); slot(f'blocks/{m}', k, f"Block sound: {kind} on {m} blocks")
count = {'blocks': len(mats), 'mobs': 0, 'effects': 0}
for n in sorted(names):
    if '.' in n: continue
    base = n[6:] if n.startswith('mstep_') else n
    mob = next((m for m in mobs if base == m or base.startswith(m + '_')), None)
    if mob: slot(f'mobs/{mob}', n, f"Creature sound: {mob}"); count['mobs'] += 1
    else: slot('effects', n, "Sound effect"); count['effects'] += 1
open(os.path.join(out, 'README.txt'), 'w').write(
"DokuQuest sounds\n================\nEvery sound the game makes has a placeholder here (<name>.txt), sorted into blocks/, mobs/ and effects/.\n"
"The game makes all of them itself (synthesised), so nothing here is needed.\n\nTo use your own recording for a sound, put <name>.wav (or .mp3 / .flac) next to its .txt (any folder under\n"
"sounds/ works; only the file name matters), e.g. blocks/stone/stone.step.wav or mobs/zombie/zombie_hurt.wav.\n"
"Recordings in <your DokuQuest folder>/sounds/ win over the ones here, so you can test without touching the game files.\n"
"Music and ambience are still generated and can't be replaced yet.\n")
print(count)
