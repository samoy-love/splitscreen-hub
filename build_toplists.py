"""
Сводит подборки «лучшие couch co-op игры для Switch» из разных изданий в один
рейтинг и пишет его в toplists.db.

Идея простая: чем в большем числе независимых подборок игра названа, тем больше
согласия вокруг неё. Это заметно устойчивее одной редакционной оценки и не
требует ничьего API.

Позиция внутри списка учитывается только как разрешение ничьих: списки разной
длины и разной степени упорядоченности (часть изданий нумерует, часть — нет),
поэтому строить на позиции основной вес было бы нечестно.

Reddit и YouTube в подборку не вошли: reddit.com целиком закрыт для загрузки
(и JSON, и поиск), а страницы YouTube не отдают описания и тайм-коды — в обоих
случаях брать было нечего.
"""

import json
import os
import sys
import re
import sqlite3

CATALOG = "catalog.db"
OUT = "toplists.db"

# Заголовки взяты как факты — перечень названий, без текста статей.
SOURCES = [
    {
        "name": "Nintendo Life",
        "url": "https://www.nintendolife.com/guides/best-nintendo-switch-couch-co-op-games",
        "games": [
            "Death Squared", "Rocket League", "Captain Toad: Treasure Tracker",
            "Lovers in a Dangerous Spacetime", "Enter the Gungeon", "Pode",
            "Minecraft", "Snipperclips Plus: Cut it out, together!",
            "Rayman Legends: Definitive Edition", "Cuphead", "TowerFall",
            "Super Mario 3D World + Bowser's Fury",
            "Marvel Ultimate Alliance 3: The Black Order", "Unravel Two",
            "Diablo III: Eternal Collection", "BOXBOY! + BOXGIRL!",
            "Mario Tennis Aces", "The Binding of Isaac: Repentance",
            "Pokemon: Let's Go, Pikachu!", "Heave Ho",
            "Mario + Rabbids Kingdom Battle", "Capcom Beat 'Em Up Bundle",
            "Brothers: A Tale of Two Sons", "Super Mario Party",
            "Killer Queen Black", "The Stretchers", "Monaco: Complete Edition",
            "ibb & obb", "Good Job!", "Luigi's Mansion 3", "Knights and Bikes",
            "Streets of Rage 4", "Phogs!", "Overcooked! All You Can Eat",
            "WarioWare: Get It Together!",
            "LEGO Star Wars: The Skywalker Saga", "Part Time UFO", "ARMS",
            "Trine 5: A Clockwork Conspiracy",
            "Teenage Mutant Ninja Turtles: Shredder's Revenge",
            "Kirby and the Forgotten Land", "Portal: Companion Collection",
            "It Takes Two", "Full Metal Furies", "Vampire Survivors",
            "Moving Out 2", "Disney Illusion Island", "Super Mario Bros. Wonder",
        ],
    },
    {
        "name": "Classic Co-op",
        "url": "https://www.classicco-op.com/coop-spotlight/2026/3/1/"
               "the-20-best-couch-co-op-games-on-nintendo-switch-all-time-ranking",
        "games": [
            "It Takes Two", "Portal 2", "Streets of Rage 4",
            "Super Mario Bros. Wonder",
            "Teenage Mutant Ninja Turtles: Shredder's Revenge",
            "Pikmin 3 Deluxe", "Blazing Chrome", "Sea of Stars",
            "Captain Toad: Treasure Tracker", "Rayman Legends",
            "Kirby's Return to Dream Land Deluxe",
            "Super Mario 3D World + Bowser's Fury", "Cuphead",
            "Children of Morta", "Unravel Two", "Overcooked",
            "Hyrule Warriors: Age of Calamity",
            "Lovers in a Dangerous Spacetime", "Diablo III: Eternal Collection",
            "Castle Crashers Remastered",
        ],
    },
    {
        "name": "Pocket Gamer",
        "url": "https://www.pocketgamer.com/switch/best-local-multiplayer-games-switch/",
        "games": [
            "Lunch A Palooza", "Garfield Kart 2: All You Can Drift",
            "Overcooked! 2", "Tools Up!", "Animal Crossing: New Horizons",
            "Stardew Valley", "Snipperclips", "Moving Out 2", "Among Us",
            "Super Mario Party", "Marvel Ultimate Alliance 3: The Black Order",
            "Rayman Legends: Definitive Edition", "Mario Kart 8 Deluxe", "ARMS",
            "Super Smash Bros. Ultimate", "Lovers in a Dangerous Spacetime",
            "Rocket League", "Don't Starve Together", "Armello", "Splatoon 2",
            "Fortnite", "Terraria", "Diablo III: Eternal Collection",
            "Teenage Mutant Ninja Turtles: Shredder's Revenge", "It Takes Two",
            "Cuphead",
        ],
    },
    {
        "name": "Eneba",
        "url": "https://www.eneba.com/hub/games/best-couch-co-op-games-switch/",
        "games": [
            "It Takes Two", "Super Mario Bros. Wonder",
            "Overcooked! All You Can Eat", "Mario Kart 8 Deluxe",
            "Super Smash Bros. Ultimate", "Luigi's Mansion 3", "Cuphead",
            "Donkey Kong Country Returns HD", "LEGO Horizon Adventures",
            "Disney Illusion Island", "Stardew Valley", "Untitled Goose Game",
            "Blanc", "Snipperclips Plus: Cut it out, together!",
            "Kirby and the Forgotten Land",
        ],
    },
    {
        "name": "Game Rant",
        "url": "https://gamerant.com/best-nintendo-switch-split-screen-local-couch-co-op-games/",
        "games": [
            "Diablo III: Eternal Collection", "Risk of Rain Returns",
            "Kirby Star Allies", "Resident Evil 5",
            "Hyrule Warriors: Age of Calamity", "River City Girls",
            "Kingdom Two Crowns", "Trine 5: A Clockwork Conspiracy",
            "Rayman Legends", "Pico Park",
        ],
    },
    {
        "name": "Games Genie (local co-op)",
        "url": "https://www.games-genie.com/articles/best-local-co-op-nintendo-switch-games",
        "games": [
            "Super Mario Bros. Wonder", "Super Mario 3D World + Bowser's Fury",
            "Snipperclips Plus: Cut it out, together!",
            "Teenage Mutant Ninja Turtles: Shredder's Revenge",
            "Vampire Survivors", "Overcooked! All You Can Eat", "It Takes Two",
            "Luigi's Mansion 3", "Moving Out",
            "Teenage Mutant Ninja Turtles: Splintered Fate", "Stardew Valley",
            "Pikmin 3 Deluxe", "Untitled Goose Game", "Spiritfarer",
            "Diablo III: Eternal Collection",
        ],
    },
    {
        "name": "Games Genie (couch co-op)",
        "url": "https://www.games-genie.com/articles/best-switch-couch-co-op-games",
        "games": [
            "Heave Ho", "Super Mario 3D World + Bowser's Fury",
            "Donkey Kong Country: Tropical Freeze", "Luigi's Mansion 3",
            "New Super Mario Bros. U Deluxe", "It Takes Two", "Death Squared",
            "Overcooked! All You Can Eat", "Phogs!", "Rayman Legends",
        ],
    },
    {
        "name": "Explosion",
        "url": "https://www.explosion.com/172150/best-multiplayer-switch-games-for-couch-co-op-in-2026/",
        "games": [
            "Mario Kart 8 Deluxe", "Super Smash Bros. Ultimate",
            "Overcooked! All You Can Eat",
            "Super Mario 3D World + Bowser's Fury", "It Takes Two",
            "Kirby and the Forgotten Land", "Diablo III: Eternal Collection",
            "Lovers in a Dangerous Spacetime", "Untitled Goose Game",
            "Splatoon 3",
        ],
    },
    {
        "name": "Her Cozy Gaming",
        "url": "https://hercozygaming.com/best-cozy-couch-co-op-games-on-switch/",
        "games": [
            "Phogs!", "LEGO Voyagers", "Overcooked! All You Can Eat",
            "Portal: Companion Collection", "Farm Together", "Melbits World",
            "Blanc", "Tools Up!", "Spiritfarer",
            "Kirby and the Forgotten Land", "Haven", "Harmony's Odyssey",
            "Lovers in a Dangerous Spacetime", "Heave Ho",
            "Puzzle Bobble Everybubble!", "Unspottable", "Pode",
            "Out of Words", "Hand in Hand", "Split Fiction",
        ],
    },
    {
        "name": "Couch Co-Op Favorites",
        "url": "https://couchcoopfavorites.com/switch",
        "games": [
            "Toasterball", "Cat Quest: The Fur-tastic Trilogy",
            "All Hands on Deck", "Mai: Child of Ages", "Dunk Dunk",
            "Smoothcade",
        ],
    },
]

SCHEMA = """
CREATE TABLE IF NOT EXISTS toplists (
  nsuid    TEXT PRIMARY KEY,
  mentions INTEGER NOT NULL,   -- в скольких подборках названа
  best_pos INTEGER,            -- лучшая позиция среди подборок
  sources  TEXT NOT NULL       -- издания через запятую
);
"""

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
        # «Overcooked! 2» в каталоге может называться «Overcooked 2: ...» —
        # запасной ключ по началу названия
        prefix.setdefault(n[:14], (nsuid, title))
    return exact, prefix


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    exact, prefix = load_catalog()

    found, missing = {}, []
    for source in SOURCES:
        for position, title in enumerate(source["games"], 1):
            n = norm(title)
            hit = exact.get(n) or (prefix.get(n[:14]) if len(n) >= 14 else None)
            if not hit:
                missing.append((source["name"], title))
                continue
            nsuid, catalog_title = hit
            entry = found.setdefault(
                nsuid, {"title": catalog_title, "sources": [], "positions": []})
            if source["name"] not in entry["sources"]:
                entry["sources"].append(source["name"])
                entry["positions"].append(position)

    if os.path.exists(OUT):
        os.remove(OUT)
    db = sqlite3.connect(OUT)
    db.executescript(SCHEMA)
    for nsuid, e in found.items():
        db.execute("INSERT INTO toplists VALUES (?,?,?,?)",
                   (nsuid, len(e["sources"]), min(e["positions"]),
                    ", ".join(e["sources"])))
    db.commit()

    total = sum(len(s["games"]) for s in SOURCES)
    print(f"подборок: {len(SOURCES)}, упоминаний всего: {total}")
    print(f"сопоставлено с каталогом: {len(found)} игр")
    print(f"не нашлось в каталоге: {len(missing)}")
    print()

    ranked = sorted(found.items(), key=lambda kv: (-kv[1]["mentions"] if "mentions" in kv[1]
                                                   else -len(kv[1]["sources"]),
                                                   min(kv[1]["positions"])))
    print("Топ по числу подборок:")
    for nsuid, e in ranked[:25]:
        print(f"  {len(e["sources"])}x  {e['title'][:44]}")

    if missing:
        print("\nНе нашлось в каталоге (не на Switch, другое название или")
        print("нет мультиплеера на одном экране):")
        for src, title in missing[:40]:
            print(f"  {title[:44]:44} — {src}")

    db.close()


if __name__ == "__main__":
    main()
