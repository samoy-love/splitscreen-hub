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

# Версию не поднять в отрыве от devkitPro: сборка накладывает их патч
# ffmpeg-$VER.patch, а он существует только для 7.1 — для 7.1.2 и 8.x его нет.
# Именно этот патч даёт --enable-nvtegra, то есть аппаратное декодирование.
# Обновляться имеет смысл тогда, когда devkitPro выпустит патч под новую версию.
VER=7.1
ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="$ROOT/build-ffmpeg"
PREFIX="$ROOT/app/lib/ffmpeg-slim"
BASE="https://raw.githubusercontent.com/devkitPro/pacman-packages/master/switch/ffmpeg"

mkdir -p "$WORK"
cd "$WORK"

[ -f "ffmpeg-$VER.tar.xz" ] || curl -sL -o "ffmpeg-$VER.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz"
[ -f "ffmpeg-$VER.patch" ] || curl -sL -o "ffmpeg-$VER.patch" "$BASE/ffmpeg-$VER.patch"
[ -f "tls.patch" ] || curl -sL -o "tls.patch" "$BASE/tls.patch"

if [ ! -d "ffmpeg-$VER" ]; then
  tar xf "ffmpeg-$VER.tar.xz"
  cd "ffmpeg-$VER"
  patch -Np1 -i "../ffmpeg-$VER.patch"
  patch -Np1 -i "../tls.patch"
  cd ..
fi

cd "ffmpeg-$VER"
source /opt/devkitpro/switchvars.sh

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

make -j8
make install

echo
echo "готово, библиотеки в $PREFIX/lib:"
ls -l "$PREFIX/lib"/*.a | awk '{printf "  %-16s %6.1f МБ\n", $NF, $5/1048576}'
