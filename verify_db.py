"""
Прогоняет по catalog.db ровно те запросы, которые выполняет приложение
(см. app/source/catalog.cpp), и проверяет, что выборки осмысленные.

Работу с железом — nsListApplicationRecord и память в applet-режиме — так
проверить нельзя, это только запуском на консоли.
"""

import os
import sqlite3
import sys

DB = "catalog.db"

# Ровно то, что выбирает приложение: SELECT_FIELDS из app/source/catalog.cpp,
# вместе с джойном подборок. Джойна ratings здесь нет намеренно — таблица пуста,
# и приложение к ней больше не обращается.
FROM_JOIN = " FROM games g LEFT JOIN toplists tl ON tl.nsuid = g.nsuid"

FIELDS = ("g.nsuid, g.title, g.title_id, g.same_screen_min, g.same_screen_max,"
          " g.players_note, g.box_art_file, g.background_color, g.headline,"
          " g.description, g.publisher, g.release_year, g.languages,"
          " g.rom_size_bytes, g.has_online, g.no_tabletop, g.has_demo,"
          " g.has_russian, tl.mentions")

# ORDER_BY[] из app/source/catalog.cpp, в том же порядке, что SORT_NAMES.
ORDER_BY = (
    " ORDER BY tl.mentions IS NULL, tl.mentions DESC, tl.best_pos, g.sort_title",
    " ORDER BY g.sort_title",
    " ORDER BY g.sort_title DESC",
    " ORDER BY g.same_screen_max DESC, g.sort_title",
    " ORDER BY g.release_year DESC, g.sort_title",
    " ORDER BY g.rom_size_bytes IS NULL, g.rom_size_bytes, g.sort_title",
    " ORDER BY g.sort_title",
)

failures = []


def check(label, condition, detail=""):
    mark = "ok  " if condition else "FAIL"
    print(f"  [{mark}] {label}{(' — ' + detail) if detail else ''}")
    if not condition:
        failures.append(label)


def where(min_players, genre=None, russian=False, search=None):
    """Повторяет Catalog::buildWhere. Псевдоним g — как в запросах приложения."""
    w = f" WHERE g.same_screen_max >= {min_players}"
    if genre:
        w += f" AND g.nsuid IN (SELECT nsuid FROM genres WHERE genre = '{genre}')"
    if russian:
        w += " AND g.has_russian = 1"
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

    print("\nВсе сортировки из ORDER_BY выполняются:")
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
    party = db.execute("SELECT count(*)" + FROM_JOIN + where(2, genre="Party")).fetchone()[0]
    check(f"жанр Party: {party}", 0 < party < base)

    print("\nПоиск по названию:")
    for term, expect in (("mario", True), ("overcook", True), ("zzzqqq", False)):
        c = db.execute("SELECT count(*)" + FROM_JOIN + where(2, search=term)).fetchone()[0]
        check(f"«{term}»: {c}", (c > 0) == expect)

    print("\nОбложки на диске совпадают с базой:")
    art_dir = os.path.join("app", "resources", "art")
    need = [r[0] for r in db.execute("SELECT box_art_file FROM games WHERE box_art_file IS NOT NULL")]
    missing = [f for f in need if not os.path.exists(os.path.join(art_dir, f))]
    check(f"файлов не хватает: {len(missing)}", not missing)
    sizes = [os.path.getsize(os.path.join(art_dir, f)) for f in need[:200]]
    avg = sum(sizes) / len(sizes) / 1024
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
