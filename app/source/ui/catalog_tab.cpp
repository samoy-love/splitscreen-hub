#include "ui/catalog_tab.hpp"

#include <algorithm>

#include "app_state.hpp"
#include "tasks.hpp"
#include "ui/game_activity.hpp"
#include "ui/game_tile.hpp"

namespace
{

// Пороги фильтра. Это именно «от N», а не точное совпадение: когда за консолью
// четверо, игра на шестерых тоже подходит.
const std::vector<int> THRESHOLDS = { 2, 3, 4, 6, 8 };

/// Размер шрифта чипов фильтра. Со штатным подписи вроде «Жанр: приключения» не
/// помещались в кнопку и переносились на вторую строку прямо внутри неё.
constexpr float CHIP_FONT_SIZE = 15.0f;

// индекс «сначала мои» в Catalog::SORT_NAMES
constexpr int SORT_INSTALLED_FIRST = 6;

/// Ширина под сетку: 1280 экрана минус боковые отступы catalog.xml по 30.
constexpr int CONTENT_WIDTH = 1280 - 60;

/// Шаг постраничной прокрутки — примерно экран строк сетки.
constexpr float PAGE_STEP = GameRow::HEIGHT * 3;

}  // namespace

// --------------------------------------------------------------------------
// вкладка
// --------------------------------------------------------------------------

CatalogTab::CatalogTab()
{
    this->inflateFromXMLRes("xml/tabs/catalog.xml");

    buildPlayerFilter();
    buildToggles();

    // Сайдбара больше нет, под сетку идёт вся ширина экрана минус боковые
    // отступы из catalog.xml.
    model = attachGameGrid(recycler, GameRow::columnsFor(CONTENT_WIDTH));
    model->onSelect = [](const std::string& nsuid) {
        brls::Application::pushActivity(new GameActivity(nsuid));
    };

    this->registerAction("Поиск", brls::BUTTON_BACK, [this](brls::View*) {
        promptSearch();
        return true;
    });
    // ZL/ZR листают сетку страницами — с 3489 играми стик утомляет. Подсказки
    // скрыты: две строки по 200 точек ради приёма, без которого прокрутка всё
    // равно работает, вытесняли из полосы нужное.
    this->registerAction("Страница вверх", brls::BUTTON_LT, [this](brls::View*) {
        recycler->setContentOffsetY(recycler->getContentOffsetY() - PAGE_STEP, true);
        return true;
    }, true);
    this->registerAction("Страница вниз", brls::BUTTON_RT, [this](brls::View*) {
        recycler->setContentOffsetY(recycler->getContentOffsetY() + PAGE_STEP, true);
        return true;
    }, true);

    reload();
}

void CatalogTab::buildPlayerFilter()
{
    AppState& state = AppState::get();

    auto* caption = new brls::Label();
    caption->setText("Игроков");
    caption->setFontSize(CHIP_FONT_SIZE);
    caption->setMarginRight(8);
    caption->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    togglesBox->addView(caption);

    for (int threshold : THRESHOLDS)
    {
        auto* button = new brls::Button();
        button->setText("от " + std::to_string(threshold));
        button->setFontSize(CHIP_FONT_SIZE);
        button->setMarginRight(6);
        button->setStyle(state.filter.minPlayers == threshold ? &brls::BUTTONSTYLE_PRIMARY
                                                              : &brls::BUTTONSTYLE_BORDERLESS);
        button->registerClickAction([this, threshold](brls::View*) {
            AppState::get().filter.minPlayers = threshold;
            // перерисовываем подсветку выбранного порога
            for (size_t i = 0; i < thresholdButtons.size(); i++)
                thresholdButtons[i]->setStyle(THRESHOLDS[i] == threshold
                                                  ? &brls::BUTTONSTYLE_PRIMARY
                                                  : &brls::BUTTONSTYLE_BORDERLESS);
            reload();
            return true;
        });
        togglesBox->addView(button);
        thresholdButtons.push_back(button);
    }
}

void CatalogTab::buildToggles()
{
    auto addToggle = [this](const std::string& label, bool Filter::*flag) -> brls::Button* {
        auto* button = new brls::Button();
        button->setText(label);
        button->setFontSize(CHIP_FONT_SIZE);
        button->setMarginRight(6);
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
    genreButton->setFontSize(CHIP_FONT_SIZE);
    genreButton->setMarginRight(6);
    genreButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    genreButton->registerClickAction([this](brls::View*) {
        chooseGenre();
        return true;
    });
    togglesBox->addView(genreButton);

    searchButton = new brls::Button();
    searchButton->setFontSize(CHIP_FONT_SIZE);
    searchButton->setMarginRight(6);
    searchButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    searchButton->registerClickAction([this](brls::View*) {
        promptSearch();
        return true;
    });
    togglesBox->addView(searchButton);

    sortButton = new brls::Button();
    sortButton->setFontSize(CHIP_FONT_SIZE);
    sortButton->setMarginRight(6);
    sortButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    sortButton->registerClickAction([this](brls::View*) {
        cycleSort();
        return true;
    });
    togglesBox->addView(sortButton);

    // Счётчик найденного — в конце той же строки: отдельная строка ради двух
    // чисел стоила бы ряда игр.
    countLabel = new brls::Label();
    countLabel->setFontSize(CHIP_FONT_SIZE);
    countLabel->setMarginLeft(8);
    countLabel->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    togglesBox->addView(countLabel);

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
    // Выборка идёт в рабочем потоке: по 3489 игр это сотни миллисекунд с
    // чтением из romfs, а reload() зовётся из обработчика каждой кнопки
    // фильтра — раньше весь этот запрос выполнялся прямо в кадре.
    auto model      = this->model;  // переживёт вкладку, если её закроют
    auto alive      = this->alive;
    Filter filter   = AppState::get().filter;
    auto* self      = this;

    tasks::io([self, alive, model, filter]() {
        AppState& state         = AppState::get();
        std::vector<Game> games = state.catalog.queryBrief(filter);
        state.decorate(games);

        // фильтр по установленным делаем уже здесь: каталог в romfs ничего не
        // знает о том, что стоит на конкретной консоли
        if (filter.onlyInstalled)
        {
            std::vector<Game> only;
            for (const Game& g : games)
                if (g.installed)
                    only.push_back(g);
            games = std::move(only);
        }

        // «сначала мои» доупорядочивается здесь, а не в SQL: о том, что
        // установлено на консоли, знает только приложение
        if (filter.sort == SORT_INSTALLED_FIRST)
            std::stable_partition(games.begin(), games.end(),
                                  [](const Game& g) { return g.installed; });

        brls::sync([self, alive, model, games = std::move(games)]() mutable {
            model->games = std::move(games);
            if (*alive)
                self->applyRows();
        });
    });
}

void CatalogTab::applyRows()
{
    AppState& state          = AppState::get();
    std::vector<Game>& games = model->games;

    int installed = state.installedCount(games);
    // коротко: полная фраза не помещалась рядом с кнопками порогов
    countLabel->setText(std::to_string(games.size()) + " · ваших "
                        + std::to_string(installed));

    bool empty = games.empty();
    emptyLabel->setVisibility(empty ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    recycler->setVisibility(empty ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

    if (!empty)
        refreshGameGrid(recycler);

    brls::Logger::info("каталог: показано {} игр (ваших {}), фильтр: игроков >= {}, жанр «{}», "
                       "поиск «{}», только установленные {}, только с русским {}",
                       games.size(), installed, state.filter.minPlayers, state.filter.genre,
                       state.filter.search, state.filter.onlyInstalled,
                       state.filter.onlyRussian);
}

CatalogTab::~CatalogTab()
{
    // Запрос мог остаться в работе: без флага его возврат через brls::sync
    // обратился бы к уничтоженной вкладке.
    *alive = false;
}

brls::View* CatalogTab::create()
{
    return new CatalogTab();
}
