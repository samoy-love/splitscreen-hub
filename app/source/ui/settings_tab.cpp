#include "ui/settings_tab.hpp"

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
    // Выход — обычный, через тот же путь, что и «+»: подмену файла и
    // перезапуск делает main() после остановки интерфейса.
    restartButton->registerClickAction([](brls::View*) {
        brls::Application::quit();
        return true;
    });

    buildLanguage();
    refresh();
    checkUpdate();
}

void SettingsTab::checkUpdate()
{
    updateVersion->setText(brls::getStr("hub/update/version", updater::currentVersion()));
    updateButton->setVisibility(brls::Visibility::GONE);
    restartButton->setVisibility(brls::Visibility::GONE);
    progressBar->setVisibility(brls::Visibility::GONE);

    // Скачано в прошлый раз, но приложение закрыли не кнопкой: подмена ждёт
    // перезапуска. Спрашивать сервер незачем — предлагаем закончить начатое.
    if (updater::hasPending())
    {
        updateStatus->setText(brls::getStr("hub/update/ready", ""));
        restartButton->setVisibility(brls::Visibility::VISIBLE);
        return;
    }

    updateStatus->setText("hub/update/checking"_i18n);
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

void SettingsTab::showProgress(const updater::Progress& p)
{
    const double part = p.total > 0 ? static_cast<double>(p.received) / static_cast<double>(p.total) : 0.0;
    progressFill->setWidthPercentage(static_cast<float>(part > 1.0 ? 100.0 : part * 100.0));

    // Всё скачано — идёт сверка суммы, это ещё пара секунд на SD.
    if (p.total > 0 && p.received >= p.total)
    {
        updateStatus->setText("hub/update/verifying"_i18n);
        return;
    }

    std::string rate = "hub/update/rate_unknown"_i18n;
    if (p.bytesPerSec > 0)
    {
        std::string left;
        if (p.etaSeconds >= 0)
        {
            const int m = p.etaSeconds / 60, sec = p.etaSeconds % 60;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d:%02d", m, sec);
            left = buf;
        }
        else
            left = "—";
        rate = brls::getStr("hub/update/rate", megabytes(static_cast<long long>(p.bytesPerSec)), left);
    }
    updateStatus->setText(brls::getStr("hub/update/downloading", pending.version,
                                       static_cast<int>(part * 100), megabytes(p.received),
                                       megabytes(p.total))
                          + " · " + rate);
}

void SettingsTab::installUpdate()
{
    if (pending.version.empty())
        return;
    updateButton->setVisibility(brls::Visibility::GONE);
    updateStatus->setText("hub/update/starting"_i18n);
    progressFill->setWidthPercentage(0.f);
    progressBar->setVisibility(brls::Visibility::VISIBLE);

    auto flag = alive;
    updater::install(
        pending,
        [this, flag](const updater::Progress& p) {
            if (*flag)
                showProgress(p);
        },
        [this, flag](bool ok, const std::string& message) {
            if (!*flag)
                return;
            progressBar->setVisibility(brls::Visibility::GONE);
            if (ok)
            {
                // Подменить себя на ходу нельзя (см. updater.hpp): кнопка
                // закрывает приложение, подмена и перезапуск идут на выходе.
                updateStatus->setText(brls::getStr("hub/update/ready", message));
                restartButton->setVisibility(brls::Visibility::VISIBLE);
                brls::Application::giveFocus(restartButton);
            }
            else
            {
                if (message == "download")
                    updateStatus->setText("hub/update/failed_download"_i18n);
                else if (message == "checksum")
                    updateStatus->setText("hub/update/failed_checksum"_i18n);
                else
                    updateStatus->setText(brls::getStr("hub/update/failed_generic", message));
                updateButton->setVisibility(brls::Visibility::VISIBLE);
                brls::Application::giveFocus(updateButton);
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
