#!/usr/bin/env python3
"""DawnLike tiles for Avanor (DragonDePlatino, palette DawnBringer, CC BY 4.0).

Reads the world's own ids (world/*.lua: terrain, monsters + class, item
templates, special items, food, plants, potion appearances, trap types, map
objects, hero races), picks a DawnLike sprite per id by hand table (names
from rvip-tools/tilesets/dawnlike_names.tsv), gives the rest a same-set
stand-in by monster class / item kind, and writes

  web/tiles-dawn.png    16x16 sprites, 16 per row, original size
  port/dawn_map.inc     { "key", slot } pairs for port/rvip_tiles.cpp

Keys: tf:/tw:/t:TERRAIN (16 slots from base by mask n=8 s=4 w=2 e=1:
floor bordered / wall joined on that side; t: all 16 alike),
c:monster, cc:class, h:race, i:item type, f:food/special item, k:KIND,
pc:potion appearance, p:plant, tr:trap type, o:object, lo:lua object.
Prints the coverage. Run from the repo root: python3 web/mkdawn.py"""
import os, re, sys, glob
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TS = os.environ.get('RVIP_TILESETS') or os.path.expanduser('~/Games/rvip-tools/tilesets')
sys.path.insert(0, TS)
from dawnlike_preview import pos, sprite

def world(pattern, files='world/**/*.lua'):
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, files), recursive=True)):
        out += re.findall(pattern, open(f).read(), re.S)
    return out

terrain = world(r'DefineTile\("([A-Z_]+)"', 'world/tiles.lua')
monsters = {m: re.findall(r'"([a-z_]+)"', v)[-1]
            for m, v in world(r'Monster\.new\("([a-z_0-9]+)"[^)]*\).*?:View\(([^\n]*)\)\s*\n')}
templates = world(r'Template\.new\(ItemKind\.[A-Z]+, *"([a-z_0-9]+)"')
specials = world(r'(?:Item|Food)\.new\("([a-z_0-9]+)"')
plants = world(r'Plant\.new\("([a-z_0-9]+)"')
colours = world(r'PotionColour\.new\("([a-z_0-9]+)"')
traps = world(r'TrapType\.new\("([a-z_0-9]+)"')
luaobj = world(r'MapObject\.new\("([a-z_0-9]+)"')
races = re.findall(r'key = "([a-z_]+)", name', open(os.path.join(ROOT, 'world/hero.lua')).read().split('The callings')[0])
classes = world(r'CreatureClass\.new\("([a-z_]+)"')

# Autotiled terrain: DawnLike floor style (16 "<style> floor <sides>") or
# wall style (13 "<style> wall <joins>"), or one sprite.
FLOOR = {'GREEN_GRASS': 'day grass', 'CAVE_FLOOR': 'night stone', 'STONE_FLOOR': 'day stone',
         'PATH': 'day dirt', 'SAND': 'morning dirt', 'ROAD': 'day brick',
         'OBSIDIAN_FLOOR': 'night tile', 'GOLDEN_FLOOR': 'morning tile', 'HILL': 'dusk grass'}
WALL = {'WOOD_WALL': 'lit orange', 'STONE_WALL': 'lit rock', 'MAGMA': 'dim heat',
        'QUARTZ': 'lit ice', 'MARBLE_WALL': 'lit snow', 'BLACK_MARBLE_WALL': 'dark deep',
        'WINDOW': 'lit blue', 'FENCE': 'lit fort', 'GOLDEN_FENCE': 'bright orange'}
# "floor+sprite": a cut-out sprite over a floor of the same set.
ONE = {'TREE': 'day grass floor c+light oak dense', 'WATER': 'blue pool', 'DEEP_WATER': 'deep water tile',
       'LAVA': 'lava pool', 'LOW_MOUNTAIN': 'day grass floor c+brown peak alone', 'MOUNTAIN': 'day dirt floor c+dark peak alone',
       'HIGH_MOUNTAIN': 'day dirt floor c+dark snowcap alone', 'BRIDGE': 'deep water tile+bridge e w', 'TELEPORT_WHITE': 'day stone floor c+magic portal tile'}

MON = {
 'rat': 'sewer rat', 'large_rat': 'giant rat', 'black_rat': 'rabid rat', 'huge_rat': 'enormous rat',
 'bat': 'giant bat', 'huge_bat': 'vampire bat', 'mongbat': 'werebat', 'greater_mongbat': 'white nosed bat',
 'spider': 'cave spider', 'giant_spider': 'giant spider', 'tarantula': 'phase spider',
 'scorpion': 'scorpion', 'black_scorpion': 'giant scorpion', 'pink_scorpion': 'scorpion',
 'fire_beetle': 'spitting beetle', 'frost_beetle': 'giant beetle', 'green_beetle': 'giant beetle',
 'killer_beetle': 'killer beetle', 'death_beetle': 'killer beetle', 'giant_bee': 'killer bee',
 'giant_wasp': 'queen bee', 'centipede': 'centipede', 'stegocentipede': 'centipede',
 'dungeon_crawler': 'bee grub', 'giant_crawler': 'bee grub', 'carrion_crawler': 'bee grub',
 'dog': 'terrier', 'large_dog': 'big hound', 'rabid_dog': 'rabid jackal', 'wolf': 'wolf',
 'large_wolf': 'warg', 'werewolf': 'werewolf', 'gekta': 'big terrier',
 'farmer': 'farmer man', 'goodwife': 'farmer woman', 'bandit': 'bandit', 'citizen': 'farmer man',
 'fcitizen': 'farmer woman', 'royal_guard': 'guard', 'royal_guard_elite': 'watchman',
 'death_knight': 'knight', 'shopkeeper': 'shopkeeper', 'dwarf': 'dwarf', 'dwarf_guard': 'dwarf knight',
 'kobold': 'kobold', 'large_kobold': 'large kobold', 'chieftain_kobold': 'kobold lord',
 'shaman_kobold': 'kobold shaman', 'magnush': 'kobold ascendant', 'gnoll': 'hobgoblin', 'gnoll_warmaster': 'bugbear',
 'lemure': 'lemure', 'dretch': 'lesser devil', 'imp': 'imp', 'quasit': 'quasit', 'hell_hound': 'hell hound',
 'bearded_devil': 'barbed devil', 'barbed_devil': 'barbed devil', 'vrock': 'vrock',
 'bone_devil': 'bone devil', 'horned_devil': 'horned devil', 'beelzevile': 'ice devil',
 'starveling': 'wraith', 'wendigo': 'yeti',
 'gray_ooze': 'gray ooze', 'gelatinous_cube': 'gelatinous cube',
 'skeleton': 'skeleton', 'zombie': 'human zombie', 'ghoul': 'ghoul', 'ghost': 'ghost',
 'spectre': 'wraith', 'dread': 'shadow skeleton', 'vampire': 'vampire', 'lich': 'lich',
 'small_snake': 'garter snake', 'gray_snake': 'snake', 'brown_snake': 'snake', 'salamander': 'salamander',
 'large_snake': 'python', 'cobra': 'cobra', 'king_cobra': 'cobra', 'rattlesnake': 'pit viper',
 'goblin': 'ordinary goblin', 'goblin_warrior': 'goblin', 'goblin_warmaster': 'goblin prince',
 'goblin_chieftain': 'goblin king', 'sheep': 'white sheep', 'goat': 'gray goat', 'cat': 'barn cat',
 'wild_cat': 'lynx', 'orc': 'orc', 'large_orc': 'orc knight', 'hill_orc': 'hill orc', 'dark_orc': 'orc rogue',
 'lieutenant_orc': 'orc samurai', 'captain_orc': 'orc barbarian', 'chieftain_orc': 'orc king',
 'xshee_voo': 'cyclops', 'torin': 'dwarf king', 'jorgus': 'thief', 'ahkulan': 'wizard', 'rotmoth': 'wizard',
 'giana': 'priestess', 'brida': 'healer', 'roderik': 'king arthur', 'gefeon': 'wizard',
 'dwarf_cleric': 'dwarf healer', 'highpriest': 'priest', 'todin': 'dwarf lord', 'yohjishiro': 'elf lord',
 'ozorik': 'knight', 'elder_gridor': 'priest',
}
CLASS = {'rat': 'sewer rat', 'bat': 'giant bat', 'feline': 'barn cat', 'canine': 'hound',
         'reptile': 'snake', 'insect': 'giant beetle', 'human': 'fighter', 'orc': 'orc',
         'giant': 'hill giant', 'kobold': 'kobold', 'undead': 'skeleton', 'goblin': 'goblin',
         'demon': 'imp', 'humanoid': 'elf', 'blob': 'gray ooze', 'other': 'lichen'}
RACE = {'human': 'fighter', 'half_elf': 'ranger', 'high_elf': 'elf', 'halfling': 'hobbit',
        'half_orc': 'orc', 'dwarf': 'dwarf', 'gnome': 'gnome'}
ITEM = {
 'club': 'club', 'war_hammer': 'war hammer', 'dagger': 'dagger', 'knife': 'knife',
 'orcish_dagger': 'orcish dagger', 'long_dagger': 'athame', 'short_sword': 'short sword',
 'long_sword': 'long sword', 'broad_sword': 'broadsword', 'rapier': 'elven short sword', 'scimitar': 'scimitar',
 'katana': 'runesword', 'wakizashi': 'dwarvish short sword', 'small_axe': 'axe', 'war_axe': 'axe',
 'battle_axe': 'battle axe', 'great_axe': 'battle axe', 'orcish_axe': 'axe', 'mace': 'mace',
 'flail': 'flail', 'short_spear': 'spear', 'long_spear': 'orcish spear', 'pitchfork': 'trident',
 'pike': 'long poleaxe', 'halberd': 'hooked polearm', 'staff': 'quarterstaff',
 'small_shield': 'small shield', 'medium_shield': 'orcish shield', 'large_shield': 'large shield',
 'tower_shield': 'dwarvish shield', 'clothes': 'peasant robes', 'dress': 'peasant robes', 'robe': 'monk robes',
 'light_mail': 'bronze armor', 'scale_mail': 'scale armor', 'plate_mail': 'full plate',
 'chain_mail': 'grandmaster mail', 'ring_mail': 'hotrock mail', 'long_bow': 'longbow',
 'short_bow': 'shortbow', 'light_crossbow': 'crossbow', 'crossbow': 'crossbow',
 'heavy_crossbow': 'crossbow', 'sling': 'sling', 'gloves': 'leather glove', 'gauntlets': 'iron gauntlet',
 'knuckles': 'surgical glove', 'sandals': 'quiet boots', 'light_boots': 'leather boots', 'soft_boots': 'elven boots',
 'hard_boots': 'mountaineer boots', 'light_cloak': 'desert cloak', 'cloak': 'hill cloak', 'shadow_cloak': 'moldy cloak',
 'cape': 'forest cloak', 'hat': 'wizard hat', 'cap': 'elven leather helm', 'helmet': 'visored helm', 'arrow': 'arrow',
 'quarrel': 'crossbow bolt', 'sling_bullet': 'flint stone', 'rock': 'rock', 'shuriken': 'shuriken',
 'alchemy_set': 'bottle', 'cooking_set': 'tin opener', 'pickaxe': 'pick axe',
 'ancient_machine_part': 'magic marker', 'amulet': 'amethyst pendant', 'ring': 'gold ring',
 'scroll': 'blank scroll', 'book': 'blank book', 'money': 'pile of gold coins', 'corpse': 'corpse',
 'chest': 'closed chest', 'herb': 'sprig of wolfsbane', 'potion': 'clear potion',
}
SPECIAL = {'avanor_defender': 'elven shield', 'forest_brother_cloak': 'forest cloak',
           'eye_of_raa': 'gleaming red gem', 'black_club': 'club', 'avanor_crown': 'kingly crown',
           'avanor_scepter': 'jeweled wand', 'torin_axe': 'battle axe', 'torin_shield': 'dwarvish shield',
           'dwarf_crown': 'kingly crown', 'great_elemental_ring': 'diamond ring', 'glamdring': 'long sword',
           'death_hack': 'battle axe', 'avanor_mitre': 'princely crown', 'rat_tail': 'strip of meat',
           'bat_wing': 'meatball', 'bone': 'bones', 'large_ration': 'food ration', 'ration': 'food ration',
           'small_ration': 'k ration', 'elvish_waybread': 'lembas wafer'}
KIND = {'HAT': 'visored helm', 'NECK': 'amethyst pendant', 'BODY': 'bronze armor', 'CLOAK': 'hill cloak',
        'WEAPON': 'long sword', 'SHIELD': 'small shield', 'GLOVES': 'leather glove', 'RING': 'gold ring',
        'BOOTS': 'leather boots', 'MISSILEW': 'shortbow', 'MISSILE': 'arrow', 'POTION': 'clear potion',
        'SCROLL': 'blank scroll', 'BOOK': 'blank book', 'WAND': 'oak wand', 'FOOD': 'food ration',
        'OTHER': 'bag', 'LIGHTSOURCE': 'brass lantern', 'TOOL': 'bag', 'GEM': 'dull red gem',
        'MONEY': 'pile of gold coins', 'CHEST': 'closed chest'}
POTION = {'clear': 'clear potion', 'smoky': 'smoky potion', 'green': 'emerald potion',
 'orange': 'orange potion', 'yellow': 'yellow potion', 'black': 'black potion', 'blue': 'brilliant blue potion',
 'white': 'white potion', 'cyan': 'cyan potion', 'purple': 'puce potion', 'haze': 'cloudy potion',
 'golden': 'golden potion', 'silver': 'effervescent potion', 'azure': 'sky blue potion', 'murky': 'murky potion',
 'red': 'ruby potion', 'glowing': 'radiant potion', 'mottled': 'swirly potion', 'blobby': 'bubbly potion',
 'pink': 'pink potion', 'mouldy': 'smelly potion', 'gray': 'dripping potion', 'mercury': 'sparkly potion',
 'oily': 'syrupy potion', 'viscous': 'sloshing potion', 'dark_red': 'sanguine potion',
 'light_red': 'purple red potion', 'dark_blue': 'sinister potion', 'light_blue': 'fizzy potion',
 'brown': 'brown potion', 'light_gray': 'milky potion', 'dark_gray': 'dark potion',
 'dark_green': 'dark green potion', 'light_green': 'winged potion', 'beige': 'sealed potion',
 'violet': 'magenta potion', 'turquoise': 'legged potion', 'spare': 'spraying potion'}
# Same-set stand-ins for the appearances DawnLike has no sprite left for.
POTION_DUP = {'aquamarine': 'cyan potion', 'coral': 'pink potion', 'ivory': 'white potion',
 'maroon': 'sanguine potion', 'tan': 'brown potion', 'dark': 'dark potion'}
POTION_ANY = 'clear potion'   # stand-in only for unknown appearances; see coverage
PLANT = {'red_mushroom': 'red cap mushroom', 'green_mushroom': 'slimy mushroom',
         'blue_mushroom': 'luminous mushroom', 'yellow_mushroom': 'hardball mushroom',
         'white_mushroom': 'fairy step mushrooms', 'valeriana_root': 'ripe root vegetable',
         'stellaria_leave': 'eucalyptus leaf', 'trifolium_leave': 'young leafy vegetable',
         'trifolium_flower': 'white flowers', 'urtica_leave': 'ripe leafy vegetable',
         'convallaria_flower': 'sparse white flowers', 'tussilago_leave': 'young herb cluster',
         'melissa_leave': 'ripe herb cluster', 'mentha_leave': 'sprig of wolfsbane',
         'taraxacum_flower': 'gold flowers', 'paeonia_root': 'young root vegetable',
         'plantago_leave': 'bamboo shoots', 'chamomilla_flower': 'sparse gold flowers'}
PLANT_ANY = 'sprig of wolfsbane'
TRAP = {'magic_arrow': 'magic trap tile', 'fire_bolt': 'fire trap tile', 'acid_bolt': 'rust trap tile',
        'teleport': 'teleportation trap tile', 'arrow': 'arrow trap tile', 'pit': 'trap door tile',
        'spear_pit': 'bear trap tile'}
OBJ = {'up': 'small stairs up', 'down': 'small stairs down', 'door_open': 'open wooden door front',
       'door_closed': 'closed wooden door front', 'teleport': 'magic portal tile', 'altar': 'altar',
       'grave': 'gravestone a', 'trap': 'magic trap tile'}
LUAOBJ = {'herb_bush': 'green scrub', 'mushroom': 'big snout mushroom'}

slots, keys, missing = [], [], set()
def known(name): return all(n in pos for n in name.split('+'))
def slot(name):
    if not known(name):
        missing.add(name); return 0
    if name not in slots: slots.append(name)
    return slots.index(name)
def put(key, name): keys.append((key, slot(name)))

stats = {}
def count(cat, dedicated):
    d, n = stats.get(cat, (0, 0)); stats[cat] = (d + (1 if dedicated else 0), n + 1)

WJOIN = {0: 'center', 8: 'up down', 4: 'up down', 12: 'up down', 2: 'left right', 1: 'left right',
         3: 'left right', 10: 'left up', 9: 'right up', 6: 'left down', 5: 'right down',
         14: 'left up down', 13: 'right up down', 11: 'left right up', 7: 'left right down',
         15: 'left right up down'}
def sides(m): return ''.join(c for c, b in zip('nswe', (8, 4, 2, 1)) if m & b) or 'c'
for t in terrain:
    if t == 'UNKNOWN': continue
    if t in FLOOR:   names = ['%s floor %s' % (FLOOR[t], sides(m)) for m in range(16)]
    elif t in WALL:  names = ['%s wall %s' % (WALL[t], WJOIN[m]) for m in range(16)]
    elif t in ONE:   names = [ONE[t]] * 16
    else:            names = None
    count('terrain', names is not None)
    if names:
        base = len(slots)
        for n in names: slots.append(n) if known(n) else missing.add(n)
        keys.append((('tf:' if t in FLOOR else 'tw:' if t in WALL else 't:') + t, base))
for m, cl in monsters.items():
    count('monsters', m in MON); put('c:' + m, MON.get(m) or CLASS[cl])
for cl in classes: put('cc:' + cl, CLASS[cl])
for r in races: count('hero races', r in RACE); put('h:' + r, RACE[r])
for it in templates + list(ITEM):
    if ('i:' + it) in dict(keys): continue
    count('item types', it in ITEM); put('i:' + it, ITEM.get(it, 'bag'))
for s in specials: count('special items/food', s in SPECIAL or s in ITEM); put('f:' + s, SPECIAL.get(s) or ITEM.get(s, 'bag'))
for k, n in KIND.items(): put('k:' + k, n)
for c in colours: count('potion appearances', c in POTION); put('pc:' + c, POTION.get(c) or POTION_DUP.get(c, POTION_ANY))
for p in plants: count('plants', p in PLANT); put('p:' + p, PLANT.get(p, PLANT_ANY))
for t in traps: count('trap types', t in TRAP); put('tr:' + t, TRAP[t])
for o, n in OBJ.items(): count('map objects', True); put('o:' + o, n)
for o in luaobj: count('map objects', o in LUAOBJ); put('lo:' + o, LUAOBJ[o])
count('map objects', False); count('map objects', False)   # furniture, outer objects: text

if missing:
    import difflib
    for n in sorted(missing):
        print('missing:', n, '->', difflib.get_close_matches(n, pos.keys(), 4, 0.5))
    sys.exit(1)

cols = 16
sheet = Image.new('RGBA', (cols * 16, (len(slots) + cols - 1) // cols * 16), (0, 0, 0, 0))
for i, n in enumerate(slots):
    for part in n.split('+'):
        sheet.alpha_composite(sprite(part), (i % cols * 16, i // cols * 16))
sheet.save(os.path.join(ROOT, 'web', 'tiles-dawn.png'), optimize=True)
with open(os.path.join(ROOT, 'port', 'dawn_map.inc'), 'w') as f:
    f.write('// Generated by web/mkdawn.py - do not edit.\n')
    for k, s in keys: f.write('{"%s", %d},\n' % (k, s))
td = tn = 0
for c, (d, n) in stats.items():
    print('%-20s %3d/%3d own sprite' % (c, d, n)); td += d; tn += n
print('total %d/%d = %.1f%% own sprite, rest same-set stand-ins; %d slots'
      % (td, tn, 100.0 * td / tn, len(slots)))
