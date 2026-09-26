#!/bin/bash
# Собирает урезанный FFmpeg для Switch: только то, что нужно плееру трейлеров.
#
# Пакет switch-ffmpeg из pacman собран «на всё»: реестр кодеков ссылается на
# каждый декодер, и линкер тянет их целиком — около 16 МБ в .nro при том, что
# трейлеры Cloudinary это ровно h264 + aac в mp4.
#
# Флаги кросс-компиляции взяты из PKGBUILD devkitPro (switch/ffmpeg), чтобы
# ABI совпадал с остальными portlibs. Отличия от него, ради размера:
#   --disable-everything и точечный список нужных компонентов
#   --disable-network        — файл качает curl, ffmpeg сеть не нужна
#   --disable-libass/-freetype/-fribidi — субтитров нет
#   --disable-libdav1d       — AV1 в трейлерах не встречается
# Аппаратное декодирование (--enable-nvtegra) сохраняем: оно из патча
# devkitPro и заметно разгружает процессор.
set -e

# Точечный релиз ветки 7.1: они выходят ради исправлений безопасности, а
# FFmpeg здесь разбирает mp4/h264/aac, пришедшие из сети.
#
# Выше ветки 7.1 не подняться в отрыве от devkitPro: сборка накладывает их
# патч, и именно он даёт --enable-nvtegra, то есть аппаратное декодирование.
# Патч называется по ветке (ffmpeg-7.1.patch) и ложится на любой 7.1.x со
# смещениями строк; для 8.x его нет. Поэтому версия исходников и версия
# патча — разные переменные: точечный релиз меняет только VER.
VER=7.1.5
PATCH_VER=7.1
APP="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$APP/build-ffmpeg"
PREFIX="$APP/lib/ffmpeg-slim"

# Патчи берём с зафиксированного коммита devkitPro/pacman-packages, а не с
# master: сборка накладывает на исходники чужой код, и правка в их ветке не
# должна молча попадать в .nro. Новый коммит — осознанная правка этой строки.
DKP_REV=5cf3a4da7665a04032565a3e788656cb485dd279
BASE="https://raw.githubusercontent.com/devkitPro/pacman-packages/$DKP_REV/switch/ffmpeg"

# Суммы сверяются после каждой загрузки и перед распаковкой: подмена на
# зеркале, в кеше или посреди пути ломает сборку, а не попадает в .nro.
# Архив сверен с подписью ffmpeg.org (ключ FFmpeg release signing key,
# FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8); патчи совпадают с
# sha256sums в PKGBUILD devkitPro на том же коммите.
SHA256_TARBALL=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
SHA256_PATCH=1792380b992e3554a4abcddf0d7b395bfd8c118ac7c6e38c8f2fb0d39753a390
SHA256_TLS=57ea7ec8ed26d13d3172d0fd589b7883d22f0c180d50b7434fcc73fb2b3ab7d7

# Качает файл, если его ещё нет, и сверяет сумму. Несовпавший файл удаляется:
# иначе он так и лежал бы в build-ffmpeg и валил каждую следующую сборку.
# -f у curl обязателен: без него 404 молча ложится в файл как HTML.
fetch() {
  local file="$1" url="$2" want="$3" got
  [ -f "$file" ] || curl -fsSL --retry 3 --connect-timeout 20 -o "$file" "$url"
  got="$(sha256sum "$file" | cut -d' ' -f1)"
  if [ "$got" != "$want" ]; then
    rm -f "$file"
    echo "$file: sha256 не совпала — ждали $want, получили $got" >&2
    exit 1
  fi
}

mkdir -p "$WORK"
cd "$WORK"

# На раннере скачивает build_release.sh — с хоста, вне контейнера, вызовом
# этого же скрипта с --fetch-only. Адреса и суммы так живут в одном месте.
fetch "ffmpeg-$VER.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz" "$SHA256_TARBALL"
fetch "ffmpeg-$PATCH_VER.patch" "$BASE/ffmpeg-$PATCH_VER.patch" "$SHA256_PATCH"
fetch "tls.patch" "$BASE/tls.patch" "$SHA256_TLS"
[ "${1:-}" = "--fetch-only" ] && exit 0

if [ ! -d "ffmpeg-$VER" ]; then
  tar xf "ffmpeg-$VER.tar.xz"
  cd "ffmpeg-$VER"
  patch -Np1 -i "../ffmpeg-$PATCH_VER.patch"
  patch -Np1 -i "../tls.patch"
  cd ..
fi

cd "ffmpeg-$VER"
source "${DEVKITPRO:-/opt/devkitpro}/switchvars.sh"

if [ ! -f config.h ]; then
  # Без --enable-gpl: ни один включённый ниже компонент (h264, aac, mp3,
  # pcm_s16le, swscale, swresample) не является GPL-only, а флаг переводил бы
  # статически слинкованные libav* в GPLv2+ и тянул бы это на весь .nro.
  ./configure --prefix="$PREFIX" --disable-shared --enable-static \
    --cross-prefix=aarch64-none-elf- --enable-cross-compile \
    --arch=aarch64 --cpu=cortex-a57 --target-os=horizon --enable-pic \
    --extra-cflags='-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' \
    --extra-ldflags="-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib" \
    --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc --disable-autodetect \
    --enable-asm --enable-neon \
    --disable-everything \
    --disable-avdevice --disable-avfilter --disable-postproc --disable-network \
    --enable-swscale --enable-swresample \
    --enable-decoder=h264,aac,aac_latm,mp3,pcm_s16le \
    --enable-parser=h264,aac,mpegaudio \
    --enable-demuxer=mov,mp4,m4v,h264,aac,mp3,matroska \
    --enable-protocol=file \
    --enable-libnx --enable-nvtegra
fi

make -j"$(nproc)"
make install

echo
echo "готово, библиотеки в $PREFIX/lib:"
ls -l "$PREFIX/lib"/*.a | awk '{printf "  %-16s %6.1f МБ\n", $NF, $5/1048576}'
