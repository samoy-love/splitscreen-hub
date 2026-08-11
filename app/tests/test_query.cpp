// Тесты отбора и порядка игр — главной логики приложения.
//
// Каждое движение фильтра проходит через catalogq::select, и до сих пор эта
// часть проверялась только запуском на консоли: собрать сборку, скопировать
// 77 МБ на карту, потыкать фильтры глазами. Здесь она проверяется за секунду и
// без консоли — модуль намеренно не знает ни про borealis, ни про romfs.

#include <cstdio>
#include <string>
#include <vector>

#include "catalog_query.hpp"

namespace
{

int failures = 0;

void check(const std::string& what, bool ok, const std::string& detail = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "ok  " : "ПРОВАЛ", what.c_str(),
                detail.empty() ? "" : " — ", detail.c_str());
    if (!ok)
        failures++;
}

/// Игра с осмысленными значениями по умолчанию: тест меняет только то поле,
/// которое проверяет, и не тонет в заполнении двух десятков остальных.
catalogq::Brief game(const std::string& title, int maxPlayers = 4)
{
    catalogq::Brief b;
    b.nsuid       = "id-" + title;
    b.title       = title;
    b.searchTitle = catalogq::searchKey(title);

    // sortTitle приходит из данных уже нормализованным: нижний регистр и без
    // ведущих артиклей. Повторяем это здесь, иначе тесты порядка проверяли бы
    // не то, что происходит на самом деле.
    std::string sort = catalogq::searchKey(title);
    for (const char* article : { "the ", "a ", "an " })
        if (sort.rfind(article, 0) == 0)
        {
            sort = sort.substr(std::string(article).size());
            break;
        }
    b.sortTitle = sort;

    b.minPlayers = 1;
    b.maxPlayers = maxPlayers;
    return b;
}

/// Названия отобранных игр по порядку — с ними удобно сравнивать.
std::vector<std::string> titles(const std::vector<const catalogq::Brief*>& hits)
{
    std::vector<std::string> out;
    for (const catalogq::Brief* b : hits)
        out.push_back(b->title);
    return out;
}

std::string join(const std::vector<std::string>& items)
{
    std::string out;
    for (const std::string& s : items)
        out += (out.empty() ? "" : ", ") + s;
    return "[" + out + "]";
}

void expect(const std::string& what, const std::vector<std::string>& got,
            const std::vector<std::string>& want)
{
    check(what, got == want, got == want ? "" : "получили " + join(got) + ", ждали " + join(want));
}

// ---------------------------------------------------------------- отбор

void testFilters()
{
    std::printf("\nотбор:\n");

    std::vector<catalogq::Brief> games = {
        game("Alpha", 2),
        game("Beta", 4),
        game("Gamma", 8),
    };
    games[0].isRetro    = true;
    games[1].hasRussian = true;
    games[2].mentions   = 3;

    Filter f;
    f.sort = 1;  // по алфавиту: порядок здесь не предмет проверки

    f.minPlayers = 2;
    expect("порог игроков 2 — ретро скрыто по умолчанию",
           titles(catalogq::select(games, f, -1)), { "Beta", "Gamma" });

    f.minPlayers = 8;
    expect("порог игроков 8", titles(catalogq::select(games, f, -1)), { "Gamma" });

    f.minPlayers = 2;
    f.showRetro  = true;
    expect("с ретро", titles(catalogq::select(games, f, -1)), { "Alpha", "Beta", "Gamma" });

    f.showRetro   = false;
    f.onlyRussian = true;
    expect("только с русским", titles(catalogq::select(games, f, -1)), { "Beta" });

    f.onlyRussian = false;
    f.onlyNotable = true;
    expect("только из подборок", titles(catalogq::select(games, f, -1)), { "Gamma" });
    f.onlyNotable = false;

    // Жанры: у Beta первый, у Gamma второй.
    games[1].genreIds = { 0 };
    games[2].genreIds = { 1 };
    expect("жанр 0", titles(catalogq::select(games, f, 0)), { "Beta" });
    expect("жанр 1", titles(catalogq::select(games, f, 1)), { "Gamma" });
    expect("жанра нет ни у кого", titles(catalogq::select(games, f, 7)), {});
}

void testSearch()
{
    std::printf("\nпоиск:\n");

    std::vector<catalogq::Brief> games = {
        game("The Jackbox Party Pack"),
        game("Battle of Polytopia"),
        game("Overcooked 2"),
    };

    Filter f;
    f.sort = 1;

    f.search = "The Jackbox";
    expect("артикль в начале запроса", titles(catalogq::select(games, f, -1)),
           { "The Jackbox Party Pack" });

    f.search = "jackbox";
    expect("без учёта регистра", titles(catalogq::select(games, f, -1)),
           { "The Jackbox Party Pack" });

    f.search = "COOKED";
    expect("подстрока в середине", titles(catalogq::select(games, f, -1)), { "Overcooked 2" });

    f.search = "чего-то нет";
    expect("ничего не нашлось", titles(catalogq::select(games, f, -1)), {});

    f.search.clear();
    // Порядок — по sortTitle, где артикль отброшен: «The Jackbox» встаёт на «j»,
    // между «battle» и «overcooked».
    expect("пустой запрос не фильтрует", titles(catalogq::select(games, f, -1)),
           { "Battle of Polytopia", "The Jackbox Party Pack", "Overcooked 2" });
}

// ---------------------------------------------------------------- порядок

void testSorting()
{
    std::printf("\nпорядок:\n");

    std::vector<catalogq::Brief> games = {
        game("Zebra", 2),
        game("Apple", 8),
        game("Mango", 4),
    };
    games[0].year = 2020; games[0].romSize = 100; games[0].mentions = 0;
    games[1].year = 2018; games[1].romSize = -1;  games[1].mentions = 5; games[1].bestPos = 2;
    games[2].year = 2022; games[2].romSize = 50;  games[2].mentions = 5; games[2].bestPos = 1;

    Filter f;

    f.sort = 0;
    expect("популярные: упоминания вниз, при равенстве — место в подборке",
           titles(catalogq::select(games, f, -1)), { "Mango", "Apple", "Zebra" });

    f.sort = 1;
    expect("по алфавиту", titles(catalogq::select(games, f, -1)),
           { "Apple", "Mango", "Zebra" });

    f.sort = 2;
    expect("больше игроков", titles(catalogq::select(games, f, -1)),
           { "Apple", "Mango", "Zebra" });

    f.sort = 3;
    expect("сначала новые", titles(catalogq::select(games, f, -1)),
           { "Mango", "Zebra", "Apple" });

    f.sort = 4;
    expect("по размеру, неизвестный в конец", titles(catalogq::select(games, f, -1)),
           { "Mango", "Zebra", "Apple" });

    f.sort = 5;
    expect("«сначала мои» упорядочивается по алфавиту, остальное делает вкладка",
           titles(catalogq::select(games, f, -1)), { "Apple", "Mango", "Zebra" });
}

void testSortStability()
{
    std::printf("\nустойчивость порядка:\n");

    // Одинаковые ключи первого уровня: без sortTitle вторым ключом такие игры
    // перемешивались бы от запроса к запросу.
    std::vector<catalogq::Brief> games = {
        game("Delta", 4), game("Charlie", 4), game("Echo", 4),
    };
    for (catalogq::Brief& b : games)
    {
        b.year     = 2020;
        b.romSize  = 42;
        b.mentions = 1;
        b.bestPos  = 1;
    }

    Filter f;
    for (int sort : { 0, 2, 3, 4 })
    {
        f.sort = sort;
        expect("сортировка " + std::to_string(sort) + ": при равных ключах — по названию",
               titles(catalogq::select(games, f, -1)), { "Charlie", "Delta", "Echo" });
    }
}

void testArticleStripping()
{
    std::printf("\nартикли в порядке:\n");

    std::vector<catalogq::Brief> games = {
        game("The Zebra"),
        game("Apple"),
    };

    Filter f;
    f.sort = 1;
    expect("«The Zebra» встаёт на «z», а не на «t»",
           titles(catalogq::select(games, f, -1)), { "Apple", "The Zebra" });
}

void testEmpty()
{
    std::printf("\nкрайние случаи:\n");

    std::vector<catalogq::Brief> none;
    Filter f;
    expect("пустой каталог", titles(catalogq::select(none, f, -1)), {});

    std::vector<catalogq::Brief> games = { game("Solo", 1) };
    f.minPlayers = 2;
    expect("никто не проходит порог", titles(catalogq::select(games, f, -1)), {});
}

}  // namespace

int main()
{
    std::printf("проверки отбора и порядка\n");

    testFilters();
    testSearch();
    testSorting();
    testSortStability();
    testArticleStripping();
    testEmpty();

    std::printf("\n%s\n", failures == 0 ? "все проверки прошли"
                                        : "провалов: " + std::to_string(failures) == ""
                                              ? ""
                                              : ("провалов: " + std::to_string(failures)).c_str());
    return failures == 0 ? 0 : 1;
}
