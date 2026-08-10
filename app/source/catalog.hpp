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
    /// В скольких подборках «лучших couch co-op» игра названа.
    int topMentions = 0;

    // заполняется уже в приложении
    bool installed = false;
    bool favorite = false;
    bool hidden = false;
};

struct Filter
{
    int minPlayers = 2;
    std::string genre;       // пусто — любой
    bool onlyInstalled = false;
    bool onlyRussian = false;
    std::string search;      // пусто — без поиска
    /// Переиздания аркад: 474 игры, 13% каталога. По умолчанию скрыты, иначе
    /// половина выдачи — «Arcade Archives ...».
    bool showRetro = false;
    /// Показывать спрятанные вручную. Сами игры хранятся в Library.
    bool showHidden = false;
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

    /// Причина неудачи последнего open(): код sqlite и его текст. Ошибка
    /// возникает до появления интерфейса, поэтому её некуда показать — её
    /// забирает журнал загрузки.
    std::string lastError;

  private:
    int countGames();
    bool openInMemory(const std::string& path);

  public:

    /// Выборка для сетки: только то, что видно на плитке. Без описаний,
    /// которые при 3489 играх дают лишние мегабайты на каждый фильтр.
    std::vector<Game> queryBrief(const Filter& filter) const;
    int count(const Filter& filter) const;

    Game byNsuid(const std::string& nsuid) const;
    std::vector<std::string> genres() const;
    /// Жанры конкретной игры — для чипов в карточке.
    std::vector<std::string> genresOf(const std::string& nsuid) const;
    std::vector<std::string> screenshots(const std::string& nsuid) const;
    std::vector<std::string> videos(const std::string& nsuid) const;

  private:
    struct sqlite3* db = nullptr;

    /// Собирает условие WHERE. Значения не подставляются в текст, а
    /// складываются в params для последующего bindParams().
    std::string buildWhere(const Filter& filter, std::vector<std::string>& params) const;
    static void bindParams(struct sqlite3_stmt* st, const std::vector<std::string>& params);
    const char* orderBy(const Filter& filter) const;
    std::vector<std::string> media(const std::string& nsuid, const char* kind) const;
};
