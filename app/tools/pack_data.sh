#!/usr/bin/env bash
# Упаковывает данные каталога в app/build/splitscreen-hub-data.tar.gz —
# артефакт цели .deploy-kit/data.env.
#
# Внутри ровно то, чего нет в git и что нужно сборке .nro и пайплайну:
#   app/resources/art/          обложки 240 px (download_art.py)
#   app/resources/catalog.bin   сетка каталога (make_ship_data.py)
#   app/resources/details.bin   карточки игр (make_ship_data.py)
#   pipeline/translations.db    русские тексты игр
# Пути в архиве — от корня репозитория, чтобы распаковка была одной командой
# и с той стороны (build_release.sh), и на машине другого разработчика.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/app/build/splitscreen-hub-data.tar.gz"
cd "$ROOT"

for f in app/resources/catalog.bin app/resources/details.bin pipeline/translations.db; do
    [ -s "$f" ] || { echo "нет $f — сначала прогоните пайплайн (README, «Данные»)" >&2; exit 1; }
done
[ -n "$(ls app/resources/art 2>/dev/null)" ] || { echo "app/resources/art пуст — запустите pipeline/download_art.py" >&2; exit 1; }

mkdir -p app/build
# Детерминированный архив: одинаковые данные — одинаковая сумма, и
# VERIFY_URL в deploy-kit сверяется без ложных расхождений.
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2000-01-01 00:00Z' \
    -czf "$OUT" app/resources/art app/resources/catalog.bin app/resources/details.bin \
    pipeline/translations.db
ls -l "$OUT"
