#include "ui/folder_picker.hpp"

#include <borealis.hpp>

#include <vector>

#include "app_state.hpp"

using namespace brls::literals;

namespace
{

void promptNewFolder(const std::string& nsuid, std::function<void()> onChanged)
{
    // Клавиатура на Switch асинхронная: имя приходит колбэком, а не возвратом.
    brls::Application::getImeManager()->openForText(
        [nsuid, onChanged](const std::string& name) {
            if (name.empty())
                return;

            AppState& state = AppState::get();
            state.library.createFolder(name);
            state.library.toggleInFolder(name, nsuid);
            brls::Application::notify(brls::getStr("hub/folders/added_to", name));
            if (onChanged)
                onChanged();
        },
        "hub/folders/name_title"_i18n, "hub/folders/name_hint"_i18n, 32);
}

}  // namespace

namespace folders
{

void pick(const std::string& nsuid, std::function<void()> onChanged)
{
    AppState& state                = AppState::get();
    std::vector<std::string> names = state.library.folderNames();

    // Избранное — первым пунктом этого же списка, а не отдельной кнопкой.
    // Раньше оно висело на X в карточке, тогда как в каталоге X означал «в
    // папку», — одна кнопка с двумя разными смыслами. По сути избранное и есть
    // особый список: в сайдбаре библиотеки оно и показано наравне с папками.
    //
    // Отметка показывает, где игра уже лежит: без неё приходилось помнить.
    std::vector<std::string> items;
    items.reserve(names.size() + 2);
    items.push_back((state.library.isFavorite(nsuid) ? "✓ " : "   ") + "hub/folders/favorites"_i18n);
    for (const std::string& name : names)
        items.push_back((state.library.inFolder(name, nsuid) ? "✓ " : "   ") + name);
    items.push_back("hub/folders/new"_i18n);

    auto* dropdown = new brls::Dropdown(
        "hub/folders/title"_i18n, items,
        [nsuid, names, onChanged](int selected) {
            AppState& s = AppState::get();

            // 0 — избранное, дальше папки, последним — создание новой
            if (selected < 0 || selected > static_cast<int>(names.size()) + 1)
                return;

            if (selected == 0)
            {
                s.library.toggleFavorite(nsuid);
                brls::Application::notify(s.library.isFavorite(nsuid)
                                              ? "hub/folders/in_favorites"_i18n
                                              : "hub/folders/out_favorites"_i18n);
            }
            else if (selected == static_cast<int>(names.size()) + 1)
            {
                promptNewFolder(nsuid, onChanged);
                return;
            }
            else
            {
                const std::string& name = names[selected - 1];
                s.library.toggleInFolder(name, nsuid);
                brls::Application::notify(brls::getStr(
                    s.library.inFolder(name, nsuid) ? "hub/folders/added_to"
                                                    : "hub/folders/removed_from",
                    name));
            }

            if (onChanged)
                onChanged();
        },
        0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace folders
