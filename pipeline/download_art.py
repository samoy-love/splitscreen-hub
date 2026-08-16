"""
Качает обложки игр в app/resources/art/<nsuid>.jpg шириной 240 точек.

240 — ровно столько, сколько плитка занимает на экране в доке: окно 1920×1080
при базе 1280 даёт масштаб 1.5, и 160 логических точек плитки превращаются в
240 физических. Всё, что шире, отбрасывается при выводе.

Cloudinary отдаёт нужный размер трансформацией в URL, но URL бывают двух видов —
/image/upload/... и /image/fetch/... — и подстановка должна учитывать оба. Иначе
трансформация молча игнорируется и качаются полноразмерные файлы по 90 КБ.
"""

import concurrent.futures
import json
import io
import os
import re
import sqlite3
import sys
import threading
import urllib.request

from paths import ART_DIR, CATALOG_DB, LOCAL_MULTIPLAYER

DB = CATALOG_DB
SOURCE = LOCAL_MULTIPLAYER
OUT_DIR = ART_DIR
TRANSFORM = "w_240,q_70,f_jpg"
WORKERS = 8
MIN_BYTES = 500  # меньше — почти наверняка заглушка, а не обложка


def optimize_jpeg(data):
    """Пережимает JPEG без потери качества.

    quality="keep" оставляет исходные таблицы квантования — коэффициенты DCT не
    меняются, картинка получается попиксельно та же. Пересчитываются только
    таблицы Хаффмана, которыми Cloudinary не занимается. На нашем каталоге это
    19% объёма: 53 МБ обложек превращаются в 43 МБ, и ровно на столько же
    худеет .nro.
    """
    try:
        from PIL import Image
    except ImportError:
        return data  # без PIL просто кладём как есть

    try:
        out = io.BytesIO()
        Image.open(io.BytesIO(data)).save(out, "JPEG", quality="keep", optimize=True)
        return out.getvalue() if out.tell() < len(data) else data
    except Exception:  # noqa: BLE001
        return data  # не JPEG или битый файл — пусть решает вызывающий


def art_url(url):
    if "/image/fetch/" in url:
        return re.sub(r"/image/fetch/[^h]*", f"/image/fetch/{TRANSFORM}/", url)
    return re.sub(r"/image/upload/(?:(?!store)[^/]+/)*", f"/image/upload/{TRANSFORM}/", url)


def load_targets():
    """nsuid -> исходный URL обложки, только для игр, попавших в базу."""
    db = sqlite3.connect(DB)
    wanted = {r[0] for r in db.execute("SELECT nsuid FROM games WHERE box_art_file IS NOT NULL")}
    db.close()
    with open(SOURCE, encoding="utf-8") as f:
        return {g["nsuid"]: g["box_art"] for g in json.load(f)
                if g.get("nsuid") in wanted and g.get("box_art")}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    targets = load_targets()

    todo = [(n, u) for n, u in targets.items()
            if not os.path.exists(os.path.join(OUT_DIR, f"{n}.jpg"))]
    print(f"обложек всего {len(targets)}, качаем {len(todo)}")
    if not todo:
        return report(targets)

    lock = threading.Lock()
    done = [0, 0]

    def fetch(item):
        nsuid, url = item
        path = os.path.join(OUT_DIR, f"{nsuid}.jpg")
        ok = False
        for _ in range(3):
            try:
                req = urllib.request.Request(art_url(url), headers={"User-Agent": "Mozilla/5.0"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    data = r.read()
                if len(data) >= MIN_BYTES:
                    tmp = path + ".tmp"
                    with open(tmp, "wb") as f:
                        f.write(optimize_jpeg(data))
                    os.replace(tmp, path)
                    ok = True
                break
            except Exception:  # noqa: BLE001
                continue
        with lock:
            done[0] += 1
            done[1] += not ok
            if done[0] % 200 == 0 or done[0] == len(todo):
                print(f"  {done[0]}/{len(todo)}, не скачалось {done[1]}")

    with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
        list(pool.map(fetch, todo))

    return report(targets)


def missing_titles(targets):
    """nsuid и название тех игр, для которых файла обложки так и нет."""
    gone = [n for n in targets if not os.path.exists(os.path.join(OUT_DIR, f"{n}.jpg"))]
    if not gone:
        return []
    db = sqlite3.connect(DB)
    names = dict(db.execute(
        "SELECT nsuid, title FROM games WHERE nsuid IN (%s)"
        % ",".join("?" * len(gone)), gone))
    db.close()
    return [(n, names.get(n, "?")) for n in gone]


def report(targets):
    files = [f for f in os.listdir(OUT_DIR) if f.endswith(".jpg")]
    total = sum(os.path.getsize(os.path.join(OUT_DIR, f)) for f in files)
    avg = total / len(files) if files else 0
    print(f"\nфайлов: {len(files)} из {len(targets)}")
    print(f"объём: {total / 1024 / 1024:.1f} МБ, средний {avg / 1024:.1f} КБ")
    if avg > 40 * 1024:
        print("ВНИМАНИЕ: средний файл великоват — похоже, трансформация Cloudinary "
              "не применилась и скачаны полноразмерные обложки", file=sys.stderr)

    # Недостача — это список названий и ненулевой код возврата, а не цифра в
    # выводе: иначе игры уезжают в релиз с пустыми плитками незамеченными.
    gone = missing_titles(targets)
    if gone:
        print(f"\nБЕЗ ОБЛОЖКИ: {len(gone)}", file=sys.stderr)
        for nsuid, title in gone:
            print(f"  {nsuid}  {title}", file=sys.stderr)
        print("Повторный запуск скачает только их.", file=sys.stderr)
    return len(gone)


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
