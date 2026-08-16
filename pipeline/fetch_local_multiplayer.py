"""
Собирает все игры Nintendo Switch (без Switch 2) с мультиплеером "на одном
экране" и всю полезную для каталога информацию: точное число игроков на одной
консоли, описания, скриншоты, видео, Title ID, размер ромa, языки и т.д.

Источники (оба публичные, используются самим nintendo.com):
  1. Algolia index `store_game_en_us` — каталог eShop US. Фасет
     waysToPlayLabels = "Play together on one console" = игра на одном экране.
     Отсюда же жанры, теги, цена.
  2. graph.nintendo.com (GraphQL) — карточка товара. Обязателен заголовок
     `apollographql-client-name: ncom`. Аргумент — OneOf-тип ProductInput,
     ровно один ключ: sku | nsuid | urlKey (по nsuid доступно заметно больше
     товаров, чем по sku). Ключевое поле — numberOfPlayers:
       .system = игроков на ОДНОЙ консоли  <- это и есть "один экран"
       .local  = локальный беспроводной на нескольких консолях
       .online = онлайн

Результат: local_multiplayer.json + local_multiplayer.csv рядом со скриптом
(pipeline/); карточки товаров кэшируются там же в products_cache.json.
"""

import concurrent.futures
import csv
import json
import os
import re
import threading
import time
import urllib.error
import urllib.request

from paths import LOCAL_MULTIPLAYER, LOCAL_MULTIPLAYER_CSV, PRODUCTS_CACHE

# Публичная search-only пара из фронтенда nintendo.com: ею пользуется сам
# сайт, эндпоинт browse для неё закрыт.
ALGOLIA_APP = "U3B6GR4UA3"
ALGOLIA_KEY = "a29c6927638bfd8cee23993e51e721c9"
GRAPH_URL = "https://graph.nintendo.com/"

ONE_CONSOLE = "Play together on one console"
# только Nintendo Switch (без Switch 2)
PLATFORMS = ["Nintendo Switch"]


def post(url, payload, headers):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers=headers, method="POST"
    )
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def algolia(payload, index="store_game_en_us"):
    return post(
        f"https://{ALGOLIA_APP.lower()}-dsn.algolia.net/1/indexes/{index}/query",
        payload,
        {
            "X-Algolia-API-Key": ALGOLIA_KEY,
            "X-Algolia-Application-Id": ALGOLIA_APP,
            "Content-Type": "application/json",
        },
    )


def graphql(query):
    return post(
        GRAPH_URL,
        {"query": query},
        {
            "Content-Type": "application/json",
            "apollographql-client-name": "ncom",
            "apollographql-client-version": "1.0.0",
            "Origin": "https://www.nintendo.com",
        },
    )


# --------------------------------------------------------------------------
# 1. Каталог из Algolia
# --------------------------------------------------------------------------

FACET_FILTERS = [
    [f"waysToPlayLabels:{ONE_CONSOLE}"],
    [f"corePlatforms:{p}" for p in PLATFORMS],
]
ATTRS = [
    "sku", "nsuid", "title", "urlKey", "url", "corePlatforms", "playerCount",
    "releaseDate", "softwarePublisher", "softwareDeveloper",
    "waysToPlayLabels", "gameGenreLabels", "gameFeatureLabels", "nsoFeatures",
    "playModes", "contentRatingCode", "availability", "price", "editions",
    "demoNsuid", "topLevelFilters",
    # ранжирование eShop по умолчанию — featuredProduct, затем дата выхода
    "featuredProduct",
]


def _search(index="store_game_en_us", numeric=None, page=0):
    payload = {
        "query": "",
        "hitsPerPage": 1000,
        "page": page,
        "facetFilters": FACET_FILTERS,
        "attributesToRetrieve": ATTRS,
    }
    if numeric:
        payload["numericFilters"] = numeric
    return algolia(payload, index)


def _collect(out, index="store_game_en_us", numeric=None):
    """Забирает все страницы среза (Algolia отдаёт максимум 1000 хитов)."""
    page = 0
    while True:
        res = _search(index, numeric, page)
        for h in res["hits"]:
            out[h["sku"]] = h
        page += 1
        if page >= res["nbPages"]:
            return res["nbHits"]


def _slice_by_price(out, lo, hi):
    """Рекурсивно режем по цене, пока срез не влезет в лимит Algolia (1000)."""
    numeric = [f"price.finalPrice>={lo}"] + ([f"price.finalPrice<{hi}"] if hi else [])
    n = _search(numeric=numeric)["nbHits"]
    if n == 0:
        return
    if n <= 1000 or (hi is not None and hi - lo < 0.02):
        _collect(out, numeric=numeric)
        return
    mid = round((lo + hi) / 2, 2) if hi is not None else lo + 20
    _slice_by_price(out, lo, mid)
    _slice_by_price(out, mid, hi)


def fetch_catalog():
    """Все игры Switch с меткой 'Play together on one console'."""
    total = _search()["nbHits"]
    out = {}
    _slice_by_price(out, 0, 200)
    _slice_by_price(out, 200, None)
    # товары без цены не попадают в numericFilters — добираем сортированными репликами
    for idx in ("store_game_en_us_title_asc", "store_game_en_us_title_des",
                "store_game_en_us_release_des"):
        _collect(out, index=idx)
    print(f"  каталог: собрано {len(out)} из {total}")
    return list(out.values())


# --------------------------------------------------------------------------
# 2. Карточки товаров из GraphQL
# --------------------------------------------------------------------------

PRODUCT_FIELDS = """
 nsuid sku applicationId productCode name headline
 description(html:false)
 playerCountDescription
 numberOfPlayers{ system{min max} local{min max} online{min max} }
 supportedLanguages backgroundColor officialSite
 releaseDate releaseDateDisplay availability
 softwarePublisher softwareDeveloper
 platform{code label}
 playModes{code label}
 nsoFeatures{code label}
 contentRating{code label} contentDescriptors{label type}
 softwareDetails{ romSizes{platform totalRomSize estimatedRomSize} rights{titleId} }
 compatibility{status caption}
 productImage(shape:"square"){url}
 productGallery{ url resourceType }
 demoNsuid
"""
PRODUCTS_QUERY = "{ products(input:{nsuids:[%s]}) {" + PRODUCT_FIELDS + "} }"

MAX_RETRIES = 3
SPLIT_THRESHOLD = 8   # ниже этого размера батч разбираем поштучно
WORKERS = 8           # параллельных запросов к graph.nintendo.com


def _fetch(nsuids):
    """Возвращает (список товаров, код ошибки|None)."""
    q = PRODUCTS_QUERY % ",".join(json.dumps(n) for n in nsuids)
    try:
        res = graphql(q)
    except urllib.error.HTTPError as e:
        return None, f"HTTP {e.code}"
    except Exception as e:  # noqa: BLE001
        return None, f"{type(e).__name__}"
    if res.get("errors"):
        codes = {(e.get("extensions") or {}).get("code", "ERROR")
                 for e in res["errors"] if isinstance(e, dict)}
        return None, ",".join(sorted(codes)) or "ERROR"
    return (res.get("data") or {}).get("products") or [], None


# UNAUTHORIZED — детерминированный ответ, ретраить его бессмысленно.
# Ретраим только транзиентное: 5xx, таймауты, обрывы соединения.
PERMANENT_ERRORS = ("UNAUTHORIZED", "GRAPHQL_VALIDATION_FAILED")


def _query_chunk(chunk, out, failed):
    """Один недоступный товар валит весь запрос, поэтому падение батча
    разбираем поштучно."""
    products, err = _fetch(chunk)
    if err is None:
        for p in products:
            out[p["nsuid"]] = p
        return
    if len(chunk) > SPLIT_THRESHOLD:
        mid = len(chunk) // 2
        _query_chunk(chunk[:mid], out, failed)
        _query_chunk(chunk[mid:], out, failed)
        return
    if len(chunk) > 1:
        # на коротком хвосте деление пополам дороже прямого перебора
        for nsuid in chunk:
            _query_chunk([nsuid], out, failed)
        return

    for attempt in range(MAX_RETRIES):
        if err in PERMANENT_ERRORS:
            break
        time.sleep(0.5 * (attempt + 1))
        products, err = _fetch(chunk)
        if err is None:
            for p in products:
                out[p["nsuid"]] = p
            return
    failed[chunk[0]] = err


# --------------------------------------------------------------------------
# 2b. Запасной путь: страница товара
# --------------------------------------------------------------------------
# Около половины каталога GraphQL анонимно не отдаёт (UNAUTHORIZED), причём
# это не связано ни с доступностью, ни с ценой. Зато страница товара
# отдаётся обычным GET (нужны полные браузерные заголовки, иначе 406), а в её
# __NEXT_DATA__ лежит тот же объект Product со всеми полями.

BROWSER_HEADERS = {
    "User-Agent": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
                   " (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36"),
    "Accept": ("text/html,application/xhtml+xml,application/xml;q=0.9,"
               "image/avif,image/webp,*/*;q=0.8"),
    "Accept-Language": "en-US,en;q=0.9",
    "sec-ch-ua": '"Chromium";v="140", "Not=A?Brand";v="24"',
    "sec-ch-ua-mobile": "?0",
    "sec-ch-ua-platform": '"Windows"',
    "Sec-Fetch-Dest": "document",
    "Sec-Fetch-Mode": "navigate",
    "Sec-Fetch-Site": "none",
    "Upgrade-Insecure-Requests": "1",
}
NEXT_DATA_RE = re.compile(
    r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', re.S)
CLOUDINARY = "https://assets.nintendo.com/{kind}/upload/{pid}"


def _find_product(node):
    """Ищет в дереве объект Product (у него есть и numberOfPlayers, и nsuid)."""
    if isinstance(node, dict):
        if "numberOfPlayers" in node and "applicationId" in node:
            return node
        for v in node.values():
            found = _find_product(v)
            if found:
                return found
    elif isinstance(node, list):
        for v in node:
            found = _find_product(v)
            if found:
                return found
    return None


def _normalize_page_product(p):
    """Приводит объект со страницы к той же форме, что отдаёт GraphQL."""
    gallery = []
    for a in p.get("productGallery") or []:
        kind = "video" if a.get("resourceType") == "video" else "image"
        pid = (a.get("publicId") or "").lstrip("/")
        url = CLOUDINARY.format(kind=kind, pid=pid) + (".mp4" if kind == "video" else "")
        gallery.append({"url": url, "resourceType": a.get("resourceType")})
    p["productGallery"] = gallery
    # в Apollo-кэше аргументы попадают прямо в имя ключа
    for key, plain in (('productImage({"shape":"square"})', "productImage"),
                       ('description({"html":true})', "description")):
        if key in p and not p.get(plain):
            p[plain] = p[key]
    if isinstance(p.get("description"), str):
        p["description"] = re.sub(r"<[^>]+>", "", p["description"])
    return p


def fetch_from_page(url):
    req = urllib.request.Request(url, headers=BROWSER_HEADERS)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            html = r.read().decode("utf-8", "replace")
    except Exception:  # noqa: BLE001
        return None
    m = NEXT_DATA_RE.search(html)
    if not m:
        return None
    try:
        p = _find_product(json.loads(m.group(1)))
    except ValueError:
        return None
    return _normalize_page_product(p) if p else None


CACHE_FILE = PRODUCTS_CACHE


def _load_cache():
    try:
        with open(CACHE_FILE, encoding="utf-8") as f:
            c = json.load(f)
        return c.get("products", {}), c.get("failed", {})
    except (OSError, ValueError):
        return {}, {}


def _save_cache(out, failed):
    tmp = CACHE_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"products": out, "failed": failed}, f, ensure_ascii=False)
    os.replace(tmp, CACHE_FILE)


def fetch_products(nsuids, batch=50, use_cache=True):
    """Кэширует карточки на диск, чтобы перезапуск продолжал с места остановки."""
    out, failed = _load_cache() if use_cache else ({}, {})
    if out or failed:
        print(f"  из кэша: {len(out)} карточек, {len(failed)} недоступных")
    todo = [n for n in nsuids if n not in out and n not in failed]
    chunks = [todo[i : i + batch] for i in range(0, len(todo), batch)]
    lock = threading.Lock()
    done = 0

    def worker(chunk):
        nonlocal done
        local, local_failed = {}, {}
        _query_chunk(chunk, local, local_failed)
        with lock:
            out.update(local)
            failed.update(local_failed)
            done += len(chunk)
            print(f"  {done}/{len(todo)} -> {len(out)}")
            if use_cache:
                _save_cache(out, failed)

    with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
        list(pool.map(worker, chunks))
    return out, failed


def fetch_missing_from_pages(games, out, failed, use_cache=True):
    """Добирает страницами то, что GraphQL не отдал."""
    todo = [g for g in games if g.get("nsuid") in failed]
    if not todo:
        return
    print(f"  запасной путь: {len(todo)} страниц")
    lock = threading.Lock()
    done = [0]

    def worker(g):
        p = fetch_from_page("https://www.nintendo.com" + (g.get("url") or ""))
        with lock:
            done[0] += 1
            if p:
                p.setdefault("nsuid", g["nsuid"])
                out[g["nsuid"]] = p
                failed.pop(g["nsuid"], None)
            if done[0] % 100 == 0:
                print(f"  {done[0]}/{len(todo)} -> {len(out)}")
                if use_cache:
                    _save_cache(out, failed)

    with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
        list(pool.map(worker, todo))
    if use_cache:
        _save_cache(out, failed)


# --------------------------------------------------------------------------
# 3. Склейка
# --------------------------------------------------------------------------

def _labels(items):
    # в данных со страницы вложенные объекты могут быть ссылками Apollo (__ref)
    return ", ".join(i["label"] for i in items or [] if isinstance(i, dict) and i.get("label"))


def build_rows(games, products, failed):
    rows = []
    for g in games:
        p = products.get(g.get("nsuid")) or {}
        npl = p.get("numberOfPlayers") or {}
        sysc = npl.get("system") or {}
        gallery = p.get("productGallery") or []
        details = p.get("softwareDetails") or {}
        rom = {r["platform"]: r for r in details.get("romSizes") or []}
        hac = rom.get("HAC") or {}
        rights = (details.get("rights") or [{}])[0]

        rows.append({
            # идентификаторы
            "nsuid": g.get("nsuid"),
            "sku": g["sku"],
            # Title ID — по нему homebrew может сопоставить установленные игры
            "title_id": p.get("applicationId") or rights.get("titleId"),
            "product_code": p.get("productCode"),
            "title": p.get("name") or g["title"],
            # игроки
            "same_screen_min": sysc.get("min"),
            "same_screen_max": sysc.get("max"),
            "local_wireless_max": (npl.get("local") or {}).get("max"),
            "online_max": (npl.get("online") or {}).get("max"),
            "player_bucket": g.get("playerCount"),
            "players_note": p.get("playerCountDescription"),
            # описания
            "headline": p.get("headline"),
            "description": p.get("description"),
            # классификация
            "genres": ", ".join(g.get("gameGenreLabels") or []),
            "features": ", ".join(g.get("gameFeatureLabels") or []),
            "ways_to_play": ", ".join(g.get("waysToPlayLabels") or []),
            # атрибут franchises в индексе пустой у всех игр, полезное — в topLevelFilters
            "store_tags": ", ".join(g.get("topLevelFilters") or []),
            "play_modes": _labels(p.get("playModes")) or ", ".join(g.get("playModes") or []),
            "nso_features": _labels(p.get("nsoFeatures")) or ", ".join(g.get("nsoFeatures") or []),
            "esrb": (p.get("contentRating") or {}).get("label") or g.get("contentRatingCode"),
            "esrb_descriptors": _labels(p.get("contentDescriptors")),
            # издатель / даты
            "publisher": p.get("softwarePublisher") or g.get("softwarePublisher"),
            "developer": p.get("softwareDeveloper") or g.get("softwareDeveloper"),
            "release": g.get("releaseDate") or p.get("releaseDate"),
            "availability": ", ".join(g.get("availability") or []),
            "price": (g.get("price") or {}).get("finalPrice"),
            # hasDlc в индексе всегда false — реальный признак лежит в topLevelFilters
            "has_dlc": "Games with DLC" in (g.get("topLevelFilters") or []),
            "featured": bool(g.get("featuredProduct")),
            "demo_nsuid": p.get("demoNsuid") or g.get("demoNsuid"),
            # техданные, полезные на самой консоли
            "rom_size_bytes": int(hac["totalRomSize"]) if hac.get("totalRomSize") else None,
            "languages": ", ".join(p.get("supportedLanguages") or []),
            "switch2_compat": (p.get("compatibility") or {}).get("status"),
            # медиа
            "background_color": p.get("backgroundColor"),
            "box_art": (p.get("productImage") or {}).get("url"),
            "images": [a["url"] for a in gallery if a.get("resourceType") == "image"],
            "videos": [a["url"] for a in gallery if a.get("resourceType") == "video"],
            # ссылки
            "official_site": p.get("officialSite"),
            "url": "https://www.nintendo.com" + (g.get("url") or ""),
            "fetch_error": failed.get(g.get("nsuid")),
        })

    rows.sort(key=lambda r: (-(r["same_screen_max"] or 0), r["title"]))
    return rows


LIST_FIELDS = ("images", "videos")


OUTPUTS = (LOCAL_MULTIPLAYER, LOCAL_MULTIPLAYER_CSV)


def main():
    # сносим прошлый результат сразу, иначе устаревшие файлы весь прогон
    # выглядят как актуальные (пишем-то мы их только в самом конце)
    for path in OUTPUTS:
        if os.path.exists(path):
            os.remove(path)

    games = fetch_catalog()
    nsuids = [g["nsuid"] for g in games if g.get("nsuid")]
    print(f"Игр с мультиплеером на одном экране: {len(games)} (с nsuid: {len(nsuids)})")

    products, failed = fetch_products(nsuids)
    fetch_missing_from_pages(games, products, failed)
    rows = build_rows(games, products, failed)

    with open(LOCAL_MULTIPLAYER, "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=1)
    with open(LOCAL_MULTIPLAYER_CSV, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        for r in rows:
            w.writerow({**r, **{k: " | ".join(r[k]) for k in LIST_FIELDS}})

    known = sum(1 for r in rows if r["same_screen_max"])
    print(f"Сохранено {len(rows)} игр, точное число игроков у {known}")
    reasons = {}
    for r in rows:
        if not r["same_screen_max"]:
            key = r["fetch_error"] or (
                "no nsuid" if not r["nsuid"] else "numberOfPlayers.system=null")
            reasons[key] = reasons.get(key, 0) + 1
    for k, v in sorted(reasons.items(), key=lambda kv: -kv[1]):
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()
