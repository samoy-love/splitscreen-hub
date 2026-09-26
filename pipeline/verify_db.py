"""
Проверяет catalog.db на осмысленность перед упаковкой в catalog.bin: фильтры
сужают выборку, известные игры стоят на своих местах, одиночные и бандлы не
просочились, обложки на диске совпадают с базой.

Работу с железом — nsListApplicationRecord и память в applet-режиме — так
проверить нельзя, это только запуском на консоли.
"""

import os
import sqlite3
import sys

from paths import ART_DIR, CATALOG_DB, TOPLISTS_DB, TRANSLATIONS_DB

DB = CATALOG_DB

FROM_JOIN = " FROM games g"

FIELDS = ("g.nsuid, g.title, g.title_id, g.same_screen_min, g.same_screen_max,"
          " g.players_note, g.box_art_file, g.background_color, g.headline,"
          " g.description, g.publisher, g.release_year, g.languages,"
          " g.rom_size_bytes, g.has_online, g.no_tabletop, g.has_demo,"
          " g.has_russian, g.mentions")

# Те же порядки, что предлагает приложение (catalog_query.cpp).
ORDER_BY = (
    " ORDER BY g.mentions = 0, g.score DESC, g.sort_title",
    " ORDER BY g.sort_title",
    " ORDER BY g.same_screen_max DESC, g.sort_title",
    " ORDER BY g.release_year DESC, g.sort_title",
    " ORDER BY g.rom_size_bytes IS NULL, g.rom_size_bytes, g.sort_title",
)

failures = []


def check(label, condition, detail=""):
    mark = "ok  " if condition else "FAIL"
    print(f"  [{mark}] {label}{(' — ' + detail) if detail else ''}")
    if not condition:
        failures.append(label)


def where(min_players, genre=None, russian=False, search=None, retro=False):
    """Условия фильтра приложения на языке SQL."""
    w = f" WHERE g.same_screen_max >= {min_players}"
    if genre:
        w += f" AND g.nsuid IN (SELECT nsuid FROM genres WHERE genre = '{genre}')"
    if russian:
        w += " AND g.has_russian = 1"
    if not retro:
        w += " AND g.is_retro = 0"
    if search:
        w += (" AND g.nsuid IN (SELECT nsuid FROM games_fts"
              f" WHERE games_fts MATCH '\"{search}\"*')")
    return w


def main():
    db = sqlite3.connect(DB)

    print("Фильтр «от N игроков» — пороги должны убывать, но не обнуляться:")
    prev = None
    for n in (2, 3, 4, 6, 8):
        c = db.execute("SELECT count(*)" + FROM_JOIN + where(n)).fetchone()[0]
        check(f"от {n}: {c} игр", c > 0 and (prev is None or c <= prev))
        prev = c

    print("\nВыборка отдаёт заполненные поля:")
    row = db.execute(f"SELECT {FIELDS}" + FROM_JOIN + where(4) + ORDER_BY[1]
                     + " LIMIT 1").fetchone()
    check("строка читается", row is not None)
    check("есть nsuid и название", bool(row[0]) and bool(row[1]))
    check("min <= max", row[3] <= row[4], f"{row[3]}–{row[4]}")

    print("\nЧисло игроков осмысленно у всех строк, а не только у первой:")
    bad = db.execute("SELECT title, same_screen_min, same_screen_max FROM games"
                     " WHERE same_screen_min < 1 OR same_screen_min > same_screen_max"
                     " LIMIT 5").fetchall()
    check("1 <= min <= max", not bad,
          "; ".join(f"{t} {a}–{b}" for t, a, b in bad))

    print("\nЗначения помещаются в поля catalog.bin и details.bin:")
    # Ширины — из make_ship_data.py. Упаковщик сам их не проверяет: u8 и u16
    # через struct.pack падают на переполнении посреди сборки, а счётчик,
    # обрезанный по модулю, дал бы на консоли не те жанры и снимки.
    for col in ("same_screen_min", "same_screen_max", "mentions", "score", "release_year"):
        lo, hi = db.execute(f"SELECT min(coalesce({col}, 0)), max(coalesce({col}, 0))"
                            " FROM games").fetchone()
        check(f"{col} в u16: {lo}..{hi}", 0 <= lo and hi <= 0xFFFF)
    # Счёт согласия — score рейтинга ×10, а рейтинг нормирован на 100.
    hi = db.execute("SELECT max(score) FROM games").fetchone()[0] or 0
    check(f"score не больше 1000: {hi}", hi <= 1000)
    kinds = db.execute("SELECT count(DISTINCT genre) FROM genres").fetchone()[0]
    check(f"жанров всего {kinds}, номер жанра — u8", kinds <= 256)
    for label, sql in (
        ("жанров у игры", "SELECT nsuid, count(*) FROM genres GROUP BY nsuid"),
        ("скриншотов у игры", "SELECT nsuid, count(*) FROM media WHERE kind = 'image'"
                              " GROUP BY nsuid"),
        ("роликов у игры", "SELECT nsuid, count(*) FROM media WHERE kind = 'video'"
                           " GROUP BY nsuid"),
    ):
        most = db.execute(sql + " ORDER BY 2 DESC LIMIT 1").fetchone()
        n = most[1] if most else 0
        check(f"{label} не больше 255 (u8): максимум {n}", n <= 255,
              most[0] if most and n > 255 else "")

    print("\nФайлы-спутники подмешались:")
    # build_db.py без файла собирает каталог без него — это законно. Но если
    # файл есть, а таблица пустая, данные потерялись по дороге.
    for path, table in ((TRANSLATIONS_DB, "translations"), (TOPLISTS_DB, "ranking")):
        if not os.path.exists(path):
            print(f"  [    ] {os.path.basename(path)} нет — {table} не проверяется")
            continue
        n = db.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
        check(f"{table}: {n} строк", n > 0)

    print("\nПереиздания аркад скрыты по умолчанию:")
    retro = db.execute("SELECT count(*) FROM games WHERE is_retro = 1").fetchone()[0]
    check(f"помечено ретро: {retro}", retro > 400)
    visible = db.execute("SELECT count(*)" + FROM_JOIN + where(2)).fetchone()[0]
    with_retro = db.execute("SELECT count(*)" + FROM_JOIN + where(2, retro=True)).fetchone()[0]
    check(f"без ретро {visible}, с ними {with_retro}", with_retro > visible)
    check("в подборках ретро нет",
          db.execute("SELECT count(*) FROM games WHERE mentions > 0 AND is_retro = 1")
            .fetchone()[0] == 0)

    print("\nЖанры переведены:")
    english = db.execute("SELECT count(*) FROM genres WHERE genre GLOB '*[A-Za-z]*'").fetchone()[0]
    check("английских названий не осталось", english == 0)

    print("\nВсе сортировки выполняются:")
    for i, order in enumerate(ORDER_BY):
        try:
            rows = db.execute(f"SELECT g.nsuid{FROM_JOIN}{where(2)}{order} LIMIT 5").fetchall()
            check(f"сортировка #{i}", len(rows) == 5)
        except sqlite3.Error as exc:
            check(f"сортировка #{i}", False, str(exc))

    print("\nКонкретные игры на своих местах:")
    for title, expect in (("Mario Kart 8 Deluxe", 4), ("Overcooked! 2", 4),
                          ("Rocket League", 4), ("Stardew Valley", 2),
                          ("Super Smash Bros. Ultimate", 8),
                          ("All You Need is Help", 4), ("Race Arcade", 6),
                          ("Moto Roader MC", 5)):
        r = db.execute("SELECT same_screen_max FROM games WHERE title = ?", (title,)).fetchone()
        check(f"{title} = {expect}", r is not None and r[0] == expect,
              "нет в базе" if r is None else f"в базе {r[0]}")

    print("\nОдиночные игры и бандлы в базу не попали:")
    for title in ("Voice of Cards: The Isle Dragon Roars", "10 in 1 Classic Games Pack",
                  "HELLCARD", "Double Kick Heroes"):
        r = db.execute("SELECT 1 FROM games WHERE title = ?", (title,)).fetchone()
        check(f"{title} отсутствует", r is None)
    check("нет бандлов по nsuid",
          db.execute("SELECT count(*) FROM games WHERE nsuid LIKE '7007%'").fetchone()[0] == 0)
    check("у всех известно число игроков",
          db.execute("SELECT count(*) FROM games WHERE same_screen_max < 2").fetchone()[0] == 0)

    print("\nОстальные фильтры сужают выборку:")
    base = db.execute("SELECT count(*)" + FROM_JOIN + where(2)).fetchone()[0]
    ru = db.execute("SELECT count(*)" + FROM_JOIN + where(2, russian=True)).fetchone()[0]
    check(f"есть русский: {ru}", 0 < ru < base)
    party = db.execute("SELECT count(*)" + FROM_JOIN + where(2, genre="Вечеринки")).fetchone()[0]
    check(f"жанр Party: {party}", 0 < party < base)

    print("\nПоиск по названию:")
    for term, expect in (("mario", True), ("overcook", True), ("zzzqqq", False)):
        c = db.execute("SELECT count(*)" + FROM_JOIN + where(2, search=term)).fetchone()[0]
        check(f"«{term}»: {c}", (c > 0) == expect)

    print("\nОбложки на диске совпадают с базой:")
    art_dir = ART_DIR
    need = [r[0] for r in db.execute("SELECT box_art_file FROM games WHERE box_art_file IS NOT NULL")]
    missing = [f for f in need if not os.path.exists(os.path.join(art_dir, f))]
    check(f"файлов не хватает: {len(missing)}", not missing)
    # Лишнее в art/ тоже уезжает в бандл и в romfs .nro: обложки выпавших из
    # каталога игр и недокачанные .tmp.
    wanted = set(need)
    stray = sorted(f for f in os.listdir(art_dir) if f not in wanted) \
        if os.path.isdir(art_dir) else []
    check(f"лишних файлов: {len(stray)}", not stray, ", ".join(stray[:5]))
    present = [f for f in need[:200] if f not in missing]
    if present:
        avg = sum(os.path.getsize(os.path.join(art_dir, f)) for f in present) / len(present) / 1024
        check(f"средний размер {avg:.1f} КБ", avg < 40,
              "великоват — трансформация Cloudinary не применилась" if avg >= 40 else "")

    db.close()

    print()
    if failures:
        print(f"НЕ ПРОШЛО {len(failures)}: " + "; ".join(failures))
        return 1
    print("Все проверки прошли.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
