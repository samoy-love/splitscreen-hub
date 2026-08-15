# -*- coding: utf-8 -*-
"""Собирает данные каталога в два двоичных файла вместо базы SQLite.

Зачем не база. Сетке нужно 268 КБ на все 3489 игр, а любой запрос к SQLite
поднимал с romfs десятки мегабайт: описания лежат в тех же строках таблицы, и
«взять семь коротких полей» физически читает страницы целиком. Отсюда время,
которое почти не зависело от числа найденных игр, — 754 мс на 37 игр против
965 мс на 3015. При 3489 записях база решает задачу, которой нет: отбор и
сортировка перебором в памяти занимают единицы миллисекунд.

Вместе с базой уходят амальгама SQLite, самодельная VFS для путей romfs и обход
блокировок fcntl — то есть самая хрупкая часть проекта, дававшая цепочку
загрузочных падений.

На выходе два файла:

  catalog.bin  — всё, что показывает сетка. Читается целиком при запуске.
  details.bin  — тексты и ссылки карточки, по записи на игру. Читается по
                 смещению, когда игру открыли.

Тексты в details.bin сжаты zlib с общим словарём. Записи короткие (в среднем
3.2 КБ), и поодиночке они жмутся вдвое; со словарём из настоящих описаний —
почти втрое, при этом каждая по-прежнему разворачивается отдельно.
"""

import io
import os
import random
import sqlite3
import struct
import zlib

SOURCE = "catalog.db"
OUT_DIR = os.path.join("app", "resources")
CATALOG = os.path.join(OUT_DIR, "catalog.bin")
DETAILS = os.path.join(OUT_DIR, "details.bin")

CATALOG_MAGIC = b"SSHC"
DETAILS_MAGIC = b"SSHD"
VERSION = 2

# 64 КБ словаря — предел zlib (окно 32 КБ учитывает только хвост, но больший
# буфер не мешает). Больше брать некуда, меньше — заметно хуже сжатие.
DICT_SIZE = 64 * 1024
DICT_SAMPLES = 300

# Отметка nsuid внутри хвоста адреса — так же, как в базе.
NSUID_MARK = "\x01"


def u8(v):
    return struct.pack("<B", v)


def u16(v):
    return struct.pack("<H", v)


def u32(v):
    return struct.pack("<I", v)


def i64(v):
    return struct.pack("<q", v)


def s16(text):
    """Строка с двухбайтовой длиной."""
    data = (text or "").encode("utf-8")
    if len(data) > 0xFFFF:
        data = data[:0xFFFF]
        while data and (data[-1] & 0xC0) == 0x80:  # не рвём символ UTF-8
            data = data[:-1]
    return u16(len(data)) + data


def s32(text):
    """Строка с четырёхбайтовой длиной — для описаний, они бывают длинными."""
    data = (text or "").encode("utf-8")
    return u32(len(data)) + data


def build_media(db):
    """nsuid -> {kind: [полные адреса по порядку]}."""
    prefixes = dict(db.execute("SELECT id, prefix FROM media_prefix"))
    out = {}
    for nsuid, kind, prefix_id, tail, _ord in db.execute(
        "SELECT nsuid, kind, prefix_id, tail, ord FROM media ORDER BY nsuid, kind, ord"
    ):
        url = prefixes.get(prefix_id, "") + (tail or "").replace(NSUID_MARK, nsuid)
        out.setdefault(nsuid, {}).setdefault(kind, []).append(url)
    return out


def build_genres(db):
    """Список названий жанров и nsuid -> [номера жанров]."""
    names = []
    index = {}
    per_game = {}
    for nsuid, genre in db.execute("SELECT nsuid, genre FROM genres ORDER BY nsuid"):
        if genre not in index:
            index[genre] = len(names)
            names.append(genre)
        per_game.setdefault(nsuid, []).append(index[genre])
    return names, per_game


def detail_record(row, tr, genres, shots, videos):
    """Содержимое записи карточки до сжатия."""
    (
        _nsuid, _title, _sort, _title_id, _min, _max, players_note, _art,
        background, headline, description, publisher, _year, languages,
        _size, has_online, no_tabletop, has_demo, _has_ru, _mentions, _best, _score,
        _retro,
    ) = row

    headline_ru, players_note_ru, description_ru = tr

    flags = (1 if has_online else 0) | (2 if no_tabletop else 0) | (4 if has_demo else 0)

    body = b"".join([
        s16(publisher),
        s16(languages),
        s16(background),
        u8(flags),

        s16(players_note), s16(players_note_ru),
        s16(headline), s16(headline_ru),
        s32(description), s32(description_ru),

        u8(len(genres)), b"".join(u8(g) for g in genres),
        u8(len(shots)), b"".join(s16(u) for u in shots),
        u8(len(videos)), b"".join(s16(u) for u in videos),
    ])
    return body


def main():
    db = sqlite3.connect(SOURCE)

    media = build_media(db)
    genre_names, genre_ids = build_genres(db)
    translations = {
        nsuid: (headline_ru, players_note_ru, description_ru)
        for nsuid, headline_ru, players_note_ru, description_ru in db.execute(
            "SELECT nsuid, headline_ru, players_note_ru, description_ru FROM translations"
        )
    }

    rows = db.execute("SELECT * FROM games ORDER BY nsuid").fetchall()

    # --- записи карточек и словарь ------------------------------------------
    bodies = []
    for row in rows:
        nsuid = row[0]
        by_kind = media.get(nsuid, {})
        bodies.append(detail_record(
            row,
            translations.get(nsuid, (None, None, None)),
            genre_ids.get(nsuid, []),
            by_kind.get("image", []),
            by_kind.get("video", []),
        ))

    # Словарь — склейка случайной выборки настоящих записей. Берём хвост: zlib
    # ищет совпадения в последних 32 КБ окна, и то, что ближе к концу словаря,
    # работает лучше.
    sample = b"".join(random.Random(1).sample(bodies, min(DICT_SAMPLES, len(bodies))))
    dictionary = sample[-DICT_SIZE:]

    packed = []
    for body in bodies:
        c = zlib.compressobj(9, zlib.DEFLATED, -15, 9, zlib.Z_DEFAULT_STRATEGY,
                             zdict=dictionary)
        packed.append(c.compress(body) + c.flush())

    # --- details.bin --------------------------------------------------------
    details = io.BytesIO()
    details.write(DETAILS_MAGIC)
    details.write(u32(VERSION))
    details.write(u32(len(dictionary)))
    details.write(dictionary)

    offsets = []
    for blob, body in zip(packed, bodies):
        offsets.append((details.tell(), len(blob), len(body)))
        details.write(blob)

    with open(DETAILS, "wb") as f:
        f.write(details.getvalue())

    # --- catalog.bin --------------------------------------------------------
    catalog = io.BytesIO()
    catalog.write(CATALOG_MAGIC)
    catalog.write(u32(VERSION))
    catalog.write(u32(len(rows)))
    catalog.write(u32(len(genre_names)))
    for name in genre_names:
        catalog.write(s16(name))

    for row, (offset, size, raw) in zip(rows, offsets):
        (nsuid, title, sort_title, title_id, min_p, max_p, _note, art,
         _bg, _hl, _desc, _pub, year, _langs, rom_size, _online, _tab, _demo,
         has_russian, mentions, _best_pos, score, is_retro) = row

        catalog.write(s16(nsuid))
        catalog.write(s16(title))
        catalog.write(s16(sort_title))
        catalog.write(s16(title_id))
        catalog.write(s16(art))
        catalog.write(u16(min_p or 0))
        catalog.write(u16(max_p or 0))
        catalog.write(u16(mentions or 0))
        # Счёт согласия ×10. Раньше здесь лежало лучшее место в подборке —
        # оно было вторым ключом сортировки, а теперь весь порядок задаёт счёт.
        catalog.write(u16(min(score or 0, 65535)))
        catalog.write(u16(year or 0))
        # -1 — размер неизвестен: при сортировке такие уходят в конец, а ноль
        # встал бы в начало
        catalog.write(i64(-1 if rom_size is None else rom_size))
        catalog.write(u8((1 if has_russian else 0) | (2 if is_retro else 0)))

        ids = genre_ids.get(nsuid, [])
        catalog.write(u8(len(ids)))
        catalog.write(b"".join(u8(g) for g in ids))

        catalog.write(struct.pack("<Q", offset))
        catalog.write(u32(size))
        catalog.write(u32(raw))

    with open(CATALOG, "wb") as f:
        f.write(catalog.getvalue())

    db.close()

    raw_total = sum(len(b) for b in bodies)
    packed_total = sum(len(b) for b in packed)
    old = os.path.join(OUT_DIR, "catalog.db")
    old_size = os.path.getsize(old) / 1048576 if os.path.exists(old) else 0

    print(f"игр: {len(rows)}, жанров: {len(genre_names)}")
    print(f"catalog.bin: {os.path.getsize(CATALOG) / 1048576:.2f} МБ")
    print(f"details.bin: {os.path.getsize(DETAILS) / 1048576:.2f} МБ "
          f"(из {raw_total / 1048576:.2f} МБ, сжатие {raw_total / packed_total:.2f}x)")
    if old_size:
        total = (os.path.getsize(CATALOG) + os.path.getsize(DETAILS)) / 1048576
        print(f"было catalog.db: {old_size:.2f} МБ, стало {total:.2f} МБ")


if __name__ == "__main__":
    main()
