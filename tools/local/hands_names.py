# after the station import: run this to give its models and pictures our own names (python3 tools/local/hands_names.py .)
# moves data/local_models/tactile -> data/local_models/hands with English names (models, pictures, the refs inside)
import json, pathlib, shutil, sys, re
root = pathlib.Path(sys.argv[1]); src = root/'data/local_models/tactile'; dst = root/'data/local_models/hands'
MODELS = {'alambic':'still','alambic_water':'still_water','atelier_gravure':'engraving_bench','bibliotheque':'bookshelf',
 **{f'bibliotheque_livre{i}':f'bookshelf_book{i}' for i in range(1,7)},'cartographe_block':'cartography_block','cartographe':'cartography',
 'coffre_buttonswap':'chest_button_page','coffre_buttontri':'chest_button_sort','coffre_ender_full':'ender_chest_full','coffre_ender':'ender_chest',
 'coffre_ender_lid':'ender_chest_lid','coffre_full':'chest_full','coffre':'chest','coffre_lid':'chest_lid','enclume':'anvil','encrier_fill':'inkwell_fill',
 'four':'furnace','fumoir_door':'smoker_door','fumoir':'smoker','grimoire_base_d':'grimoire_base_r','grimoire_base_g':'grimoire_base_l',
 'grimoire_page_d':'grimoire_page_r','grimoire_page_g':'grimoire_page_l','grimoire_reliure':'grimoire_spine','haut_fourneau':'blast_furnace',
 'haut_fourneau_porte':'blast_furnace_door','jukebox_arm':'jukebox_arm','jukebox':'jukebox','livre_accueil':'book_home','livre_alchimie':'book_alchemy',
 'livre_base_d':'book_base_r','livre_base_g':'book_base_l','livre_fleche_prec':'book_arrow_prev','livre_fleche_suiv':'book_arrow_next',
 'livre_page_d':'book_page_r','livre_page_g':'book_page_l','livre_reliure':'book_spine','meule_all':'grindstone_all','meule':'grindstone',
 'meule_static':'grindstone_static','meule_wheel':'grindstone_wheel','table_de_forge':'smithing','table_enchantement':'enchanting',
 'table_enchantement_lapis':'enchanting_lapis'}
TEX = {'coffre':'chest_wood','enclume':'anvil','livre':'book','roue_meule':'grindstone_wheel','verre':'glass','ender':'ender_chest'}
def ref(r):
  if r.startswith('tactile:block/'):
    n = r[len('tactile:block/'):]; return 'hands:block/' + TEX.get(n, n)
  if r.startswith('minecraft:'): return r[len('minecraft:'):]
  return r
def walk(j):
  if isinstance(j, dict): return {k: walk(v) for k, v in j.items()}
  if isinstance(j, list): return [walk(v) for v in j]
  if isinstance(j, str): return ref(j)
  return j
if dst.exists(): shutil.rmtree(dst)
(dst/'models').mkdir(parents=True); (dst/'textures').mkdir()
for f in (src/'models').glob('*.json'):
  j = walk(json.load(open(f)))
  if isinstance(j, dict) and j.get('parent') == 'block/block': del j['parent']
  assert f.stem in MODELS, f.stem
  json.dump(j, open(dst/'models'/(MODELS[f.stem]+'.json'), 'w'), indent=1)
for f in (src/'textures').iterdir():
  shutil.copy(f, dst/'textures'/(TEX.get(f.stem, f.stem) + f.suffix))
st = json.load(open(src/'stations.json'))
for k, v in st.items():
  if 'state' in v: v['state'] = MODELS.get(v['state'], v['state'])
  if isinstance(v.get('model'), list): v['model'] = [MODELS.get(m, m) for m in v['model']]
  elif isinstance(v.get('model'), str): v['model'] = MODELS.get(v['model'], v['model'])
json.dump(st, open(dst/'stations.json', 'w'), indent=1)
left = [m for m in (dst).rglob('*') if m.is_file() and re.search(r'tactile|coffre|livre|minecraft', m.read_text(errors='ignore') if m.suffix=='.json' else m.name)]
print('done', len(list((dst/'models').glob('*'))), 'models', len(list((dst/'textures').glob('*'))), 'pictures; leftovers:', left)
