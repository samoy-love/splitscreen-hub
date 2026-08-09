#include "app_state.hpp"

#include <cstdio>

AppState& AppState::get()
{
    static AppState state;
    return state;
}

void AppState::decorate(Game& game) const
{
    game.installed = !game.titleId.empty() && installedTitleIds.count(game.titleId) > 0;
    game.favorite  = library.isFavorite(game.nsuid);
}

void AppState::decorate(std::vector<Game>& games) const
{
    for (Game& g : games)
        decorate(g);
}

int AppState::installedCount(const std::vector<Game>& games) const
{
    int n = 0;
    for (const Game& g : games)
        n += g.installed;
    return n;
}

std::string formatSize(long long bytes)
{
    if (bytes <= 0)
        return "";

    char buf[32];
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0)
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", gb);
    else
        std::snprintf(buf, sizeof(buf), "%.0f МБ", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

int languageCount(const std::string& languages)
{
    if (languages.empty())
        return 0;
    int n = 1;
    for (char c : languages)
        n += c == ',';
    return n;
}
