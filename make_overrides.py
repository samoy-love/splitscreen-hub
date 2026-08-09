"""
Ручные переопределения числа игроков на одном экране для игр, у которых eShop
не отдал numberOfPlayers.system.

Каждое значение проверено по сети: искалась именно версия для Nintendo Switch и
максимум игроков на ОДНОМ экране. Это важно — метаданные Nintendo и европейский
API смешивают локальный режим с онлайном: Terraria там 8 при сплитскрине на 2,
Rocket League 8 при 4, Stardew Valley 4 (это издание для Switch 2, на оригинальном
Switch — 2), а Voice of Cards показывает 4 у полностью одиночной игры.

Игры, по которым надёжного ответа нет, сюда не попадают и в каталог не идут.

Генерирует overrides.json: nsuid -> {players, note, source}.
"""

import json

# Заголовок -> (игроков на одном экране, пояснение, источник)
VERIFIED = {
    # Крупные игры, по каждой отдельная проверка
    "Rocket League": (4, None, "epicgames.com/help — 4-player split-screen on Switch"),
    "Stardew Valley": (
        2, "На Nintendo Switch сплитскрин на двоих; 4 игрока — только в издании для Switch 2.",
        "stardewvalleywiki.com + анонс издания для Switch 2",
    ),
    "Terraria": (
        2, "Сплитскрин на двоих в док-режиме. 4 игрока — на Xbox и PlayStation, не на Switch.",
        "thegamer.com — Switch split-screen update",
    ),
    "Cave Story+": (2, None, "gematsu.com — local two-player co-op update"),
    "For The King": (3, None, "co-optimus.com — 3 players local, общий экран без сплита"),
    "Risk of Rain": (
        2, "Локально двое; четверо — в Risk of Rain Returns, это другая игра.",
        "gameskinny.com / wikipedia",
    ),
    "Titan Quest": (2, None, "nintendolife.com — two player couch co-op, 6 только по сети"),
    "Inkulinati": (2, "Режим hotseat: игроки ходят по очереди на одном экране.",
                   "playco-opgame.com / steam FAQ"),
    "Mr. DRILLER DrillLand": (4, None, "bandainamcoent.com — up to 4 players on one console"),
    "Oddworld: New 'n' Tasty": (
        2, "Кооп с передачей управления: второй игрок перехватывает после смерти первого.",
        "gonintendo.com dev blog",
    ),
    "Bang-On Balls: Chronicles": (2, "Сплитскрин на двоих; вчетвером — только по сети.",
                                  "co-optimus.com"),
    "ibb & obb": (2, "Игра рассчитана ровно на двоих, в одиночку не проходится.",
                  "co-optimus.com"),
    "It Takes Two": (
        2, "Игра рассчитана ровно на двоих, режима на одного нет.",
        "самая часто называемая игра в подборках couch co-op; сплитскрин на двоих"),
    "LEGO Voyagers": (2, "Игра рассчитана ровно на двоих, режима на одного нет.",
                      "nintendo.com / lightbrick.com"),

    # Jackbox: игроки подключаются телефонами, а не геймпадами
    "Drawful 2": (8, "Игроки подключаются телефонами, дополнительные геймпады не нужны.",
                  "jackboxgames.com"),
    "Quiplash": (8, "Игроки подключаются телефонами, дополнительные геймпады не нужны.",
                 "jackboxgames.com"),
    "Fibbage XL": (8, "Игроки подключаются телефонами, дополнительные геймпады не нужны.",
                   "jackboxgames.com"),

    # Число прямо названо в описании от издателя — источник надёжнее любого
    # агрегатора, текст лежит в самом каталоге eShop
    "All Hands on Deck": (2, None, "описание издателя: «a 2-player cooperative puzzle platformer»"),
    "All You Need is Help": (
        4, None, "описание издателя: «a four player game (online and/or local co-op)»"),
    "Race Arcade": (6, None, "описание издателя: «perfect game to play locally with up to 6 players»"),
    "Tanky Tanks - Reloaded": (
        4, "Вчетвером — режим VS; кампания в кооперативе на двоих.",
        "описание издателя: «Defeat your friends in the VS Mode for up to 4 players»"),
    "Drone Master Racing": (
        5, "Ходы по очереди на одном экране, дополнительные контроллеры не нужны.",
        "описание издателя: «Up to 5 players per turns in competition mode»"),
    "How 2 Escape": (
        2, "Второй игрок подключается смартфоном через приложение-компаньон.",
        "описание издателя: «2 players, 2 ways to play»"),
    "How 2 Escape: Lost Submarine": (
        2, "Второй игрок подключается смартфоном через приложение-компаньон.",
        "описание издателя: «2 players, 2 ways to play»"),
    "Moto Roader MC": (5, None, "seafoamgaming.com — up to 5 player local"),
    "Outbuddies DX": (2, None, "co-optimus.com — local co-op, второй игрок управляет Buddy"),

    "Ketsu Battler": (
        2, "Игра рассчитана ровно на двоих: поединок один на один.",
        "gonintendo.com — «one-on-one action game designed exclusively for two players»"),
    "Pair Horror": (
        2, None,
        "gonintendo.com — «2-player game», 20 кооперативных мини-игр"),
    # Sabec: в описаниях этих игр прямо сказано «против друга или против ИИ»
    "Battleground": (2, None, "Nintendo eShop — «play against either another player or an AI»"),
    "Bowling": (2, None, "Nintendo eShop — «play against a friend or against the computer»"),

    # Серия Arcade Archives от HAMSTER: везде «Single System (1-2)»
    "Arcade Archives EXCITEBIKE": (2, None, "Nintendo eShop — Single System (1-2)"),
    "Arcade Archives TOKYO WARS": (2, None, "Nintendo eShop — Single System (1-2)"),
    "Arcade Archives VS. CASTLEVANIA": (2, None, "Nintendo eShop — Single System (1-2)"),

    # Небольшие игры: европейский API даёт ровно 2-2 без онлайна и это совпадает
    # с бакетом playerCount из US-каталога
    "Battleship War: Time to Sink the Fleet": (2, None, "Nintendo EU (2-2) + playerCount 2+"),
    "Duo Games": (2, None, "Nintendo EU (2) + playerCount 2+"),
    "Let's Cook Together": (2, None, "Nintendo EU (2-2) + playerCount 2+"),
    "Red Hands - 2 Player Games": (2, None, "Nintendo EU (2-2) + playerCount 2+"),
    "Soccer Pinball": (2, None, "Nintendo EU (2-2) + playerCount 2+"),
    "Talk it Out: Handheld Game": (2, None, "Nintendo EU (2-2) + playerCount 2+"),
    "The Viking's Games: Madness Fight": (2, None, "Nintendo EU (2-2) + playerCount 2+"),
}

# Все выпуски Jackbox Party Pack — телефоны вместо геймпадов
JACKBOX_NOTE = "Игроки подключаются телефонами, дополнительные геймпады не нужны."
JACKBOX_SOURCE = "jackboxgames.com / Nintendo World Report"
for i in ["", " 2", " 3", " 4", " 5", " 6", " 7", " 8", " 9", " 11"]:
    VERIFIED[f"The Jackbox Party Pack{i}"] = (8, JACKBOX_NOTE, JACKBOX_SOURCE)
VERIFIED["The Jackbox Party Pack 10"] = (9, JACKBOX_NOTE, JACKBOX_SOURCE)
VERIFIED["The Jackbox Party Starter"] = (8, JACKBOX_NOTE, JACKBOX_SOURCE)
VERIFIED["The Jackbox Survey Scramble"] = (8, JACKBOX_NOTE, JACKBOX_SOURCE)


# Игры с меткой «на одном экране», у которых мультиплеера на одном экране на
# самом деле нет. Метка eShop здесь просто неверна, в каталог они не идут.
WRONGLY_TAGGED = {
    "HELLCARD": "кооператив только по сети, локального нет",
    "Double Kick Heroes": "игра одиночная",
    "LEGO® Voyagers Friend’s Pass": "не игра, а бесплатный пропуск для приглашения по сети",
    "Teddy Gangs": "максимум 1 игрок",
}


def main():
    with open("local_multiplayer.json", encoding="utf-8") as f:
        games = json.load(f)

    by_title = {}
    for g in games:
        if g["same_screen_max"] or (g["nsuid"] or "").startswith("7007"):
            continue
        by_title.setdefault(g["title"].strip().replace("®", "").replace("™", ""), g)

    overrides, missing = {}, []
    for title, (players, note, source) in VERIFIED.items():
        g = by_title.get(title)
        if not g:
            missing.append(title)
            continue
        overrides[g["nsuid"]] = {
            "title": title, "same_screen_min": 2 if players == 2 else 1,
            "same_screen_max": players, "players_note": note, "source": source,
        }

    with open("overrides.json", "w", encoding="utf-8") as f:
        json.dump(overrides, f, ensure_ascii=False, indent=1)

    unresolved = len(by_title) - len(overrides) - len(WRONGLY_TAGGED)
    print(f"переопределений: {len(overrides)} из {len(by_title)} игр без счётчика")
    print(f"метка eShop неверна, исключены: {len(WRONGLY_TAGGED)}")
    for title, reason in WRONGLY_TAGGED.items():
        print(f"  {title} — {reason}")
    print(f"осталось без подтверждения: {unresolved}")
    if missing:
        print("не нашлось по названию:", missing)


if __name__ == "__main__":
    main()
