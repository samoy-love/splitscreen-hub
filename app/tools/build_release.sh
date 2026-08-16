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
set -Eeuo pipefail

APP="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="devkitpro/devkita64:latest"

build_native() {
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    cd "$APP"
    git -C "$APP/.." submodule update --init --recursive
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
    docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" bash -c '
        set -e
        git config --global --add safe.directory "*"
        apt-get update -qq && apt-get install -y -qq ninja-build make patch xz-utils curl >/dev/null
        dkp-pacman -Syu --noconfirm --needed switch-curl switch-mbedtls switch-sdl2 switch-zlib switch-bzip2
        bash app/tools/build_release.sh
    '
    if command -v id >/dev/null 2>&1; then
        docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" chown -R "$(id -u):$(id -g)" app/build app/lib || true
    fi
else
    echo "нет ни devkitPro, ни docker — собирать нечем" >&2
    exit 1
fi
