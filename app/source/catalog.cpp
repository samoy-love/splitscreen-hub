#include "catalog.hpp"

#include <borealis.hpp>
#include <sqlite3.h>

namespace
{

// Тексты берутся из games напрямую: в базе, которая едет в romfs, перевод уже
// вписан поверх оригинала (make_ship_db.py), а таблица translations удалена —
// два языка одного и того же текста в сборке не нужны. Английские черновики
// остаются в рабочей catalog.db в репозитории.
const char* SELECT_FIELDS =
    "SELECT g.nsuid, g.title, g.title_id, g.same_screen_min, g.same_screen_max,"
    " g.players_note, g.box_art_file, g.background_color,"
    " g.headline, g.description,"
    " g.publisher, g.release_year, g.languages, g.rom_size_bytes, g.has_online,"
    " g.no_tabletop, g.has_demo, g.has_russian,"
    " r.rating, r.votes, r.source, tl.mentions"
    " FROM games g"
    " LEFT JOIN ratings r ON r.nsuid = g.nsuid"
    " LEFT JOIN toplists tl ON tl.nsuid = g.nsuid";

// Сетке не нужны описания: они есть только у карточки, где игра одна.
// При 3489 играх полная выборка тянет в память несколько мегабайт текста —
// и делает это заново на каждое нажатие фильтра.
const char* SELECT_BRIEF =
    "SELECT g.nsuid, g.title, g.title_id, g.same_screen_min, g.same_screen_max,"
    " g.box_art_file, r.rating, r.votes, tl.mentions"
    " FROM games g"
    " LEFT JOIN ratings r ON r.nsuid = g.nsuid"
    " LEFT JOIN toplists tl ON tl.nsuid = g.nsuid";

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
    g.rating          = sqlite3_column_type(st, 18) == SQLITE_NULL
        ? 0
        : sqlite3_column_int(st, 18);
    g.ratingVotes     = sqlite3_column_int(st, 19);
    g.ratingSource    = text(st, 20);
    g.topMentions     = sqlite3_column_int(st, 21);
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
    g.rating        = sqlite3_column_type(st, 6) == SQLITE_NULL
        ? 0
        : sqlite3_column_int(st, 6);
    g.ratingVotes   = sqlite3_column_int(st, 7);
    g.topMentions   = sqlite3_column_int(st, 8);
    return g;
}

/// Экранирует строку для подстановки в SQL-литерал.
std::string quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
            out += '\'';
        out += c;
    }
    return out + "'";
}

/// Порядок в SQL для каждой сортировки. Приписка `sort_title` вторым ключом
/// нужна везде: без неё игры с одинаковым числом игроков или годом выпуска
/// перемешиваются от запроса к запросу.
const char* ORDER_BY[] = {
    // Согласие обзоров: в скольких независимых подборках «лучших couch co-op
    // игр» игра названа. Ставится по умолчанию — из 3480 игр это единственный
    // порядок, который сразу показывает то, во что стоит играть.
    " ORDER BY tl.mentions IS NULL, tl.mentions DESC, tl.best_pos, g.sort_title",
    " ORDER BY g.sort_title",                              // название А→Я
    " ORDER BY g.sort_title DESC",                         // название Я→А
    " ORDER BY g.same_screen_max DESC, g.sort_title",      // больше игроков
    " ORDER BY g.release_year DESC, g.sort_title",         // сначала новые
    // оценки есть не у всех, игры без неё уходят в конец, а не наверх
    " ORDER BY r.rating IS NULL, r.rating DESC, g.sort_title",
    // «что влезет на карту» — сначала маленькие, размер известен не везде
    " ORDER BY g.rom_size_bytes IS NULL, g.rom_size_bytes, g.sort_title",
    " ORDER BY g.sort_title",  // сначала мои: доупорядочивается в приложении
};

}  // namespace

// Названия короткие: они целиком печатаются на чипе, а строка фильтров и так
// упирается в 820 точек ширины контента.
const std::vector<std::string> Catalog::SORT_NAMES = {
    "по обзорам",
    "по названию А–Я",
    "по названию Я–А",
    "больше игроков",
    "сначала новые",
    "по оценке",
    "по размеру",
    "сначала мои",
};

Catalog::~Catalog()
{
    if (db)
        sqlite3_close(db);
}

bool Catalog::open(const std::string& path)
{
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        brls::Logger::error("Не удалось открыть каталог {}: {}", path, sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    return true;
}

std::string Catalog::buildWhere(const Filter& f) const
{
    std::string w = " WHERE g.same_screen_max >= " + std::to_string(f.minPlayers);

    if (!f.genre.empty())
        w += " AND g.nsuid IN (SELECT nsuid FROM genres WHERE genre = " + quote(f.genre) + ")";

    if (f.onlyRussian)
        w += " AND g.has_russian = 1";

    if (!f.search.empty())
    {
        // FTS5 ищет по префиксу, чтобы результат обновлялся по мере набора
        std::string term = f.search;
        for (char& c : term)
            if (c == '"' || c == '*' || c == '\'')
                c = ' ';
        w += " AND g.nsuid IN (SELECT nsuid FROM games_fts WHERE games_fts MATCH "
             + quote("\"" + term + "\"*") + ")";
    }

    return w;
}

const char* Catalog::orderBy(const Filter& f) const
{
    const size_t count = sizeof(ORDER_BY) / sizeof(ORDER_BY[0]);
    return ORDER_BY[f.sort >= 0 && static_cast<size_t>(f.sort) < count ? f.sort : 0];
}

std::vector<Game> Catalog::query(const Filter& f) const
{
    std::vector<Game> out;
    if (!db)
        return out;

    std::string sql = std::string(SELECT_FIELDS) + buildWhere(f) + orderBy(f);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    {
        brls::Logger::error("Запрос каталога не выполнился: {}", sqlite3_errmsg(db));
        return out;
    }

    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(readGame(st));
    sqlite3_finalize(st);

    return out;
}

std::vector<Game> Catalog::queryBrief(const Filter& f) const
{
    std::vector<Game> out;
    if (!db)
        return out;

    std::string sql = std::string(SELECT_BRIEF) + buildWhere(f) + orderBy(f);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    {
        brls::Logger::error("Запрос каталога не выполнился: {}", sqlite3_errmsg(db));
        return out;
    }

    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(readGameBrief(st));
    sqlite3_finalize(st);

    return out;
}

int Catalog::count(const Filter& f) const
{
    if (!db)
        return 0;
    std::string sql = "SELECT count(*) FROM games g" + buildWhere(f);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        return 0;
    int n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

int Catalog::countAtLeast(int players) const
{
    Filter f;
    f.minPlayers = players;
    return count(f);
}

Game Catalog::byNsuid(const std::string& nsuid) const
{
    Game g;
    if (!db)
        return g;
    std::string sql = std::string(SELECT_FIELDS) + " WHERE g.nsuid = ?";
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
