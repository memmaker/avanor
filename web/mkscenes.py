#!/usr/bin/env python3
"""Preview scenes for the remapper (design time only, Avanor never reads them).
Reads the ids from web/avanor-dawnlike.rec, writes web/avanor-scenes.rec:
  overworld  grass, forest, river with bridge, road, hills, mountains, a village
  dungeon    cave rooms, walls, doors, stairs, traps, lava, altar, monsters, items
  terrain    every world id: all 16 autotile variants of every floor and wall
             style, the single terrains, map objects and traps
  bestiary   every monster, items  every item
Tiles as port/rvip_tiles.cpp picks them: a floor is bordered on the sides whose
neighbour is other terrain, a wall joins walls and doors; a thing is drawn over
its cell's terrain (ground), as map.cpp sends bg and fg. Each scene of a
category asserts it shows every id of it. Run after mkdawn.py:
  python3 web/mkscenes.py"""
import os, random, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.environ.get('RVIP_TILESETS', os.path.expanduser('~/Games/rvip-tools/tilesets')))
import dawnlike_rec

NAMES = dawnlike_rec.read_ids(os.path.join(ROOT, 'web', 'avanor-dawnlike.rec'))
IDS = {c: [i for i, _ in v] for c, v in NAMES.items()}
FLOORS = sorted({i.rsplit('_', 1)[0] for i, n in NAMES['world'] if '(border' in n})
WALLS = sorted({i.rsplit('_', 1)[0] for i, n in NAMES['world'] if '(joins' in n})
AUTO = {i for i, n in NAMES['world'] if '(border' in n or '(joins' in n}
SINGLE = [i for i in IDS['world'] if i not in AUTO]
TRAPS = [i for i in SINGLE if i.startswith('trap')]
DOORS = ('door_open', 'door_closed')


class Scene:
    def __init__(self, w, h, ground):
        self.w, self.h = w, h
        self.terr = [[ground] * w for _ in range(h)]   # floor / wall style or single terrain id
        self.over = [[None] * w for _ in range(h)]     # "category/id" standing on it

    def fill(self, x0, y0, x1, y1, t):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.terr[y][x] = t

    def room(self, x0, y0, x1, y1, wall, floor):
        """floor x0..x1, y0..y1, a ring of wall around it"""
        self.fill(x0 - 1, y0 - 1, x1 + 1, y1 + 1, wall)
        self.fill(x0, y0, x1, y1, floor)

    def put(self, key, x, y):
        self.over[y][x] = key

    def free(self, x, y):
        return self.terr[y][x] in FLOORS and self.over[y][x] is None

    def scatter(self, keys, x0, y0, x1, y1, step=2, rnd=None):
        spots = [(x, y) for y in range(y0, y1 + 1, step) for x in range(x0, x1 + 1, step) if self.free(x, y)]
        if rnd:
            spots = rnd.sample(spots, len(spots))
        assert len(spots) >= len(keys), (len(spots), len(keys))
        for k, (x, y) in zip(keys, spots):
            self.put(k, x, y)

    def terrain(self, x, y):
        """the terrain tile as rvip_tiles.cpp RvipTerrainTile() picks it"""
        def wallish(x, y):
            return (0 <= x < self.w and 0 <= y < self.h
                    and (self.terr[y][x] in WALLS or self.over[y][x] in ['world/' + d for d in DOORS]))

        def same(x, y, t):   # outside the map counts as the same floor (SameFloor)
            return not (0 <= x < self.w and 0 <= y < self.h) or self.terr[y][x] == t
        sides = list(zip((8, 4, 2, 1), ((0, -1), (0, 1), (-1, 0), (1, 0))))
        t = self.terr[y][x]
        if t is None:
            return None
        if t in FLOORS:
            return 'world/%s_%d' % (t, sum(b for b, (dx, dy) in sides if not same(x + dx, y + dy, t)))
        if t in WALLS:
            return 'world/%s_%d' % (t, sum(b for b, (dx, dy) in sides if wallish(x + dx, y + dy)))
        return 'world/' + t

    def layers(self):
        """(map, ground) as map.cpp sends a cell: the object, item or creature on top (fg), the cell's
        terrain under it (bg); a cell with nothing on it is its terrain alone"""
        top = [[self.over[y][x] or self.terrain(x, y) for x in range(self.w)] for y in range(self.h)]
        ground = [[self.terrain(x, y) if self.over[y][x] else None for x in range(self.w)] for y in range(self.h)]
        return top, ground


MNEMONIC = {'world/up': '<', 'world/down': '>', 'world/door_closed': '+', 'world/door_open': "'",
            'world/water': '~', 'world/deep_water': '=', 'world/tree': 'T', 'world/lava': '&'}


def rec(sc, sid, name, cats):
    return dawnlike_rec.scene_lines(sid, name, cats, *sc.layers(), mnemonic=MNEMONIC)


def check(sc, cat):
    used = {c for grid in sc.layers() for row in grid for c in row if c}
    missing = [i for i in IDS[cat] if cat + '/' + i not in used]
    assert not missing, (cat, missing)


def things(rnd, n_mon, n_obj):
    mons = rnd.sample([i for i in IDS['monster'] if not i.startswith(('any_', 'hero_'))], n_mon)
    objs = rnd.sample([i for i in IDS['object'] if not i.startswith('any_')], n_obj)
    return ['monster/' + m for m in mons] + ['object/' + o for o in objs]


def overworld():
    rnd = random.Random(3)
    s = Scene(64, 26, 'green_grass')
    for _ in range(90):                                  # forest in the west
        s.terr[rnd.randrange(1, 25)][rnd.randrange(1, 18)] = 'tree'
    s.fill(40, 0, 44, 25, 'water')                       # the river, deep in the middle
    s.fill(42, 0, 42, 25, 'deep_water')
    s.fill(38, 0, 39, 25, 'sand')
    s.fill(45, 0, 46, 25, 'sand')
    s.fill(0, 12, 63, 12, 'road')                        # a road over a bridge
    s.fill(40, 12, 44, 12, 'bridge')
    s.fill(30, 13, 30, 25, 'path')
    s.fill(48, 1, 62, 9, 'hill')                         # hills and mountains in the north east
    for x, y, t in ((52, 3, 'low_mountain'), (53, 3, 'low_mountain'), (54, 4, 'mountain'), (55, 4, 'mountain'),
                    (56, 5, 'high_mountain'), (55, 5, 'mountain'), (53, 4, 'low_mountain'), (57, 5, 'mountain')):
        s.terr[y][x] = t
    s.room(21, 3, 29, 7, 'wood_wall', 'stone_floor')     # a village: two houses and a fenced field
    s.terr[2][24], s.terr[2][27] = 'window', 'window'
    s.terr[8][25] = 'stone_floor'   # a door stands on floor, the wall runs round it
    s.put('world/door_closed', 25, 8)
    s.room(21, 16, 27, 20, 'wood_wall', 'stone_floor')
    s.terr[15][24] = 'stone_floor'
    s.put('world/door_open', 24, 15)
    s.fill(49, 15, 60, 23, 'fence')
    s.fill(50, 16, 59, 22, 'green_grass')
    s.fill(51, 17, 58, 21, 'path')
    s.put('world/herb_bush', 12, 20)
    s.put('world/mushroom', 14, 22)
    s.put('world/up', 5, 22)
    spots = [(x, y) for y in range(26) for x in range(64) if s.terr[y][x] in ('green_grass', 'stone_floor', 'path', 'sand', 'road') and not s.over[y][x]]
    for k, xy in zip(things(rnd, 14, 10), rnd.sample(spots, 24)):
        s.put(k, *xy)
    return rec(s, 'overworld', 'Overworld', ['world', 'monster', 'object'])


def dungeon():
    rnd = random.Random(5)
    s = Scene(66, 24, None)
    rooms = [(2, 2, 14, 7), (22, 1, 34, 5), (44, 2, 62, 8), (4, 13, 16, 20), (26, 11, 40, 20), (48, 14, 62, 21)]
    for r in rooms:
        s.room(*r, 'stone_wall', 'cave_floor')
    for (x0, y0), (x1, y1) in (((15, 4), (21, 4)), ((35, 3), (43, 3)), ((8, 8), (8, 12)), ((28, 6), (28, 10)),
                               ((17, 16), (25, 16)), ((52, 9), (52, 13)), ((41, 18), (47, 18))):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            for x in range(min(x0, x1), max(x0, x1) + 1):
                if s.terr[y][x] == 'stone_wall':
                    s.terr[y][x] = 'cave_floor'
                    s.put('world/' + rnd.choice(DOORS), x, y)
                elif s.terr[y][x] is None:
                    s.terr[y][x] = 'cave_floor'
    s.fill(29, 14, 31, 15, 'lava')
    s.put('world/up', 4, 3)
    s.put('world/down', 60, 20)
    s.put('world/altar', 33, 12)
    s.put('world/grave', 50, 3)
    s.put('world/teleport', 12, 18)
    for t in rnd.sample(TRAPS, 3):
        s.put('world/' + t, *rnd.choice([(x, y) for y in range(11, 21) for x in range(26, 41) if s.free(x, y)]))
    spots = [(x, y) for (x0, y0, x1, y1) in rooms for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if s.free(x, y)]
    for k, xy in zip(things(rnd, 14, 20), rnd.sample(spots, 34)):
        s.put(k, *xy)
    return rec(s, 'dungeon', 'Dungeon level', ['world', 'monster', 'object'])


def terrain():
    """every floor style as a room, a one-row strip, a one-column strip and a single cell (16 borders);
    every wall style as a lattice (corners, tees, a crossing, straights), two stubs and a lone block (16 joins)"""
    per_row = 5
    fw, fh, ww, wh = 13, 6, 12, 8
    rows_f = -(-len(FLOORS) // per_row)
    rows_w = -(-len(WALLS) // per_row)
    w = per_row * max(fw, ww) + 1
    h = rows_f * fh + rows_w * wh + 4
    s = Scene(w, h, None)
    for i, f in enumerate(FLOORS):
        x, y = 1 + i % per_row * max(fw, ww), 1 + i // per_row * fh
        s.fill(x - 1, y - 1, x + fw - 2, y + fh - 2, 'obsidian_floor' if f != 'obsidian_floor' else 'stone_floor')
        s.fill(x, y, x + 3, y + 2, f)            # room: none, n, s, w, e and the corners
        s.fill(x + 5, y, x + 7, y, f)            # row: ns, nsw, nse
        s.fill(x + 9, y, x + 9, y + 2, f)        # column: we, nwe, swe
        s.fill(x + 6, y + 2, x + 6, y + 2, f)    # single: nswe
    top = rows_f * fh + 1
    for i, wl in enumerate(WALLS):
        x, y = 1 + i % per_row * max(fw, ww), top + i // per_row * wh
        s.fill(x - 1, y - 1, x + ww - 2, y + wh - 2, 'stone_floor')
        for d in (0, 2, 4):
            s.fill(x, y + d, x + 4, y + d, wl)
            s.fill(x + d, y, x + d, y + 4, wl)
        s.fill(x + 6, y, x + 7, y, wl)           # stub: ends e, w
        s.fill(x + 6, y + 2, x + 6, y + 3, wl)   # stub: ends s, n
        s.fill(x + 9, y + 4, x + 9, y + 4, wl)   # lone block
    y = h - 2
    s.fill(0, y - 1, w - 1, y + 1, 'stone_floor')
    x = 1
    for t in SINGLE:
        if t in DOORS or t in TRAPS or t in ('up', 'down', 'teleport', 'altar', 'grave', 'trap', 'herb_bush', 'mushroom'):
            s.put('world/' + t, x, y)
        else:
            s.terr[y][x] = t
        x += 2
    check(s, 'world')
    return rec(s, 'terrain', 'All terrain', ['world'])


def gallery(cat, sid, name, w, skip=()):
    keys = [cat + '/' + i for i in IDS[cat] if not i.startswith(skip)]
    h = 2 * -(-len(keys) // ((w - 3) // 2 + 1)) + 1
    s = Scene(w + 4, h + 4, None)
    s.room(2, 2, w + 1, h + 1, 'stone_wall', 'stone_floor')
    s.scatter(keys, 3, 3, w, h)
    check(s, cat)
    return rec(s, sid, name, [cat])


out = ['# Remapper preview scenes for Avanor (web/mkscenes.py writes them; design time only)', '', '%rec: Scene', '']
for r in (overworld(), dungeon(), terrain(), gallery('monster', 'bestiary', 'Every monster', 41),
          gallery('object', 'items', 'Every item', 51)):
    out += r + ['']
open(os.path.join(ROOT, 'web', 'avanor-scenes.rec'), 'w').write('\n'.join(out))
print('avanor-scenes.rec: 5 scenes; world, monster and object ids all shown')
