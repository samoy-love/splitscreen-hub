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
#
#   pack_data.sh             собрать архив
#   pack_data.sh --version   напечатать версию бандла — начало sha256 того
#                            же архива, файл не пишется (VERSION_CMD в
#                            .deploy-kit/data.env)
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/app/build/splitscreen-hub-data.tar.gz"
cd "$ROOT"

for f in app/resources/catalog.bin app/resources/details.bin pipeline/translations.db; do
    [ -s "$f" ] || { echo "нет $f — сначала прогоните пайплайн (README, «Данные»)" >&2; exit 1; }
done
[ -n "$(ls app/resources/art 2>/dev/null)" ] || { echo "app/resources/art пуст — запустите pipeline/download_art.py" >&2; exit 1; }

# Детерминированный архив: одинаковые данные — байт в байт одинаковый файл.
# На этом держатся и сверка VERIFY_URL в deploy-kit, и версия бандла.
# Имена по порядку, владелец, права и время — постоянные: иначе архив зависел
# бы от того, на чьей машине и когда скачаны обложки. gzip -n — без имени и
# времени в заголовке; tar -z этого не гарантирует: gzip, читающий из трубы,
# может записать туда время.
# *.tmp — недокачанные обложки download_art.py: в бандл и в romfs им нельзя.
pack() {
    tar --sort=name --format=gnu --owner=0 --group=0 --numeric-owner \
        --mode='a+rX,u+w,go-w' --mtime='2000-01-01 00:00Z' --exclude='*.tmp' \
        -cf - app/resources/art app/resources/catalog.bin app/resources/details.bin \
        pipeline/translations.db \
        | gzip -9 -n
}

if [ "${1:-}" = "--version" ]; then
    # deploy-kit спрашивает версию ДО сборки, поэтому архив собирается здесь
    # ещё раз в трубу. Отпечаток всего архива, а не только catalog.bin и
    # details.bin: смена одних обложек — тоже новые данные и новая версия.
    pack | sha256sum | cut -c1-12
    exit 0
fi

mkdir -p app/build
pack > "$OUT.tmp"
mv -f "$OUT.tmp" "$OUT"
ls -l "$OUT"
