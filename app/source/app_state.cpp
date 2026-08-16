#include "app_state.hpp"

AppState& AppState::get()
{
    static AppState state;
    return state;
}

void AppState::decorate(Game& game) const
{
    game.installed = !game.titleId.empty() && installedTitleIds.count(game.titleId) > 0;
    game.favorite  = library.isFavorite(game.nsuid);
    game.hidden    = library.isHidden(game.nsuid);
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
