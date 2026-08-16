#include "ui/settings_tab.hpp"

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

SettingsTab::SettingsTab()
    : alive(std::make_shared<std::atomic_bool>(true))
{
    this->inflateFromXMLRes("xml/tabs/settings.xml");

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

    updateButton->registerClickAction([this](brls::View*) {
        installUpdate();
        return true;
    });

    buildLanguage();
    refresh();
    checkUpdate();
}

void SettingsTab::checkUpdate()
{
    updateVersion->setText(brls::getStr("hub/update/version", updater::currentVersion()));
    updateStatus->setText("hub/update/checking"_i18n);
    updateButton->setVisibility(brls::Visibility::GONE);

    auto flag = alive;
    updater::check([this, flag](bool available, const updater::Info& info, const std::string& message) {
        if (!*flag)
            return;
        if (available)
        {
            pending = info;
            updateStatus->setText(brls::getStr("hub/update/available", info.version));
            updateButton->setVisibility(brls::Visibility::VISIBLE);
        }
        else
            updateStatus->setText(message.empty() ? "hub/update/latest"_i18n
                                                  : "hub/update/offline"_i18n);
    });
}

void SettingsTab::installUpdate()
{
    if (pending.version.empty())
        return;
    updateButton->setVisibility(brls::Visibility::GONE);
    updateStatus->setText(brls::getStr("hub/update/downloading", 0));

    auto flag = alive;
    updater::install(
        pending,
        [this, flag](float part) {
            if (*flag)
                updateStatus->setText(brls::getStr("hub/update/downloading", static_cast<int>(part * 100)));
        },
        [this, flag](bool ok, const std::string& message) {
            if (!*flag)
                return;
            if (ok)
            {
                updateStatus->setText(brls::getStr("hub/update/installed", message));
                auto* dialog = new brls::Dialog(brls::getStr("hub/update/installed", message));
                dialog->addButton("hub/action/ok"_i18n, []() {});
                dialog->open();
            }
            else
            {
                updateStatus->setText(brls::getStr("hub/update/failed", message));
                updateButton->setVisibility(brls::Visibility::VISIBLE);
            }
        });
}

void SettingsTab::buildLanguage()
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

            // Только подсветка, без пересоздания кнопок. Кнопка, по которой
            // нажали, держит фокус, и уничтожить её нельзя ни сейчас, ни
            // кадром позже: borealis остаётся с указателем на освобождённую
            // память и роняет вместе с собой Atmosphere. Менять же здесь нужно
            // ровно две заливки.
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

std::string SettingsTab::effectiveLanguage()
{
    const std::string chosen = AppState::get().library.language();
    if (!chosen.empty())
        return chosen;
    return brls::Application::getLocale() == brls::LOCALE_RU ? "ru" : "en";
}

void SettingsTab::highlightLanguage()
{
    const std::string current = effectiveLanguage();
    for (auto& [code, button] : languageButtons)
        button->setStyle(current == code ? &brls::BUTTONSTYLE_PRIMARY
                                         : &brls::BUTTONSTYLE_BORDERLESS);
}

SettingsTab::~SettingsTab()
{
    *alive = false;
}

void SettingsTab::refresh()
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

void SettingsTab::confirmClear()
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

brls::View* SettingsTab::create()
{
    return new SettingsTab();
}
