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
    char buf[32];
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", mb / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.0f МБ", mb);
    return buf;
}

}  // namespace

CacheTab::CacheTab()
    : alive(std::make_shared<std::atomic_bool>(true))
{
    this->inflateFromXMLRes("xml/tabs/cache.xml");

    summary->setText("Считаем…");
    breakdown->setText("");
    hint->setText("");

    clearVideosButton->registerClickAction([this](brls::View*) {
        confirmClear(true);
        return true;
    });
    clearAllButton->registerClickAction([this](brls::View*) {
        confirmClear(false);
        return true;
    });

    this->registerAction("Обновить", brls::BUTTON_X, [this](brls::View*) {
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
    const std::string current = AppState::get().library.language();

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
            buildLanguage();
            brls::Application::notify("hub/settings/restart"_i18n);
            return true;
        });
        languageBox->addView(button);
    };

    add("en", "English");
    add("ru", "Русский");
}

CacheTab::~CacheTab()
{
    *alive = false;
}

void CacheTab::refresh()
{
    summary->setText("Считаем…");

    auto flag = alive;
    // обход каталога с тысячами файлов — не для UI-потока
    tasks::io([this, flag]() {
        net::CacheStats stats = net::cacheStats();

        brls::sync([this, flag, stats]() {
            if (!*flag)
                return;  // вкладку успели покинуть

            const long long images = stats.bytes - stats.videoBytes;
            const int imageFiles   = stats.files - stats.videoFiles;
            const int percent      = stats.limitBytes > 0
                     ? static_cast<int>(stats.bytes * 100 / stats.limitBytes)
                     : 0;

            summary->setText(megabytes(stats.bytes) + " из " + megabytes(stats.limitBytes)
                             + "  (" + std::to_string(percent) + "%)");

            breakdown->setText(
                "Скриншоты: " + std::to_string(imageFiles) + " шт, " + megabytes(images)
                + "\nТрейлеры: " + std::to_string(stats.videoFiles) + " шт, "
                + megabytes(stats.videoBytes));

            // Объясняем, что удаление безопасно: без этого чистить страшно.
            hint->setText(
                stats.files == 0
                    ? "Кэш пуст. Он наполнится сам, когда вы будете открывать карточки игр."
                    : "Обложки лежат внутри приложения и здесь не учитываются — каталог "
                      "останется рабочим даже с пустым кэшем. Удалённое загрузится заново "
                      "при следующем открытии карточки.");

            clearVideosButton->setState(stats.videoFiles > 0 ? brls::ButtonState::ENABLED
                                                             : brls::ButtonState::DISABLED);
            clearAllButton->setState(stats.files > 0 ? brls::ButtonState::ENABLED
                                                     : brls::ButtonState::DISABLED);
        });
    });
}

void CacheTab::confirmClear(bool onlyVideos)
{
    const std::string what = onlyVideos ? "все загруженные трейлеры"
                                        : "весь кэш — скриншоты и трейлеры";

    auto* dialog = new brls::Dialog("Удалить " + what + "?");
    dialog->addButton("Отмена", []() {});
    dialog->addButton("Удалить", [this, onlyVideos]() {
        auto flag = alive;
        summary->setText("Удаляем…");

        tasks::io([this, flag, onlyVideos]() {
            const int removed = net::clearCache(onlyVideos);

            brls::sync([this, flag, removed]() {
                if (!*flag)
                    return;
                brls::Application::notify("Удалено файлов: " + std::to_string(removed));
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
