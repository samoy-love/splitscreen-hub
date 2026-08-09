#pragma once

#include <string>
#include <vector>

struct Game
{
    std::string nsuid;
    std::string title;
    std::string titleId;
    int sameScreenMin = 1;
    int sameScreenMax = 2;
    std::string playersNote;
    std::string boxArtFile;
    std::string backgroundColor;
    std::string headline;
    std::string description;
    std::string publisher;
    int releaseYear = 0;
    std::string languages;
    long long romSizeBytes = 0;
    bool hasOnline = false;
    bool noTabletop = false;
    bool hasDemo = false;
    bool hasRussian = false;
    // Оценка показывается вместе с числом голосов и источником: «87% из
    // 74 978, Steam» честнее и информативнее абстрактного числа.
    int rating = 0;  // 0-100, 0 — оценки нет
    int ratingVotes = 0;
    std::string ratingSource;
    /// В скольких подборках «лучших couch co-op» игра названа.
    int topMentions = 0;

    // заполняется уже в приложении
    bool installed = false;
    bool favorite = false;
};

struct Filter
{
    int minPlayers = 2;
    std::string genre;       // пусто — любой
    bool onlyInstalled = false;
    bool onlyRussian = false;
    std::string search;      // пусто — без поиска
    int sort = 0;            // индекс в Catalog::SORT_NAMES
};

/// Каталог игр из romfs. Держит открытым один SQLite-коннект на всё время работы.
class Catalog
{
  public:
    /// Порядок совпадает с Filter::sort.
    ///
    /// Первые четыре — то полезное, что есть в самом eShop (сортировки по цене
    /// и по скидке нам не нужны, цену мы не переносим). Остальные eShop не
    /// умеет вовсе, но для выбора игры на вечер они важнее: сколько игроков,
    /// что уже стоит на консоли, что влезет на карту памяти.
    static const std::vector<std::string> SORT_NAMES;

    ~Catalog();

    /// Возвращает false, если базу открыть не удалось.
    bool open(const std::string& path);

    /// Полная выборка — со всеми текстами. Нужна редко.
    std::vector<Game> query(const Filter& filter) const;

    /// Выборка для сетки: только то, что видно на плитке. Без описаний,
    /// которые при 3489 играх дают лишние мегабайты на каждый фильтр.
    std::vector<Game> queryBrief(const Filter& filter) const;
    int count(const Filter& filter) const;

    /// Сколько игр найдётся для каждого порога — для подписей на кнопках фильтра.
    int countAtLeast(int players) const;

    Game byNsuid(const std::string& nsuid) const;
    std::vector<std::string> genres() const;
    /// Жанры конкретной игры — для чипов в карточке.
    std::vector<std::string> genresOf(const std::string& nsuid) const;
    std::vector<std::string> screenshots(const std::string& nsuid) const;
    std::vector<std::string> videos(const std::string& nsuid) const;

  private:
    struct sqlite3* db = nullptr;

    std::string buildWhere(const Filter& filter) const;
    const char* orderBy(const Filter& filter) const;
    std::vector<std::string> media(const std::string& nsuid, const char* kind) const;
};
