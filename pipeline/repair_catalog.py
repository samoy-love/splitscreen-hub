"""
Добирает игры, которые перечисление каталога пропускает.

Проблема: `fetch_catalog()` перебирает индекс пустым запросом, а Algolia в этом
режиме отдаёт не все записи. Diablo III: Eternal Collection под наши фильтры
подходит (метка «Play together on one console», платформа Switch, в продаже),
находится текстовым поиском — но в перечислении её нет. Эндпоинт `browse`,
который снял бы ограничение, закрыт для публичного ключа.

Обнаружилось это только потому, что игра стоит в подборках «лучших couch co-op»
и не сопоставилась с каталогом. Поэтому здесь — добор по списку названий:
что не находится перечислением, ищется поиском поштучно.

Названия берутся из подборок (toplist_sources.py) и из файла extra_titles.txt
рядом со скриптом, если он есть — по строке на название.
"""

import json
import os
import sys

import fetch_local_multiplayer as F
from paths import EXTRA_TITLES, LOCAL_MULTIPLAYER
from toplist_sources import SOURCES, norm

EXTRA_FILE = EXTRA_TITLES
SOURCE = LOCAL_MULTIPLAYER


def wanted_titles():
    titles = []
    for source in SOURCES:
        titles.extend(source["games"])
    try:
        with open(EXTRA_FILE, encoding="utf-8") as f:
            titles.extend(line.strip() for line in f if line.strip())
    except OSError:
        pass
    # порядок не важен, но дубликаты дорого стоят по запросам
    seen, out = set(), []
    for t in titles:
        if norm(t) not in seen:
            seen.add(norm(t))
            out.append(t)
    return out


def search(title):
    """Ищет игру по названию под теми же фильтрами, что и основной сбор."""
    res = F.algolia({
        "query": title,
        "hitsPerPage": 5,
        "facetFilters": F.FACET_FILTERS,
        "attributesToRetrieve": F.ATTRS,
    })
    for hit in res["hits"]:
        if norm(hit["title"]) == norm(title):
            return hit
    return None


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    with open(SOURCE, encoding="utf-8") as f:
        games = json.load(f)
    have = {g.get("nsuid") for g in games}

    found = []
    for title in wanted_titles():
        hit = search(title)
        if hit and hit.get("nsuid") and hit["nsuid"] not in have:
            found.append(hit)
            have.add(hit["nsuid"])
            print(f"  добавляем: {hit['title'][:46]}")

    if not found:
        print("перечисление ничего не пропустило")
        return

    print(f"\nнедостающих игр: {len(found)}, забираем карточки")
    products, failed = F.fetch_products([h["nsuid"] for h in found], batch=25)
    F.fetch_missing_from_pages(found, products, failed)

    rows = F.build_rows(found, products, failed)
    games.extend(rows)
    with open(SOURCE, "w", encoding="utf-8") as f:
        json.dump(games, f, ensure_ascii=False, indent=1)

    known = [r for r in rows if r["same_screen_max"]]
    print(f"\nдописано в {os.path.basename(SOURCE)}: {len(rows)}, из них с числом игроков {len(known)}")
    for r in known:
        print(f"  {r['same_screen_min']}-{r['same_screen_max']}  {r['title'][:46]}")


if __name__ == "__main__":
    main()
