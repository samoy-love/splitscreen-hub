"""
Готовит catalog.db для romfs: подставляет переводы прямо в тексты игр и
выбрасывает всё, что нужно только на этапе сборки.

Зачем отдельный шаг. Рабочая `catalog.db` держит английские оригиналы в таблице
games и русские переводы рядом в translations — так удобно и переводчику
(видно, что ещё не переведено), и инструментам. Но в .nro оба языка ехать не
должны: это дубль одного и того же текста, который никто никогда не увидит
по-английски, если перевод есть.

Поэтому здесь перевод вписывается в games поверх оригинала, а таблица
translations удаляется целиком. Английские черновики остаются в репозитории —
в `catalog.db` и `local_multiplayer.json`.

Игры без перевода сохраняют английский текст: показать оригинал лучше, чем
пустую карточку.

Результат: app/resources/catalog.db
"""

import os
import shutil
import sqlite3
import sys

SOURCE = "catalog.db"
TARGET = os.path.join("app", "resources", "catalog.db")

FIELDS = [("headline", "headline_ru"),
          ("description", "description_ru"),
          ("players_note", "players_note_ru")]


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    if not os.path.exists(SOURCE):
        print(f"нет {SOURCE} — сначала build_db.py")
        return 1

    os.makedirs(os.path.dirname(TARGET), exist_ok=True)
    shutil.copyfile(SOURCE, TARGET)

    db = sqlite3.connect(TARGET)
    has_translations = db.execute(
        "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='translations'"
    ).fetchone()[0]

    replaced = {name: 0 for name, _ in FIELDS}
    if has_translations:
        for target_col, source_col in FIELDS:
            cur = db.execute(
                f"UPDATE games SET {target_col} = ("
                f"  SELECT {source_col} FROM translations t WHERE t.nsuid = games.nsuid)"
                f" WHERE nsuid IN (SELECT nsuid FROM translations"
                f"                 WHERE {source_col} IS NOT NULL AND {source_col} != '')")
            replaced[target_col] = cur.rowcount
            cur.close()

        db.execute("DROP TABLE translations")

    db.commit()
    db.execute("VACUUM")
    db.close()

    before = os.path.getsize(SOURCE) / 1024 / 1024
    after = os.path.getsize(TARGET) / 1024 / 1024
    print(f"{TARGET}: {after:.1f} МБ (рабочая база {before:.1f} МБ)")
    for name, n in replaced.items():
        print(f"  переведено {name}: {n}")
    if not has_translations:
        print("  таблицы translations нет — база и так одноязычная")
    return 0


if __name__ == "__main__":
    sys.exit(main())
