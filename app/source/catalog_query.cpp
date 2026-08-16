#include "catalog_query.hpp"

#include <algorithm>

namespace catalogq
{

std::string searchKey(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    return out;
}

int findGenre(const std::vector<std::string>& names, const std::string& genre)
{
    for (size_t i = 0; i < names.size(); i++)
        if (names[i] == genre)
            return static_cast<int>(i);
    return -1;
}

std::vector<const Brief*> select(const std::vector<Brief>& briefs, const Filter& filter,
                                 int genreId)
{
    const std::string needle = searchKey(filter.search);

    std::vector<const Brief*> hits;
    hits.reserve(briefs.size());

    for (const Brief& b : briefs)
    {
        if (b.maxPlayers < filter.minPlayers)
            continue;
        if (!filter.showRetro && b.isRetro)
            continue;
        if (filter.onlyRussian && !b.hasRussian)
            continue;
        if (filter.onlyNotable && b.mentions == 0)
            continue;
        if (genreId >= 0
            && std::find(b.genreIds.begin(), b.genreIds.end(), genreId) == b.genreIds.end())
            continue;
        if (!needle.empty() && b.searchTitle.find(needle) == std::string::npos)
            continue;

        hits.push_back(&b);
    }

    // Вторым ключом всюду sortTitle, чтобы порядок был устойчив: без него игры
    // с одинаковым числом игроков или годом выпуска перемешивались бы от
    // запроса к запросу.
    auto byTitle = [](const Brief* a, const Brief* b) { return a->sortTitle < b->sortTitle; };

    switch (filter.sort)
    {
        case 0:  // популярные: сначала те, о которых сошлись внешние источники
            //
            // Порядок задаёт готовый счёт согласия, а не число упоминаний.
            // Считать упоминания напрямую нельзя: список из 75 игр называет
            // всё подряд, пять статей одного сайта — это одно мнение, а тред на
            // 900 комментариев дешевле треда на 60. Всё это сведено в score при
            // сборке данных; здесь остаётся сравнить два числа.
            std::sort(hits.begin(), hits.end(), [&](const Brief* a, const Brief* b) {
                if ((a->mentions == 0) != (b->mentions == 0))
                    return b->mentions == 0;
                if (a->score != b->score)
                    return a->score > b->score;
                return byTitle(a, b);
            });
            break;

        case 2:  // больше игроков
            std::sort(hits.begin(), hits.end(), [&](const Brief* a, const Brief* b) {
                if (a->maxPlayers != b->maxPlayers)
                    return a->maxPlayers > b->maxPlayers;
                return byTitle(a, b);
            });
            break;

        case 3:  // сначала новые
            std::sort(hits.begin(), hits.end(), [&](const Brief* a, const Brief* b) {
                if (a->year != b->year)
                    return a->year > b->year;
                return byTitle(a, b);
            });
            break;

        default:  // название А→Я и «сначала установленные» — тот доупорядочивается снаружи
            std::sort(hits.begin(), hits.end(), byTitle);
            break;
    }

    return hits;
}

}  // namespace catalogq
