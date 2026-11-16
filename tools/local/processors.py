"""Minecraft structure processors (worldgen/processor_list) as simple swap rules for our loaders:
[[input block, probability (1 = always), output Name, output Properties], ...] in the order Minecraft tries them.
Only the rule processor's block_match / random_block_match / tag-free inputs are kept (what the street and ruin processors
use: placeholder colours to paths, stone to mossy/cracked). gen/village_pieces.cpp makePiece applies them per block."""
import json, pathlib


def rules_of(roots, ref):
    """ref: a processor list's id ("ns:path") or an inline {"processors": [...]}; roots: mod folders to look in."""
    if isinstance(ref, dict): procs = ref.get('processors', [])
    elif isinstance(ref, str) and ref not in ('minecraft:empty',):
        ns, _, p = ref.partition(':')
        procs = None
        for r in roots:
            f = pathlib.Path(r) / 'data' / ns / 'worldgen' / 'processor_list' / (p + '.json')
            if f.exists():
                j = json.loads(f.read_text()); procs = j.get('processors', j) if isinstance(j, dict) else j; break
        if procs is None: return []
    else: return []
    out = []
    for pr in procs or []:
        if pr.get('processor_type') == 'integrated_api:integrated_block_replace_processor':
            # IDAS: a placeholder that becomes another mod's block, else its "otherwise" (that mod is never here)
            if pr.get('input_block'):
                out.append([pr['input_block'], 1.0, pr.get('otherwise_block', 'minecraft:air'), {}])
            continue
        if pr.get('processor_type') != 'minecraft:rule': continue
        for r in pr.get('rules', []):
            if r.get('location_predicate', {}).get('predicate_type', 'minecraft:always_true') != 'minecraft:always_true': continue
            ip = r.get('input_predicate', {}); t = ip.get('predicate_type')
            if t == 'minecraft:block_match': prob = 1.0
            elif t == 'minecraft:random_block_match': prob = float(ip.get('probability', 1))
            else: continue
            o = r.get('output_state', {})
            out.append([ip.get('block'), prob, o.get('Name'), o.get('Properties', {})])
    return out
