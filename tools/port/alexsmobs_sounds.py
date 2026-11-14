#!/usr/bin/env python3
"""Alex's Mobs' own voices for the creatures imported from it (tools/port/alexsmobs_import.py), local only: every .ogg of
the mod analysed into our synthesis profiles (tools/port/sound_analyze.py: the same format as data/sounds.*, nothing of
the recordings kept) -> data/local_models/alexsmobs_sounds.json + .bin (git-ignored; audio/sfxlib.cpp loads every
data/local_models/*_sounds.json beside the pack's). Its events become ours under the creature's voice "am_<key>":
  mob.am_<key>        its idle call (<key>_idle, or the first of its own sounds that isn't a hurt or a death)
  mob.am_<key>_hurt   <key>_hurt
  mob.am_<key>_death  <key>_die / <key>_death (else the hurt, lower: audio.cpp's own fallback)
  mob.am_<key>_angry  <key>_attack / _bite / _roar / _hiss ... (the beasts' angry call)
and the rest of its events as mob.am_<event> (a creature's extra calls: the roadrunner's meep, the rattlesnake's rattle).
  python3 tools/port/alexsmobs_sounds.py [data-dir]
"""
import json, pathlib, re, sys, zlib

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import sound_analyze as SA

ROOT = pathlib.Path('/home/matt/soulrend_extract/mods/alexsmobs-1.22.9/assets/alexsmobs')
# the mod's sound names for creatures whose key differs
SOUND_NAME = {'centipede_head': 'centipede', 'crimson_mosquito': 'mosquito', 'warped_mosco': 'mosco', 'bone_serpent': 'bone_serpent',
              'alligator_snapping_turtle': 'alligator_snapping_turtle', 'void_worm': 'void_worm', 'cachalot_whale': 'cachalot_whale'}
ANGRY = r'attack|bite|roar|hiss|growl|angry|scream|screech|snarl|charge|shriek|bark|rattle'


def main(d='data'):
    d = pathlib.Path(d); od = d / 'local_models'; od.mkdir(parents=True, exist_ok=True)
    out_json, out_bin = od / 'alexsmobs_sounds.json', od / 'alexsmobs_sounds.bin'
    table = json.loads((ROOT / 'sounds.json').read_text())
    # the profiles, kept from an earlier run when the file is the same (the analysis is slow)
    old = {}
    if out_json.exists() and out_bin.exists():
        try:
            m = json.loads(out_json.read_text()); blob = zlib.decompress(out_bin.read_bytes())
            old = {k: bytes(blob[o:o + n]) for k, (o, n) in m['sounds'].items()}
        except (ValueError, zlib.error, KeyError): old = {}
    files = sorted(p.relative_to(ROOT / 'sounds').with_suffix('').as_posix() for p in (ROOT / 'sounds').rglob('*.ogg'))
    nb = bytearray(); index = {}
    for i, name in enumerate(files):
        key = 'am/' + name
        b = old.get(key)
        if b is None:
            x = SA.decode(ROOT / 'sounds' / (name + '.ogg'))
            if len(x) < 64: continue
            b = SA.encode(SA.analyze(x))
            if i % 25 == 0: print(i, '/', len(files), name, file=sys.stderr)
        index[key] = [len(nb), len(b)]; nb += b
    # the mod's events: their files (with its pitch and volume)
    ev = {}
    for e, t in table.items():
        got = []
        for x in t.get('sounds', []):
            n, pt, vo = (x, 1.0, 1.0) if isinstance(x, str) else (x.get('name', ''), float(x.get('pitch', 1)), float(x.get('volume', 1)))
            n = 'am/' + n.split(':', 1)[-1]
            if n in index: got.append([n, round(pt, 3), round(vo, 3)])
        if got: ev[e] = got
    events = {}
    for e, got in ev.items(): events['mob.am_' + e] = got
    # each creature's voice: its call, its hurt, its death, its anger
    am = json.loads((d / 'local_models/alexsmobs.json').read_text()) if (d / 'local_models/alexsmobs.json').exists() else {'models': {}}
    voices = {}
    keys = set(am.get('models', {})) | set(am.get('voiceOf', {}).values())
    src_of = am.get('voiceOf', {})   # (our key -> the mod's: brown_bear -> grizzly_bear)
    for ours in sorted(set(am.get('models', {}))):
        k = src_of.get(ours, ours); s = SOUND_NAME.get(k, k)
        mine = [e for e in ev if e == s or e.startswith(s + '_')]
        if not mine: continue
        idle = next((e for e in mine if e == s + '_idle'), None) or next((e for e in mine if not re.search(r'hurt|die|death|' + ANGRY, e)), None)
        hurt = next((e for e in mine if e == s + '_hurt'), None)
        death = next((e for e in mine if e in (s + '_die', s + '_death')), None)
        angry = next((e for e in mine if re.search(ANGRY, e[len(s):])), None)
        v = 'am_' + ours
        for kind, e in (('', idle), ('_hurt', hurt), ('_death', death), ('_angry', angry)):
            if e: events['mob.' + v + kind] = ev[e]
        if idle or hurt: voices[ours] = v
    z = zlib.compress(bytes(nb), 9)
    main_meta = json.loads((d / 'sounds.json').read_text())
    meta = {k: main_meta[k] for k in ('rate', 'bands', 'nfft', 'fine', 'db0', 'edges') if k in main_meta}
    meta.update({'about': "Alex's Mobs' sounds as synthesis profiles (local: tools/port/alexsmobs_sounds.py)", 'raw': len(nb), 'sounds': index, 'events': events, 'voices': voices})
    out_bin.write_bytes(z); out_json.write_text(json.dumps(meta, separators=(',', ':')))
    # the creatures wear them: a new kind its voice outright; one of ours keeps its ways, the mod's voice in its throat
    if am.get('models'):
        patch = am.setdefault('mobPatch', {})
        for ours, v in voices.items():
            if ours in am.get('mobs', {}): am['mobs'][ours]['sound'] = v; am['mobs'][ours].pop('voice', None)
            else: patch[ours] = {'voice': v}
        (d / 'local_models/alexsmobs.json').write_text(json.dumps(am, separators=(',', ':')))
    print('alexsmobs_sounds: %d profiles, %d events, %d creatures voiced; %d KB' % (len(index), len(events), len(voices), len(z) // 1024))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
