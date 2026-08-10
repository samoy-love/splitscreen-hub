"""
Собирает catalog.db для приложения из local_multiplayer.json + overrides.json.

В базу попадают только вышедшие игры Nintendo Switch с известным точным числом
игроков на одном экране. Не попадают: бандлы-сборники (nsuid 7007*), будущие
релизы и всё, для чего число игроков осталось неизвестным.
"""

import json
import os
import re
import sqlite3
from datetime import date

SOURCE = "local_multiplayer.json"
OVERRIDES = "overrides.json"
DB = "catalog.db"

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
  -- Копия из toplists. Сортировка по умолчанию идёт именно по ней, и через
  -- LEFT JOIN она обходилась в секунду на консоли: соединение не давало
  -- воспользоваться индексом, и 3489 строк каждый раз уходили во временное
  -- B-дерево. Своя колонка в games делает порядок индексируемым.
  mentions         INTEGER NOT NULL DEFAULT 0,
  best_pos         INTEGER NOT NULL DEFAULT 0,
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

-- главный запрос приложения: WHERE same_screen_max >= ?
CREATE INDEX idx_players ON games(same_screen_max);
CREATE INDEX idx_title_id ON games(title_id);
CREATE INDEX idx_sort ON games(sort_title);
CREATE INDEX idx_genre ON genres(genre);
CREATE INDEX idx_media ON media(nsuid);
-- Под остальные сортировки из ORDER_BY[] в app/source/catalog.cpp. Без них
-- «сначала новые» и «по размеру» строили временное B-дерево на 3489 строк при
-- каждом движении фильтра. Вторым ключом всюду sort_title — он же идёт вторым
-- в самих запросах, поэтому индекс покрывает порядок целиком.
CREATE INDEX idx_year ON games(release_year DESC, sort_title);
-- Индекс по выражению, а не по колонке: ведущий член сортировки —
-- `rom_size_bytes IS NULL` (игры с неизвестным размером уходят в конец), и по
-- обычному индексу такой порядок не берётся.
CREATE INDEX idx_size ON games(rom_size_bytes IS NULL, rom_size_bytes, sort_title);

CREATE VIRTUAL TABLE games_fts USING fts5(
  title, nsuid UNINDEXED, tokenize='unicode61'
);

-- Наработки, которые собираются отдельно и переживают пересборку каталога:
-- перевод и рейтинги лежат в своих файлах, сюда только подмешиваются.
CREATE TABLE translations (
  nsuid           TEXT PRIMARY KEY,
  headline_ru     TEXT,
  players_note_ru TEXT,
  description_ru  TEXT
);

CREATE TABLE toplists (
  nsuid    TEXT PRIMARY KEY,
  mentions INTEGER NOT NULL,  -- в скольких подборках названа
  best_pos INTEGER,
  sources  TEXT NOT NULL
);

CREATE TABLE ratings (
  nsuid  TEXT PRIMARY KEY,
  rating INTEGER,   -- 0-100
  votes  INTEGER,   -- на скольких отзывах основана оценка
  source TEXT       -- откуда взята: показывается в карточке
);
"""

# Файл -> (таблица, колонки). Пересборка каталога не должна уничтожать работу
# параллельных сессий, поэтому они пишут в свои базы, а не в catalog.db.
SIDECARS = {
    "translations.db": ("translations",
                        "nsuid, headline_ru, players_note_ru, description_ru"),
    "ratings.db": ("ratings", "nsuid, rating, votes, source"),
    "toplists.db": ("toplists", "nsuid, mentions, best_pos, sources"),
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
            print(f"  подмешано из {path}: {cur.rowcount}")
            cur.close()
        except sqlite3.Error as e:
            print(f"  {path}: пропущен, {e}")
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
    rows, skipped = [], {"бандл": 0, "нет числа игроков": 0, "не вышла": 0}

    for g in games:
        nsuid = g.get("nsuid")
        if not nsuid:
            skipped["нет числа игроков"] += 1
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

        # Колонки перечислены поимённо: позиционный список из двадцати с лишним
        # «?» уже однажды разъехался со схемой при добавлении колонки.
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

    # Переносим упоминания в games — см. комментарий у колонок.
    db.execute("""
        UPDATE games SET
          mentions = coalesce((SELECT t.mentions FROM toplists t WHERE t.nsuid = games.nsuid), 0),
          best_pos = coalesce((SELECT t.best_pos FROM toplists t WHERE t.nsuid = games.nsuid), 0)
    """)
    db.execute("CREATE INDEX IF NOT EXISTS idx_top ON games("
               "mentions = 0, mentions DESC, best_pos, sort_title)")
    db.commit()
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
    print(f"\n{DB}: {os.path.getsize(DB) / 1024 / 1024:.1f} МБ")


if __name__ == "__main__":
    main()
