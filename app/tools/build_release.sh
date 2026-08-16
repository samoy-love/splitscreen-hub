#!/usr/bin/env bash
# Собирает app/build/SplitScreenHub.nro одной командой — так, чтобы одна и та
# же строка BUILD_CMD в .deploy-kit/nro.env работала и на раннере CI, и на
# машине разработчика.
#
# Три окружения, которые скрипт различает сам:
#
#   * внутри контейнера devkitPro (или на Linux с devkitPro в /opt/devkitpro) —
#     собирает напрямую;
#   * на Windows с devkitPro — перезапускает себя в msys2, который идёт в
#     комплекте: только там cmake видит тулчейн (см. README, «Сборка»);
#   * иначе, если есть docker (ubuntu-раннер GitHub Actions), — поднимает
#     контейнер devkitpro/devkita64 и вызывает себя же внутри него.
#
# FFmpeg собирается на месте, если lib/ffmpeg-slim ещё нет: в контейнере это
# несколько минут, зато .nro на выходе ровно тот, что и локально.
#
# Данные каталога (обложки, catalog.bin, details.bin) в git не лежат — это
# материалы издателей, см. .gitignore. Если их нет на месте, сборка скачивает
# бандл с сервера выкатки (его публикует цель .deploy-kit/data.env) и сверяет
# с опубликованной рядом суммой; на раннере это единственный источник.
set -Eeuo pipefail

APP="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="devkitpro/devkita64:latest"
DATA_URL="${DATA_URL:-https://samoy.love/splitscreen-hub/splitscreen-hub-data.tar.gz}"

# Скачанные бандлы лежат в app/build/data-cache/<sha256>.tar.gz: на раннере
# этот каталог (вместе с lib/ffmpeg-slim) сохраняет кеш пайплайна
# (CACHE_PATHS в .deploy-kit/nro.env), и при неизменных данных сборка не
# ходит за 46 МБ вовсе — только за суммой.
DATA_CACHE="$APP/build/data-cache"

fetch_data() {
    [ -s "$APP/resources/catalog.bin" ] && [ -s "$APP/resources/details.bin" ] \
        && [ -n "$(ls "$APP/resources/art" 2>/dev/null)" ] && return 0
    echo "данных каталога нет в дереве — беру бандл $DATA_URL"
    local want
    want="$(curl -fsSL --retry 3 "$DATA_URL.sha256" | awk '{print tolower($1); exit}')"
    [ -n "$want" ] || { echo "сервер не отдал сумму бандла данных" >&2; exit 1; }
    mkdir -p "$DATA_CACHE"
    local tgz="$DATA_CACHE/$want.tar.gz"
    if [ -s "$tgz" ]; then
        echo "бандл $want есть в кеше"
    else
        curl -fsSL --retry 3 -o "$tgz.part" "$DATA_URL"
        local got; got="$(sha256sum "$tgz.part" | cut -d' ' -f1)"
        [ "$want" = "$got" ] || { rm -f "$tgz.part"; echo "бандл данных не совпал с суммой: ждали $want, получили $got" >&2; exit 1; }
        mv "$tgz.part" "$tgz"
        # Старые бандлы в кеше не нужны: ключ кеша всё равно один на цель.
        find "$DATA_CACHE" -name '*.tar.gz' ! -name "$want.tar.gz" -delete
    fi
    tar -xzf "$tgz" -C "$APP/.."
}

build_native() {
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    cd "$APP"
    git -C "$APP/.." submodule update --init --recursive
    fetch_data
    if [ ! -f lib/ffmpeg-slim/lib/libavcodec.a ]; then
        bash tools/build_ffmpeg_slim.sh
    fi
    cmake -B build -G Ninja -DPLATFORM_SWITCH=ON -DUSE_SDL2=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target SplitScreenHub.nro
    ls -l build/SplitScreenHub.nro
}

if [ -d "${DEVKITPRO:-/opt/devkitpro}/devkitA64" ] && command -v cmake >/dev/null 2>&1; then
    build_native
elif [ -x /c/devkitPro/msys2/usr/bin/bash ]; then
    exec /c/devkitPro/msys2/usr/bin/bash -lc "export DEVKITPRO=/opt/devkitpro; bash '$(cygpath -u "$APP" 2>/dev/null || echo "$APP")/tools/build_release.sh'"
elif command -v docker >/dev/null 2>&1; then
    # Контейнер пишет в каталог репозитория; на раннере он и так наш, а
    # владельца файлов возвращаем себе после сборки.
    ROOT="$(cd "$APP/.." && pwd)"
    #
    # Portlibs (curl, mbedtls, SDL2, zlib, bzip2) в образе уже есть — он
    # ставит группу switch-portlibs целиком. dkp-pacman отсюда не зовём:
    # pkg.devkitpro.org отвечает CI-раннерам 403, ради чего образы и сделаны.
    docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" bash -c '
        set -e
        git config --global --add safe.directory "*"
        apt-get update -qq && apt-get install -y -qq ninja-build make patch xz-utils curl >/dev/null
        bash app/tools/build_release.sh
    '
    if command -v id >/dev/null 2>&1; then
        docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" chown -R "$(id -u):$(id -g)" app/build app/lib || true
    fi
else
    echo "нет ни devkitPro, ни docker — собирать нечем" >&2
    exit 1
fi
