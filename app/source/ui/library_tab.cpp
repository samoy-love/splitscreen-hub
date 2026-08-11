#include "ui/library_tab.hpp"

#include "app_state.hpp"
#include "tasks.hpp"
#include "ui/fonts.hpp"
#include "ui/game_activity.hpp"
#include "ui/game_tile.hpp"
#include "ui/text_fit.hpp"

using namespace brls::literals;

namespace
{
const char* FAVORITES = "";  // пустое имя = избранное

/// Имя раздела скрытых. Папку с таким именем не завести: в имени есть символ,
/// который не наберёшь с экранной клавиатуры.
const char* HIDDEN = "hidden";

/// Ширина под сетку: 1280 экрана минус поля по краям (space::SCREEN с обеих
/// сторон), минус список папок с его правым полем.
constexpr int CONTENT_WIDTH = 1280 - 30 - 30 - 200 - 20;

/// Ширина списка папок минус боковые отступы кнопки (по 25 с каждой стороны).
constexpr float SIDEBAR_TEXT_WIDTH = 200 - 50;
/// Место под счётчик вида «  99» — он приписывается после имени.
constexpr float TAIL_WIDTH = 30;
}

LibraryTab::LibraryTab()
{
    this->inflateFromXMLRes("xml/tabs/library.xml");

    model = attachGameGrid(grid, GameRow::columnsFor(CONTENT_WIDTH));
    model->onSelect = [](const Game& game) { GameActivity::open(game); };
    model->onFocus = [this](const std::string& nsuid) { focusedNsuid = nsuid; };

    rebuildSidebar();
    showSelection();

    // Не BUTTON_START: на него в main.cpp повешен выход из приложения
    // (setGlobalQuit), и попытка создать папку закрывала бы программу.
    this->registerAction("hub/action/new_folder"_i18n, brls::BUTTON_RT, [this](brls::View*) {
        promptNewFolder();
        return true;
    });
    this->registerAction("hub/action/rename_folder"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        promptRename();
        return true;
    });
    // X работает по контексту: на выбранной игре — убрать её из списка,
    // иначе — удалить саму папку
    this->registerAction("hub/action/remove"_i18n, brls::BUTTON_X, [this](brls::View*) {
        if (!focusedNsuid.empty())
            removeFocused();
        else
            confirmRemove();
        return true;
    });
}

LibraryTab::~LibraryTab()
{
    *alive = false;
}

void LibraryTab::rebuildSidebar()
{
    sidebar->clearViews();
    AppState& state = AppState::get();

    auto addEntry = [this](const std::string& name, const std::string& label, size_t count) {
        auto* button = new brls::Button();

        // Имя папки задаёт пользователь, до 32 символов, а в списке на него
        // приходится около 120 точек. Без обрезки Label переносит строку, и
        // кнопки становятся разной высоты.
        const std::string tail = "  " + std::to_string(count);
        button->setText(textfit::ellipsize(label, SIDEBAR_TEXT_WIDTH - TAIL_WIDTH,
                                           fonts::CAPTION)
                        + tail);
        button->setFontSize(fonts::CAPTION);
        button->setMarginBottom(4);
        button->setStyle(selected == name ? &brls::BUTTONSTYLE_PRIMARY
                                          : &brls::BUTTONSTYLE_BORDERLESS);
        button->registerClickAction([this, name](brls::View*) {
            selected = name;
            rebuildSidebar();
            showSelection();
            return true;
        });

        // Пока фокус на списке папок, «убрать» относится к папке, а не к игре.
        // Без этого сброса X, нажатый на папке после того как курсор побывал на
        // плитке, тихо выкидывал ту игру из списка, а папка оставалась.
        button->getFocusEvent()->subscribe([this](brls::View*) { focusedNsuid.clear(); });

        sidebar->addView(button);
    };

    addEntry(FAVORITES, "hub/library/favorites"_i18n, state.library.favorites().size());

    // Скрытые — такой же список, как избранное, и место им здесь. Переключатель
    // в каталоге показывает их вперемешку с остальными играми, а это отдельный
    // вопрос: «что я вообще прятал».
    addEntry(HIDDEN, "hub/library/hidden"_i18n, state.library.hiddenGames().size());

    for (const std::string& name : state.library.folderNames())
        addEntry(name, name, state.library.folder(name).size());

    auto* add = new brls::Button();
    add->setText("hub/action/new_folder"_i18n);
    add->setFontSize(fonts::CAPTION);
    add->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    add->registerClickAction([this](brls::View*) {
        promptNewFolder();
        return true;
    });
    add->getFocusEvent()->subscribe([this](brls::View*) { focusedNsuid.clear(); });
    sidebar->addView(add);
}

void LibraryTab::showSelection()
{
    AppState& state = AppState::get();
    // Копия, а не ссылка: список читается в рабочем потоке, а библиотеку в это
    // время могут изменить с другого экрана.
    const std::vector<std::string> ids = selected == HIDDEN ? state.library.hiddenGames()
        : selected.empty()                                  ? state.library.favorites()
                                                            : state.library.folder(selected);

    focusedNsuid.clear();

    // По запросу к базе на каждую игру, и все — в UI-потоке: на сотне
    // избранного это сотня полных строк с описаниями в одном кадре. Читаем в
    // рабочем потоке, как каталог и карточка.
    auto flag  = alive;
    auto* self = this;

    tasks::io([flag, self, ids]() {
        AppState& state = AppState::get();
        std::vector<Game> games;
        games.reserve(ids.size());

        for (const std::string& nsuid : ids)
        {
            Game g = state.catalog.byNsuid(nsuid);
            if (g.nsuid.empty())
                continue;  // игра выпала из каталога при обновлении базы
            state.decorate(g);
            games.push_back(std::move(g));
        }

        if (!*flag)
            return;

        brls::sync([flag, self, games = std::move(games)]() mutable {
            if (*flag)
                self->applyGames(std::move(games));
        });
    });
}

void LibraryTab::applyGames(std::vector<Game> games)
{
    model->games = std::move(games);
    refreshGameGrid(grid);

    const bool empty = model->games.empty();
    emptyLabel->setText(
        selected == HIDDEN ? "hub/library/empty_hidden"_i18n
        : selected.empty() ? "hub/library/empty_favorites"_i18n
                           : "hub/library/empty_folder"_i18n);
    emptyLabel->setVisibility(empty ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    grid->setVisibility(empty ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

}

void LibraryTab::removeFocused()
{
    AppState& state = AppState::get();

    if (selected == HIDDEN)
    {
        state.library.toggleHidden(focusedNsuid);
        brls::Application::notify("hub/library/restored"_i18n);
    }
    else if (selected.empty())
    {
        state.library.toggleFavorite(focusedNsuid);
        brls::Application::notify("hub/folders/out_favorites"_i18n);
    }
    else
    {
        state.library.toggleInFolder(selected, focusedNsuid);
        brls::Application::notify(brls::getStr("hub/folders/removed_from", selected));
    }

    focusedNsuid.clear();
    rebuildSidebar();
    showSelection();
}

void LibraryTab::promptNewFolder()
{
    brls::Application::getImeManager()->openForText(
        [this](const std::string& text) { onFolderNamed(text); },
        "hub/folders/name_title"_i18n, "hub/folders/name_hint"_i18n, 32);
}

void LibraryTab::onFolderNamed(const std::string& name)
{
    if (name.empty())
        return;
    AppState::get().library.createFolder(name);
    selected = name;
    rebuildSidebar();
    showSelection();
}

void LibraryTab::promptRename()
{
    if (selected.empty() || selected == HIDDEN)
        return;  // избранное и скрытые — не папки, переименовывать нечего

    brls::Application::getImeManager()->openForText(
        [this](const std::string& text) { onFolderRenamed(text); },
        "hub/folders/rename_title"_i18n, "", 32, selected);
}

void LibraryTab::onFolderRenamed(const std::string& name)
{
    if (name.empty() || selected.empty())
        return;
    AppState::get().library.renameFolder(selected, name);
    selected = name;
    rebuildSidebar();
    showSelection();
}

void LibraryTab::confirmRemove()
{
    if (selected.empty() || selected == HIDDEN)
        return;

    std::string name = selected;
    auto* dialog     = new brls::Dialog(brls::getStr("hub/library/delete_folder", name));
    dialog->addButton("hub/action/cancel"_i18n, []() {});
    dialog->addButton("hub/action/delete"_i18n, [this, name]() {
        AppState::get().library.removeFolder(name);
        selected.clear();
        rebuildSidebar();
        showSelection();
    });
    dialog->open();
}

brls::View* LibraryTab::create()
{
    return new LibraryTab();
}
