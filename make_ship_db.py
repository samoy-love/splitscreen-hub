"""
Готовит catalog.db для romfs: оставляет оба языка и выбрасывает всё, что нужно
только на этапе сборки.

Раньше здесь перевод вписывался поверх оригинала, а таблица translations
удалялась: интерфейс был русским, и второй язык считался мёртвым весом. Теперь
язык переключается в самом приложении, поэтому в .nro едут оба текста — русский
в translations, английский в games.

Стоит это 6.4 МБ описаний, то есть около 5 МБ после сжатия базы. Плата за то,
что каталог читается на обоих языках без пересборки.

Игры без перевода показывают английский: в translations у них пусто, и
приложение само откатывается к оригиналу.

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
        # Пустые переводы выкидываем: приложение отличает «нет перевода» от
        # «перевод есть» по наличию строки, и пустая строка сбивала бы его на
        # пустую карточку вместо английского оригинала.
        for _, source_col in FIELDS:
            db.execute(f"UPDATE translations SET {source_col} = NULL"
                       f" WHERE {source_col} = ''")
        db.execute("DELETE FROM translations WHERE headline_ru IS NULL"
                   " AND description_ru IS NULL AND players_note_ru IS NULL")

        for target_col, source_col in FIELDS:
            replaced[target_col] = db.execute(
                f"SELECT count(*) FROM translations WHERE {source_col} IS NOT NULL"
            ).fetchone()[0]

        # Индекс под соединение: перевод подмешивается к каждой выборке каталога.
        db.execute("CREATE INDEX IF NOT EXISTS idx_tr ON translations(nsuid)")

    # Источник оценок так и не появился, таблица пуста, и приложение к ней
    # больше не обращается — в релизной базе ей делать нечего.
    db.execute("DROP TABLE IF EXISTS ratings")

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
