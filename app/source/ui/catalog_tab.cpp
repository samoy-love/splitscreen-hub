#include "ui/catalog_tab.hpp"

#include <algorithm>

#include "app_state.hpp"
#include "perf.hpp"
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

// индекс «сначала установленные» в Catalog::sortNames()
constexpr int SORT_INSTALLED_FIRST = 4;

/// Ширина под сетку: 1280 экрана минус боковые отступы catalog.xml по 30.
constexpr int CONTENT_WIDTH = 1280 - 60;

/// Шаг постраничной прокрутки — примерно экран строк сетки.
constexpr float PAGE_STEP = GameRow::HEIGHT * 3;

/// Приписка к чипам, которые открывают список, а не переключаются на месте.
///
/// U+25BC, а не более уместный по размеру U+25BE: последнего в romfs:/font/font.ttf
/// просто нет, и вместо стрелки рисовался пустой квадрат с крестом.
const std::string DROPDOWN_MARK = "  ▼";

}  // namespace

// --------------------------------------------------------------------------
// вкладка
// --------------------------------------------------------------------------

CatalogTab::CatalogTab()
{
    perf::Scope timer("создание вкладки каталога");

    this->inflateFromXMLRes("xml/tabs/catalog.xml");

    // Чипы добавляем до привязки сетки. Каждый addView пересчитывает раскладку,
    // и пока сетка уже подключена, этот пересчёт тянет за собой её строки — на
    // старте в журнале было около двадцати циклов создания и уничтожения ячейки
    // подряд, ещё до первой выборки.
    buildPlayerFilter();
    buildToggles();

    // Под сетку идёт вся ширина экрана минус боковые отступы из catalog.xml.
    model = attachGameGrid(recycler, GameRow::columnsFor(CONTENT_WIDTH));
    model->onSelect = [](const Game& game) { GameActivity::open(game); };
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
        // Перечитывать весь каталог ради отметки не нужно: на плитке видно
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

    // Отбор «заметных» — единственный внешний признак качества, какой у нас
    // есть; почему он нужен, см. catalog_query.hpp.
    addToggle("hub/filter/notable"_i18n, &Filter::onlyNotable);

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

    // Стрелка отличает раскрывающийся список от переключателя. «Русский»,
    // «Ретро» и «Скрытые» переключаются на месте, а «Жанр» и «Сортировка»
    // открывают отдельный экран со списком — по одинаковым чипам предсказать,
    // что произойдёт при нажатии, было нельзя.
    genreButton->setText(
        (f.genre.empty() ? "hub/filter/genre"_i18n : Catalog::genreLabel(f.genre))
        + DROPDOWN_MARK);
    genreButton->setStyle(f.genre.empty() ? &brls::BUTTONSTYLE_BORDERLESS
                                          : &brls::BUTTONSTYLE_PRIMARY);

    searchButton->setText(f.search.empty() ? "hub/filter/search"_i18n
                                           : brls::getStr("hub/filter/search_on", f.search));
    searchButton->setStyle(f.search.empty() ? &brls::BUTTONSTYLE_BORDERLESS
                                            : &brls::BUTTONSTYLE_PRIMARY);

    // Подписи короткие намеренно: пять чипов с полными фразами не помещались
    // в 820 точек и уезжали за край. Контейнер к тому же переносит строку,
    // если названию жанра всё-таки не хватит места.
    sortButton->setText(Catalog::sortNames()[f.sort] + DROPDOWN_MARK);

    if (retroButton)
        retroButton->setText(f.showRetro ? "hub/filter/retro_on"_i18n : "hub/filter/retro"_i18n);
    if (hiddenButton)
        hiddenButton->setText(f.showHidden ? "hub/filter/hidden_on"_i18n
                                           : "hub/filter/hidden"_i18n);

    // счётчик прямо на чипе — как в макете «Только установленные · 12»
    if (installedButton)
        installedButton->setText(
            brls::getStr("hub/filter/installed", AppState::get().installedTitleIds.size()));
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
    // Жанры собраны при загрузке каталога в память, поэтому уходить в рабочий
    // поток здесь незачем.
    //
    // Значения и подписи расходятся: фильтровать надо по тому, что записано в
    // каталоге (жанры там русские), а показывать — на языке интерфейса.
    const std::vector<std::string> values = AppState::get().catalog.genreNames();

    // Каталог ещё грузится — жанров пока нет. Показывать пустой список хуже,
    // чем честно сказать «подождите»: он выглядел бы как «жанров не бывает».
    if (values.empty())
    {
        brls::Application::notify("hub/filter/loading"_i18n);
        return;
    }

    std::vector<std::string> labels = { "hub/filter/genre_any"_i18n };
    for (const std::string& value : values)
        labels.push_back(Catalog::genreLabel(value));

    auto* dropdown = new brls::Dropdown(
        "hub/filter/genre"_i18n, labels,
        [this, values](int selected) {
            if (selected < 0 || selected > static_cast<int>(values.size()))
                return;
            AppState::get().filter.genre = selected == 0 ? "" : values[selected - 1];
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
    // Не голый reloadData(): он заканчивается setContentOffsetY(0) и после
    // отметки игры в середине списка выбрасывал каталог в начало.
    refreshGameGrid(recycler, true);
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
    // Список тот же, из него лишь исчезла одна игра: возвращать пользователя
    // в начало каталога из-за этого незачем.
    reload(true);
}

void CatalogTab::reload(bool keepScroll)
{
    // Выборка идёт в рабочем потоке: по нескольким тысячам игр это сотни
    // миллисекунд с чтением из romfs, а reload() зовётся из обработчика каждой
    // кнопки фильтра — в кадре отрисовки этому не место.
    auto model      = this->model;  // переживёт вкладку, если её закроют
    auto alive      = this->alive;
    Filter filter   = AppState::get().filter;
    auto* self      = this;

    // Номер выборки. Пока она идёт (а на консоли это до секунды), фильтр
    // успевают подвинуть ещё раз, и в работе оказываются две выборки сразу.
    // Без номера применялась бы та, что финишировала последней, — и в сетке
    // оставался бы результат уже отменённого фильтра. Устаревший результат
    // просто выбрасываем.
    auto counter                        = queryGeneration;
    const unsigned long long generation = ++(*counter);

    // Запоминаем сразу: выдача строится по этому состоянию библиотеки, и
    // повторно перезапрашивать из-за него не нужно.
    seenRevision = AppState::get().library.revision();

    // Выборка идёт в фоне и на консоли занимает заметное время. Без индикатора
    // нажатие на фильтр выглядит как «ничего не произошло».
    spinner->setVisibility(brls::Visibility::VISIBLE);

    tasks::io([self, alive, model, filter, generation, counter, keepScroll]() {
        AppState& state         = AppState::get();
        std::vector<Game> games = state.catalog.queryBrief(filter);

        // Проверка сразу после запроса: отбраковать до сортировки и разметки
        // дешевле, чем после.
        if (*counter != generation)
            return;  // фильтр успели подвинуть, эта выдача уже не нужна

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

        // «сначала установленные» доупорядочивается здесь, а не в каталоге: о
        // том, что установлено на консоли, знает только приложение
        if (filter.sort == SORT_INSTALLED_FIRST)
            std::stable_partition(games.begin(), games.end(),
                                  [](const Game& g) { return g.installed; });

        brls::sync([self, alive, model, generation, counter, keepScroll,
                    games = std::move(games)]() mutable {
            // Ещё раз: между рабочим потоком и этим кадром фильтр тоже могли
            // подвинуть. Здесь уже UI-поток, и обращаться к вкладке можно —
            // флаг alive проверяется в том же потоке, где её и разрушают.
            if (!*alive || *counter != generation)
                return;

            model->games = std::move(games);
            self->applyRows(keepScroll);
        });
    });
}

void CatalogTab::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx)
{
    // Игру спрятали или добавили в список из карточки — выдача устарела.
    // Проверка идёт в отрисовке, а спрятанная вкладка не рисуется, так что
    // лишней работы это не создаёт.
    if (AppState::get().library.revision() != seenRevision)
        reload(true);

    // Ширина сетки изменилась — кадры ячеек посчитаны под прежнюю. Полагаться
    // на checkWidth() внутри borealis нельзя, см. gridWidth.
    if ((int)recycler->getWidth() != gridWidth && recycler->getWidth() > 0)
    {
        gridWidth = (int)recycler->getWidth();
        refreshGameGrid(recycler, true);
    }

    Box::draw(vg, x, y, width, height, style, ctx);

    // Полоса прокрутки. В каталоге больше четырёхсот строк, а на экране их две
    // с небольшим: без неё непонятно ни где ты находишься, ни сколько осталось.
    // Высоту содержимого считаем сами из числа игр — так не приходится гадать,
    // что именно RecyclerFrame считает своим размером в текущем кадре.
    const size_t games = model->games.size();
    const float visible = recycler->getHeight();
    if (games == 0 || visible <= 0)
        return;

    const int columns = GameRow::columnsFor(CONTENT_WIDTH);
    const float content =
        float((games + columns - 1) / columns) * float(GameRow::HEIGHT);
    if (content <= visible)
        return;  // всё поместилось, показывать нечего

    const float offset = std::min(std::max(recycler->getContentOffsetY(), 0.0f),
                                  content - visible);

    constexpr float BAR_WIDTH = 4;
    constexpr float MIN_THUMB = 24;

    const float trackX = recycler->getX() + recycler->getWidth() - BAR_WIDTH;
    const float trackY = recycler->getY();

    float thumb = std::max(visible * visible / content, MIN_THUMB);
    float thumbY = trackY + (visible - thumb) * (offset / (content - visible));

    nvgBeginPath(vg);
    nvgRoundedRect(vg, trackX, trackY, BAR_WIDTH, visible, BAR_WIDTH / 2);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 20));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, trackX, thumbY, BAR_WIDTH, thumb, BAR_WIDTH / 2);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 90));
    nvgFill(vg);
}

void CatalogTab::applyRows(bool keepScroll)
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
        refreshGameGrid(recycler, keepScroll);

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
