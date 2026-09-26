"""
Проверяет упакованные catalog.bin и details.bin так, как их прочтёт консоль.

verify_db.py смотрит на catalog.db до упаковки, но приложение базу не видит:
оно читает два двоичных файла, и ошибка упаковщика (сдвиг поля, не тот
словарь, смещения от прошлой сборки) проявится только на консоли — пустым
каталогом или карточкой, которая не открывается. Здесь файлы читаются тем же
порядком, что и в app/source/catalog.cpp (Catalog::loadBriefs и
Catalog::detailsFor), включая сырой inflate с общим словарём, и каждая запись
сверяется с catalog.db.

Если формат в catalog.cpp меняется, этот файл меняется вместе с ним.
"""

import os
import sqlite3
import struct
import sys
import zlib

from paths import ART_DIR, CATALOG_BIN, CATALOG_DB, DETAILS_BIN

CATALOG_MAGIC = b"SSHC"
DETAILS_MAGIC = b"SSHD"
FORMAT_VERSION = 2
MAX_DICT = 1 << 20  # тот же предел, что в loadBriefs

failures = []


def check(label, condition, detail=""):
    mark = "ok  " if condition else "FAIL"
    print(f"  [{mark}] {label}{(' — ' + detail) if detail else ''}")
    if not condition:
        failures.append(label)


class Reader:
    """Как Reader в catalog.cpp, только выход за границу — исключение, а не
    молчаливые нули: здесь нужно знать, где именно файл оборвался."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError(f"выход за конец на смещении {self.pos} (+{n})")
        chunk = self.data[self.pos:self.pos + n]
        self.pos += n
        return chunk

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.take(8))[0]

    def i64(self):
        return struct.unpack("<q", self.take(8))[0]

    def str16(self):
        return self.take(self.u16()).decode("utf-8")

    def str32(self):
        return self.take(self.u32()).decode("utf-8")

    def at_end(self):
        return self.pos == len(self.data)


def read_catalog(data):
    """Catalog::loadBriefs: заголовок, жанры, краткие записи."""
    r = Reader(data)
    if r.take(4) != CATALOG_MAGIC:
        raise ValueError("не та сигнатура")
    version = r.u32()
    if version != FORMAT_VERSION:
        raise ValueError(f"версия {version}, приложение ждёт {FORMAT_VERSION}")
    games, kinds = r.u32(), r.u32()
    names = [r.str16() for _ in range(kinds)]
    briefs = []
    for _ in range(games):
        b = {
            "nsuid": r.str16(), "title": r.str16(), "sort_title": r.str16(),
            "title_id": r.str16(), "box_art": r.str16(),
            "min": r.u16(), "max": r.u16(), "mentions": r.u16(),
            "score": r.u16(), "year": r.u16(), "rom_size": r.i64(),
        }
        flags = r.u8()
        b["has_russian"], b["is_retro"] = bool(flags & 1), bool(flags & 2)
        b["genres"] = [r.u8() for _ in range(r.u8())]
        b["offset"], b["packed"], b["raw"] = r.u64(), r.u32(), r.u32()
        briefs.append(b)
    return names, briefs, r.at_end()


def read_dictionary(data):
    """Голова details.bin, как её читает loadBriefs."""
    if data[:4] != DETAILS_MAGIC:
        raise ValueError("не та сигнатура")
    version, size = struct.unpack("<II", data[4:12])
    if version != FORMAT_VERSION:
        raise ValueError(f"версия {version}, приложение ждёт {FORMAT_VERSION}")
    if not 0 < size <= MAX_DICT:
        raise ValueError(f"словарь {size} байт — приложение его не возьмёт")
    if 12 + size > len(data):
        raise ValueError("словарь длиннее файла")
    return data[12:12 + size]


def inflate(blob, dictionary, raw):
    """Catalog::detailsFor: inflateInit2(-15), inflateSetDictionary, inflate
    с Z_FINISH в буфер ровно raw байт; успех — только Z_STREAM_END."""
    z = zlib.decompressobj(-15, zdict=dictionary)
    body = z.decompress(blob, raw)
    if not z.eof:
        raise ValueError("поток не закончился (не Z_STREAM_END)")
    if z.unconsumed_tail or z.unused_data:
        raise ValueError("после конца потока остались байты")
    if len(body) != raw:
        raise ValueError(f"развернулось {len(body)} байт, ожидалось {raw}")
    return body


def parse_details(body):
    """Разбор записи карточки в том же порядке, что в detailsFor."""
    r = Reader(body)
    d = {"publisher": r.str16(), "languages": r.str16(), "background": r.str16()}
    d["flags"] = r.u8()
    d["note"], d["note_ru"] = r.str16(), r.str16()
    d["headline"], d["headline_ru"] = r.str16(), r.str16()
    d["description"], d["description_ru"] = r.str32(), r.str32()
    d["genres"] = [r.u8() for _ in range(r.u8())]
    d["shots"] = [r.str16() for _ in range(r.u8())]
    d["videos"] = [r.str16() for _ in range(r.u8())]
    if not r.at_end():
        raise ValueError(f"лишние {len(body) - r.pos} байт в конце записи")
    return d


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    for path in (CATALOG_BIN, DETAILS_BIN, CATALOG_DB):
        if not os.path.exists(path):
            print(f"нет {path} — сначала make_ship_data.py", file=sys.stderr)
            return 1

    with open(CATALOG_BIN, "rb") as f:
        catalog = f.read()
    with open(DETAILS_BIN, "rb") as f:
        details = f.read()

    print("catalog.bin читается, как в Catalog::loadBriefs:")
    try:
        names, briefs, clean_end = read_catalog(catalog)
    except (ValueError, UnicodeDecodeError) as e:
        check("файл разобран", False, str(e))
        return report()
    check(f"игр {len(briefs)}, жанров {len(names)}", True)
    check("после последней игры в файле ничего нет", clean_end)

    print("\nСловарь из головы details.bin:")
    try:
        dictionary = read_dictionary(details)
    except (ValueError, struct.error) as e:
        check("словарь прочитан", False, str(e))
        return report()
    check(f"словарь {len(dictionary)} байт", True)

    db = sqlite3.connect(CATALOG_DB)
    rows = {r[0]: r for r in db.execute(
        "SELECT nsuid, title, same_screen_min, same_screen_max, mentions, score,"
        " box_art_file, publisher, description FROM games")}
    ru = {r[0] for r in db.execute(
        "SELECT nsuid FROM translations WHERE coalesce(description_ru, '') != ''")}
    db.close()

    print("\ncatalog.bin совпадает с catalog.db:")
    check(f"игр столько же, сколько в базе ({len(rows)})", len(briefs) == len(rows))
    nsuids = [b["nsuid"] for b in briefs]
    check("nsuid не повторяются", len(set(nsuids)) == len(nsuids))
    check("все игры базы на месте", set(nsuids) == set(rows))
    mismatch = [b["nsuid"] for b in briefs if b["nsuid"] in rows and (
        (b["title"], b["min"], b["max"], b["mentions"], b["score"])
        != tuple(rows[b["nsuid"]][1:6]))]
    check(f"поля сетки совпадают, расхождений {len(mismatch)}", not mismatch,
          ", ".join(mismatch[:5]))
    bad_genre = [b["nsuid"] for b in briefs if any(g >= len(names) for g in b["genres"])]
    check("номера жанров внутри списка", not bad_genre, ", ".join(bad_genre[:5]))
    no_art = [b["nsuid"] for b in briefs if b["box_art"]
              and not os.path.exists(os.path.join(ART_DIR, b["box_art"]))]
    check(f"обложек не хватает: {len(no_art)}", not no_art, ", ".join(no_art[:5]))

    print("\nКаждая карточка разворачивается, как в Catalog::detailsFor:")
    head = 12 + len(dictionary)
    broken, overlap, wrong = [], [], []
    prev_end = head
    for b in sorted(briefs, key=lambda b: b["offset"]):
        start, end = b["offset"], b["offset"] + b["packed"]
        if start < prev_end:
            overlap.append(b["nsuid"])
        prev_end = max(prev_end, end)
        if end > len(details):
            broken.append(f"{b['nsuid']}: за концом файла")
            continue
        try:
            d = parse_details(inflate(details[start:end], dictionary, b["raw"]))
        except (ValueError, UnicodeDecodeError, zlib.error) as e:
            broken.append(f"{b['nsuid']}: {e}")
            continue
        row = rows.get(b["nsuid"])
        if row and ((d["publisher"] or None, d["description"] or None)
                    != (row[7] or None, row[8] or None)
                    or d["genres"] != b["genres"]
                    or bool(d["description_ru"]) != (b["nsuid"] in ru)):
            wrong.append(b["nsuid"])
    check(f"развернулось {len(briefs) - len(broken)} из {len(briefs)}", not broken,
          "; ".join(broken[:3]))
    check("записи не налезают друг на друга и на словарь", not overlap,
          ", ".join(overlap[:5]))
    check("после последней записи в details.bin ничего нет", prev_end == len(details),
          f"{len(details) - prev_end} байт" if prev_end != len(details) else "")
    check(f"тексты совпадают с базой, расхождений {len(wrong)}", not wrong,
          ", ".join(wrong[:5]))

    return report()


def report():
    print()
    if failures:
        print(f"НЕ ПРОШЛО {len(failures)}: " + "; ".join(failures))
        return 1
    print("Упакованные файлы читаются так, как их прочтёт приложение.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
