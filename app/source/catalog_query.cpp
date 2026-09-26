#include "catalog_query.hpp"

#include <algorithm>

namespace catalogq
{

std::string searchKey(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    // Кириллицу разбираем по байтам UTF-8: вся она лежит в двухбайтных
    // D0 xx и D1 xx, и заводить ради неё локаль или ICU незачем.
    for (size_t i = 0; i < text.size(); i++)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c >= 'A' && c <= 'Z')
        {
            out += char(c - 'A' + 'a');
            continue;
        }

        if ((c == 0xD0 || c == 0xD1) && i + 1 < text.size())
        {
            const unsigned char n = static_cast<unsigned char>(text[i + 1]);

            // Ё и ё сводим к е: букву ё почти никто не набирает, и «ежик» должен
            // находить «Ёжик». Ключ из названия и ключ из запроса считает одна и
            // та же функция, так что сравнение от этого не разъезжается.
            if ((c == 0xD0 && n == 0x81) || (c == 0xD1 && n == 0x91))
            {
                out += "\xD0\xB5";  // е
                i++;
                continue;
            }
            if (c == 0xD0 && n >= 0x90 && n <= 0x9F)  // А–П → а–п
            {
                out += char(0xD0);
                out += char(n + 0x20);
                i++;
                continue;
            }
            if (c == 0xD0 && n >= 0xA0 && n <= 0xAF)  // Р–Я → р–я
            {
                out += char(0xD1);
                out += char(n - 0x20);
                i++;
                continue;
            }
        }

        out += char(c);
    }
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
