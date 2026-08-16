#pragma once

#include <atomic>
#include <memory>

#include <borealis.hpp>

/// Вкладка «Настройки»: выбор языка и кэш на SD-карте — сколько занято и чем,
/// с возможностью почистить.
///
/// Кэш растёт незаметно: каждый открытый скриншот и каждый просмотренный
/// трейлер оседают в sdmc:/switch/splitscreen-hub/cache/. Предел приложение
/// соблюдает само, вытесняя старое, но увидеть занятое и освободить место
/// немедленно нужно отдельное место в интерфейсе.
///
/// Именно вкладка, а не кнопка: все кнопки геймпада заняты навигацией и
/// действиями каталога, а сочетаний система действий borealis не поддерживает —
/// registerAction принимает одну кнопку.
///
/// Обход каталога идёт в фоновом потоке: при тысячах файлов это заметное
/// время, а интерфейс замирать не должен.
class CacheTab : public brls::Box
{
  public:
    CacheTab();
    ~CacheTab() override;

    static brls::View* create();

  private:
    /// Вкладку можно покинуть раньше, чем закончится обход каталога.
    std::shared_ptr<std::atomic_bool> alive;

    /// Кнопки выбора языка и их коды. Смена языка меняет только подсветку:
    /// пересоздавать кнопку, на которой стоит фокус, нельзя — см.
    /// highlightLanguage().
    std::vector<std::pair<std::string, brls::Button*>> languageButtons;
    void highlightLanguage();
    /// Язык, который приложение показывает сейчас: выбранный вручную, а если
    /// выбора не было — тот, что достался от языка консоли.
    static std::string effectiveLanguage();

    void refresh();
    /// Кнопки выбора языка. Смена требует перезапуска, о чём и говорим.
    void buildLanguage();
    void confirmClear();

    BRLS_BIND(brls::Box, languageBox, "settings/language");
    BRLS_BIND(brls::Label, summary, "cache/summary");
    BRLS_BIND(brls::Label, breakdown, "cache/breakdown");
    BRLS_BIND(brls::Label, hint, "cache/hint");
    BRLS_BIND(brls::Button, clearAllButton, "cache/clear_all");
};
