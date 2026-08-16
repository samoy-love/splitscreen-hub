# Third-party licenses · Лицензии третьих сторон

Исходный код SplitScreen Hub — MIT (см. [LICENSE](../LICENSE)). Собранный
`SplitScreenHub.nro` содержит перечисленные ниже библиотеки; их лицензии
действуют независимо от MIT.

The SplitScreen Hub source code is MIT-licensed (see [LICENSE](../LICENSE)).
The built `SplitScreenHub.nro` statically links the libraries below; their
licenses apply independently of MIT.

## Что не под MIT · Not covered by MIT

Названия игр, обложки, описания, скриншоты, трейлеры и их русские переводы
принадлежат соответствующим издателям и используются только для
идентификации игр. Они не хранятся в этом репозитории (см. `.gitignore`), но
входят в собранный `.nro`. Правообладатель может запросить удаление
конкретной игры: откройте issue или напишите на адрес из профиля автора на
GitHub — материал будет убран в ближайшей сборке.

Game titles, cover art, descriptions, screenshots, trailers and their Russian
translations belong to their respective publishers and are used solely to
identify games. They are not stored in this repository (see `.gitignore`) but
are included in the built `.nro`. Rights holders may request removal of a
specific game: open an issue or e-mail the address on the author's GitHub
profile, and the material will be dropped in the next build.

Nintendo Switch, Nintendo eShop и Joy-Con — товарные знаки Nintendo. Проект
не связан с Nintendo, не одобрен и не спонсируется ею. Текст на консоли
рисуется системным шрифтом самой консоли, получаемым в рантайме через
`pl:u`; копия шрифта в приложении не распространяется.

Nintendo Switch, Nintendo eShop and Joy-Con are trademarks of Nintendo. This
project is not affiliated with, endorsed or sponsored by Nintendo. Text on the
console is rendered with the console's own system font obtained at runtime
via `pl:u`; no copy of the font is distributed with the app.

## Библиотеки · Libraries

| Library | License | Source |
|---|---|---|
| [borealis](https://github.com/xfangfang/borealis) (fork by xfangfang) | Apache-2.0 | `app/lib/borealis` (submodule) |
| [FFmpeg](https://ffmpeg.org/) 7.1 — libavformat, libavcodec, libswscale, libswresample, libavutil | **LGPL-2.1-or-later** — built without `--enable-gpl` / `--enable-nonfree`; h264, aac, mp3, pcm decoders only | built by `app/tools/build_ffmpeg_slim.sh` from FFmpeg 7.1 with the [devkitPro `switch-ffmpeg` patch](https://github.com/devkitPro/pacman-packages) (`--enable-libnx --enable-nvtegra`) |
| [libnx](https://github.com/switchbrew/libnx) | ISC | devkitPro |
| [curl](https://curl.se/) | curl (MIT-style) | devkitPro `switch-curl` |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | Apache-2.0 | devkitPro `switch-mbedtls` |
| [SDL2](https://www.libsdl.org/) | zlib | devkitPro `switch-sdl2` |
| [zlib](https://zlib.net/) | zlib | devkitPro `switch-zlib` |
| [bzip2](https://sourceware.org/bzip2/) | bzip2 (BSD-style) | devkitPro `switch-bzip2` |
| [nanovg](https://github.com/memononen/nanovg) (borealis fork) | zlib | borealis `lib/extern/nanovg` |
| [Yoga](https://github.com/facebook/yoga) | MIT | borealis `lib/extern/yoga` |
| [{fmt}](https://github.com/fmtlib/fmt) | MIT | borealis `lib/extern/fmt` |
| [tinyxml2](https://github.com/leethomason/tinyxml2) | zlib | borealis `lib/extern/tinyxml2` |
| [tweeny](https://github.com/mobius3/tweeny) | MIT | borealis `lib/extern/tweeny` |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | borealis `include/borealis/extern/nlohmann` |
| [libromfs](https://github.com/WerWolv/libromfs) | MIT | borealis `lib/extern/libromfs` |
| [switch-libpulsar](https://github.com/p-sam/switch-libpulsar) | MIT | borealis `lib/extern/switch-libpulsar` |
| [libretro-common](https://github.com/libretro/libretro-common) (parts) | MIT | borealis `lib/extern/libretro-common` |
| [glad](https://github.com/Dav1dde/glad) | MIT / generated code public domain | borealis `lib/extern/glad` |
| [Material Design Icons](https://github.com/google/material-design-icons) font | Apache-2.0 | borealis `resources/material` (the only borealis font copied into romfs — see `COPY_RESOURCES` in `app/CMakeLists.txt`) |

### LGPL: как выполнить условия · How the LGPL terms are met

FFmpeg линкуется статически. Согласно LGPL 2.1 §6 пользователь должен иметь
возможность пересобрать приложение с изменённой версией библиотеки. Для этого:

* исходники FFmpeg — [ffmpeg.org](https://ffmpeg.org/download.html), версия
  7.1; патч devkitPro и точная конфигурация — в
  [`app/tools/build_ffmpeg_slim.sh`](../app/tools/build_ffmpeg_slim.sh);
* приложение собирается из этого репозитория по инструкции в README
  («Сборка»); замена `app/lib/ffmpeg-slim` на другую сборку FFmpeg 7.1 с тем
  же набором компонентов и повторная сборка `.nro` — и есть перелинковка;
* текст лицензии: <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>.

FFmpeg is statically linked. Under LGPL 2.1 §6 users must be able to relink the
application against a modified library: FFmpeg 7.1 sources are at
ffmpeg.org, the devkitPro patch and exact configure line are in
`app/tools/build_ffmpeg_slim.sh`, and rebuilding the `.nro` from this
repository with a replaced `app/lib/ffmpeg-slim` is the relink.

## Данные · Data sources

* Каталог и карточки — публичные API nintendo.com (Algolia
  `store_game_en_us`, graph.nintendo.com); используются только факты о
  продуктах: название, число игроков, жанр, размер, год, издатель.
* Рейтинг — перечни названий из редакционных подборок и тредов Reddit /
  Famiboards, без текста статей и комментариев
  (`pipeline/toplist_sources.py`, `pipeline/rank_toplists.py`).

Catalog and product cards come from the public nintendo.com APIs; only facts
about products are used. The ranking uses lists of titles from editorial
articles and Reddit / Famiboards threads — never article or comment text.
