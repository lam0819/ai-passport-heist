#!/usr/bin/env python3
"""Generates the Midnight Heist levels, checked before anything is written:

  main/levels.c       the firmware's const level table (flash, no RAM)
  prototype/levels.js the same maps for the browser prototype

Each level lists its floors top to bottom, one character per 16 px column:

  .  corridor          S  start (ground floor)    E  roof exit (top floor)
  H  hiding alcove     |  door (closed)            $  loot (gold bar in a bonus)
  ^  stairs up         v  stairs down: same column on the floor above a ^
  g  guard             G  shield guard (stun pellets bounce off the front)
  z  sleeping guard    d  guard dog                c  empty can (noisy)
  x  crate (jump it)   p  banana peel (jump it or slip noisily)
  b  springboard: bounces you through the hatch 'o' in the same column of
     the floor above (one way: there is no way down through a hatch)

Levels are played strictly in order; a bonus stage follows every third level.

Checks, per level (a level that fails any of them is refused):
  * fits the firmware's fixed arrays (main/config.h MAX_*) and the 5x7 font;
  * structure: one start on the ground floor, one exit on the top floor,
    stairs and springboard/hatch pairs line up, crates keep clear of stairs,
    doors, alcoves, springboards and the level ends, crates and springboards
    leave two clear columns for a run-up, crates stay 3 columns from stairs
    and doors (there OK uses the stairs / door instead of jumping), doors are not
    at the ends or next to each other;
  * solvability: a route graph over every (floor, column) cell -- walking,
    opening doors, jumping crates and peels, stairs both ways, springboards
    one way up -- must reach every loot from the start and then the exit
    (bonus: every gold bar). A springboard you only pass has to be jumped,
    like a crate (stepping on it bounces you upstairs). The shortest full route (start -> all loot in
    the best order -> exit, solved exactly) gives the route time;
  * the route must fit the level's timer with room to spare for stealth
    (route + a per-guard allowance <= 85% of the timer);
  * fairness: no awake guard or dog within 6 columns of the start on its
    floor, none standing on a stairs or hatch landing, an alcove on every
    floor that has an awake guard (bonus: on every spotlit floor);
  * the difficulty curve: across the regular levels the threat score
    (guards, dogs, shield guards, hazards, floors) and the time pressure
    never go down; bonus stages get harder one after another too.

Run:  python3 tools/gen_levels.py           (writes both files)
      python3 tools/gen_levels.py --check   (checks and prints the table only)
"""

import heapq
import itertools
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
FONT_RE = re.compile(r"[ A-Z0-9.,:!?\-+/<>()'=_*$]*")

LOOT = dict(gold="LOOT_GOLD", watch="LOOT_WATCH", jade="LOOT_JADE", vase="LOOT_VASE",
            painting="LOOT_PAINTING", jewel="LOOT_JEWEL", bar="LOOT_BAR")

# Route-time model (seconds), from main/config.h: walking 70 px/s over 16 px columns.
COL_S = 16 / 70
DOOR_S, CRATE_S, PEEL_S, STAIRS_S, SPRING_S, EXIT_S = 0.6, 0.3, 0.2, 0.9, 1.0, 0.5
GUARD_ALLOW_S = dict(g=3.0, G=4.0, d=3.5, z=1.0)   # stealth allowance per guard
PRESSURE_MAX, BONUS_PRESSURE_MAX = 0.85, 0.80


def pal(wall, stripe, dado, floor):
    h = lambda s: tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
    return dict(wall=h(wall), stripe=h(stripe), dado=h(dado), floor=h(floor))


LEVELS = [
    dict(name="1-1 PAWNSHOP", zh="1-1 大押", sub="TUTORIAL - THE OLD PAWNSHOP", tutorial=True, time=150,
         pal=pal("#3c4a6b", "#36435f", "#2b3550", "#5a4a3c"),
         loot=[("gold", 1000), ("watch", 800), ("jade", 1500)],
         floors=[
             "....H....$..v.....|....Ho..g.$.E",
             "S..c..x..H..^...g...|.$.b..H..z.",
         ]),
    dict(name="1-2 TEA HALL", zh="1-2 茶樓", sub="DIM SUM HALL AFTER CLOSING", time=150,
         pal=pal("#6b3c3c", "#613636", "#4a2828", "#6a5038"),
         loot=[("jade", 1200), ("gold", 1000), ("watch", 800), ("vase", 1500)],
         floors=[
             "..$...H...v...|....g...H...$...z...E..",
             "S....c...H^....x...|..g...H..$...c..$.",
         ]),
    dict(name="1-3 ARCADE", zh="1-3 機舖", sub="NEON GAMES CENTRE - WATCH THE DOG", time=150,
         pal=pal("#3a2f5c", "#342a54", "#261f40", "#4a4058"),
         loot=[("gold", 1000), ("jewel", 2000), ("watch", 800), ("vase", 1200)],
         floors=[
             "...$...H....v....z...|...H...o...$...E..",
             ".$...H..g...^...p...|...H..v.b...d..H...",
             "S...c...H...x...g...|..H...^...x...$..c.",
         ]),
    dict(name="BONUS: VAULT", zh="金庫探射燈", sub="THE GOLD VAULT - DODGE THE SPOTLIGHTS", bonus=True, time=30,
         pal=pal("#2e2a22", "#29261f", "#1f1c17", "#4a4030"),
         lights=[(0, 44, 40), (1, 40, 52), (2, 48, 34)],
         floors=[
             "..$..H...v...$..H.$.",
             ".$..H..$.^..v.H..$..",
             "S...H.$.....^..H..$.",
         ]),
    dict(name="2-1 GINSENG", zh="2-1 參茸海味舖", sub="THE HERBALIST - MEET THE SHIELD GUARD", time=150,
         pal=pal("#5a4a2a", "#524426", "#3e331c", "#6a5a3a"),
         loot=[("jade", 1500), ("gold", 1000), ("vase", 1200), ("watch", 800), ("jewel", 2000)],
         floors=[
             "..$...H...v...G...|...H...z...$...H...v.E.",
             ".H...$....^...g...H..|...c...$...Hv...^.p.",
             "S...H...x...g...H...|...c...x...H.^.$..z..",
         ]),
    dict(name="2-2 TRAMYARD", zh="2-2 電車廠", sub="DING DING DEPOT AT MIDNIGHT", time=150,
         pal=pal("#2f5a46", "#2a5240", "#1f3e30", "#5a5048"),
         loot=[("gold", 1000), ("watch", 800), ("jewel", 2000), ("vase", 1200), ("painting", 1800)],
         floors=[
             "..$...H...o...g...|...H...$...v...G...H...E...",
             ".H..$..d..b...|...H...o...x...^...g...H..$..p.",
             "S...H...c...x...g...H.b...|...H...p...$...g...",
         ]),
    dict(name="2-3 MANSION", zh="2-3 富豪大宅", sub="THE TYCOON'S MANSION", time=135,
         pal=pal("#5b3a4a", "#533544", "#3d2733", "#6a5038"),
         loot=[("vase", 1200), ("painting", 2000), ("gold", 1000), ("jewel", 2500), ("watch", 800)],
         floors=[
             "..$...H...v..x..|...G....H..o.$..|...z.H..E.",
             "...H..g...^..c...|..H..$.p..b.v..d..|.H..$..",
             "S...c..H...g..x.H..p..|..H.x..^..g..H..$.c..",
         ]),
    dict(name="BONUS: JADE", zh="玉器市場", sub="THE JADE MARKET STRONGROOM", bonus=True, time=30,
         pal=pal("#24463a", "#203f34", "#183028", "#3e5a48"),
         lights=[(0, 44, 50), (1, 40, 60), (2, 46, 46)],
         floors=[
             ".$..H.....v...$..H...$....",
             "..$..H....^..$..v..H..$...",
             "S.....H..$..x...^..$..H...",
         ]),
    dict(name="3-1 FERRY", zh="3-1 渡輪碼頭", sub="THE HARBOUR FERRY PIER", time=150,
         pal=pal("#2a4a6a", "#264462", "#1c344c", "#4a5a6a"),
         loot=[("watch", 800), ("gold", 1000), ("painting", 1800), ("jade", 1500), ("vase", 1200), ("jewel", 2200)],
         floors=[
             "..$...H..v...g...|...H...z...$...H...G...|...v..E.",
             ".H...$...^...d...H...|...cv..x...g...H...$...^.p..",
             "S...H...c...g...H...x...|.^.$...H...g...p...H..$..",
         ]),
    dict(name="3-2 TAILOR", zh="3-2 洋服店", sub="SUITS MADE IN 24 HOURS", time=140,
         pal=pal("#4a3a5a", "#443552", "#33283e", "#5a4a4a"),
         loot=[("gold", 1000), ("jewel", 2200), ("watch", 800), ("painting", 1800), ("vase", 1200), ("jade", 1500)],
         floors=[
             ".H..$...G...H..v...|...g...H..o...$...|...z...H...E.",
             ".$...H...d..x..^...H...|...g..b...H..v...p...$...G..",
             "S...H...c...g...|...H...x...$...g....^...H...c..$.d.",
         ]),
    dict(name="3-3 JEWELLER", zh="3-3 金舖", sub="GOLD SHOP ON THE GOLDEN MILE", time=140,
         pal=pal("#6a5a2a", "#625226", "#4a3e1c", "#6a5a48"),
         loot=[("gold", 1000), ("jewel", 2500), ("watch", 900), ("gold", 1000), ("jade", 1600), ("jewel", 2500), ("vase", 1200)],
         floors=[
             "..$...H...v...g...|...H...G...$...H...z...E.",
             ".H...$....^...x...H...|...d...H..v...$...g..",
             "$...H...g...|...c...H...x...$....^..H..v..p.",
             "S...H...p...g...H...|...c...H...x...$..^..G.",
         ]),
    dict(name="BONUS: JUNK", zh="帆船貨艙", sub="THE JUNK BOAT'S HOLD", bonus=True, time=35,
         pal=pal("#4a3020", "#432b1d", "#332116", "#5a4030"),
         lights=[(0, 48, 56), (1, 44, 66), (2, 50, 52)],
         floors=[
             ".$..H......v..$...H..$...H....",
             ".$..H..$...^.....v..H...$..H..",
             "S..H..$..x.....H.^...$..H..$..",
         ]),
    dict(name="4-1 HOTEL", zh="4-1 海景酒店", sub="THE GRAND HARBOUR HOTEL", time=150,
         pal=pal("#3a4a5a", "#354452", "#28333e", "#5a5048"),
         loot=[("jewel", 2500), ("watch", 900), ("gold", 1000), ("painting", 2000), ("vase", 1200), ("jade", 1600), ("gold", 1000), ("watch", 800)],
         floors=[
             "..$...H...G...|...H...v...g...$...H...o...z...H.E.",
             ".H...$...g...x...H....^..|...d...H....b...$...v...",
             ".$...H...c...|...g...H.x.$....v..H...G...p....^...",
             "S...H...c...g...H...|...x...$.^.H...g...|...H..$..",
         ]),
    dict(name="4-2 MUSEUM", zh="4-2 博物館", sub="THE MUSEUM OF ART AFTER HOURS", time=150,
         pal=pal("#4a4a4a", "#444444", "#333333", "#5a5048"),
         loot=[("painting", 2000), ("vase", 1500), ("jade", 1600), ("painting", 2400), ("jewel", 2500), ("vase", 1200), ("gold", 1000), ("watch", 900), ("jewel", 2200)],
         floors=[
             "..$...H...g...|...H...z...$...G...H...v...|...$...H..E..",
             ".H...$...d...x...H...|...g...H...o....^...H...c...G...$.",
             ".$...H...g...|...p...H...v...$...b...H...x...g...|...H..",
             "S...H...c...g...H...|....^..x...$...H...d...p...H...$...",
         ]),
    dict(name="4-3 TOWER", zh="4-3 銀行大廈", sub="THE BANK TOWER - THE LAST JOB", time=160,
         pal=pal("#2a3a4a", "#263544", "#1c2833", "#4a4a5a"),
         loot=[("gold", 1000), ("jewel", 2500), ("watch", 900), ("gold", 1000), ("painting", 2400), ("jade", 1600), ("jewel", 3000), ("vase", 1500), ("gold", 1200), ("painting", 2600)],
         floors=[
             "..$...H...G...|...H...g...$...v...H...z...|...$...G...H..E..",
             ".H...$...g...x...H...|...d....^..H...$...o...|...g...H..v...",
             ".$...H...|...g...c...H...x...$v..G...H...b...p...H...$..^...",
             "S...H...c...g...H...|...x...$.^.H...g...|...H...d...p..$....",
         ]),
    dict(name="BONUS: PEAK", zh="山頂寶庫", sub="THE TREASURE ROOM ON THE PEAK", bonus=True, time=42,
         pal=pal("#3a3050", "#342b48", "#272038", "#504860"),
         lights=[(0, 46, 58), (1, 44, 70), (2, 48, 62), (3, 46, 54)],
         floors=[
             ".$..H.....v..$...H......H.$.",
             "....H..$..^...v..H..$...H...",
             "..$..H.....v..^..H..$...H...",
             "S..H..$....^.....H..$...H.$.",
         ]),
]


def config():
    vals = {}
    for m in re.finditer(r"#define\s+(\w+)\s+(\d+)\b", (HERE / "main" / "config.h").read_text()):
        vals[m.group(1)] = int(m.group(2))
    return vals


def tiles(floors):
    t = {k: [] for k in "SEH|$^vgGzdcxpbo"}
    bad = []
    for f, row in enumerate(floors):
        for col, ch in enumerate(row):
            if ch == ".":
                continue
            if ch not in t:
                bad.append(f"unknown tile {ch!r} at floor {f} col {col}")
                continue
            t[ch].append((f, col))
    return t, bad


def route_graph(floors, t):
    """Edges (cost) between (floor, col) cells."""
    nf, w = len(floors), len(floors[0])
    step = {}
    for f, row in enumerate(floors):
        for col in range(w):
            ch = row[col]
            # a springboard you only pass must be jumped, like a crate: walking onto it bounces you up
            step[(f, col)] = COL_S + (DOOR_S if ch == "|" else CRATE_S if ch in "xb" else PEEL_S if ch == "p" else 0)
    adj = {(f, c): [] for f in range(nf) for c in range(w)}
    for f in range(nf):
        for c in range(w):
            if c + 1 < w:
                adj[(f, c)].append(((f, c + 1), step[(f, c + 1)]))
                adj[(f, c + 1)].append(((f, c), step[(f, c)]))
    for f, c in t["^"]:
        adj[(f, c)].append(((f - 1, c), STAIRS_S))
    for f, c in t["v"]:
        adj[(f, c)].append(((f + 1, c), STAIRS_S))
    for f, c in t["b"]:
        adj[(f, c)].append(((f - 1, c), SPRING_S))
    return adj


def dijkstra(adj, src):
    dist = {src: 0.0}
    pq = [(0.0, src)]
    while pq:
        d, u = heapq.heappop(pq)
        if d > dist.get(u, 1e18):
            continue
        for v, c in adj[u]:
            nd = d + c
            if nd < dist.get(v, 1e18):
                dist[v] = nd
                heapq.heappush(pq, (nd, v))
    return dist


def best_route(adj, start, items, end):
    """Exact shortest start -> every item (any order) [-> end] (Held-Karp)."""
    pts = [start] + items + ([end] if end else [])
    D = [dijkstra(adj, p) for p in pts]
    INF = float("inf")
    d = lambda i, j: D[i].get(pts[j], INF)
    n = len(items)
    best = {(1 << i, i): d(0, 1 + i) for i in range(n)}
    for size in range(2, n + 1):
        for subset in itertools.combinations(range(n), size):
            mask = sum(1 << i for i in subset)
            for j in subset:
                prev = mask & ~(1 << j)
                best[(mask, j)] = min((best[(prev, k)] + d(1 + k, 1 + j) for k in subset if k != j), default=INF)
    full = (1 << n) - 1
    if n == 0:
        return d(0, len(pts) - 1) if end else 0.0
    if end:
        return min(best[(full, j)] + d(1 + j, n + 1) for j in range(n))
    return min(best[(full, j)] for j in range(n))


def check(lv, cfg):
    name, floors = lv["name"], lv["floors"]
    nf, w = len(floors), len(floors[0])
    bonus = lv.get("bonus", False)
    err, info = [], {}
    if not FONT_RE.fullmatch(name + lv["sub"]):
        err.append("name/sub must use the 5x7 font (capitals)")
    if len(name) > 12:
        err.append("name longer than 12 characters (the HUD shows 12)")
    if len(lv["sub"]) > 50:
        err.append("subtitle longer than 50 characters")
    if not 2 <= nf <= cfg["MAX_FLOORS"]:
        err.append(f"{nf} floors: want 2..{cfg['MAX_FLOORS']}")
    if not 20 <= w <= cfg["MAX_COLS"]:
        err.append(f"{w} columns: want 20..{cfg['MAX_COLS']}")
    if not 20 <= lv["time"] <= 600:
        err.append("timer out of range")
    for f, row in enumerate(floors):
        if len(row) != w:
            err.append(f"floor {f} is {len(row)} wide, want {w}")
    if err:
        return err, info
    t, bad = tiles(floors)
    err += bad
    limits = dict(doors=("|", "MAX_DOORS"), hides=("H", "MAX_HIDES"), loot=("$", "MAX_LOOT"),
                  cans=("c", "MAX_CANS"), crates=("x", "MAX_CRATES"), peels=("p", "MAX_PEELS"),
                  springs=("b", "MAX_SPRINGS"))
    for what, (ch, lim) in limits.items():
        if len(t[ch]) > cfg[lim]:
            err.append(f"{len(t[ch])} {what} > {lim} {cfg[lim]}")
    if len(t["^"]) + len(t["v"]) > cfg["MAX_STAIRS"]:
        err.append("too many stairs")
    if sum(len(t[c]) for c in "gGzd") > cfg["MAX_GUARDS"]:
        err.append("too many guards")
    if len(t["S"]) != 1 or t["S"][0][0] != nf - 1:
        err.append("exactly one start, on the ground floor")
    if bonus:
        if t["E"] or any(t[c] for c in "gGzd|"):
            err.append("a bonus stage has no exit, guards or doors")
        if len(lv["lights"]) > cfg["MAX_LIGHTS"]:
            err.append("too many spotlights")
        if len(t["$"]) < 5:
            err.append("a bonus stage needs at least 5 gold bars")
        for f, bw, sp in lv["lights"]:
            if not 0 <= f < nf or not 24 <= bw <= 64 or not 20 <= sp <= 90:
                err.append(f"spotlight {(f, bw, sp)} out of range")
            if not any(hf == f for hf, _ in t["H"]):
                err.append(f"spotlit floor {f} has no alcove")
    else:
        if len(t["E"]) != 1 or t["E"][0][0] != 0:
            err.append("exactly one exit, on the top floor")
        if len(t["$"]) != len(lv["loot"]):
            err.append(f"{len(t['$'])} $ tiles but {len(lv['loot'])} loot entries")
        if lv.get("lights"):
            err.append("spotlights only in bonus stages")
    for f, col in t["^"]:
        if f == 0 or (f - 1, col) not in t["v"]:
            err.append(f"stairs up at floor {f} col {col} lead nowhere")
    for f, col in t["v"]:
        if (f + 1, col) not in t["^"]:
            err.append(f"stairs down at floor {f} col {col} lead nowhere")
    for f, col in t["b"]:
        if (f - 1, col) not in t["o"]:
            err.append(f"springboard at floor {f} col {col} has no hatch above")
    for f, col in t["o"]:
        if (f + 1, col) not in t["b"]:
            err.append(f"hatch at floor {f} col {col} has no springboard below")
    near = t["^"] + t["v"] + t["|"] + t["H"] + t["b"]
    for f, col in t["x"]:
        if any(nf_ == f and abs(nc - col) < 2 for nf_, nc in near):
            err.append(f"crate at floor {f} col {col} too close to stairs/door/alcove/spring")
        if col in (0, w - 1):
            err.append(f"crate at floor {f} col {col} against the wall")
    for f, col in t["x"]:   # near stairs or a door, OK means "use it", not "jump"
        if any(sf_ == f and abs(sc_ - col) < 3 for sf_, sc_ in t["^"] + t["v"] + t["|"] + t["E"]):
            err.append(f"crate at floor {f} col {col}: the take-off would be a stairs/door action")
    for f, col in t["x"] + t["b"]:   # a running jump needs two clear columns on each side
        if any(df == f and abs(dc - col) <= 2 for df, dc in t["|"] + t["x"] + t["b"] if (df, dc) != (f, col)):
            err.append(f"{'crate' if (f, col) in t['x'] else 'springboard'} at floor {f} col {col}: no room for a run-up")
    for f, col in t["|"]:
        if col < 2 or col > w - 3:
            err.append(f"door at floor {f} col {col} too close to the end")
        if any(df == f and 0 < abs(dc - col) < 3 for df, dc in t["|"]):
            err.append(f"door at floor {f} col {col} next to another door")
    # fairness
    sf, sc = t["S"][0] if t["S"] else (nf - 1, 0)
    awake = t["g"] + t["G"] + t["d"]
    for f, col in awake:
        if f == sf and abs(col - sc) < 6:
            err.append(f"awake guard at floor {f} col {col} too close to the start")
    landings = t["^"] + t["v"] + [(f - 1, c) for f, c in t["^"]] + [(f + 1, c) for f, c in t["v"]] + t["o"]
    for f, col in awake + t["z"]:
        if any(lf == f and abs(lc - col) < 2 for lf, lc in landings):
            err.append(f"guard at floor {f} col {col} stands on a stairs/hatch landing")
    if not bonus:
        for f in range(nf):
            if any(gf == f for gf, _ in awake) and not any(hf == f for hf, _ in t["H"]):
                err.append(f"floor {f} has awake guards but no alcove")
    if err:
        return err, info
    # solvability: every loot reachable from the start, then the exit
    adj = route_graph(floors, t)
    start = (sf, sc)
    reach = dijkstra(adj, start)
    for p in t["$"] + t["E"]:
        if p not in reach:
            err.append(f"floor {p[0]} col {p[1]} unreachable from the start")
    for p in t["$"]:
        if t["E"] and t["E"][0] not in dijkstra(adj, p):
            err.append(f"no way to the exit from the loot at floor {p[0]} col {p[1]}")
    if err:
        return err, info
    route = best_route(adj, start, t["$"], t["E"][0] if t["E"] else None) + (0 if bonus else EXIT_S)
    allowance = sum(GUARD_ALLOW_S[c] * len(t[c]) for c in "gGdz")
    pressure = (route + allowance) / lv["time"]
    threat = (len(t["g"]) + 1.6 * len(t["G"]) + 1.4 * len(t["d"]) + 0.5 * len(t["z"]) +
              0.3 * (len(t["c"]) + len(t["p"]) + len(t["x"])) + 0.5 * len(t["$"]) + 1.0 * nf + w / 16)
    if pressure > (BONUS_PRESSURE_MAX if bonus else PRESSURE_MAX):
        err.append(f"too tight: route {route:.1f} s + allowance {allowance:.0f} s = {pressure:.0%} of {lv['time']} s")
    info = dict(route=route, pressure=pressure, threat=threat, nf=nf, w=w, loot=len(t["$"]),
                guards=len(t["g"]), shields=len(t["G"]), dogs=len(t["d"]), sleepers=len(t["z"]),
                hazards=len(t["c"]) + len(t["p"]) + len(t["x"]), springs=len(t["b"]), doors=len(t["|"]),
                hides=len(t["H"]))
    return err, info


def curve(infos):
    """The difficulty never drops from one level to the next of the same kind."""
    err = []
    for kind in (False, True):
        seq = [(lv, inf) for lv, inf in infos if lv.get("bonus", False) == kind]
        for (a, ia), (b, ib) in zip(seq, seq[1:]):
            if ib["threat"] < ia["threat"]:
                err.append(f"{b['name']}: threat {ib['threat']:.1f} below {a['name']} ({ia['threat']:.1f})")
            if ib["pressure"] + 0.005 < ia["pressure"]:
                err.append(f"{b['name']}: time pressure {ib['pressure']:.0%} below {a['name']} ({ia['pressure']:.0%})")
    return err


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_c(path):
    out = ["/* Generated by tools/gen_levels.py. Do not edit by hand. */",
           '#include "levels.h"', "", "const level_t g_levels[] = {"]
    for lv in LEVELS:
        p = lv["pal"]
        pal_s = ", ".join("{ %d, %d, %d }" % p[k] for k in ("wall", "stripe", "dado", "floor"))
        rows = ", ".join(c_str(r) for r in lv["floors"])
        loot = lv.get("loot", [])
        kinds = ", ".join(LOOT[k] for k, _ in loot) or "0"
        vals = ", ".join(str(v) for _, v in loot) or "0"
        lights = ", ".join("{ %d, %d, %d }" % l for l in lv.get("lights", [])) or "{ 0 }"
        out.append("    {")
        out.append(f"        .name = {c_str(lv['name'])}, .sub = {c_str(lv['sub'])},")
        out.append(f"        .tutorial = {str(lv.get('tutorial', False)).lower()}, .bonus = {str(lv.get('bonus', False)).lower()}, .time = {lv['time']},")
        out.append(f"        .floors = {len(lv['floors'])}, .w = {len(lv['floors'][0])},")
        out.append(f"        .pal = {{ {pal_s} }},")
        out.append(f"        .rows = {{ {rows} }},")
        out.append(f"        .nloot = {len(loot)}, .loot_kind = {{ {kinds} }}, .loot_value = {{ {vals} }},")
        out.append(f"        .nlights = {len(lv.get('lights', []))}, .lights = {{ {lights} }},")
        out.append("    },")
    out += ["};", f"const int g_level_count = {len(LEVELS)};", ""]
    path.write_text("\n".join(out))


def write_js(path):
    hx = lambda c: "#%02x%02x%02x" % c
    items = []
    for lv in LEVELS:
        o = dict(name=lv["zh"], en=lv["name"], sub=lv["sub"], time=lv["time"],
                 pal={k: hx(v) for k, v in lv["pal"].items()}, floors=lv["floors"])
        if lv.get("tutorial"):
            o["tutorial"] = True
        if lv.get("bonus"):
            o["bonus"] = True
            o["lights"] = [list(l) for l in lv["lights"]]
        else:
            o["loot"] = [list(l) for l in lv["loot"]]
        items.append("  " + json.dumps(o, ensure_ascii=False))
    path.write_text("// Generated by tools/gen_levels.py. Do not edit by hand.\n"
                    "// The same maps as main/levels.c, played strictly in order.\n"
                    "const LEVELS = [\n" + ",\n".join(items) + ",\n];\n", encoding="utf-8")


def main():
    cfg = config()
    bad, infos = False, []
    for lv in LEVELS:
        err, info = check(lv, cfg)
        for e in err:
            print(f"{lv['name']}: {e}", file=sys.stderr)
            bad = True
        infos.append((lv, info))
    if len(LEVELS) > cfg["MAX_LEVELS"]:
        print(f"{len(LEVELS)} levels > MAX_LEVELS {cfg['MAX_LEVELS']}", file=sys.stderr)
        bad = True
    if not bad:
        for e in curve(infos):
            print(e, file=sys.stderr)
            bad = True
    if bad:
        sys.exit(1)
    print(f"{'#':>2} {'level':<13} {'fl':>2} {'cols':>4} {'loot':>4} {'g/G/d/z':>8} {'haz':>3} {'time':>4} {'route':>6} {'press':>5} {'threat':>6}")
    for i, (lv, f) in enumerate(infos):
        gd = f"{f['guards']}/{f['shields']}/{f['dogs']}/{f['sleepers']}"
        print(f"{i + 1:>2} {lv['name']:<13} {f['nf']:>2} {f['w']:>4} {f['loot']:>4} {gd:>8} {f['hazards']:>3} "
              f"{lv['time']:>4} {f['route']:>5.1f}s {f['pressure']:>5.0%} {f['threat']:>6.1f}")
    if "--check" in sys.argv:
        print(f"{len(LEVELS)} levels, all checks passed (nothing written)")
        return
    write_c(HERE / "main" / "levels.c")
    write_js(HERE / "prototype" / "levels.js")
    print(f"wrote main/levels.c and prototype/levels.js: {len(LEVELS)} levels, all checks passed")


if __name__ == "__main__":
    main()
