"""
Где лежат файлы пайплайна. Все пути абсолютные и считаются от этой папки,
чтобы скрипты одинаково работали из корня репозитория и из pipeline/.

Данные пайплайна (json, базы, кэш) живут рядом со скриптами; в app/resources
уезжает только то, что читает приложение.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))   # pipeline/
ROOT = os.path.dirname(HERE)                        # корень репозитория

# результат fetch_local_multiplayer.py
LOCAL_MULTIPLAYER = os.path.join(HERE, "local_multiplayer.json")
LOCAL_MULTIPLAYER_CSV = os.path.join(HERE, "local_multiplayer.csv")
PRODUCTS_CACHE = os.path.join(HERE, "products_cache.json")
# названия для добора repair_catalog.py, по строке на игру (может не быть)
EXTRA_TITLES = os.path.join(HERE, "extra_titles.txt")

# ручные правки и переводы — коммитятся
OVERRIDES = os.path.join(HERE, "overrides.json")
TRANSLATIONS_DB = os.path.join(HERE, "translations.db")

# базы, которые пересобираются скриптами
CATALOG_DB = os.path.join(HERE, "catalog.db")
TOPLISTS_DB = os.path.join(HERE, "toplists.db")

# то, что читает приложение
RESOURCES_DIR = os.path.join(ROOT, "app", "resources")
ART_DIR = os.path.join(RESOURCES_DIR, "art")
CATALOG_BIN = os.path.join(RESOURCES_DIR, "catalog.bin")
DETAILS_BIN = os.path.join(RESOURCES_DIR, "details.bin")
