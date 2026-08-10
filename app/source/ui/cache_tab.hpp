#pragma once

#include <atomic>
#include <memory>

#include <borealis.hpp>

/// Вкладка «Кэш»: сколько занято на SD-карте и чем, с возможностью почистить.
///
/// Кэш растёт незаметно: каждый открытый скриншот и каждый просмотренный
/// трейлер оседают в sdmc:/switch/splitscreen-hub/cache/. Предел приложение
/// соблюдает само, вытесняя старое, но увидеть занятое и освободить место
/// немедленно было негде.
///
/// Почему вкладка, а не кнопка: свободных кнопок не осталось — A и B заняты
/// навигацией borealis, X и Y избранным и папками, ZL/ZR листанием и созданием
/// папки, «плюс» выходом из приложения, «минус» поиском. Сочетаний система
/// действий borealis не поддерживает, registerAction принимает одну кнопку.
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

    void refresh();
    /// Кнопки выбора языка. Смена требует перезапуска, о чём и говорим.
    void buildLanguage();
    void confirmClear(bool onlyVideos);

    BRLS_BIND(brls::Box, languageBox, "settings/language");
    BRLS_BIND(brls::Label, summary, "cache/summary");
    BRLS_BIND(brls::Label, breakdown, "cache/breakdown");
    BRLS_BIND(brls::Label, hint, "cache/hint");
    BRLS_BIND(brls::Button, clearVideosButton, "cache/clear_videos");
    BRLS_BIND(brls::Button, clearAllButton, "cache/clear_all");
};
