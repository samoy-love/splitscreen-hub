"""
Редакционные подборки «лучшие couch co-op игры для Switch» и сопоставление
их названий с каталогом.

Сами списки — только перечни названий (факты), без текста статей. Как из них
и из тредов получается рейтинг — в rank_toplists.py.
"""

import re
import sqlite3

CATALOG = "catalog.db"

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
    {
        "name": "Couch Co-Op Favorites (Joy-Con)",
        "url": "https://couchcoopfavorites.com/joy-con",
        "games": [
            "Cat Quest: The Fur-tastic Trilogy", "All Hands on Deck",
            "Mai: Child of Ages", "Dunk Dunk", "Smoothcade", "Pool Party",
        ],
    },
    {
        "name": "Twinfinite",
        "url": "https://twinfinite.net/guides/"
               "best-nintendo-switch-couch-co-op-local-multiplayer-games/",
        "games": [
            "GoldenEye 007", "Advance Wars 1+2: Re-Boot Camp",
            "Disney Speedstorm", "Super Mario Bros. Wonder",
            "Snipperclips: Cut It Out, Together!", "Super Mario Party",
            "Super Smash Bros. Ultimate", "Super Mario Odyssey",
            "The Binding of Isaac: Afterbirth+", "Sega Genesis Classics",
            "Shovel Knight", "Mario + Rabbids Kingdom Battle",
            "Super Bomberman R", "Mario Kart 8 Deluxe", "Minecraft",
            "Death Squared", "Sonic Mania", "Rayman Legends",
            "EA Sports FC 24", "Fire Emblem Warriors",
            "Fire Emblem Warriors: Three Hopes", "Rocket League",
            "Skylanders Imaginators", "Metal Slug 3",
            "Hyrule Warriors: Definitive Edition", "Enter the Gungeon",
            "Wizard of Legend", "Resident Evil Revelations 2",
            "Kirby Star Allies", "Nine Parchments", "ARMS",
            "Puyo Puyo Tetris", "Lovers in a Dangerous Spacetime",
            "Wulverblade", "Scribblenauts Showdown", "The Escapists 2",
            "Donkey Kong Country: Tropical Freeze", "Overcooked 2",
            "Captain Toad: Treasure Tracker", "Mario Tennis Aces",
            "Salt and Sanctuary", "Spelunker Party!", "Broforce",
            "Guacamelee! 2", "Cuphead", "Yoshi's Crafted World",
            "Mortal Kombat 11", "Cadence of Hyrule",
            "New Super Mario Bros. U Deluxe",
            "Marvel Ultimate Alliance 3: The Black Order",
            "Pokemon: Let's Go, Pikachu!", "Crash Team Racing Nitro-Fueled",
            "Team Sonic Racing", "Trials Rising", "Super Mario Maker 2",
            "1-2-Switch", "Moon Hunters",
            "LEGO Star Wars: The Skywalker Saga", "Mercenary Kings",
            "Tetris 99", "Dragon Ball FighterZ", "The Jackbox Party Pack",
            "NBA 2K Playgrounds 2", "Super Crate Box",
            "Animal Crossing: New Horizons", "Streets of Rage 4",
            "Minecraft Dungeons", "Borderlands: The Handsome Collection",
            "Hyrule Warriors: Age of Calamity",
            "Scott Pilgrim vs. The World: The Game - Complete Edition",
            "Clubhouse Games: 51 Worldwide Classics", "Kingdom Two Crowns",
            "Persona 5 Strikers", "Mario Party Superstars", "Moving Out",
            "Nintendo Switch Sports",
        ],
    },
    {
        "name": "GamesRadar+ (two-player)",
        "url": "https://www.gamesradar.com/best-two-player-switch-games/",
        # В статье список идёт от 25-го места к 1-му — здесь развёрнут, чтобы
        # позиция означала то же, что и в остальных подборках.
        "games": [
            "Brothers: A Tale of Two Sons", "Snipperclips",
            "Super Smash Bros. Ultimate", "It Takes Two",
            "Yoshi's Crafted World", "Super Mario Bros. Wonder",
            "Mario Kart 8 Deluxe", "Trine 4: The Nightmare Prince",
            "Overcooked 2", "Knights and Bikes",
            "Diablo III: Eternal Collection", "Mario + Rabbids Kingdom Battle",
            "The Stretchers", "Animal Crossing: New Horizons",
            "Super Mario Maker 2", "Unravel Two",
            "Rayman Legends: Definitive Edition",
            "Keep Talking and Nobody Explodes", "Cuphead", "Moving Out",
            "Super Mario Party", "Death Squared",
            "Captain Toad: Treasure Tracker",
            "Marvel Ultimate Alliance 3: The Black Order",
            "Luigi's Mansion 3",
        ],
    },
    {
        "name": "GameSpew",
        "url": "https://www.gamespew.com/2024/12/"
               "best-couch-co-op-games-on-nintendo-switch/",
        "games": [
            "Super Mario Bros. Wonder", "Super Crazy Rhythm Castle",
            "It Takes Two", "Disney Illusion Island",
            "Super Mario 3D World + Bowser's Fury", "Streets of Rage 4",
            "LEGO City Undercover", "Moving Out 2", "Crossy Road Castle",
            "LEGO Horizon Adventures", "Overcooked! 2", "Luigi's Mansion 3",
            "Minecraft Dungeons", "Knights and Bikes",
            "Diablo III: Eternal Collection", "Cadence of Hyrule",
            "Rogue Heroes: Ruins of Tasos",
            "Donkey Kong Country: Tropical Freeze", "Manic Mechanics",
            "Contra: Operation Galuga",
        ],
    },
    {
        "name": "iMore",
        "url": "https://www.imore.com/"
               "best-nintendo-switch-games-split-screen-or-couch-co-op",
        "games": [
            "Super Smash Bros. Ultimate", "Luigi's Mansion 3",
            "Overcooked! All You Can Eat", "Mario Party Superstars",
            "Mario Kart 8 Deluxe", "New Super Mario Bros. U Deluxe",
            "Clubhouse Games: 51 Worldwide Classics",
            "Capcom Beat 'Em Up Bundle",
            "Resident Evil Revelations Collection", "ARMS",
            "Mario Tennis Aces", "Puyo Puyo Tetris",
            "Mario + Rabbids Kingdom Battle",
            "Snipperclips: Cut it out, together!", "LEGO DC Super-Villains",
            "LEGO Marvel Super Heroes 2", "LEGO City Undercover",
            "Dragon Ball Xenoverse 2", "Pokken Tournament DX",
            "Rayman Legends: Definitive Edition",
            "Diablo III: Eternal Collection",
            "Super Mario 3D World + Bowser's Fury",
            "Hyrule Warriors: Age of Calamity", "The Jackbox Party Pack 8",
            "Moving Out", "Mario & Sonic at the Olympic Games Tokyo 2020",
            "Super Mario Maker 2", "Mario Golf: Super Rush",
        ],
    },
    {
        "name": "Games Genie (split-screen)",
        "url": "https://www.games-genie.com/articles/"
               "best-split-screen-co-op-nintendo-switch-games",
        "games": [
            "It Takes Two", "Unravel Two",
            "LEGO Star Wars: The Skywalker Saga", "Minecraft", "Rocket League",
            "Portal Knights", "Borderlands: The Handsome Collection",
            "LEGO Marvel Super Heroes", "Hyrule Warriors: Age of Calamity",
            "Resident Evil 5",
        ],
    },
    {
        "name": "Games Genie (co-op)",
        "url": "https://www.games-genie.com/articles/"
               "best-nintendo-switch-co-op-games",
        "games": [
            "It Takes Two", "Super Mario Bros. Wonder", "Monster Hunter Rise",
            "Stardew Valley", "Kirby and the Forgotten Land",
            "Pikmin 3 Deluxe", "Diablo III: Eternal Collection",
            "Teenage Mutant Ninja Turtles: Shredder's Revenge",
            "Portal: Companion Collection",
            "Snipperclips Plus: Cut it out, together!", "Luigi's Mansion 3",
            "Overcooked! All You Can Eat", "Minecraft",
            "Captain Toad: Treasure Tracker", "Untitled Goose Game",
        ],
    },
    {
        "name": "Games Genie (couples)",
        "url": "https://www.games-genie.com/articles/"
               "best-switch-games-for-couples",
        "games": [
            "Stardew Valley", "It Takes Two",
            "Snipperclips Plus: Cut it out, together!",
            "Super Mario 3D World + Bowser's Fury", "Super Mario Bros. Wonder",
            "Animal Crossing: New Horizons", "Split Fiction", "Unravel Two",
            "Kirby's Return to Dream Land Deluxe",
            "Overcooked! All You Can Eat", "Mario Kart 8 Deluxe",
            "Clubhouse Games: 51 Worldwide Classics",
            "Kirby and the Forgotten Land", "Untitled Goose Game",
            "Lovers in a Dangerous Spacetime",
        ],
    },
    {
        "name": "Eneba (two-player)",
        "url": "https://www.eneba.com/hub/games/best-two-player-switch-games/",
        "games": [
            "Minecraft", "Mortal Kombat 11", "It Takes Two",
            "Streets of Rage 4", "Mario Kart 8 Deluxe",
            "Overcooked! All You Can Eat", "Cuphead", "Stardew Valley",
            "ARMS", "Rayman Legends",
        ],
    },
]


ARTICLES = re.compile(r"^(the|a|an)\s+", re.I)


def norm(title):
    t = title.lower().replace("™", "").replace("®", "")
    t = t.replace("&", "and").replace("’", "'")
    t = ARTICLES.sub("", t)
    return re.sub(r"[^a-z0-9]", "", t)


def load_catalog():
    """Два индекса по каталогу: точное нормализованное название и его первые
    14 знаков — «Overcooked! 2» в подборке против «Overcooked 2: ...» в eShop."""
    db = sqlite3.connect(CATALOG)
    rows = db.execute("SELECT nsuid, title FROM games").fetchall()
    db.close()

    exact, prefix = {}, {}
    for nsuid, title in rows:
        n = norm(title)
        exact.setdefault(n, (nsuid, title))
        prefix.setdefault(n[:14], (nsuid, title))
    return exact, prefix
