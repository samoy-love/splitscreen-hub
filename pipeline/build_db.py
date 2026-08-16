"""
Собирает catalog.db — рабочую базу пайплайна — из local_multiplayer.json и
overrides.json. Приложение эту базу не читает: make_ship_data.py упаковывает её
в catalog.bin и details.bin.

В базу попадают только вышедшие игры Nintendo Switch с известным точным числом
игроков на одном экране. Не попадают: бандлы-сборники (nsuid 7007*), будущие
релизы и всё, для чего число игроков осталось неизвестным.

Все файлы лежат в pipeline/ рядом со скриптом. Если там есть translations.db (русские тексты) и toplists.db (рейтинг
подборок из rank_toplists.py), их содержимое подмешивается.
"""

import json
import os
import re
import sqlite3
from datetime import date

from paths import CATALOG_DB, LOCAL_MULTIPLAYER, OVERRIDES, TOPLISTS_DB, TRANSLATIONS_DB

SOURCE = LOCAL_MULTIPLAYER
DB = CATALOG_DB

SCHEMA = """
CREATE TABLE games (
  nsuid            TEXT PRIMARY KEY,
  title            TEXT NOT NULL,
  sort_title       TEXT NOT NULL,
  title_id         TEXT,
  same_screen_min  INTEGER NOT NULL,
  same_screen_max  INTEGER NOT NULL,
  players_note     TEXT,
  box_art_file     TEXT,
  background_color TEXT,
  headline         TEXT,
  description      TEXT,
  publisher        TEXT,
  release_year     INTEGER,
  languages        TEXT,
  rom_size_bytes   INTEGER,
  has_online       INTEGER NOT NULL DEFAULT 0,
  no_tabletop      INTEGER NOT NULL DEFAULT 0,
  has_demo         INTEGER NOT NULL DEFAULT 0,
  has_russian      INTEGER NOT NULL DEFAULT 0,
  -- Число независимых источников (подборок и тредов), назвавших игру,
  -- и счёт согласия между ними ×10 (0..1000) — см. rank_toplists.py.
  -- Счёт целый, потому что в catalog.bin уезжает как u16.
  mentions         INTEGER NOT NULL DEFAULT 0,
  score            INTEGER NOT NULL DEFAULT 0,
  -- Переиздание аркадного автомата или консоли прошлого века. Таких в каталоге
  -- 474 штуки — 13%, и почти все от одного издателя. Формально они подходят под
  -- «вдвоём на одном экране», но когда ищешь во что поиграть вечером, полтысячи
  -- «Arcade Archives ...» просто засоряют выдачу. Скрыты по умолчанию,
  -- включаются отдельным фильтром.
  is_retro         INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE genres (
  nsuid TEXT NOT NULL REFERENCES games(nsuid),
  genre TEXT NOT NULL
);

-- Адреса медиа занимали 3.4 МБ на 20 тысяч строк, притом что различаются
-- только хвостом: префиксов всего четыре, а nsuid уже есть в самой строке.
-- Держим префиксы словарём, а в tail вместо nsuid стоит байт 0x01 —
-- получается 1.7 МБ вместо 3.4.
CREATE TABLE media_prefix (
  id     INTEGER PRIMARY KEY,
  prefix TEXT NOT NULL
);

CREATE TABLE media (
  nsuid     TEXT NOT NULL REFERENCES games(nsuid),
  kind      TEXT NOT NULL,
  prefix_id INTEGER NOT NULL REFERENCES media_prefix(id),
  tail      TEXT NOT NULL,
  ord       INTEGER NOT NULL
);

CREATE INDEX idx_players ON games(same_screen_max);
CREATE INDEX idx_title_id ON games(title_id);
CREATE INDEX idx_genre ON genres(genre);
CREATE INDEX idx_media ON media(nsuid);

-- Только для проверок verify_db.py: приложение ищет по названию само.
CREATE VIRTUAL TABLE games_fts USING fts5(
  title, nsuid UNINDEXED, tokenize='unicode61'
);

-- Собираются отдельно и переживают пересборку каталога: лежат в своих файлах,
-- сюда только подмешиваются.
CREATE TABLE translations (
  nsuid           TEXT PRIMARY KEY,
  headline_ru     TEXT,
  players_note_ru TEXT,
  description_ru  TEXT
);

CREATE TABLE ranking (
  nsuid     TEXT PRIMARY KEY,
  score     REAL NOT NULL,   -- 0..100, среднее геометрическое двух каналов
  editorial REAL NOT NULL,
  community REAL NOT NULL,
  families  INTEGER NOT NULL -- независимых источников
);
"""

# Файл -> (таблица, колонки). Каждый этап пишет в свою базу, а не в catalog.db,
# чтобы пересборка каталога не уничтожала его результат.
SIDECARS = {
    TRANSLATIONS_DB: ("translations",
                      "nsuid, headline_ru, players_note_ru, description_ru"),
    TOPLISTS_DB: ("ranking", "nsuid, score, editorial, community, families"),
}


def merge_sidecars(db):
    for path, (table, columns) in SIDECARS.items():
        if not os.path.exists(path):
            continue
        db.execute("ATTACH DATABASE ? AS side", (path,))
        try:
            cur = db.execute(
                f"INSERT OR REPLACE INTO {table} ({columns})"
                f" SELECT {columns} FROM side.{table}"
                f" WHERE nsuid IN (SELECT nsuid FROM games)")
            print(f"  подмешано из {os.path.basename(path)}: {cur.rowcount}")
            cur.close()
        except sqlite3.Error as e:
            print(f"  {os.path.basename(path)}: пропущен, {e}")
        # без коммита открытая транзакция держит присоединённый файл и
        # DETACH падает с «database side is locked»
        db.commit()
        db.execute("DETACH DATABASE side")

# Серии переизданий: определяем по названию, а не по издателю. HAMSTER выпускает
# только их, но SEGA — и переиздания, и обычные игры, так что признак по
# издателю зацепил бы лишнее.
RETRO_PREFIXES = ("ACA NEOGEO ", "Arcade Archives ", "SEGA AGES ")


def is_retro(title):
    return int(any(title.startswith(p) for p in RETRO_PREFIXES))


# Жанры приходят из eShop по-английски. Интерфейс русский целиком, поэтому
# переводим их прямо здесь: так и фильтр, и подписи в карточке говорят на одном
# языке, и приложению не нужен отдельный словарь.
GENRES_RU = {
    "Action": "Экшен",
    "Adventure": "Приключения",
    "Application": "Приложения",
    "Education": "Обучающие",
    "Fighting": "Файтинги",
    "Music": "Музыкальные",
    "Narrative adventure": "Сюжетные",
    "Party": "Вечеринки",
    "Pinball": "Пинбол",
    "Puzzle": "Головоломки",
    "Racing": "Гонки",
    "Role playing": "Ролевые",
    "Shooting": "Шутеры",
    "Simulation": "Симуляторы",
    "Sports": "Спорт",
    "Strategy": "Стратегии",
    "Tabletop": "Настольные",
    "Training": "Тренировки",
}


ARTICLES = re.compile(r"^(the|a|an)\s+", re.I)


def sort_key(title):
    t = ARTICLES.sub("", title.strip().lstrip("#").strip())
    return t.lower()


def clean(s):
    """Убирает символы товарных знаков — на консоли они рисуются как мусор."""
    if not s:
        return s
    return s.replace("™", "").replace("®", "").strip()


def load_games():
    with open(SOURCE, encoding="utf-8") as f:
        games = json.load(f)
    with open(OVERRIDES, encoding="utf-8") as f:
        overrides = json.load(f)

    today = date.today().isoformat()
    rows, skipped = [], {"нет nsuid": 0, "бандл": 0, "нет числа игроков": 0, "не вышла": 0}

    for g in games:
        nsuid = g.get("nsuid")
        if not nsuid:
            skipped["нет nsuid"] += 1
            continue
        if nsuid.startswith("7007"):
            skipped["бандл"] += 1
            continue

        ov = overrides.get(nsuid)
        smin = ov["same_screen_min"] if ov else g["same_screen_min"]
        smax = ov["same_screen_max"] if ov else g["same_screen_max"]
        if not smax or smax < 2:
            skipped["нет числа игроков"] += 1
            continue

        release = (g.get("release") or "")[:10]
        if not release or release > today:
            skipped["не вышла"] += 1
            continue

        g["_min"], g["_max"] = smin or 1, smax
        g["_note"] = (ov or {}).get("players_note") or g.get("players_note")
        rows.append(g)

    return rows, skipped


MEDIA_SPLIT_AT = "store/software/"
NSUID_MARK = chr(1)  # нечитаемый байт-метка, в адресах не встречается


def split_media_url(nsuid, url, prefixes):
    """Режет адрес на словарный префикс и хвост с подставленным nsuid."""
    at = url.find(MEDIA_SPLIT_AT)
    prefix = url[: at + len(MEDIA_SPLIT_AT)] if at >= 0 else ""
    tail = url[len(prefix):].replace(nsuid, NSUID_MARK)
    if prefix not in prefixes:
        prefixes[prefix] = len(prefixes) + 1
    return prefixes[prefix], tail


def main():
    rows, skipped = load_games()
    prefixes = {}

    if os.path.exists(DB):
        os.remove(DB)
    db = sqlite3.connect(DB)
    db.executescript(SCHEMA)

    for g in rows:
        nsuid = g["nsuid"]
        title = clean(g["title"])
        ways = g.get("ways_to_play") or ""
        modes = g.get("play_modes") or ""
        langs = g.get("languages") or ""

        # Колонки перечислены поимённо, чтобы не разъехаться со схемой.
        db.execute(
            "INSERT INTO games (nsuid, title, sort_title, title_id,"
            " same_screen_min, same_screen_max, players_note, box_art_file,"
            " background_color, headline, description, publisher, release_year,"
            " languages, rom_size_bytes, has_online, no_tabletop, has_demo,"
            " has_russian, is_retro)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (
                nsuid, title, sort_key(title), g.get("title_id"),
                g["_min"], g["_max"], clean(g.get("_note")),
                f"{nsuid}.jpg" if g.get("box_art") else None,
                g.get("background_color"),
                clean(g.get("headline")), clean(g.get("description")),
                clean(g.get("publisher")),
                int(g["release"][:4]) if g.get("release") else None,
                langs, g.get("rom_size_bytes"),
                int("online" in ways.lower()),
                int(bool(modes) and "Tabletop" not in modes),
                int(bool(g.get("demo_nsuid"))),
                int("Russian" in langs),
                is_retro(title),
            ),
        )
        db.execute("INSERT INTO games_fts (title, nsuid) VALUES (?,?)", (title, nsuid))

        for genre in filter(None, (x.strip() for x in (g.get("genres") or "").split(","))):
            ru = GENRES_RU.get(genre)
            if ru is None:
                # Новый жанр в исходных данных — заметить это лучше сразу, чем
                # увидеть на консоли английское слово среди русских.
                print(f"  ВНИМАНИЕ: нет перевода жанра «{genre}»")
                ru = genre
            db.execute("INSERT INTO genres VALUES (?,?)", (nsuid, ru))

        for kind, urls in (("image", g.get("images") or []), ("video", g.get("videos") or [])):
            for i, url in enumerate(urls):
                prefix_id, tail = split_media_url(nsuid, url, prefixes)
                db.execute("INSERT INTO media VALUES (?,?,?,?,?)",
                           (nsuid, kind, prefix_id, tail, i))

    for prefix, pid in prefixes.items():
        db.execute("INSERT INTO media_prefix VALUES (?,?)", (pid, prefix))

    merge_sidecars(db)

    # mentions — число независимых источников, а не только редакционных
    # подборок: игру могли обсуждать в пяти тредах и не назвать ни в одном
    # списке, и для фильтра «советуют» это такой же довод.
    db.execute("""
        UPDATE games SET
          mentions = coalesce((SELECT r.families FROM ranking r WHERE r.nsuid = games.nsuid), 0),
          score    = coalesce((SELECT cast(round(r.score * 10) AS INTEGER) FROM ranking r
                               WHERE r.nsuid = games.nsuid), 0)
    """)
    db.commit()

    print(f"игр в базе: {len(rows)}")
    for reason, n in skipped.items():
        print(f"  отброшено, {reason}: {n}")
    print()
    for n in (2, 3, 4, 6, 8):
        c = db.execute("SELECT count(*) FROM games WHERE same_screen_max >= ?", (n,)).fetchone()[0]
        print(f"  от {n} игроков: {c}")
    print()
    for label, sql in (
        ("с русским языком", "SELECT count(*) FROM games WHERE has_russian"),
        ("есть и онлайн", "SELECT count(*) FROM games WHERE has_online"),
        ("без настольного режима", "SELECT count(*) FROM games WHERE no_tabletop"),
        ("есть демо", "SELECT count(*) FROM games WHERE has_demo"),
        ("с Title ID", "SELECT count(*) FROM games WHERE title_id IS NOT NULL"),
        ("жанров", "SELECT count(DISTINCT genre) FROM genres"),
        ("скриншотов", "SELECT count(*) FROM media WHERE kind='image'"),
        ("префиксов медиа", "SELECT count(*) FROM media_prefix"),
    ):
        print(f"  {label}: {db.execute(sql).fetchone()[0]}")

    db.execute("VACUUM")
    db.close()
    print(f"\n{os.path.basename(DB)}: {os.path.getsize(DB) / 1024 / 1024:.1f} МБ")


if __name__ == "__main__":
    main()
