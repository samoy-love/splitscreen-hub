#include "ui/folder_picker.hpp"

#include <borealis.hpp>

#include <vector>

#include "app_state.hpp"

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
            brls::Application::notify("Добавлено в «" + name + "»");
            if (onChanged)
                onChanged();
        },
        "Название папки", "Например: «Вечер с друзьями»", 32);
}

}  // namespace

namespace folders
{

void pick(const std::string& nsuid, std::function<void()> onChanged)
{
    AppState& state                = AppState::get();
    std::vector<std::string> names = state.library.folderNames();

    // Отметка показывает, где игра уже лежит: без неё приходилось помнить.
    std::vector<std::string> items;
    items.reserve(names.size() + 1);
    for (const std::string& name : names)
        items.push_back((state.library.inFolder(name, nsuid) ? "★ " : "   ") + name);
    items.push_back("+ Новая папка…");

    auto* dropdown = new brls::Dropdown(
        "В какую папку", items,
        [nsuid, names, onChanged](int selected) {
            if (selected < 0 || selected > static_cast<int>(names.size()))
                return;

            if (selected == static_cast<int>(names.size()))
            {
                promptNewFolder(nsuid, onChanged);
                return;
            }

            AppState& s = AppState::get();
            s.library.toggleInFolder(names[selected], nsuid);
            brls::Application::notify(s.library.inFolder(names[selected], nsuid)
                                          ? "Добавлено в «" + names[selected] + "»"
                                          : "Убрано из «" + names[selected] + "»");
            if (onChanged)
                onChanged();
        },
        0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace folders
