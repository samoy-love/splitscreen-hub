"""
Сводит редакционные подборки и обсуждения в один рейтинг «согласия».

Почему не счётчик упоминаний. Простое «в скольких списках названа» считает
голоса, которые стоят разного. Список из 75 игр называет всё подряд, и попасть
в него почти ничего не значит; список из 10 отбирает жёстко. Четыре статьи
одного сайта — это одно мнение, а не четыре. Упоминание в треде на 945
комментариев дешевле, чем в треде на 64. И главное: игра, названная десять раз
в одном канале, вызывает меньше доверия, чем игра, названную и редакциями,
и людьми в обсуждениях, — а сумма этого не различает.

Здесь считается иначе:

  1. Вес источника — избирательность. w = ln(P / n), где n — сколько игр
     источник назвал, а P — размер общего пула кандидатов (все игры, названные
     хоть где-то). Попасть в список из 10 весит вдвое больше, чем в список из 75.

  2. Позиция — мягкий множитель 0.7…1.0 и только для тех списков, что
     действительно ранжированы. Строить на позиции больше нельзя: часть изданий
     не нумерует вовсе.

  3. Семейство — 1/sqrt(k) на каждый источник семейства. Четыре списка Games
     Genie вместе весят как два независимых, а не как четыре.

  4. Свежесть — период полураспада 8 лет. Список 2019 года не выбрасывается,
     но весит меньше свежего.

  5. Два канала — редакции и обсуждения — считаются раздельно и сводятся
     средним геометрическим. Это и есть замена счётчику: высоко поднимается то,
     что подтверждено с двух сторон, а не то, что часто повторили с одной.

  6. Порог доверия — игры, названные меньше чем в двух независимых семействах,
     уходят в хвост и не смешиваются с верхом списка.

Чего эта схема не чинит: известность сама по себе. Mario Kart называют везде
не потому, что это лучший кооп, а потому что это Mario Kart. Отделить славу от
пригодности имеющимися данными нечем — нужен корпус общей упоминаемости игр,
которого у нас нет.
"""

import math
import os
import re
import sqlite3
import sys

CATALOG = "catalog.db"
OUT = "toplists.db"

# --- метаданные источников -----------------------------------------------
#
# family — издатель источника; несколько списков одного издателя не считаются
# независимыми мнениями. ordered — нумерует ли издание свой список.
SOURCE_META = {
    "Nintendo Life":                   ("nintendolife", 2026, False),
    "Classic Co-op":                   ("classiccoop", 2026, True),
    "Pocket Gamer":                    ("pocketgamer", 2025, False),
    "Eneba":                           ("eneba", 2026, True),
    "Eneba (two-player)":              ("eneba", 2026, True),
    "Game Rant":                       ("gamerant", 2025, False),
    "Games Genie (local co-op)":       ("gamesgenie", 2026, False),
    "Games Genie (couch co-op)":       ("gamesgenie", 2026, False),
    "Games Genie (split-screen)":      ("gamesgenie", 2026, False),
    "Games Genie (co-op)":             ("gamesgenie", 2026, False),
    "Games Genie (couples)":           ("gamesgenie", 2026, False),
    "Explosion":                       ("explosion", 2026, False),
    "Her Cozy Gaming":                 ("hercozygaming", 2025, False),
    "Couch Co-Op Favorites":           ("couchcoopfav", 2026, False),
    "Couch Co-Op Favorites (Joy-Con)": ("couchcoopfav", 2026, False),
    "Twinfinite":                      ("twinfinite", 2024, True),
    "GamesRadar+ (two-player)":        ("gamesradar", 2025, True),
    "GameSpew":                        ("gamespew", 2024, False),
    "iMore":                           ("imore", 2022, False),
}

NOW = 2026
HALF_LIFE = 8.0

# Насколько дорого обходится молчание одного из каналов.
#
# Каналы сводятся средним геометрическим sqrt((E+S)*(C+S)). При S=1 нулевой
# канал почти обнуляет счёт — и тогда Boomerang Fu, Ultimate Chicken Horse и
# прочие находки Reddit проваливаются в самый низ, хотя именно ради них треды и
# читались. Это неверно по существу: редакционные списки перекошены в сторону
# крупных издателей, и их молчание об инди — не довод против игры, а свойство
# самих списков. S=10 оставляет перекос односторонних игр заметным, но не
# смертельным: молчание канала стоит примерно как слабое в нём присутствие.
SMOOTH = 10.0

# --- обсуждения ------------------------------------------------------------
#
# Порядок ключей совпадает с порядком в строках матрицы: индекс в строке — это
# номер ключа здесь. Матрица снята с Reddit: для каждого треда сколько
# отдельных комментариев упомянули игру.
KEYS = ("overcooked;overcooked2;overcookedallyoucaneat;mariokart8;stardewvalley;"
        "supermarioparty;mariopartysuperstars;supermariopartyjamboree;diablo3;"
        "diabloiii;snipperclips;boomerangfu;ittakestwo;castlecrashers;"
        "ultimatechickenhorse;towerfall;loversinadangerousspacetime;animalcrossing;"
        "supermario3dworld;minecraft;minecraftdungeons;supermariobroswonder;"
        "mariowonder;cuphead;untitledgoosegame;movingout;movingout2;streetsofrage4;"
        "nineparchments;unraveltwo;unravel2;deathsquared;childrenofmorta;heaveho;"
        "raymanlegends;broforce;supermarioodyssey;captaintoad;catquest2;catquestii;"
        "catquest3;catquestiii;unrailed;killerqueenblack;supersmashbros;pikmin3;"
        "pikmin4;borderlands;pode;crawl;hyrulewarriors;yoshiscraftedworld;"
        "rocketleague;marvelultimatealliance3;kirbystarallies;enterthegungeon;"
        "deathroadtocanada;superchariot;trickytowers;kirbyandtheforgottenland;"
        "runbow;superbombermanr;splitfiction;toolsup;humanfallflat;keywe;"
        "fullmetalfuries;portalknights;portal2;legostarwars;teenagemutantninjaturtles;"
        "shreddersrevenge;picopark;duckgame;brotato;dysmantle;luigismansion3;"
        "degreesofseparation;lostcastle;contraoperationgaluga;astrobears;"
        "mariotennisaces;newsupermariobrosu;mariorabbids;fortheking;lightfingers;"
        "boxboy;brothersataleoftwosons;biped;trine;plateup;vampiresurvivors;"
        "mortalkombat11;dontstarvetogether;warioware;aegisdefenders;astralchain;"
        "knightsandbikes;gangbeasts;justshapesandbeats;kingdomtwocrowns;"
        "clubhousegames;51worldwideclassics;puyopuyotetris;veryveryvalet;spelunky;"
        "spelunky2;donkeykongcountrytropicalfreeze;starlink;splatoon;fastrmx;"
        "nintendoswitchsports;jackbox;spiderheck;cultofthelamb;knightsquad2;"
        "teamsonicracing;smoothcade;wormswmd;arms;hiddeninplainsight;speedrunners;"
        "nidhogg2;huntdown;ibbandobb;blanc;splinteredfate;goatsimulator3;"
        "trashsailors;saltandsanctuary;supermariomaker2;rogueheroes;rivercitygirls;"
        "doubledragongaiden;snowbroswonderland;pikuniku;popucom;stormlancers;"
        "parttimeufo;thestretchers;goodjob;fightcrab;residentevil5;nintendoland;"
        "overcooked1").split(";")

# Ключи, которые в каталоге называются иначе. Пустое значение — игра, которую
# в тредах называют, а в каталоге её нет (не на Switch или нет игры на одном
# экране), либо название неоднозначно: «Trine» и «Splatoon» без номера могут
# значить разные игры, и приписывать голос одной из них нечестно.
ALIASES = {
    "overcooked": "Overcooked! All You Can Eat",
    "overcooked1": "Overcooked! All You Can Eat",
    "mariokart8": "Mario Kart 8 Deluxe",
    "diablo3": "Diablo III: Eternal Collection",
    "diabloiii": "Diablo III: Eternal Collection",
    "snipperclips": "Snipperclips – Cut it out, together!",
    "mariowonder": "Super Mario Bros. Wonder",
    "unravel2": "Unravel Two",
    "raymanlegends": "Rayman Legends Definitive Edition",
    "captaintoad": "Captain Toad: Treasure Tracker",
    "catquest2": "Cat Quest II",
    "catquest3": "Cat Quest III",
    "pikmin3": "Pikmin 3 Deluxe",
    "borderlands": "Borderlands: The Handsome Collection",
    "legostarwars": "LEGO Star Wars: The Skywalker Saga",
    "shreddersrevenge": "Teenage Mutant Ninja Turtles: Shredder's Revenge",
    "mariorabbids": "Mario + Rabbids Kingdom Battle",
    "boxboy": "BOXBOY! + BOXGIRL!",
    "warioware": "WarioWare: Get It Together!",
    "51worldwideclassics": "Clubhouse Games: 51 Worldwide Classics",
    "starlink": "Starlink: Battle for Atlas Digital Edition",
    "jackbox": "The Jackbox Party Pack",
    "splinteredfate": "Teenage Mutant Ninja Turtles: Splintered Fate",
    "rogueheroes": "Rogue Heroes: Ruins of Tasos",
    "thestretchers": "The Stretchers",
    "untitledgoosegame": "", "killerqueenblack": "", "crawl": "",
    "splitfiction": "", "trine": "", "splatoon": "", "nintendoland": "",
}

# Ключ-подстрока ловит и более длинное название: «Overcooked 2» содержит
# «overcooked». Вычитаем, иначе у общего ключа накапливается чужой счёт.
CONTAINED_IN = {
    "overcooked": ["overcooked2", "overcookedallyoucaneat", "overcooked1"],
    "supermarioparty": ["mariopartysuperstars", "supermariopartyjamboree"],
    "minecraft": ["minecraftdungeons"],
    "movingout": ["movingout2"],
    "spelunky": ["spelunky2"],
    "catquest2": [], "catquest3": [],
    "unravel2": [], "pikmin3": [],
}

# --- отрицательные упоминания ---------------------------------------------
#
# Упоминание игры — не всегда голос за неё. В двух тредах автор прямо
# перечисляет, что пробовал и что не понравилось, а счётчик засчитывал такие
# названия наравне с советами. Списки выверены глазами по тексту постов.
#
# Вклад такого треда не обнуляется, а меняет знак с коэффициентом NEG_VOTE:
# это по-прежнему сигнал — игру знают и обсуждают, — но сигнал против.
NEG_VOTE = -0.5

DISLIKED = {
    # «Games we tried but didn't like» в посте с обзором игр для девушки.
    "pk8c45": ["Overcooked! 2", "Nine Parchments", "Death Squared",
               "Degrees of Separation", "Cuphead"],
    # Вступительный пост: «мы пробовали, и всё это не зашло».
    "r30tjx": ["Super Mario Odyssey", "Luigi's Mansion 3", "Super Chariot"],
}

# Доля упоминаний, рядом с которыми стоит явно отрицательная оценка, — по
# всему корпусу тредов. Считано регуляркой в окне ±90 знаков от названия,
# поэтому это оценка сверху и применяется мягко: множитель 1 − 0.5·доля.
# Игры с одним отрицательным упоминанием в список не берутся — это шум.
NEG_SHARE = {
    "rogueheroes": 2 / 7,
    "unraveltwo": 3 / 16,
    "overcooked2": 4 / 42,
    "overcooked": 14 / 225,
    "loversinadangerousspacetime": 2 / 46,
    "movingout": 2 / 54,
    "towerfall": 2 / 61,
    "minecraft": 2 / 63,
    "ittakestwo": 2 / 117,
}

# id | комментариев | год | площадка | строка матрицы (base36)
THREADS = [
    ("r30tjx", 945, 2021, "reddit", "d9:00z01402203104d06308e0a80b10cz0dh0e10f30g80h30i90j70k40nc0o20p50r60s70t20uf0v40w90x30y50z11011281351511631711811961bc1c21d31e41f21h21i11j91k41l21o11r21s21t21u31v22012122412532622a32e22g12h82m42n12o12p22q12r12s32t32u12v12w12x22y12z23033g43m13n23o73r23w63y2405"),
    ("1fs54lx", 933, 2024, "reddit", "dl:00z01b02303e04i05706e0720830910aa0bn0c90d40ea0f20g40hu0ig0ji0k40l50mz0n50oc0p70q10r10s20u20v20x80y30z110711612214216218e1911a21b11e31f51g11h21i51j11m11n91o11s11w11x61y31zb20424a2812922a22b22c22h12j22m52o22q32r12t32w12z331d3223383423513633b23c13i23m13n13o13p13r13u13v13w1404"),
    ("pk8c45", 895, 2021, "reddit", "cy:00z01903304i0510880910aa0b40ch0d30e10f10g70h90i50j60k40n60o50pd0r10s30t50u20v10w20x60y40z31011191221631811971b41c41d11e21f11g21i21l51m11o21p11r31s41t21u11v11w22012112432512822912a22b72c12d12e12f12g12h22l12m82p12s12v12w22x32y13a13b13g13n13r13v23w73y2"),
    ("dlx6od", 474, 2019, "reddit", "cr:00d01503204708b0920a40d90eb0ff0g20j20n10s70u40v50w10x70y30z91211311741811b11c51d41f11g51h21i31j51k31l21m41o31s41u31v12022152412512612822932a22b12c22d32e42f22h32l12n22o12q22r22s12v23163213463b13l23m2404"),
    ("1t1oaf4", 302, 2026, "reddit", "81:0090c40g20j30p50q10s40v20x91211411631c11d11q41r21t11w42c22f12g22h62i92r12w32x22y23423513713923a23b23y2"),
    ("mw3308", 257, 2021, "reddit", "72:00d0150210320440520820a60ba0d40ea0fj0g70h20i60j60k10n30o30p50r10s30t10u10v30x50y20z21121311621731831941b31c31d41e21f11g21h31i31j11k31m31o21p11r11u21v11x120121b2412512822922a12b32e22g12h32p12t22v33153453723a23b33c43d23e13f13m13n13o13r13u1401"),
    ("1saiwuy", 247, 2026, "reddit", "6m:00c0110210510710810a10bi0c20d80eg0fe0g50i20j20k10m30o20p30r60s20v10x90z41641721821b31d61g11h21m31o21q21r11u11w11y11z72062142212712832d12h12i32j12m12q32r12v13463543713c13d23g13n13p13s13w1401"),
    ("10ubuj9", 160, 2023, "reddit", "4e:00b0160490610850a50b50ci0d40e10f20g30h20i90j40k30n20o20p30r40t10u50v10w10x10y40z11141211981b11f51g21k11n51o11w21y11z22432a32c12h12l12m12s12x13013a13d13h13o13u13v1"),
    ("1tnmj0b", 131, 2026, "reddit", "3h:00b0470610880a40b40c20d30e10f20g10i10j10k10m30n10o50p20q10r20t10v20w20x10z21121231411911d21g11h11j21q31s11t11w21y31z42012222322412e12g12h22i72j32l12r12s12t12w13623b13h13i33j23o23r13w1"),
    ("vlmn9k", 96, 2022, "reddit", "2o:0010410810a10j50k40n10s10t10w51011231311921b11e11h11y11z32412b12h12k13n13o1"),
    ("1cn3xna", 89, 2024, "reddit", "2h:00b0220310430910a10c80d40e10f10g50j30k10n20o20p60q10r20s10t10u10v10w40y20z21011241b41c11d11g11j11k11p21r31s21t21v21w11x21y11z12012342412512612c22f12g22h42i22j22k22l22n12p12q22s22v12x12y13613813b13e13g13k13l13y1"),
    ("1i6fzbw", 79, 2025, "reddit", "2b:0050440810a40c30d10f20g10i10j40k40m50n10o10p20r30s10u10w20y20z21411911b31e11i11j11l11n21s11v11x11y11z32112222312h12j22k12o12p12z13013613913f13g13i23k13o13w13y1"),
    ("1qxmpgt", 71, 2026, "reddit", "20:0070210460810910b10c70d40g10j10k10m10n10o10p30r10s10t30u10v10w20z11221411621911a11b31c11h11q71r11s11t11w11z22232332512612a12c22h42i12l22s33643j23l13y3"),
    ("1jgcvdq", 65, 2025, "reddit", "1s:0020850c40j10o10s30w11q52312c12h12i12j13613b1"),
    ("1b53ths", 64, 2024, "reddit", "1t:0030110220310420c80e10h10i10m40o30s10t10u10v10w10y11221421911d11e11q31t11x12312412612e12h12i22l23113413g13h1"),
]

# Famiboards: тред целиком — это список названных игр, отдельных счётчиков нет,
# треды маленькие. Считаем каждое название одним голосом.
FAMIBOARDS = [
    ("17092", 37, 2026, [
        "Snipperclips", "Untitled Goose Game", "Part Time UFO", "The Stretchers",
        "Good Job!", "Shovel Knight", "Scott Pilgrim vs. The World: The Game",
        "Guacamelee!", "Streets of Rage 4", "Huntdown", "Escape Academy",
        "Fight Crab", "Portal 2", "Cat Quest III", "Resident Evil 5",
        "Minecraft Dungeons", "POPUCOM", "Storm Lancers", "Blanc",
        "BOXBOY! + BOXGIRL!", "River City Girls", "Pikuniku", "Pode",
        "Rogue Heroes: Ruins of Tasos", "Diablo III: Eternal Collection",
        "Overcooked", "Captain Toad: Treasure Tracker", "Contra: Operation Galuga",
        "Nine Parchments", "LEGO Voyagers",
        "Marvel Ultimate Alliance 3: The Black Order", "Snow Bros. Wonderland",
        "Teenage Mutant Ninja Turtles: Shredder's Revenge",
    ]),
    ("3407", 26, 2022, [
        "Nintendo Switch Sports", "Super Smash Bros. Ultimate", "Super Mario Party",
        "Mario Party Superstars", "Cat Quest II",
        "Marvel Ultimate Alliance 3: The Black Order", "Killer Queen Black",
        "Teenage Mutant Ninja Turtles: Shredder's Revenge",
        "Mario Strikers: Battle League", "Luigi's Mansion 3", "Minecraft",
        "Rocket League", "Super Monkey Ball Banana Mania", "Overcooked! 2",
        "Diablo III: Eternal Collection",
    ]),
    ("14351", 16, 2025, [
        "Survival Kids", "Cat Quest III",
        "Teenage Mutant Ninja Turtles: Shredder's Revenge", "Double Dragon Neon",
        "River City Girls 2", "Pikmin 4", "Luigi's Mansion 3", "Pizza Possum",
        "Streets of Rage 4", "Monster Hunter Rise", "Huntdown",
        "Gotta Protectors: Cart of Darkness", "Split Fiction", "It Takes Two",
        "Sonic Mania", "Guacamelee!", "Double Dragon Gaiden: Rise of the Dragons",
        "The Ninja Saviors: Return of the Warriors",
        "Gal Guardians: Servants of the Dark",
    ]),
]

ARTICLES = re.compile(r"^(the|a|an)\s+", re.I)


def norm(title):
    t = title.lower().replace("™", "").replace("®", "")
    t = t.replace("&", "and").replace("’", "'")
    t = ARTICLES.sub("", t)
    return re.sub(r"[^a-z0-9]", "", t)


def load_catalog():
    db = sqlite3.connect(CATALOG)
    rows = db.execute("SELECT nsuid, title FROM games").fetchall()
    db.close()
    exact, prefix = {}, {}
    for nsuid, title in rows:
        n = norm(title)
        exact.setdefault(n, (nsuid, title))
        prefix.setdefault(n[:14], (nsuid, title))
    return exact, prefix


def resolve(title, exact, prefix):
    if title in ALIASES:
        alias = ALIASES[title]
        if not alias:
            return None
        title = alias
    n = norm(title)
    return exact.get(n) or (prefix.get(n[:14]) if len(n) >= 14 else None)


def decode_row(row):
    """base36-строка «N:iiCiiC…» -> (число комментариев, {индекс ключа: счёт})."""
    head, _, body = row.partition(":")
    total = int(head, 36)
    counts = {}
    for i in range(0, len(body), 3):
        idx = int(body[i:i + 2], 36)
        counts[idx] = int(body[i + 2], 36)
    return total, counts


def recency(year):
    return 0.5 ** ((NOW - year) / HALF_LIFE)


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    exact, prefix = load_catalog()

    # --- пул кандидатов: всё, что названо хоть где-то ----------------------
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    src = open("build_toplists.py", encoding="utf-8").read()
    ns = {}
    exec(src[src.index("SOURCES = ["):src.index("SCHEMA = ")], ns)
    SOURCES = ns["SOURCES"]

    pool = set()
    for s in SOURCES:
        for t in s["games"]:
            hit = resolve(t, exact, prefix)
            if hit:
                pool.add(hit[0])
    for t in THREADS:
        _, counts = decode_row(t[4])
        for idx in counts:
            hit = resolve(KEYS[idx], exact, prefix)
            if hit:
                pool.add(hit[0])
    for _, _, _, games in FAMIBOARDS:
        for g in games:
            hit = resolve(g, exact, prefix)
            if hit:
                pool.add(hit[0])
    P = max(len(pool), 50)

    family_size = {}
    for name in SOURCES:
        fam = SOURCE_META[name["name"]][0]
        family_size[fam] = family_size.get(fam, 0) + 1

    titles = {}
    editorial, community = {}, {}
    families = {}

    # --- канал 1: редакционные подборки ------------------------------------
    for s in SOURCES:
        fam, year, ordered = SOURCE_META[s["name"]]
        hits = [(pos, resolve(t, exact, prefix))
                for pos, t in enumerate(s["games"], 1)]
        hits = [(pos, h) for pos, h in hits if h]
        n = max(len(hits), 1)
        w = math.log(P / n) if n < P else 0.1
        w = max(w, 0.1)
        w *= 1.0 / math.sqrt(family_size[fam])
        w *= recency(year)
        for pos, (nsuid, title) in hits:
            p = (0.7 + 0.3 * (1 - (pos - 1) / n)) if ordered else 1.0
            editorial[nsuid] = editorial.get(nsuid, 0.0) + w * p
            titles[nsuid] = title
            families.setdefault(nsuid, set()).add(fam)

    # --- канал 2: обсуждения ------------------------------------------------
    def add_thread(fam, year, named, n_games, disliked=()):
        w = math.log(P / max(n_games, 1)) if n_games < P else 0.1
        w = max(w, 0.1) * recency(year)
        top = max(named.values()) if named else 1
        for nsuid, d in named.items():
            rel = math.log1p(d) / math.log1p(top)
            sign = NEG_VOTE if nsuid in disliked else 1.0
            community[nsuid] = community.get(nsuid, 0.0) + w * rel * sign
            families.setdefault(nsuid, set()).add(fam)

    for tid, _, year, plat, row in THREADS:
        _, counts = decode_row(row)
        by_key = {KEYS[i]: c for i, c in counts.items()}
        for base, longer in CONTAINED_IN.items():
            if base in by_key:
                by_key[base] = max(0, by_key[base] - sum(by_key.get(l, 0)
                                                         for l in longer))
        named = {}
        for key, d in by_key.items():
            if d <= 0:
                continue
            hit = resolve(key, exact, prefix)
            if not hit:
                continue
            # доля упоминаний рядом с отрицательной оценкой — снимаем её часть
            d *= 1 - 0.5 * NEG_SHARE.get(key, 0.0)
            named[hit[0]] = named.get(hit[0], 0) + d
            titles[hit[0]] = hit[1]
        bad = set()
        for t in DISLIKED.get(tid, []):
            hit = resolve(t, exact, prefix)
            if hit:
                bad.add(hit[0])
        add_thread("reddit:" + tid, year, named, len(named), bad)

    for tid, _, year, games in FAMIBOARDS:
        named = {}
        for g in games:
            hit = resolve(g, exact, prefix)
            if hit:
                named[hit[0]] = 1
                titles[hit[0]] = hit[1]
        add_thread("fami:" + tid, year, named, len(named))

    # --- сведение каналов ---------------------------------------------------
    #
    # Среднее геометрическое, а не сумма: игра, которую хвалят и редакции, и
    # люди в тредах, обгоняет ту, что набрала столько же в одном канале.
    emax = max(editorial.values()) if editorial else 1.0
    cmax = max(community.values()) if community else 1.0
    rows = []
    for nsuid in set(editorial) | set(community):
        e = 100 * editorial.get(nsuid, 0.0) / emax
        c = 100 * community.get(nsuid, 0.0) / cmax
        score = math.sqrt((e + SMOOTH) * (c + SMOOTH)) - SMOOTH
        rows.append((score, e, c, len(families[nsuid]), titles[nsuid], nsuid))

    rows.sort(key=lambda r: -r[0])
    trusted = [r for r in rows if r[3] >= 2]
    tail = [r for r in rows if r[3] < 2]

    print(f"пул кандидатов: {P} игр")
    print(f"источников: {len(SOURCES)} подборок, {len(THREADS)} тредов Reddit, "
          f"{len(FAMIBOARDS)} тредов Famiboards")
    print(f"в рейтинге: {len(rows)} игр, из них с подтверждением "
          f"от 2+ независимых источников: {len(trusted)}\n")
    print(f"{'место':>5}  {'счёт':>6}  {'ред.':>5}  {'общ.':>5}  {'ист.':>4}  игра")
    for i, (score, e, c, fam, title, _) in enumerate(trusted[:40], 1):
        print(f"{i:>5}  {score:>6.1f}  {e:>5.1f}  {c:>5.1f}  {fam:>4}  {title[:46]}")

    print(f"\nхвост (один источник): {len(tail)} игр")
    for score, e, c, fam, title, _ in tail[:12]:
        print(f"       {score:>6.1f}  {e:>5.1f}  {c:>5.1f}  {fam:>4}  {title[:46]}")

    if "--write" in sys.argv:
        db = sqlite3.connect(OUT)
        db.execute("DROP TABLE IF EXISTS ranking")
        db.execute("CREATE TABLE ranking (nsuid TEXT PRIMARY KEY, score REAL, "
                   "editorial REAL, community REAL, families INTEGER)")
        for score, e, c, fam, _, nsuid in rows:
            db.execute("INSERT INTO ranking VALUES (?,?,?,?,?)",
                       (nsuid, score, e, c, fam))
        db.commit()
        db.close()
        print(f"\nзаписано в {OUT}: таблица ranking, {len(rows)} строк")


if __name__ == "__main__":
    main()
