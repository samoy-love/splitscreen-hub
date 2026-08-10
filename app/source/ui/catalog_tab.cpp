#include "ui/catalog_tab.hpp"

#include <algorithm>

#include "app_state.hpp"
#include "ui/fonts.hpp"
#include "tasks.hpp"
#include "ui/folder_picker.hpp"
#include "ui/game_activity.hpp"
#include "ui/game_tile.hpp"

using namespace brls::literals;

namespace
{

// Пороги фильтра. Это именно «от N», а не точное совпадение: когда за консолью
// четверо, игра на шестерых тоже подходит.
const std::vector<int> THRESHOLDS = { 2, 3, 4, 6, 8 };

// индекс «сначала мои» в Catalog::SORT_NAMES
constexpr int SORT_INSTALLED_FIRST = 5;

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
    model->onSelect = [](const Game& game) {
        brls::Application::pushActivity(new GameActivity(game));
    };
    model->onFocus = [this](const std::string& nsuid) { focusedNsuid = nsuid; };

    this->registerAction("hub/action/search"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        promptSearch();
        return true;
    });
    this->registerAction("hub/action/hide"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        toggleHidden();
        return true;
    });
    // Раскладывать игры по папкам, не открывая каждую карточку.
    this->registerAction("hub/action/to_folder"_i18n, brls::BUTTON_X, [this](brls::View*) {
        if (focusedNsuid.empty())
        {
            brls::Application::notify("hub/filter/point_at_game"_i18n);
            return true;
        }
        // Перечитывать 3015 строк ради отметки не нужно: на плитке видно
        // только избранное, и его достаточно перерисовать на месте.
        const std::string nsuid = focusedNsuid;
        folders::pick(nsuid, [this, nsuid]() { refreshTile(nsuid); });
        return true;
    });
    // ZL/ZR листают сетку страницами — с 3489 играми стик утомляет. Подсказки
    // скрыты: две строки по 200 точек ради приёма, без которого прокрутка всё
    // равно работает, вытесняли из полосы нужное.
    this->registerAction("hub/action/page_up"_i18n, brls::BUTTON_LT, [this](brls::View*) {
        recycler->setContentOffsetY(recycler->getContentOffsetY() - PAGE_STEP, true);
        return true;
    }, true);
    this->registerAction("hub/action/page_down"_i18n, brls::BUTTON_RT, [this](brls::View*) {
        recycler->setContentOffsetY(recycler->getContentOffsetY() + PAGE_STEP, true);
        return true;
    }, true);

    reload();
}

void CatalogTab::buildPlayerFilter()
{
    AppState& state = AppState::get();

    for (int threshold : THRESHOLDS)
    {
        auto* button = new brls::Button();
        button->setText(brls::getStr("hub/filter/at_least", threshold));
        button->setFontSize(fonts::CAPTION);
        button->setMarginRight(6);
        button->setStyle(state.filter.minPlayers == threshold ? &brls::BUTTONSTYLE_PRIMARY
                                                              : &brls::BUTTONSTYLE_BORDERLESS);
        button->getFocusEvent()->subscribe([this](brls::View*) { focusedNsuid.clear(); });
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
        playersBox->addView(button);
        thresholdButtons.push_back(button);
    }
}

void CatalogTab::buildToggles()
{
    auto addToggle = [this](const std::string& label, bool Filter::*flag) -> brls::Button* {
        auto* button = new brls::Button();
        button->setText(label);
        button->setFontSize(fonts::CAPTION);
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
        // Пока курсор на чипе, «скрыть» не относится ни к какой игре: иначе Y,
        // нажатый на фильтре, тихо прятал бы ту, что была выделена до этого.
        // Ровно на этом уже обжигались в библиотеке.
        button->getFocusEvent()->subscribe([this](brls::View*) { focusedNsuid.clear(); });

        togglesBox->addView(button);
        return button;
    };

    installedButton = addToggle("", &Filter::onlyInstalled);
    addToggle("hub/filter/russian"_i18n, &Filter::onlyRussian);

    genreButton = new brls::Button();
    genreButton->setFontSize(fonts::CAPTION);
    genreButton->setMarginRight(6);
    genreButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    genreButton->registerClickAction([this](brls::View*) {
        chooseGenre();
        return true;
    });
    togglesBox->addView(genreButton);

    searchButton = new brls::Button();
    searchButton->setFontSize(fonts::CAPTION);
    searchButton->setMarginRight(6);
    searchButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    searchButton->registerClickAction([this](brls::View*) {
        promptSearch();
        return true;
    });
    togglesBox->addView(searchButton);

    retroButton  = addToggle("hub/filter/retro"_i18n, &Filter::showRetro);
    hiddenButton = addToggle("hub/filter/hidden"_i18n, &Filter::showHidden);

    sortButton->setFontSize(fonts::CAPTION);
    sortButton->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    sortButton->getFocusEvent()->subscribe([this](brls::View*) { focusedNsuid.clear(); });
    sortButton->registerClickAction([this](brls::View*) {
        chooseSort();
        return true;
    });

    refreshToggleLabels();
}

void CatalogTab::refreshToggleLabels()
{
    const Filter& f = AppState::get().filter;

    genreButton->setText(f.genre.empty() ? "hub/filter/genre"_i18n : f.genre);
    genreButton->setStyle(f.genre.empty() ? &brls::BUTTONSTYLE_BORDERLESS
                                          : &brls::BUTTONSTYLE_PRIMARY);

    searchButton->setText(f.search.empty() ? "hub/filter/search"_i18n
                                           : brls::getStr("hub/filter/search_on", f.search));
    searchButton->setStyle(f.search.empty() ? &brls::BUTTONSTYLE_BORDERLESS
                                            : &brls::BUTTONSTYLE_PRIMARY);

    // Подписи короткие намеренно: пять чипов с полными фразами не помещались
    // в 820 точек и уезжали за край. Контейнер к тому же переносит строку,
    // если названию жанра всё-таки не хватит места.
    sortButton->setText(Catalog::sortNames()[f.sort]);

    if (retroButton)
        retroButton->setText(f.showRetro ? "hub/filter/retro_on"_i18n : "hub/filter/retro"_i18n);
    if (hiddenButton)
        hiddenButton->setText(f.showHidden ? "hub/filter/hidden_on"_i18n
                                           : "hub/filter/hidden"_i18n);

    // счётчик прямо на чипе — как в макете «Только мои · 12»
    if (installedButton)
        installedButton->setText(
            brls::getStr("hub/filter/mine", AppState::get().installedTitleIds.size()));
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
        "hub/filter/search_title"_i18n, "hub/filter/search_hint"_i18n, 40, f.search);
}

void CatalogTab::chooseGenre()
{
    // Список жанров — это DISTINCT по таблице в romfs, то есть последнее место,
    // где мы ещё читали базу прямо в кадре. Читаем в рабочем потоке и открываем
    // список, когда он готов: список жанров не меняется, поэтому запоминаем.
    if (!genreCache.empty())
    {
        openGenreDropdown();
        return;
    }

    auto flag  = alive;
    auto* self = this;

    tasks::io([flag, self]() {
        std::vector<std::string> list = AppState::get().catalog.genres();
        if (!*flag)
            return;

        brls::sync([flag, self, list = std::move(list)]() mutable {
            if (!*flag)
                return;
            self->genreCache = std::move(list);
            self->openGenreDropdown();
        });
    });
}

void CatalogTab::openGenreDropdown()
{
    std::vector<std::string> options = { "hub/filter/genre_any"_i18n };
    options.insert(options.end(), genreCache.begin(), genreCache.end());

    auto* dropdown = new brls::Dropdown(
        "hub/filter/genre"_i18n, options,
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

void CatalogTab::chooseSort()
{
    // Список, а не перебор по кругу: перебором не видно, какие порядки вообще
    // есть, и до нужного приходится щёлкать вслепую.
    auto* dropdown = new brls::Dropdown(
        "hub/filter/sort"_i18n, Catalog::sortNames(),
        [this](int selected) {
            if (selected < 0 || selected >= static_cast<int>(Catalog::sortNames().size()))
                return;
            AppState::get().filter.sort = selected;
            refreshToggleLabels();
            reload();
        },
        AppState::get().filter.sort);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void CatalogTab::refreshTile(const std::string& nsuid)
{
    AppState& state = AppState::get();
    for (Game& g : model->games)
    {
        if (g.nsuid != nsuid)
            continue;
        state.decorate(g);
        break;
    }
    recycler->reloadData();
}

void CatalogTab::toggleHidden()
{
    if (focusedNsuid.empty())
    {
        brls::Application::notify("hub/filter/point_to_hide"_i18n);
        return;
    }

    AppState& state    = AppState::get();
    const bool wasShown = !state.library.isHidden(focusedNsuid);
    state.library.toggleHidden(focusedNsuid);
    brls::Application::notify(wasShown ? "hub/filter/hidden_done"_i18n
                                      : "hub/filter/shown_done"_i18n);
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

    // Выборка идёт в фоне и на консоли занимает заметное время. Без индикатора
    // нажатие на фильтр выглядит как «ничего не произошло».
    spinner->setVisibility(brls::Visibility::VISIBLE);

    tasks::io([self, alive, model, filter]() {
        AppState& state         = AppState::get();
        std::vector<Game> games = state.catalog.queryBrief(filter);
        state.decorate(games);

        // Установленные и скрытые отсеиваем уже здесь: каталог в romfs не знает
        // ни что стоит на консоли, ни что пользователь спрятал, — и то и другое
        // живёт в library.json.
        if (filter.onlyInstalled || !filter.showHidden)
        {
            std::vector<Game> kept;
            kept.reserve(games.size());
            for (const Game& g : games)
            {
                if (filter.onlyInstalled && !g.installed)
                    continue;
                if (!filter.showHidden && g.hidden)
                    continue;
                kept.push_back(g);
            }
            games = std::move(kept);
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
    spinner->setVisibility(brls::Visibility::GONE);

    AppState& state          = AppState::get();
    std::vector<Game>& games = model->games;

    int installed = state.installedCount(games);
    // коротко: полная фраза не помещалась рядом с кнопками порогов
    countLabel->setText(brls::getStr("hub/filter/count", games.size(), installed));

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
