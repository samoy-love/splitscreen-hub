#include "catalog.hpp"

#include <borealis.hpp>
#include <sqlite3.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include "perf.hpp"

using namespace brls::literals;

namespace
{

/// Родительская VFS. Отдельной переменной, а не в pAppData копии: unix-VFS
/// хранит там свой указатель на выбор стиля блокировок и разыменовывает его при
/// каждом открытии файла — подмена поля обернулась бы падением внутри sqlite.
sqlite3_vfs* nxParentVfs = nullptr;

/// Регистрирует VFS для путей devkitPro и возвращает её имя.
///
/// sqlite считает путь абсолютным только когда он начинается с «/», а
/// «romfs:/catalog.db» начинается с буквы. Поэтому штатная unixFullPathname
/// приклеивает спереди текущий каталог и получает несуществующее имя — база не
/// открывается с «unable to open database file», хотя файл на месте и читается
/// обычным fopen. Подменяем единственный метод: путь с префиксом устройства
/// отдаём как есть.
///
/// Это обёртка вокруг штатной VFS, а не своя реализация: чтение, блокировки и
/// всё остальное остаются родными.
const char* nxVfsName()
{
    static sqlite3_vfs nxVfs;
    static bool registered = false;

    if (registered)
        return nxVfs.zName;

    // Оборачиваем «unix-none», а не VFS по умолчанию: перед чтением схемы sqlite
    // берёт разделяемую блокировку через fcntl, а romfs блокировок не умеет и
    // отвечает ошибкой ввода-вывода. Файл здесь только для чтения и никем не
    // изменяется, так что блокировки не нужны вовсе.
    sqlite3_vfs* base = sqlite3_vfs_find("unix-none");
    if (!base)
        base = sqlite3_vfs_find(nullptr);
    if (!base)
        return nullptr;

    nxParentVfs = base;

    nxVfs               = *base;
    nxVfs.zName         = "nx";
    nxVfs.xFullPathname = [](sqlite3_vfs* vfs, const char* path, int outSize, char* out) -> int
    {
        (void)vfs;
        // «устройство:» в начале — путь уже абсолютный
        const char* colon = std::strchr(path, ':');
        const char* slash = std::strchr(path, '/');
        if (colon && (!slash || colon < slash))
        {
            const size_t len = std::strlen(path);
            if ((int)len >= outSize)
                return SQLITE_CANTOPEN;
            std::memcpy(out, path, len + 1);
            return SQLITE_OK;
        }

        return nxParentVfs->xFullPathname(nxParentVfs, path, outSize, out);
    };

    if (sqlite3_vfs_register(&nxVfs, 0) != SQLITE_OK)
        return nullptr;

    registered = true;
    return nxVfs.zName;
}

// Ни одного соединения: и оценки, и подборки раньше подмешивались через LEFT
// JOIN. Таблица ratings оказалась пустой и убрана совсем, а упоминания в
// подборках перенесены прямо в games — соединение мешало взять индекс, и на
// консоли сортировка по умолчанию стоила 1088 мс на 3489 строк.
//
// Тексты берутся из games напрямую: в базе, которая едет в romfs, перевод уже
// вписан поверх оригинала (make_ship_db.py), а таблица translations удалена —
// два языка одного и того же текста в сборке не нужны. Английские черновики
// остаются в рабочей catalog.db в репозитории.
// Тексты берутся по языку: английский лежит в games, русский — в translations.
// coalesce, а не join-и-надейся: перевод есть не у всех игр, и для остальных
// правильный ответ — показать оригинал, а не пустоту.
const char* SELECT_HEAD =
    "SELECT g.nsuid, g.title, g.title_id, g.same_screen_min, g.same_screen_max,";
const char* SELECT_TAIL =
    " g.publisher, g.release_year, g.languages, g.rom_size_bytes, g.has_online,"
    " g.no_tabletop, g.has_demo, g.has_russian, g.mentions";

// Сетке не нужны описания: они есть только у карточки, где игра одна.
// При 3489 играх полная выборка тянет в память несколько мегабайт текста —
// и делает это заново на каждое нажатие фильтра.
const char* SELECT_BRIEF =
    "SELECT g.nsuid, g.title, g.title_id, g.same_screen_min, g.same_screen_max,"
    " g.box_art_file, g.mentions"
    " FROM games g";

std::string text(sqlite3_stmt* st, int col)
{
    const unsigned char* s = sqlite3_column_text(st, col);
    return s ? reinterpret_cast<const char*>(s) : "";
}

Game readGame(sqlite3_stmt* st)
{
    Game g;
    g.nsuid           = text(st, 0);
    g.title           = text(st, 1);
    g.titleId         = text(st, 2);
    g.sameScreenMin   = sqlite3_column_int(st, 3);
    g.sameScreenMax   = sqlite3_column_int(st, 4);
    g.playersNote     = text(st, 5);
    g.boxArtFile      = text(st, 6);
    g.backgroundColor = text(st, 7);
    g.headline        = text(st, 8);
    g.description     = text(st, 9);
    g.publisher       = text(st, 10);
    g.releaseYear     = sqlite3_column_int(st, 11);
    g.languages       = text(st, 12);
    g.romSizeBytes    = sqlite3_column_int64(st, 13);
    g.hasOnline       = sqlite3_column_int(st, 14) != 0;
    g.noTabletop      = sqlite3_column_int(st, 15) != 0;
    g.hasDemo         = sqlite3_column_int(st, 16) != 0;
    g.hasRussian      = sqlite3_column_int(st, 17) != 0;
    g.topMentions     = sqlite3_column_int(st, 18);
    return g;
}

Game readGameBrief(sqlite3_stmt* st)
{
    Game g;
    g.nsuid         = text(st, 0);
    g.title         = text(st, 1);
    g.titleId       = text(st, 2);
    g.sameScreenMin = sqlite3_column_int(st, 3);
    g.sameScreenMax = sqlite3_column_int(st, 4);
    g.boxArtFile    = text(st, 5);
    g.topMentions   = sqlite3_column_int(st, 6);
    return g;
}

/// Порядок в SQL для каждой сортировки. Приписка `sort_title` вторым ключом
/// нужна везде: без неё игры с одинаковым числом игроков или годом выпуска
/// перемешиваются от запроса к запросу.
const char* ORDER_BY[] = {
    // В скольких независимых подборках «лучших couch co-op игр» игра названа.
    // Стоит по умолчанию: упоминания есть только у 85 игр, но именно они и
    // отвечают на вопрос «во что поиграть», а остальное идёт по алфавиту.
    " ORDER BY g.mentions = 0, g.mentions DESC, g.best_pos, g.sort_title",
    " ORDER BY g.sort_title",                              // название А→Я
    " ORDER BY g.same_screen_max DESC, g.sort_title",      // больше игроков
    " ORDER BY g.release_year DESC, g.sort_title",         // сначала новые
    // «что влезет на карту» — сначала маленькие, размер известен не везде
    " ORDER BY g.rom_size_bytes IS NULL, g.rom_size_bytes, g.sort_title",
    " ORDER BY g.sort_title",  // сначала мои: доупорядочивается в приложении
};

}  // namespace

// «Из подборок», а не «по обзорам»: упоминания есть у 85 игр из 3489, то есть
// осмысленно упорядочены первые несколько десятков, а остальные идут по
// алфавиту. Прежнее название обещало сортировку всего каталога по качеству.
//
// Обратного алфавитного порядка здесь больше нет: он занимал место в списке, а
// ответа ни на один вопрос не давал — от «Я» к «А» игру не ищут.
std::vector<std::string> Catalog::sortNames()
{
    return {
        "hub/sort/toplists"_i18n,
        "hub/sort/alphabet"_i18n,
        "hub/sort/players"_i18n,
        "hub/sort/newest"_i18n,
        "hub/sort/compact"_i18n,
        "hub/sort/mine"_i18n,
    };
}

Catalog::~Catalog()
{
    if (db)
        sqlite3_close(db);
}

/// Сколько игр видит уже открытая база. Отрицательное значение — база
/// открылась, но не читается: такое бывает при отказе ввода-вывода на первой же
/// странице, и молча поднимать пустой интерфейс в этом случае нельзя.
int Catalog::countGames()
{
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM games", -1, &st, nullptr);
    if (rc != SQLITE_OK)
    {
        // Расширенный код важнее базового: SQLITE_IOERR один на десяток разных
        // причин, а по уточнению видно, чтение это, блокировка или fstat.
        lastError = "rc=" + std::to_string(rc) + "/" +
                    std::to_string(sqlite3_extended_errcode(db)) + " " + sqlite3_errmsg(db);
        return -1;
    }

    rc       = sqlite3_step(st);
    int games = rc == SQLITE_ROW ? sqlite3_column_int(st, 0) : -1;
    if (games < 0)
        lastError = "rc=" + std::to_string(rc) + "/" +
                    std::to_string(sqlite3_extended_errcode(db)) + " " + sqlite3_errmsg(db);
    sqlite3_finalize(st);
    return games;
}

/// Последний рубеж: база целиком читается обычным потоком и отдаётся sqlite как
/// готовый образ в памяти. Это снимает разом все особенности romfs — блокировки,
/// нестандартное чтение, префиксы устройств, — ценой памяти под сам файл.
bool Catalog::openInMemory(const std::string& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.good())
    {
        lastError = "не открылся файл " + path;
        return false;
    }

    const std::streamsize size = in.tellg();
    in.seekg(0);

    // Освобождает sqlite при закрытии базы, поэтому именно sqlite3_malloc64.
    auto* image = static_cast<unsigned char*>(sqlite3_malloc64(size));
    if (!image)
    {
        lastError = "не хватило памяти на образ базы (" + std::to_string(size) + " Б)";
        return false;
    }

    if (!in.read(reinterpret_cast<char*>(image), size))
    {
        sqlite3_free(image);
        lastError = "файл прочитался не целиком";
        return false;
    }

    int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK)
    {
        sqlite3_free(image);
        lastError = "не создалась база в памяти: " + std::string(sqlite3_errstr(rc));
        db        = nullptr;
        return false;
    }

    rc = sqlite3_deserialize(db, "main", image, size, size,
                             SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_READONLY);
    if (rc != SQLITE_OK)
    {
        lastError = "sqlite3_deserialize: " + std::string(sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    const int games = countGames();
    if (games < 0)
    {
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    brls::Logger::warning("catalog: чтение из romfs не удалось, база поднята в память "
                          "({} МБ), игр: {}",
                          size / (1024 * 1024), games);
    return true;
}

bool Catalog::open(const std::string& path)
{
    // Порядок попыток от дешёвой к дорогой. «nx» — обёртка над «unix-none» с
    // починенным разбором пути, дальше штатные VFS, и лишь в конце образ в
    // памяти, который стоит 11 МБ.
    const char* const vfsOrder[] = { nxVfsName(), nullptr, "unix-none" };

    for (const char* vfs : vfsOrder)
    {
        int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, vfs);
        if (rc == SQLITE_OK)
        {
            const int games = countGames();
            if (games >= 0)
            {
                brls::Logger::info("catalog: открыт {} через VFS «{}» (sqlite {}), игр в базе: {}",
                                   path, vfs ? vfs : "по умолчанию", sqlite3_libversion(), games);
                return true;
            }

            brls::Logger::warning("catalog: VFS «{}» — база открылась, но не читается: {}",
                                  vfs ? vfs : "по умолчанию", lastError);
        }
        else
        {
            lastError = "rc=" + std::to_string(rc) + " " +
                        (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
            brls::Logger::warning("catalog: VFS «{}» не открыла {}: {}",
                                  vfs ? vfs : "по умолчанию", path, lastError);
        }

        sqlite3_close(db);
        db = nullptr;
    }

    if (openInMemory(path))
        return true;

    brls::Logger::error("Не удалось открыть каталог {}: {}", path, lastError);
    return false;
}

std::string Catalog::selectFields() const
{
    if (lang == Language::Russian)
        return std::string(SELECT_HEAD)
            + " coalesce(t.players_note_ru, g.players_note), g.box_art_file,"
              " g.background_color,"
              " coalesce(t.headline_ru, g.headline), coalesce(t.description_ru, g.description),"
            + SELECT_TAIL
            + " FROM games g LEFT JOIN translations t ON t.nsuid = g.nsuid";

    return std::string(SELECT_HEAD)
        + " g.players_note, g.box_art_file, g.background_color,"
          " g.headline, g.description,"
        + SELECT_TAIL + " FROM games g";
}

std::string Catalog::buildWhere(const Filter& f, std::vector<std::string>& params) const
{
    // Значения подставляются через bind, а не склейкой текста: рядом в этом же
    // классе byNsuid и genresOf уже так делают, и держать два способа — повод
    // однажды забыть про экранирование.
    params.clear();

    std::string w = " WHERE g.same_screen_max >= " + std::to_string(f.minPlayers);

    if (!f.genre.empty())
    {
        w += " AND g.nsuid IN (SELECT nsuid FROM genres WHERE genre = ?)";
        params.push_back(f.genre);
    }

    if (f.onlyRussian)
        w += " AND g.has_russian = 1";

    if (!f.showRetro)
        w += " AND g.is_retro = 0";

    if (!f.search.empty())
    {
        // Кавычки и звёздочка — это синтаксис самого FTS5, а не текст запроса:
        // оставленные как есть, они ломают разбор выражения.
        std::string term = f.search;
        for (char& c : term)
            if (c == '"' || c == '*' || c == '\'')
                c = ' ';

        // FTS5 ищет по префиксу, чтобы результат обновлялся по мере набора
        w += " AND g.nsuid IN (SELECT nsuid FROM games_fts WHERE games_fts MATCH ?)";
        params.push_back("\"" + term + "\"*");
    }

    return w;
}

void Catalog::bindParams(sqlite3_stmt* st, const std::vector<std::string>& params)
{
    for (size_t i = 0; i < params.size(); i++)
        sqlite3_bind_text(st, static_cast<int>(i + 1), params[i].c_str(), -1, SQLITE_TRANSIENT);
}

const char* Catalog::orderBy(const Filter& f) const
{
    const size_t count = sizeof(ORDER_BY) / sizeof(ORDER_BY[0]);
    return ORDER_BY[f.sort >= 0 && static_cast<size_t>(f.sort) < count ? f.sort : 0];
}


std::vector<Game> Catalog::queryBrief(const Filter& f) const
{
    std::vector<Game> out;
    if (!db)
        return out;

    std::vector<std::string> params;
    std::string sql = std::string(SELECT_BRIEF) + buildWhere(f, params) + orderBy(f);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    {
        brls::Logger::error("Запрос каталога не выполнился: {}", sqlite3_errmsg(db));
        return out;
    }
    bindParams(st, params);

    // Этот запрос идёт на каждое движение фильтра и определяет отзывчивость
    // сетки, поэтому его время меряем всегда.
    perf::Scope timer("выборка каталога");

    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(readGameBrief(st));
    sqlite3_finalize(st);

    perf::count(perf::Counter::DbQueries);
    perf::count(perf::Counter::DbMs, (long long)timer.elapsedMs());
    return out;
}

int Catalog::count(const Filter& f) const
{
    if (!db)
        return 0;
    std::vector<std::string> params;
    std::string sql  = "SELECT count(*) FROM games g" + buildWhere(f, params);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        return 0;
    int n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}


Game Catalog::byNsuid(const std::string& nsuid) const
{
    Game g;
    if (!db)
        return g;
    std::string sql = selectFields() + " WHERE g.nsuid = ?";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        return g;
    sqlite3_bind_text(st, 1, nsuid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW)
        g = readGame(st);
    sqlite3_finalize(st);
    return g;
}

std::vector<std::string> Catalog::genres() const
{
    std::vector<std::string> out;
    if (!db)
        return out;
    sqlite3_stmt* st = nullptr;
    const char* sql  = "SELECT genre, count(*) c FROM genres GROUP BY genre"
                       " ORDER BY c DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(text(st, 0));
    sqlite3_finalize(st);
    return out;
}

std::vector<std::string> Catalog::genresOf(const std::string& nsuid) const
{
    std::vector<std::string> out;
    if (!db)
        return out;
    sqlite3_stmt* st = nullptr;
    const char* sql  = "SELECT genre FROM genres WHERE nsuid = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_text(st, 1, nsuid.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(text(st, 0));
    sqlite3_finalize(st);
    return out;
}

std::vector<std::string> Catalog::media(const std::string& nsuid, const char* kind) const
{
    std::vector<std::string> out;
    if (!db)
        return out;
    sqlite3_stmt* st = nullptr;
    // Адрес хранится по частям: словарный префикс плюс хвост, в котором
    // вместо nsuid стоит байт 0x01. На 20 тысяч строк это экономит 1.7 МБ
    // в romfs, а собрать строку обратно стоит одну склейку.
    const char* sql = "SELECT p.prefix, m.tail FROM media m"
                      " JOIN media_prefix p ON p.id = m.prefix_id"
                      " WHERE m.nsuid = ? AND m.kind = ? ORDER BY m.ord";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_text(st, 1, nsuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, kind, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        std::string url = text(st, 0);
        std::string tail = text(st, 1);
        for (size_t at = tail.find(''); at != std::string::npos;
             at = tail.find('', at + nsuid.size()))
            tail.replace(at, 1, nsuid);
        out.push_back(url + tail);
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<std::string> Catalog::screenshots(const std::string& nsuid) const
{
    return media(nsuid, "image");
}

std::vector<std::string> Catalog::videos(const std::string& nsuid) const
{
    return media(nsuid, "video");
}
