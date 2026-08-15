#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog_query.hpp"

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


/// Каталог игр из romfs.
///
/// Данные лежат в двух файлах, а не в базе: catalog.bin читается целиком при
/// запуске (всё, что показывает сетка, — 0.45 МБ), details.bin отдаёт по записи
/// на игру при открытии карточки. Почему не SQLite — в make_ship_data.py.
class Catalog
{
  public:
    /// Язык текстов каталога. Названия игр не переводятся никогда — это имена
    /// собственные; язык влияет на подзаголовок, описание и пояснение об
    /// игроках.
    enum class Language
    {
        English,
        Russian,
    };

    /// Английский по умолчанию: у части игр перевода нет вовсе, и оригинал —
    /// единственное, что можно показать наверняка.
    void setLanguage(Language language) { lang = language; }
    Language language() const { return lang; }

    /// Названия сортировок в порядке Filter::sort — уже переведённые.
    ///
    /// Функция, а не константа: перевод берётся из i18n, а он готов только
    /// после инициализации borealis, тогда как статический вектор
    /// инициализировался бы раньше и остался бы на языке ключей.
    static std::vector<std::string> sortNames();

    /// Подпись жанра на языке интерфейса.
    ///
    /// В данных жанры хранятся русскими строками — они же служат значением
    /// фильтра, и менять их нельзя. Но при английском интерфейсе показывать
    /// «Экшен» в чипе, в списке и в карточке неправильно, поэтому подпись
    /// берётся из i18n по слагу, а само значение остаётся прежним.
    static std::string genreLabel(const std::string& value);

    ~Catalog();

    /// Проверяет, что файлы каталога на месте. directory — с завершающим «/».
    bool open(const std::string& directory);

    /// Причина неудачи последнего open(). Ошибка возникает до появления
    /// интерфейса, поэтому её некуда показать — её забирает журнал загрузки.
    std::string lastError;

    /// Читает catalog.bin в память. Зовётся один раз при старте, в рабочем
    /// потоке: файл небольшой, но лежит в romfs.
    void loadBriefs();

    /// Выборка для сетки: только то, что видно на плитке. Ждёт готовности
    /// каталога в памяти, поэтому зовётся из рабочего потока.
    std::vector<Game> queryBrief(const Filter& filter) const;

    /// Все жанры каталога по алфавиту. Пустой список — каталог ещё грузится.
    std::vector<std::string> genreNames() const;

    Game byNsuid(const std::string& nsuid) const;
    /// Жанры конкретной игры — для строки в карточке.
    std::vector<std::string> genresOf(const std::string& nsuid) const;
    std::vector<std::string> screenshots(const std::string& nsuid) const;
    std::vector<std::string> videos(const std::string& nsuid) const;

  private:
    Language lang = Language::English;

    std::string catalogPath;
    std::string detailsPath;

    /// Всё, что показывает сетка: 3489 записей, около мегабайта вместе с
    /// накладными расходами std::string.
    std::vector<catalogq::Brief> briefs;
    /// nsuid -> номер в briefs. Карточка открывается по нему.
    std::unordered_map<std::string, size_t> byId;
    /// Все жанры каталога, по одному разу. Brief::genreIds — индексы в нём.
    std::vector<std::string> allGenres;
    /// Общий словарь для распаковки записей карточек.
    std::vector<unsigned char> dict;

    /// Готовность каталога в памяти. Первый запрос из интерфейса приходит
    /// раньше, чем чтение закончится, и должен его дождаться.
    mutable std::mutex briefsMutex;
    mutable std::condition_variable briefsReady;
    bool briefsLoaded = false;

    /// Распакованная запись карточки.
    struct Details
    {
        std::string nsuid;
        std::string publisher;
        std::string languages;
        std::string background;
        std::string playersNote;
        std::string headline;
        std::string description;
        std::vector<std::string> screenshots;
        std::vector<std::string> videos;
        bool hasOnline  = false;
        bool noTabletop = false;
        bool hasDemo    = false;
    };

    /// Последняя прочитанная запись. Карточка спрашивает одну и ту же игру
    /// четырьмя вызовами подряд, и разворачивать её четырежды незачем.
    mutable std::mutex detailsMutex;
    mutable Details cached;

    /// Копия, а не указатель на cached: запись читают три io-потока сразу
    /// (карточка и список библиотеки), и указатель наружу оставался бы жить
    /// после снятия detailsMutex — соседний поток успевал подменить cached
    /// прямо во время копирования строк из него.
    ///
    /// false — записи нет или она не развернулась.
    bool detailsFor(const std::string& nsuid, Details& out) const;
};
