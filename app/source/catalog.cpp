#include "catalog.hpp"

#include <borealis.hpp>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "catalog_query.hpp"
#include "perf.hpp"

using namespace brls::literals;

namespace
{

const char CATALOG_MAGIC[4] = { 'S', 'S', 'H', 'C' };
const char DETAILS_MAGIC[4] = { 'S', 'S', 'H', 'D' };
constexpr unsigned FORMAT_VERSION = 2;

/// Последовательное чтение из буфера в памяти.
///
/// Проверяет границы на каждом шаге и после первого выхода за них молча отдаёт
/// нули: испорченный файл должен дать пустой каталог, а не чтение чужой памяти.
class Reader
{
  public:
    Reader(const unsigned char* data, size_t size)
        : data(data)
        , size(size)
    {
    }

    bool ok() const { return !failed; }

    unsigned char u8()
    {
        if (!take(1))
            return 0;
        return data[pos - 1];
    }

    unsigned u16()
    {
        if (!take(2))
            return 0;
        return unsigned(data[pos - 2]) | (unsigned(data[pos - 1]) << 8);
    }

    unsigned u32()
    {
        if (!take(4))
            return 0;
        unsigned v = 0;
        std::memcpy(&v, data + pos - 4, 4);
        return v;
    }

    unsigned long long u64()
    {
        if (!take(8))
            return 0;
        unsigned long long v = 0;
        std::memcpy(&v, data + pos - 8, 8);
        return v;
    }

    long long i64()
    {
        if (!take(8))
            return 0;
        long long v = 0;
        std::memcpy(&v, data + pos - 8, 8);
        return v;
    }

    std::string str16()
    {
        const unsigned length = u16();
        if (!take(length))
            return {};
        return std::string(reinterpret_cast<const char*>(data + pos - length), length);
    }

    std::string str32()
    {
        const unsigned length = u32();
        if (!take(length))
            return {};
        return std::string(reinterpret_cast<const char*>(data + pos - length), length);
    }

    bool magic(const char expected[4])
    {
        if (!take(4))
            return false;
        return std::memcmp(data + pos - 4, expected, 4) == 0;
    }

  private:
    bool take(size_t n)
    {
        if (failed || pos + n > size)
        {
            failed = true;
            return false;
        }
        pos += n;
        return true;
    }

    const unsigned char* data;
    size_t size;
    size_t pos  = 0;
    bool failed = false;
};

/// Читает файл целиком. Пустой вектор — файла нет или он не читается.
std::vector<unsigned char> readWhole(const std::string& path)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return {};

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    std::vector<unsigned char> data;
    if (size > 0)
    {
        data.resize(static_cast<size_t>(size));
        if (std::fread(data.data(), 1, data.size(), file) != data.size())
            data.clear();
    }
    std::fclose(file);
    return data;
}

}  // namespace

// --------------------------------------------------------------------------
// служебное
// --------------------------------------------------------------------------

// «Популярные», а не «по обзорам»: внешние источники знают лишь малую часть
// каталога, то есть осмысленно упорядочена лишь верхушка, а остальные идут по
// алфавиту.
std::vector<std::string> Catalog::sortNames()
{
    return {
        "hub/sort/toplists"_i18n,
        "hub/sort/alphabet"_i18n,
        "hub/sort/players"_i18n,
        "hub/sort/newest"_i18n,
        "hub/sort/installed"_i18n,
    };
}

std::string Catalog::genreLabel(const std::string& value)
{
    // Восемнадцать жанров, зафиксированных pipeline/build_db.py. В данных они хранятся
    // русскими строками — они же служат значением фильтра, и менять их нельзя.
    // Незнакомое значение показываем как есть: лучше русское слово, чем пустое
    // место.
    static const std::unordered_map<std::string, const char*> keys = {
        { "Экшен", "hub/genre/action" },
        { "Шутеры", "hub/genre/shooter" },
        { "Спорт", "hub/genre/sports" },
        { "Вечеринки", "hub/genre/party" },
        { "Головоломки", "hub/genre/puzzle" },
        { "Приключения", "hub/genre/adventure" },
        { "Гонки", "hub/genre/racing" },
        { "Ролевые", "hub/genre/rpg" },
        { "Файтинги", "hub/genre/fighting" },
        { "Стратегии", "hub/genre/strategy" },
        { "Симуляторы", "hub/genre/simulation" },
        { "Настольные", "hub/genre/board" },
        { "Музыкальные", "hub/genre/music" },
        { "Сюжетные", "hub/genre/story" },
        { "Тренировки", "hub/genre/training" },
        { "Обучающие", "hub/genre/education" },
        { "Пинбол", "hub/genre/pinball" },
        { "Приложения", "hub/genre/apps" },
    };

    auto it = keys.find(value);
    return it == keys.end() ? value : brls::getStr(it->second);
}

// --------------------------------------------------------------------------
// загрузка
// --------------------------------------------------------------------------

bool Catalog::open(const std::string& directory)
{
    catalogPath = directory + "catalog.bin";
    detailsPath = directory + "details.bin";

    // Проверяем только доступность: читать пять мегабайт описаний при запуске
    // незачем, они нужны по одной записи при открытии карточки.
    for (const std::string& path : { catalogPath, detailsPath })
    {
        std::FILE* probe = std::fopen(path.c_str(), "rb");
        if (!probe)
        {
            lastError = "не открылся " + path;
            return false;
        }
        std::fclose(probe);
    }

    return true;
}

void Catalog::loadBriefs()
{
    perf::Scope timer("каталог в память");

    std::vector<catalogq::Brief> loaded;
    std::vector<std::string> names;
    std::vector<unsigned char> dictionary;

    const std::vector<unsigned char> data = readWhole(catalogPath);

    Reader r(data.data(), data.size());
    if (data.empty() || !r.magic(CATALOG_MAGIC) || r.u32() != FORMAT_VERSION)
    {
        brls::Logger::error("каталог: {} не читается или чужого формата", catalogPath);
    }
    else
    {
        const unsigned games = r.u32();
        const unsigned kinds = r.u32();

        names.reserve(kinds);
        for (unsigned i = 0; i < kinds && r.ok(); i++)
            names.push_back(r.str16());

        loaded.reserve(games);
        for (unsigned i = 0; i < games && r.ok(); i++)
        {
            catalogq::Brief b;
            b.nsuid      = r.str16();
            b.title      = r.str16();
            b.sortTitle  = r.str16();
            b.titleId    = r.str16();
            b.boxArt     = r.str16();
            b.minPlayers = (int)r.u16();
            b.maxPlayers = (int)r.u16();
            b.mentions   = (int)r.u16();
            b.score      = (int)r.u16();
            b.year       = (int)r.u16();
            b.romSize    = r.i64();

            const unsigned char flags = r.u8();
            b.hasRussian              = (flags & 1) != 0;
            b.isRetro                 = (flags & 2) != 0;

            const unsigned char genres = r.u8();
            b.genreIds.reserve(genres);
            for (unsigned char g = 0; g < genres; g++)
                b.genreIds.push_back((int)r.u8());

            b.detailsOffset = r.u64();
            b.detailsPacked = r.u32();
            b.detailsRaw    = r.u32();

            // Ключ поиска считаем здесь, а не храним в файле: это тот же title
            // в нижнем регистре, и лишние 268 КБ в romfs ради него не нужны.
            b.searchTitle = catalogq::searchKey(b.title);

            loaded.push_back(std::move(b));
        }

        if (!r.ok())
            brls::Logger::error("каталог: {} оборван на игре {}", catalogPath, loaded.size());
    }

    // Словарь из details.bin: без него ни одна запись карточки не развернётся.
    std::FILE* file = std::fopen(detailsPath.c_str(), "rb");
    if (file)
    {
        unsigned char head[12] = {};
        if (std::fread(head, 1, sizeof(head), file) == sizeof(head)
            && std::memcmp(head, DETAILS_MAGIC, 4) == 0)
        {
            unsigned version = 0, size = 0;
            std::memcpy(&version, head + 4, 4);
            std::memcpy(&size, head + 8, 4);

            if (version == FORMAT_VERSION && size > 0 && size <= (1u << 20))
            {
                dictionary.resize(size);
                if (std::fread(dictionary.data(), 1, size, file) != size)
                    dictionary.clear();
            }
        }
        std::fclose(file);
    }
    if (dictionary.empty())
        brls::Logger::error("каталог: словарь из {} не прочитался", detailsPath);

    // Флаг готовности выставляется при любом исходе: queryBrief его дожидается,
    // и невыставленный подвесил бы рабочий поток навсегда вместо пустого
    // каталога с честной надписью «ничего не найдено».
    {
        std::lock_guard<std::mutex> lock(briefsMutex);

        briefs    = std::move(loaded);
        allGenres = std::move(names);
        dict      = std::move(dictionary);

        byId.reserve(briefs.size());
        for (size_t i = 0; i < briefs.size(); i++)
            byId[briefs[i].nsuid] = i;

        briefsLoaded = true;
    }
    briefsReady.notify_all();

    brls::Logger::info("каталог: в памяти {} игр, жанров {}", briefs.size(), allGenres.size());
}

// --------------------------------------------------------------------------
// сетка
// --------------------------------------------------------------------------

std::vector<Game> Catalog::queryBrief(const Filter& f) const
{
    // Ждём готовности: первый запрос из интерфейса приходит раньше, чем чтение
    // закончится. Ожидание бывает ровно один раз — на первом запросе после
    // запуска, и приходится оно на рабочий поток.
    {
        std::unique_lock<std::mutex> lock(briefsMutex);
        briefsReady.wait(lock, [this] { return briefsLoaded; });
    }

    perf::Scope timer("выборка каталога");

    std::lock_guard<std::mutex> lock(briefsMutex);

    const int genreId = f.genre.empty() ? -1 : catalogq::findGenre(allGenres, f.genre);
    if (!f.genre.empty() && genreId < 0)
        return {};  // жанра нет в каталоге — и игр с ним тоже

    const std::vector<const catalogq::Brief*> hits = catalogq::select(briefs, f, genreId);

    std::vector<Game> out;
    out.reserve(hits.size());
    for (const catalogq::Brief* b : hits)
    {
        Game g;
        g.nsuid         = b->nsuid;
        g.title         = b->title;
        g.titleId       = b->titleId;
        g.sameScreenMin = b->minPlayers;
        g.sameScreenMax = b->maxPlayers;
        g.boxArtFile    = b->boxArt;
        g.topMentions   = b->mentions;
        out.push_back(std::move(g));
    }

    perf::count(perf::Counter::CatalogQueries);
    perf::count(perf::Counter::CatalogMs, (long long)timer.elapsedMs());
    return out;
}

std::vector<std::string> Catalog::genreNames() const
{
    // Не ждём готовности: метод зовётся из UI-потока при нажатии на чип, и
    // ожидание на условной переменной означало бы замерший интерфейс на всё
    // время первичной загрузки. Пока каталога нет, отвечаем пустым списком —
    // вызывающий скажет об этом человеку.
    std::lock_guard<std::mutex> lock(briefsMutex);
    if (!briefsLoaded)
        return {};

    std::vector<std::string> sorted = allGenres;
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

// --------------------------------------------------------------------------
// карточка
// --------------------------------------------------------------------------

bool Catalog::detailsFor(const std::string& nsuid, Details& out) const
{
    std::lock_guard<std::mutex> lock(detailsMutex);

    // Карточка спрашивает одну и ту же игру четырьмя вызовами подряд: описание,
    // жанры, снимки, ролики. Читать и разворачивать запись четырежды незачем,
    // поэтому держим последнюю.
    if (!nsuid.empty() && cached.nsuid == nsuid)
    {
        out = cached;
        return true;
    }

    unsigned long long offset = 0;
    unsigned packed = 0, raw = 0;
    {
        std::lock_guard<std::mutex> briefLock(briefsMutex);
        auto it = byId.find(nsuid);
        if (it == byId.end() || dict.empty())
            return false;
        offset = briefs[it->second].detailsOffset;
        packed = briefs[it->second].detailsPacked;
        raw    = briefs[it->second].detailsRaw;
    }

    std::vector<unsigned char> blob(packed);
    std::FILE* file = std::fopen(detailsPath.c_str(), "rb");
    if (!file)
        return false;
    std::fseek(file, static_cast<long>(offset), SEEK_SET);
    const bool read = std::fread(blob.data(), 1, packed, file) == packed;
    std::fclose(file);
    if (!read)
    {
        brls::Logger::error("карточка: не прочиталась запись {} из {}", nsuid, detailsPath);
        return false;
    }

    // Сырой deflate с общим словарём: записи короткие, и поодиночке они жались
    // бы вдвое хуже. Словарь один на все и прочитан при загрузке каталога.
    std::vector<unsigned char> body(raw);
    z_stream z {};
    if (inflateInit2(&z, -15) != Z_OK)
        return false;
    inflateSetDictionary(&z, dict.data(), (uInt)dict.size());

    z.next_in   = blob.data();
    z.avail_in  = (uInt)blob.size();
    z.next_out  = body.data();
    z.avail_out = (uInt)body.size();

    const int rc = inflate(&z, Z_FINISH);
    inflateEnd(&z);

    if (rc != Z_STREAM_END)
    {
        brls::Logger::error("карточка: запись {} не развернулась (zlib {})", nsuid, rc);
        return false;
    }

    Details d;
    d.nsuid = nsuid;

    Reader r(body.data(), body.size());
    d.publisher  = r.str16();
    d.languages  = r.str16();
    d.background = r.str16();

    const unsigned char flags = r.u8();
    d.hasOnline               = (flags & 1) != 0;
    d.noTabletop              = (flags & 2) != 0;
    d.hasDemo                 = (flags & 4) != 0;

    const std::string noteEn = r.str16(), noteRu = r.str16();
    const std::string headEn = r.str16(), headRu = r.str16();
    const std::string textEn = r.str32(), textRu = r.str32();

    // Перевод есть не у всех игр: там, где его нет, правильный ответ — показать
    // оригинал, а не пустоту.
    const bool russian = lang == Language::Russian;
    auto pick          = [russian](const std::string& en, const std::string& ru) {
        return russian && !ru.empty() ? ru : en;
    };
    d.playersNote = pick(noteEn, noteRu);
    d.headline    = pick(headEn, headRu);
    d.description = pick(textEn, textRu);

    const unsigned char genres = r.u8();
    for (unsigned char i = 0; i < genres; i++)
        r.u8();  // жанры берём из Brief, здесь они лежат для полноты записи

    const unsigned char shots = r.u8();
    d.screenshots.reserve(shots);
    for (unsigned char i = 0; i < shots; i++)
        d.screenshots.push_back(r.str16());

    const unsigned char videos = r.u8();
    d.videos.reserve(videos);
    for (unsigned char i = 0; i < videos; i++)
        d.videos.push_back(r.str16());

    if (!r.ok())
    {
        brls::Logger::error("карточка: запись {} оборвана", nsuid);
        return false;
    }

    cached = std::move(d);
    out    = cached;
    return true;
}

Game Catalog::byNsuid(const std::string& nsuid) const
{
    perf::Scope timer("");

    Game g;
    {
        std::lock_guard<std::mutex> lock(briefsMutex);
        auto it = byId.find(nsuid);
        if (it == byId.end())
            return g;

        const catalogq::Brief& b = briefs[it->second];
        g.nsuid                  = b.nsuid;
        g.title                  = b.title;
        g.titleId                = b.titleId;
        g.sameScreenMin          = b.minPlayers;
        g.sameScreenMax          = b.maxPlayers;
        g.boxArtFile             = b.boxArt;
        g.releaseYear            = b.year;
        g.romSizeBytes           = b.romSize < 0 ? 0 : b.romSize;
        g.hasRussian             = b.hasRussian;
        g.topMentions            = b.mentions;
    }

    Details d;
    if (detailsFor(nsuid, d))
    {
        g.playersNote     = std::move(d.playersNote);
        g.backgroundColor = std::move(d.background);
        g.headline        = std::move(d.headline);
        g.description     = std::move(d.description);
        g.publisher       = std::move(d.publisher);
        g.languages       = std::move(d.languages);
        g.hasOnline       = d.hasOnline;
        g.noTabletop      = d.noTabletop;
        g.hasDemo         = d.hasDemo;
    }

    perf::count(perf::Counter::CatalogQueries);
    perf::count(perf::Counter::CatalogMs, (long long)timer.elapsedMs());
    return g;
}

std::vector<std::string> Catalog::genresOf(const std::string& nsuid) const
{
    std::lock_guard<std::mutex> lock(briefsMutex);

    auto it = byId.find(nsuid);
    if (it == byId.end())
        return {};

    std::vector<std::string> out;
    for (int id : briefs[it->second].genreIds)
        if (id >= 0 && id < (int)allGenres.size())
            out.push_back(allGenres[id]);
    return out;
}

std::vector<std::string> Catalog::screenshots(const std::string& nsuid) const
{
    Details d;
    return detailsFor(nsuid, d) ? std::move(d.screenshots) : std::vector<std::string>();
}

std::vector<std::string> Catalog::videos(const std::string& nsuid) const
{
    Details d;
    return detailsFor(nsuid, d) ? std::move(d.videos) : std::vector<std::string>();
}
