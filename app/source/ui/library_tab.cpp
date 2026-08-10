#include "ui/library_tab.hpp"

#include "app_state.hpp"
#include "tasks.hpp"
#include "ui/fonts.hpp"
#include "ui/game_activity.hpp"
#include "ui/game_tile.hpp"

namespace
{
const char* FAVORITES = "";  // пустое имя = избранное

/// Ширина под сетку: 1280 экрана минус поля по краям (space::SCREEN с обеих
/// сторон), минус список папок с его правым полем.
constexpr int CONTENT_WIDTH = 1280 - 30 - 30 - 200 - 20;
}

LibraryTab::LibraryTab()
{
    this->inflateFromXMLRes("xml/tabs/library.xml");

    model = attachGameGrid(grid, GameRow::columnsFor(CONTENT_WIDTH));
    model->onSelect = [](const Game& game) {
        brls::Application::pushActivity(new GameActivity(game));
    };
    model->onFocus = [this](const std::string& nsuid) { focusedNsuid = nsuid; };

    rebuildSidebar();
    showSelection();

    // Не BUTTON_START: на него в main.cpp повешен выход из приложения
    // (setGlobalQuit), и попытка создать папку закрывала бы программу.
    this->registerAction("Новая папка", brls::BUTTON_RT, [this](brls::View*) {
        promptNewFolder();
        return true;
    });
    this->registerAction("Имя папки", brls::BUTTON_Y, [this](brls::View*) {
        promptRename();
        return true;
    });
    // X работает по контексту: на выбранной игре — убрать её из списка,
    // иначе — удалить саму папку
    this->registerAction("Убрать", brls::BUTTON_X, [this](brls::View*) {
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
        button->setText(label + "  " + std::to_string(count));
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

    addEntry(FAVORITES, "★ Избранное", state.library.favorites().size());
    for (const std::string& name : state.library.folderNames())
        addEntry(name, name, state.library.folder(name).size());

    auto* add = new brls::Button();
    add->setText("Новая папка");
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
    const std::vector<std::string> ids =
        selected.empty() ? state.library.favorites() : state.library.folder(selected);

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
    emptyLabel->setText(selected.empty()
                            ? "Избранное пустое. Отметьте игру кнопкой X."
                            : "Папка пустая. Положите в неё игру кнопкой X.");
    emptyLabel->setVisibility(empty ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    grid->setVisibility(empty ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

    brls::Logger::info("библиотека: «{}» — игр {}", selected.empty() ? "Избранное" : selected,
                       model->games.size());
}

void LibraryTab::removeFocused()
{
    AppState& state = AppState::get();
    if (selected.empty())
        state.library.toggleFavorite(focusedNsuid);
    else
        state.library.toggleInFolder(selected, focusedNsuid);

    focusedNsuid.clear();
    rebuildSidebar();
    showSelection();
    brls::Application::notify("Убрано из списка");
}

void LibraryTab::promptNewFolder()
{
    brls::Application::getImeManager()->openForText(
        [this](const std::string& text) { onFolderNamed(text); },
        "Название папки", "Например: «Вечер с друзьями»", 32);
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
    if (selected.empty())
        return;  // избранное переименовать нельзя

    brls::Application::getImeManager()->openForText(
        [this](const std::string& text) { onFolderRenamed(text); },
        "Новое название", "", 32, selected);
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
    if (selected.empty())
        return;

    std::string name = selected;
    auto* dialog     = new brls::Dialog("Удалить папку «" + name + "»? Игры останутся в каталоге.");
    dialog->addButton("Отмена", []() {});
    dialog->addButton("Удалить", [this, name]() {
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
