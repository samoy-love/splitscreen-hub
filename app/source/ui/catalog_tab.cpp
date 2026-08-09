#include "ui/catalog_tab.hpp"

#include <algorithm>

#include "app_state.hpp"
#include "ui/game_activity.hpp"
#include "ui/game_tile.hpp"

namespace
{

// Пороги фильтра. Это именно «от N», а не точное совпадение: когда за консолью
// четверо, игра на шестерых тоже подходит.
const std::vector<int> THRESHOLDS = { 2, 3, 4, 6, 8 };

// индекс «сначала мои» в Catalog::SORT_NAMES
constexpr int SORT_INSTALLED_FIRST = 7;

std::vector<Game>* rowsSource = nullptr;
std::function<void(const std::string&)> rowsOnSelect;

}  // namespace

// --------------------------------------------------------------------------
// строка сетки
// --------------------------------------------------------------------------

GameRow::GameRow()
{
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setHeight(222);
    this->setPaddingBottom(8);

    for (int i = 0; i < COLUMNS; i++)
    {
        auto* tile = new GameTile();
        tile->setMarginRight(12);
        this->addView(tile);
        slots.push_back(tile);
    }
}

void GameRow::setGames(const std::vector<Game>& games, size_t from,
                       const std::function<void(const std::string&)>& onSelect)
{
    for (int i = 0; i < COLUMNS; i++)
    {
        size_t idx = from + i;
        auto* tile = static_cast<GameTile*>(slots[i]);

        if (idx >= games.size())
        {
            // хвост последней строки: место занимаем, но фокус туда не пускаем
            tile->setVisibility(brls::Visibility::INVISIBLE);
            tile->setFocusable(false);
            continue;
        }

        tile->setVisibility(brls::Visibility::VISIBLE);
        tile->setGame(games[idx]);
        tile->setFocusable(true);
        tile->setOnSelect(onSelect);
    }
}

// --------------------------------------------------------------------------
// источник данных
// --------------------------------------------------------------------------

namespace
{

class GridDataSource : public brls::RecyclerDataSource
{
  public:
    int numberOfRows(brls::RecyclerFrame*, int) override
    {
        if (!rowsSource || rowsSource->empty())
            return 0;
        return static_cast<int>((rowsSource->size() + GameRow::COLUMNS - 1) / GameRow::COLUMNS);
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath indexPath) override
    {
        auto* row = static_cast<GameRow*>(recycler->dequeueReusableCell("Row"));
        row->setGames(*rowsSource, static_cast<size_t>(indexPath.row) * GameRow::COLUMNS,
                      rowsOnSelect);
        return row;
    }
};

}  // namespace

// --------------------------------------------------------------------------
// вкладка
// --------------------------------------------------------------------------

CatalogTab::CatalogTab()
{
    this->inflateFromXMLRes("xml/tabs/catalog.xml");

    buildPlayerFilter();
    buildToggles();

    recycler->estimatedRowHeight = 222;
    recycler->registerCell("Row", []() { return new GameRow(); });
    recycler->setDataSource(new GridDataSource());

    // ZL/ZR листают сетку страницами — с 3468 играми стик утомляет
    this->registerAction("Поиск", brls::BUTTON_BACK, [this](brls::View*) {
        promptSearch();
        return true;
    });
    this->registerAction("Страница вверх", brls::BUTTON_LT, [this](brls::View*) {
        recycler->setContentOffsetY(recycler->getContentOffsetY() - 666, true);
        return true;
    });
    this->registerAction("Страница вниз", brls::BUTTON_RT, [this](brls::View*) {
        recycler->setContentOffsetY(recycler->getContentOffsetY() + 666, true);
        return true;
    });

    reload();
}

void CatalogTab::buildPlayerFilter()
{
    AppState& state = AppState::get();

    for (int threshold : THRESHOLDS)
    {
        auto* button = new brls::Button();
        button->setText("от " + std::to_string(threshold));
        button->setMarginRight(10);
        button->setStyle(state.filter.minPlayers == threshold ? &brls::BUTTONSTYLE_PRIMARY
                                                              : &brls::BUTTONSTYLE_BORDERLESS);
        button->registerClickAction([this, threshold](brls::View*) {
            AppState::get().filter.minPlayers = threshold;
            // перерисовываем подсветку выбранного порога
            for (size_t i = 0; i < THRESHOLDS.size(); i++)
            {
                auto* b = static_cast<brls::Button*>(playersBox->getChildren()[i]);
                b->setStyle(THRESHOLDS[i] == threshold ? &brls::BUTTONSTYLE_PRIMARY
                                                       : &brls::BUTTONSTYLE_BORDERLESS);
            }
            reload();
            return true;
        });
        playersBox->addView(button);
    }
}

void CatalogTab::buildToggles()
{
    auto addToggle = [this](const std::string& label, bool Filter::*flag) -> brls::Button* {
        auto* button = new brls::Button();
        button->setText(label);
        button->setMarginRight(10);
        button->setStyle(AppState::get().filter.*flag ? &brls::BUTTONSTYLE_PRIMARY
                                                      : &brls::BUTTONSTYLE_BORDERLESS);
        button->registerClickAction([this, button, flag](brls::View*) {
            bool& value = AppState::get().filter.*flag;
            value       = !value;
            button->setStyle(value ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERLESS);
            reload();
            return true;
        });
        togglesBox->addView(button);
        return button;
    };

    installedButton = addToggle("Мои", &Filter::onlyInstalled);
    addToggle("Русский", &Filter::onlyRussian);

    genreButton = new brls::Button();
    genreButton->setMarginRight(10);
    genreButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    genreButton->registerClickAction([this](brls::View*) {
        chooseGenre();
        return true;
    });
    togglesBox->addView(genreButton);

    searchButton = new brls::Button();
    searchButton->setMarginRight(10);
    searchButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    searchButton->registerClickAction([this](brls::View*) {
        promptSearch();
        return true;
    });
    togglesBox->addView(searchButton);

    sortButton = new brls::Button();
    sortButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    sortButton->registerClickAction([this](brls::View*) {
        cycleSort();
        return true;
    });
    togglesBox->addView(sortButton);

    refreshToggleLabels();
}

void CatalogTab::refreshToggleLabels()
{
    const Filter& f = AppState::get().filter;

    genreButton->setText(f.genre.empty() ? "Жанр: любой" : "Жанр: " + f.genre);
    genreButton->setStyle(f.genre.empty() ? &brls::BUTTONSTYLE_BORDERLESS
                                          : &brls::BUTTONSTYLE_PRIMARY);

    searchButton->setText(f.search.empty() ? "Поиск" : "Поиск: " + f.search);
    searchButton->setStyle(f.search.empty() ? &brls::BUTTONSTYLE_BORDERLESS
                                            : &brls::BUTTONSTYLE_PRIMARY);

    // Подписи короткие намеренно: пять чипов с полными фразами не помещались
    // в 820 точек и уезжали за край. Контейнер к тому же переносит строку,
    // если названию жанра всё-таки не хватит места.
    sortButton->setText(Catalog::SORT_NAMES[f.sort]);

    // счётчик прямо на чипе — как в макете «Только мои · 12»
    if (installedButton)
        installedButton->setText("Мои · "
                                 + std::to_string(AppState::get().installedTitleIds.size()));
}

void CatalogTab::promptSearch()
{
    Filter& f = AppState::get().filter;

    brls::Application::getImeManager()->openForText(
        [this, &f](const std::string& text) {
            f.search = text;
            refreshToggleLabels();
            reload();
        },
        "Поиск по названию", "Пустая строка снимет фильтр", 40, f.search);
}

void CatalogTab::chooseGenre()
{
    std::vector<std::string> options = { "Любой" };
    for (const std::string& g : AppState::get().catalog.genres())
        options.push_back(g);

    auto* dropdown = new brls::Dropdown(
        "Жанр", options,
        [this, options](int selected) {
            if (selected < 0 || selected >= static_cast<int>(options.size()))
                return;
            AppState::get().filter.genre = selected == 0 ? "" : options[selected];
            refreshToggleLabels();
            reload();
        },
        0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void CatalogTab::cycleSort()
{
    Filter& f = AppState::get().filter;
    f.sort    = (f.sort + 1) % static_cast<int>(Catalog::SORT_NAMES.size());
    refreshToggleLabels();
    reload();
}

void CatalogTab::reload()
{
    AppState& state = AppState::get();

    // сетке хватает краткой выборки: описания нужны только в карточке
    games = state.catalog.queryBrief(state.filter);
    state.decorate(games);

    // фильтр по установленным делаем уже здесь: каталог в romfs ничего не знает
    // о том, что стоит на конкретной консоли
    if (state.filter.onlyInstalled)
    {
        std::vector<Game> only;
        for (const Game& g : games)
            if (g.installed)
                only.push_back(g);
        games = std::move(only);
    }

    // «сначала мои» доупорядочивается здесь, а не в SQL: о том, что установлено
    // на консоли, знает только приложение
    if (state.filter.sort == SORT_INSTALLED_FIRST)
        std::stable_partition(games.begin(), games.end(),
                              [](const Game& g) { return g.installed; });

    rowsSource   = &games;
    rowsOnSelect = [](const std::string& nsuid) {
        brls::Application::pushActivity(new GameActivity(nsuid));
    };

    int installed = state.installedCount(games);
    // коротко: полная фраза не помещалась рядом с кнопками порогов
    countLabel->setText(std::to_string(games.size()) + " · ваших "
                        + std::to_string(installed));

    bool empty = games.empty();
    emptyLabel->setVisibility(empty ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    recycler->setVisibility(empty ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

    if (!empty)
        recycler->reloadData();
}

brls::View* CatalogTab::create()
{
    return new CatalogTab();
}
