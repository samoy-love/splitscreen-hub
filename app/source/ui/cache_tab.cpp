#include "ui/cache_tab.hpp"

#include <cstdio>

#include "app_state.hpp"
#include "net.hpp"
#include "ui/fonts.hpp"
#include "tasks.hpp"

using namespace brls::literals;

namespace
{

std::string megabytes(long long bytes)
{
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return mb >= 1024.0 ? brls::getStr("hub/cache/gb", mb / 1024.0)
                        : brls::getStr("hub/cache/mb", mb);
}

}  // namespace

CacheTab::CacheTab()
    : alive(std::make_shared<std::atomic_bool>(true))
{
    this->inflateFromXMLRes("xml/tabs/cache.xml");

    summary->setText("hub/cache/counting"_i18n);
    breakdown->setText("");
    hint->setText("");

    clearAllButton->registerClickAction([this](brls::View*) {
        confirmClear();
        return true;
    });

    this->registerAction("hub/action/refresh"_i18n, brls::BUTTON_X, [this](brls::View*) {
        refresh();
        return true;
    });

    buildLanguage();
    refresh();
}

void CacheTab::buildLanguage()
{
    languageBox->clearViews();

    // Язык применяется при запуске: локаль borealis задаётся один раз, до
    // создания окна, и менять её на ходу она не умеет. Поэтому просим
    // перезапустить, а не делаем вид, что переключили.
    // Пока язык не выбран, подсвечиваем тот, что применён на самом деле:
    // пустая настройка означает «как на консоли», и обе кнопки без заливки
    // выглядели бы как поломка.
    const std::string current = effectiveLanguage();

    auto add = [this, current](const char* code, const char* label) {
        auto* button = new brls::Button();
        button->setText(label);
        button->setFontSize(fonts::CAPTION);
        button->setMarginRight(fonts::CAPTION / 2);
        button->setStyle(current == code ? &brls::BUTTONSTYLE_PRIMARY
                                         : &brls::BUTTONSTYLE_BORDERLESS);
        button->registerClickAction([this, code](brls::View*) {
            if (AppState::get().library.language() == code)
                return true;

            AppState::get().library.setLanguage(code);
            brls::Application::notify("hub/settings/restart"_i18n);

            // Только подсветка, без пересоздания кнопок.
            //
            // Прошлая попытка откладывала buildLanguage() на следующий кадр —
            // этого мало. Кнопка, по которой нажали, держит фокус, и уничтожить
            // её нельзя ни сейчас, ни кадром позже: borealis остаётся с
            // указателем на освобождённую память и роняет вместе с собой
            // Atmosphere. Менять же здесь нужно ровно две заливки.
            highlightLanguage();
            return true;
        });
        languageBox->addView(button);
        languageButtons.emplace_back(code, button);
    };

    languageButtons.clear();
    add("en", "English");
    add("ru", "Русский");  // название языка не переводится
}

std::string CacheTab::effectiveLanguage()
{
    const std::string chosen = AppState::get().library.language();
    if (!chosen.empty())
        return chosen;
    return brls::Application::getLocale() == brls::LOCALE_RU ? "ru" : "en";
}

void CacheTab::highlightLanguage()
{
    const std::string current = effectiveLanguage();
    for (auto& [code, button] : languageButtons)
        button->setStyle(current == code ? &brls::BUTTONSTYLE_PRIMARY
                                         : &brls::BUTTONSTYLE_BORDERLESS);
}

CacheTab::~CacheTab()
{
    *alive = false;
}

void CacheTab::refresh()
{
    summary->setText("hub/cache/counting"_i18n);

    auto flag = alive;
    // обход каталога с тысячами файлов — не для UI-потока
    tasks::io([this, flag]() {
        net::CacheStats stats = net::cacheStats();

        brls::sync([this, flag, stats]() {
            if (!*flag)
                return;  // вкладку успели покинуть

            const int percent = stats.limitBytes > 0
                     ? static_cast<int>(stats.bytes * 100 / stats.limitBytes)
                     : 0;

            summary->setText(brls::getStr("hub/cache/of", megabytes(stats.bytes),
                                          megabytes(stats.limitBytes))
                             + "  (" + std::to_string(percent) + "%)");

            breakdown->setText(brls::getStr("hub/cache/breakdown", stats.files,
                                            megabytes(stats.bytes)));

            // Объясняем, что удаление безопасно: без этого чистить страшно.
            hint->setText(stats.files == 0 ? "hub/cache/empty"_i18n : "hub/cache/hint"_i18n);

            clearAllButton->setState(stats.files > 0 ? brls::ButtonState::ENABLED
                                                     : brls::ButtonState::DISABLED);
        });
    });
}

void CacheTab::confirmClear()
{
    auto* dialog = new brls::Dialog("hub/cache/confirm_all"_i18n);
    dialog->addButton("hub/action/cancel"_i18n, []() {});
    dialog->addButton("hub/action/delete"_i18n, [this]() {
        auto flag = alive;
        summary->setText("hub/cache/clearing"_i18n);

        tasks::io([this, flag]() {
            const int removed = net::clearCache();

            brls::sync([this, flag, removed]() {
                if (!*flag)
                    return;
                brls::Application::notify(brls::getStr("hub/cache/cleared", removed));
                refresh();
            });
        });
    });
    dialog->open();
}

brls::View* CacheTab::create()
{
    return new CacheTab();
}
