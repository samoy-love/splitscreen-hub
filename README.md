# SplitScreen Hub

English · [Русский](README.ru.md)

[![checks](https://github.com/tr0llex/splitscreen-hub/actions/workflows/checks.yml/badge.svg)](https://github.com/tr0llex/splitscreen-hub/actions/workflows/checks.yml)
[![deploy](https://github.com/tr0llex/splitscreen-hub/actions/workflows/deploy.yml/badge.svg)](https://github.com/tr0llex/splitscreen-hub/actions/workflows/deploy.yml)
[![prod](https://img.shields.io/website?url=https%3A%2F%2Fsamoy.love%2Fsplitscreen-hub%2FSplitScreenHub.nro.json&up_message=online&up_color=2ea043&down_message=offline&label=samoy.love%2Fsplitscreen-hub)](https://samoy.love/splitscreen-hub/SplitScreenHub.nro.json)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A Nintendo Switch homebrew catalog of games with **same-screen multiplayer**.
It answers "there are four of us — what do we play?": a "from N players"
filter, a mark on what is already installed on the console, favorites and
custom folders, trailers and screenshots right in the game card.

The app is C++17 on top of [borealis](https://github.com/xfangfang/borealis)
(devkitPro). The data is collected by a set of Python scripts from the public
nintendo.com APIs and lives in two binary files in romfs — the console needs
neither a database nor a network connection to browse the catalog. The code
is MIT licensed; game covers and texts belong to their publishers and are not
part of the repository (see [License](#license)).

About 3,500 games; for every one the exact number of players on a single
console is known — not "has multiplayer" but "1–4". Arcade re-releases
(ACA NEOGEO, Arcade Archives, SEGA AGES — roughly 470 titles) are hidden by
default and enabled with a separate filter.

---

- [Features](#features)
- [Install](#install)
- [Updates and deployment](#updates-and-deployment)
- [Repository layout](#repository-layout)
- [How the app works](#how-the-app-works)
- [Where the data comes from](#where-the-data-comes-from)
- [Building](#building)
- [Checks](#checks)
- [Controls](#controls)
- [Known gaps](#known-gaps)
- [License](#license)

---

## Features

- **Filters**: from N players, genre, installed only, has Russian, title
  search, arcade re-releases, hidden games.
- **Sorting**: by curated lists · alphabetical · more players · newest first ·
  smallest first · installed first.
- **"From curated lists" ranking** — agreement between 19 editorial "best couch
  co-op on Switch" lists and 18 Reddit/Famiboards threads, not a mention counter
  (see [below](#curated-list-ranking)).
- **Game card**: player count with an explanation (split-screen, hotseat, phones
  instead of gamepads), description, screenshots, trailers, size, languages,
  publisher, year.
- **Library**: favorites and your own folders, hiding what you don't need. Stored
  in `sdmc:/switch/splitscreen-hub/library.json`, written atomically.
- **Installed**: matched against the console via `nsListApplicationRecord` —
  "12 of the matching games are already installed".
- **Two languages** for the UI and the game texts: English and Russian.
- **Offline**: the catalog, covers and navigation work without a network;
  only screenshots and trailers need it, and they are cached on the SD card.

## Install

Download [`SplitScreenHub.nro`](https://samoy.love/splitscreen-hub/SplitScreenHub.nro)
(the [`.sha256`](https://samoy.love/splitscreen-hub/SplitScreenHub.nro.sha256)
is next to it) and put it into `/switch/` on the SD card. The build is served
from the project's own server only — GitHub Releases carry no binaries.
Launch it from the Homebrew Menu; in applet mode (over a running game) there is
less memory, but the catalog works.

## Updates and deployment

The app updates itself. On startup it reads
[`samoy.love/splitscreen-hub/SplitScreenHub.nro.json`](https://samoy.love/splitscreen-hub/SplitScreenHub.nro.json)
— version, size and sha256 of the current build — and, if it is newer than the
one baked into the `.nro`, shows a toast and an **Install** button on the
Settings tab. The new `.nro` is streamed next to the running one, checked
against the sha256 (TLS is not verified on the console — see below), and
swapped in; it starts on the next launch. Code: [`updater.cpp`](app/source/updater.cpp).

The manifest is written by the release pipeline. Every push to `master` that
touches `app/**` runs [`deploy.yml`](.github/workflows/deploy.yml), which calls
the shared [deploy-kit](https://github.com/tr0llex/deploy-kit) artifact
workflow: the `.nro` is built in the `devkitpro/devkita64` container by
[`build_release.sh`](app/tools/build_release.sh), the version is taken from
`CMakeLists.txt`, the file goes to the server over SSH, and `publish-file.sh`
swaps it atomically (the previous build stays as `.prev` for instant rollback),
publishes `.sha256` and the manifest, and verifies that the server serves
exactly the checksum the runner computed. The target is described once in
[`.deploy-kit/nro.env`](.deploy-kit/nro.env) and is used both by CI and by a
local `dk deploy`. The server is the only distribution channel.

Catalog data (covers, `catalog.bin`, `details.bin`, `translations.db`) is not
in git — it is publisher material. It travels as a separate bundle,
[`splitscreen-hub-data.tar.gz`](https://samoy.love/splitscreen-hub/splitscreen-hub-data.tar.gz),
published by hand after a pipeline run with `dk deploy splitscreen-hub-data`
([`.deploy-kit/data.env`](.deploy-kit/data.env), packed by
[`pack_data.sh`](app/tools/pack_data.sh)). `build_release.sh` downloads and
checks it when the files are missing, so CI builds the same `.nro` as a
developer machine.

## Repository layout

```
app/                      the Switch application
  source/                 C++: catalog, library, network, player
    ui/                   borealis screens and widgets
  resources/              romfs: xml layouts, i18n, icon;
                          catalog.bin, details.bin, art/ come from the pipeline (not in git)
  tests/                  tests of pure functions (no borealis, no console)
  tools/                  check_xml.py, run_tests.sh, build_ffmpeg_slim.sh,
                          build_release.sh, pack_data.sh, make_icon.py
  lib/borealis            UI library submodule
  lib/ffmpeg-slim/        trimmed FFmpeg, built locally (gitignored)
pipeline/                 data collection: eShop → catalog.db → catalog.bin
  paths.py                shared paths; scripts run from any directory
  overrides.json          manual player-count corrections (committed)
  translations.db         Russian game texts (in the data bundle, not in git)
docs/third-party-licenses.md  what the .nro is built from and under which terms
.deploy-kit/nro.env       the .nro target: how to build, where to publish
.deploy-kit/data.env      the catalog data bundle target
.github/workflows/        checks on every push, deploy on push to master
```

## How the app works

```
romfs:/catalog.bin ──► Catalog ──► CatalogQuery(Filter) ──► GridModel ──► GameGrid
romfs:/details.bin ──►   │  byNsuid()                                     │ GameTile
                         │                                                 ▼
sdmc:/…/library.json ──► Library (favorites, folders, hidden, language)  GameActivity
ns:am ─────────────────► installed::titleIds()                          │
                                                                        ├─ RemoteImage ◄─ net (curl, SD cache)
                                                                        └─ VideoDecoder ◄─ HttpStream (curl → AVIO)
```

**Data.** [`Catalog`](app/source/catalog.hpp) reads `catalog.bin` in full at
startup (everything the grid needs — about 0.5 MB) and keeps it in memory; the
game card fetches its record from `details.bin` by offset. Selection and
ordering live in [`catalog_query.cpp`](app/source/catalog_query.cpp): pure
functions over a vector, without borealis or files, so they are covered by tests.
[`AppState`](app/source/app_state.hpp) is the single owner of the catalog, the
library and the installed set.

**Threads.** borealis is not thread-safe; all drawing happens on the UI thread.
[`tasks`](app/source/tasks.hpp) provides two channels: a small `io()` pool for
short jobs (JPEG decoding of covers, screenshots) and `heavy()` for long ones
(network init). Results come back through `brls::sync`. Grid tiles are recycled,
so covers live in [`covers`](app/source/ui/cover_cache.hpp) — a cache of ready
textures with eviction; a tile compares the request generation and never shows
someone else's picture.

**Network.** [`net`](app/source/net.hpp) — curl with an SD cache for
screenshots. [`HttpStream`](app/source/http_stream.hpp) streams a trailer into
an AVIO buffer, resumes with Range after a drop and writes the cache along the
way. [`VideoDecoder`](app/source/ui/video_player.hpp) is a home-grown player on
top of FFmpeg: demux/decode in its own thread, frames through swscale into an
nanovg texture, audio through swresample into SDL2. TLS certificate verification
is disabled on purpose: libnx has no system root store, and a bundled one
expires over time and silently breaks downloads.

**Screens.** [`MainTabs`](app/source/ui/main_tabs.hpp) — Catalog, Library,
Settings. [`GameActivity`](app/source/ui/game_activity.hpp) — the game card,
[`GalleryActivity`](app/source/ui/gallery_activity.hpp) — full-screen
screenshots, [`VideoPlayer`](app/source/ui/video_player.hpp) — the trailer.
Screen layouts are XML in `app/resources/xml/`; `check_xml.py` checks them
against the attributes borealis actually registers, because an unknown attribute
crashes the app when the screen opens.

## Where the data comes from

Two public APIs used by nintendo.com itself.

**Algolia** (`store_game_en_us`) — the eShop catalog. The key facet is
`waysToPlayLabels: "Play together on one console"`. The API key is the public
search-only key of the site's front end.

**graph.nintendo.com** — the product card. The header
`apollographql-client-name: ncom` is mandatory, otherwise the server answers
`Internal server error`. The argument is a OneOf `ProductInput` with exactly one
key: `sku` | `nsuid` | `urlKey` (noticeably more products are reachable by
`nsuid` than by `sku`).

The key field is `numberOfPlayers`, and it holds three different counters:

| Field | Meaning |
|---|---|
| `.system` | **players on one console** — the one we need |
| `.local` | local wireless across several consoles |
| `.online` | online |

For Mario Kart World that is `system 1–4`, `local 2–8`, `online 2–24`. The
European API (`searching.nintendo-europe.com`) is unusable here: its single
`players_to` field is a total — Fortnite says 100, Terraria says 8 with
two-player split-screen.

About half of the catalog is refused anonymously by GraphQL (`UNAUTHORIZED`),
unrelated to availability or price. Those cards are taken from the product page:
it opens with a plain GET and full browser headers (406 without them), and its
`__NEXT_DATA__` holds the same `Product` object.

### What is not in the catalog

- **bundles** (nsuid `7007*`) — no player count, no Title ID, no size;
- **future releases**;
- **32 games** with no reliable confirmation of multiplayer;
- **games mislabeled by the eShop**: HELLCARD is co-op online only, Double Kick
  Heroes is single-player, LEGO Voyagers Friend's Pass is not a game but an
  online invite pass.

Another fifty or so are labeled `Single player` by the eShop despite having
multiplayer — all Jackbox Party Packs, Rocket League, Stardew Valley and
Terraria among them. Their numbers were verified by hand and live in
[`pipeline/make_overrides.py`](pipeline/make_overrides.py) with a source for
each. It also shows why the Switch version specifically must be checked: Stardew
Valley on the original Switch is two-player split-screen; four players are only
in the Switch 2 edition.

The most useful source turned out to be the data itself: many publisher
descriptions name the count outright ("a 2-player cooperative puzzle
platformer", "up to 5 players per turns"). More reliable than any aggregator and
needs no network.

### Curated-list ranking

The default sort and the "recommended" filter are built on **agreement between
sources**, not on a mention counter: a list of 75 names everything, five articles
from one site are one opinion, and a mention in a 900-comment thread is cheaper
than one in a 60-comment thread.

Titles from editorial articles live in
[`pipeline/toplist_sources.py`](pipeline/toplist_sources.py); threads and the
formula are in [`pipeline/rank_toplists.py`](pipeline/rank_toplists.py). In
short: a source's weight is its selectivity `ln(P/n)`; several lists from one
outlet share weight as one opinion; list position is a soft multiplier and only
where the outlet actually numbers; threads are damped by size; editorial and
community channels are scored separately and combined by geometric mean; games
with a single source go to the tail. Games explicitly disliked in threads get a
negative vote. The full reasoning is in the script's docstring.

### Data format

There is no SQLite in the app: with three and a half thousand games a database
solved a problem that does not exist. The grid needs 268 KB for the whole
catalog, while any query pulled tens of megabytes off romfs because
descriptions sit in the same rows. Instead
[`make_ship_data.py`](pipeline/make_ship_data.py) writes two files:

- `catalog.bin` (~0.5 MB) — everything the grid shows. Read in full at startup;
  filtering and sorting are then an in-memory scan taking milliseconds;
- `details.bin` (~5 MB) — card texts and links, one record per game, read by
  offset. Records are zlib-compressed with a shared 64 KB dictionary: alone they
  halve, with the dictionary they shrink almost threefold, and each still
  inflates independently.

`catalog.db` is the pipeline's working database; the app never sees it.

## Building

### Data

```bash
python pipeline/fetch_local_multiplayer.py   # eShop -> local_multiplayer.json (card cache in products_cache.json)
python pipeline/repair_catalog.py            # optional: picks up games Algolia enumeration misses
python pipeline/make_overrides.py            # manual corrections -> overrides.json
python pipeline/build_db.py                  # working database catalog.db
python pipeline/rank_toplists.py             # curated-list ranking -> toplists.db
python pipeline/build_db.py                  # again: merges ranking and translations
python pipeline/download_art.py              # 240x240 covers -> app/resources/art/
python pipeline/verify_db.py                 # data invariants
python pipeline/make_ship_data.py            # catalog.bin and details.bin -> app/resources/
```

`build_db.py` runs twice: `rank_toplists.py` matches list titles against the
catalog, so the catalog must already exist. Scripts run from any directory —
paths are resolved via `pipeline/paths.py`.

Covers are fetched at 240 px — exactly what a tile occupies on screen when
docked (a 1920×1080 window at base 1280 gives scale 1.5, and 160 logical tile
points become 240 physical ones).

Russian game texts live in `pipeline/translations.db` (table `translations`:
`nsuid, headline_ru, players_note_ru, description_ru`). The file was machine
translated once; there is no producing script. It is not in git (it is a
derivative of publisher texts) and comes with the data bundle — without it the
catalog builds with English texts.

Everything this section produces is publisher material and stays out of git:
`app/resources/art/`, `catalog.bin`, `details.bin`, `translations.db` are
ignored and shipped as a bundle instead (see [Updates and
deployment](#updates-and-deployment)). Without a pipeline run,
`build_release.sh` fetches the current bundle from the server.

### Application

You need devkitPro with `switch-curl`, `switch-mbedtls`, SDL2 and Ninja:

```bash
pacman -S switch-curl switch-mbedtls switch-sdl2 ninja
```

FFmpeg from pacman **does not fit**: the build expects a trimmed local
`app/lib/ffmpeg-slim` (h264 + aac only, version 7.1 — the only one with a
devkitPro patch that has `--enable-nvtegra`). The `switch-ffmpeg` package pulls
the full codec set and adds over ten megabytes to the `.nro`.

```bash
bash app/tools/build_ffmpeg_slim.sh
```

Two build quirks that can easily eat an evening:

- **Ninja only.** The Unix Makefiles generator breaks on a space in the project
  path (`multiple target patterns` in dependency files).
- **`DEVKITPRO` in POSIX form** (`/opt/devkitpro`, not `C:/devkitPro`) and from
  PowerShell — git-bash environment variables do not reach msys2 cmake.

```powershell
cd app
$env:DEVKITPRO = "/opt/devkitpro"
cmake -B build -G Ninja -DPLATFORM_SWITCH=ON -DUSE_SDL2=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SplitScreenHub.nro
```

The output is `app/build/SplitScreenHub.nro` (~58 MB, mostly covers in romfs).

There is no font in romfs: text is drawn with the console's own system font,
which the app takes from the console at runtime via `pl:u`
([`fonts.cpp`](app/source/ui/fonts.cpp)). borealis loads it but does not make
it the default, so the app re-points the default itself; on the desktop build
borealis uses its bundled font.

Why a home-grown player rather than `libmpv` as in NXMP and SwitchWave: there is
no `switch-mpv` package in pacman, and building mpv with the patched FFmpeg for
`aarch64-none-elf` needs a separate meson toolchain and a set of patches. A
software decoder is enough for 30–60-second trailers.

## Checks

```bash
python app/tools/check_xml.py    # layouts against attributes borealis understands
bash   app/tools/run_tests.sh    # pure-function tests, plain g++
python pipeline/verify_db.py     # catalog.db invariants before packing
```

CI runs the first two on every push. On Windows run the tests from the msys2
that ships with devkitPro — from Git Bash the compiler cannot find its C++
headers because `/usr` there points at Git's own directory:

```bash
/c/devkitPro/msys2/usr/bin/bash -lc "cd /c/…/app && bash tools/run_tests.sh"
```

Only checked on the console, by hand: memory in applet mode, focus loss and
resume from sleep, matching against installed games, trailer resume on a bad
connection.

## Languages

The UI and the game texts switch between English and Russian on the Settings
tab. English is the default: some games have no translation, and the original
is the only thing that can be shown for sure. In Russian mode untranslated
games show English.

Until a language is chosen by hand, the app looks at the console language and
enables Russian only on a Russian console. Everything else — from French to
Japanese — gets English: the Russian translation is incomplete, and
substituting it for the original where it is foreign anyway is worse than not
substituting.

The choice is stored in `library.json` and applied at startup: the borealis
locale is set before the window is created and cannot change on the fly, so
the switch asks for a restart. UI strings live in
`app/resources/i18n/<locale>/hub.json`.

## Controls

| Button | Action |
|---|---|
| A | select · open game |
| B | back |
| L / R | previous · next tab |
| **+** | **quit the app, from any screen** |

Catalog:

| Button | Action |
|---|---|
| − | search by title |
| ZL / ZR | page through the grid |

Library:

| Button | Action |
|---|---|
| X | on a game — remove from list; on a folder — delete folder |
| Y | rename folder |
| ZR | new folder |

Catalog and game card — same buttons:

| Button | Action |
|---|---|
| X | add to favorites or a folder |
| Y | hide game · unhide |

Trailer player (no bottom hint bar there):

| Button | Action |
|---|---|
| A | pause |
| Left / Right | seek 10 seconds |
| B | close |

Settings: X recalculates the cache size.

## Known gaps

- **32 games without a player count** — the eShop "one console" label is
  there but no number, so they are not in the catalog. Added by hand in
  `pipeline/make_overrides.py` with a source each; the Switch version must be
  checked separately from other platforms.
- **No game ratings.** The idea is the Steam positive-review percentage (a
  third of the catalog has 50+ reviews there, no keys needed) with the caveat
  that it is the PC version's reception. Not in the UI.
- **Games lost to eShop labels.** Some well-known couch co-op games (PHOGS!,
  Untitled Goose Game, Unspottable) lack the "one console" label despite real
  local co-op; some do not match by title spelling. Every way to bring them back
  changes the selection rules, so it is left as is.
- **Other regions.** The pipeline is parameterized by one string: Algolia has
  `en_ca`, `es_mx`, `fr_ca`, `pt_br` indices.

## License

The source code is [MIT](LICENSE). Third-party libraries in the built `.nro`
(borealis, FFmpeg under LGPL-2.1+, curl, Mbed TLS, SDL2, libnx and others) keep
their own licenses — the full list and how the LGPL terms are met are in
[docs/third-party-licenses.md](docs/third-party-licenses.md).

Game titles, cover art, descriptions, screenshots, trailers and their Russian
translations belong to their respective publishers and are used solely to
identify games. They are **not part of the licensed work and are not stored in
this repository**; the pipeline collects only facts about products (player
count, genre, size, year, publisher) from the public nintendo.com APIs. If you
are a rights holder and want a game's materials removed from the app, open an
issue or e-mail the address on the author's GitHub profile — it will be dropped
in the next build.

SplitScreen Hub is an independent homebrew project. It is not affiliated with,
endorsed or sponsored by Nintendo. Nintendo Switch, Nintendo eShop and Joy-Con
are trademarks of Nintendo. The console's system font is used at runtime and is
not distributed with the app.
