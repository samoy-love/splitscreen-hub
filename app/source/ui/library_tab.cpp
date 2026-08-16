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
const char* HIDDEN = "\x01hidden";

/// Ширина под сетку: 1280 экрана минус поля по краям (space::SCREEN с обеих
/// сторон), минус список папок с его правым полем.
constexpr int CONTENT_WIDTH = 1280 - 30 - 30 - 200 - 20;

/// Ширина списка папок минус боковые отступы кнопки (по 25 с каждой стороны).
constexpr float SIDEBAR_TEXT_WIDTH = 200 - 50;
/// Место под счётчик вида «99» у правого края.
constexpr float TAIL_WIDTH = 30;

/// Высота строки списка разделов.
constexpr float SIDEBAR_ROW_HEIGHT = 40;
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

void LibraryTab::scheduleSidebar()
{
    // Перестройка списка папок уничтожает его кнопки. Делать это из обработчика
    // нажатия нельзя: обработчик принадлежит одной из этих кнопок, и borealis
    // обращается к ней после возврата. Точно так же нельзя удалять кнопку,
    // которая держит фокус.
    //
    // brls::sync всегда откладывает на следующий кадр, даже вызванный из
    // UI-потока, — к этому моменту обработчик уже завершён и фокус снят.
    auto flag = alive;
    brls::sync([this, flag]() {
        if (*flag)
            rebuildSidebar();
    });
}

void LibraryTab::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx)
{
    // Игру отметили в каталоге или в карточке — раздел под нами изменился.
    // Пересобираем состав разделов и перечитываем содержимое выбранного.
    if (AppState::get().library.revision() != seenRevision)
    {
        rebuildSidebar();
        showSelection();
    }

    // Ширина сетки изменилась — кадры ячеек посчитаны под прежнюю. Полагаться
    // на checkWidth() внутри borealis нельзя, см. gridWidth.
    if ((int)grid->getWidth() != gridWidth && grid->getWidth() > 0)
    {
        gridWidth = (int)grid->getWidth();
        refreshGameGrid(grid, true);
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

void LibraryTab::refreshSidebar()
{
    AppState& state = AppState::get();

    for (Entry& e : entries)
    {
        const size_t count = e.name == HIDDEN ? state.library.hiddenGames().size()
            : e.name.empty()                  ? state.library.favorites().size()
                                              : state.library.folder(e.name).size();

        e.count->setText(std::to_string(count));

        const bool active = selected == e.name;
        e.row->setBackgroundColor(active ? brls::Application::getTheme()["brls/accent"]
                                         : nvgRGBA(0, 0, 0, 0));
        // Поверх заливки акцентом светлый текст не читается — на выбранной
        // строке он тёмный, как на кнопках того же стиля.
        const NVGcolor text = active ? nvgRGB(20, 20, 20)
                                     : brls::Application::getTheme()["brls/text"];
        e.label->setTextColor(text);
        e.count->setTextColor(active ? text
                                     : brls::Application::getTheme()["brls/text_disabled"]);
    }
}

void LibraryTab::rebuildSidebar()
{
    // Пересоздание нужно только когда меняется сам состав разделов: завели или
    // удалили папку. Фокус при этом уходит в никуда, поэтому после перестройки
    // его отдают заново — см. конец метода.
    entries.clear();
    sidebar->clearViews();
    AppState& state = AppState::get();

    auto addEntry = [this](const std::string& name, const std::string& label) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setHeight(SIDEBAR_ROW_HEIGHT);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPaddingLeft(12);
        row->setPaddingRight(12);
        row->setMarginBottom(2);
        row->setCornerRadius(6);
        row->setFocusable(true);

        // Своя заливка вместо подсветки фокуса.
        //
        // borealis под сфокусированным видом рисует ещё и заливку
        // brls/highlight/background — в тёмной теме это почти чёрный
        // (31, 34, 39). Она ложится поверх фона самой строки, то есть поверх
        // акцента у выбранного раздела, а текст на нём тёмный: наведёшь на
        // «Избранное» или «Скрытые», и подпись пропадает на тёмном.
        //
        // Убираем только заливку. Рамка подсветки — её borealis рисует
        // отдельным проходом поверх всего — остаётся, и по ней видно, где
        // фокус; а по акценту под ней — какой раздел открыт. Так эти два
        // состояния не спорят за один и тот же цвет.
        row->setHideHighlightBackground(true);

        auto* title = new brls::Label();
        // Имя папки задаёт пользователь, до 32 символов, а места на него — около
        // 120 точек. Без обрезки Label переносит строку, и строки списка
        // становятся разной высоты.
        title->setText(textfit::ellipsize(label, SIDEBAR_TEXT_WIDTH - TAIL_WIDTH, fonts::CAPTION));
        title->setFontSize(fonts::CAPTION);
        title->setVerticalAlign(brls::VerticalAlign::CENTER);
        title->setGrow(1.0f);

        // Счётчик прижат к правому краю: так числа стоят столбцом и их можно
        // сравнить взглядом, не читая названий.
        auto* count = new brls::Label();
        count->setFontSize(fonts::CAPTION);
        count->setVerticalAlign(brls::VerticalAlign::CENTER);
        count->setHorizontalAlign(brls::HorizontalAlign::RIGHT);

        row->addView(title);
        row->addView(count);

        row->registerClickAction([this, name](brls::View*) {
            selected = name;
            // Только подсветка. Пересоздание списка уничтожило бы строку, на
            // которой в этот момент стоит фокус, — borealis остаётся с
            // указателем на освобождённую память и падает вместе с Atmosphere.
            // Отложить на следующий кадр недостаточно: к тому времени фокус
            // никуда не девается, он всё на той же строке.
            refreshSidebar();
            showSelection();
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        // Пока фокус на списке разделов, «убрать» относится к папке, а не к
        // игре. Без этого сброса X, нажатый на папке после того как курсор
        // побывал на плитке, тихо выкидывал ту игру из списка.
        row->getFocusEvent()->subscribe([this](brls::View*) { focusedNsuid.clear(); });

        sidebar->addView(row);
        entries.push_back({ name, row, title, count });
    };

    addEntry(FAVORITES, "hub/library/favorites"_i18n);

    // Скрытые — такой же список, как избранное, и место им здесь. Переключатель
    // в каталоге показывает их вперемешку с остальными играми, а это отдельный
    // вопрос: «что я вообще прятал».
    addEntry(HIDDEN, "hub/library/hidden"_i18n);

    for (const std::string& name : state.library.folderNames())
        addEntry(name, name);

    // Черта отделяет действие от разделов. В одном столбце и одним видом стояли
    // две разные вещи: разделы, между которыми переключаются, и кнопка, которая
    // создаёт новый, — по виду они не различались.
    auto* separator = new brls::Box();
    separator->setHeight(1);
    separator->setMarginTop(8);
    separator->setMarginBottom(8);
    separator->setBackgroundColor(
        brls::Application::getTheme()["brls/applet_frame/separator"]);
    sidebar->addView(separator);

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

    refreshSidebar();
    seenRevision = AppState::get().library.revision();

    // Фокус мог стоять на кнопке, которой больше нет. Возвращаем его на
    // выбранный раздел, иначе borealis остаётся с указателем в никуда.
    for (const Entry& e : entries)
        if (e.name == selected)
        {
            brls::Application::giveFocus(e.row);
            break;
        }
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

    // Читаем в рабочем потоке, как каталог и карточка: на сотне избранного
    // это сотня поисков в каталоге, и делать их в кадре отрисовки незачем.
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
                continue;  // игра выпала из каталога при его обновлении
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
    scheduleSidebar();
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
    scheduleSidebar();
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
    // Имя могло быть занято — тогда на экране менять нечего. Переключить
    // selected на несуществующую папку нельзя: показался бы пустой раздел, а
    // rebuildSidebar() не нашёл бы его среди строк и не вернул фокус — тот
    // остался бы на строке, которую в конце кадра сносит deletionPool borealis.
    if (!AppState::get().library.renameFolder(selected, name))
    {
        brls::Application::notify("hub/folders/name_taken"_i18n);
        return;
    }

    selected = name;
    scheduleSidebar();
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
        scheduleSidebar();
        showSelection();
    });
    dialog->open();
}

brls::View* LibraryTab::create()
{
    return new LibraryTab();
}
