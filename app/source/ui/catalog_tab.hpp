#pragma once

#include <borealis.hpp>

#include <atomic>
#include <memory>

#include "catalog.hpp"
#include "ui/game_grid.hpp"

/// Вкладка каталога: фильтр «от N игроков» и сетка обложек.
class CatalogTab : public brls::Box
{
  public:
    CatalogTab();
    static brls::View* create();

    /// Заново отбирает игры из каталога по фильтру и обновляет сетку. Сама
    /// выборка уходит в рабочий поток, результат возвращается в UI через
    /// applyRows().
    /// keepScroll — не сбрасывать позицию прокрутки. Нужен, когда список
    /// перечитывается не по воле пользователя: спрятанная игра исчезает, а
    /// остальные должны остаться там же, где были.
    void reload(bool keepScroll = false);

    ~CatalogTab() override;

    /// Поверх сетки рисуется полоса прокрутки — см. тело.
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    /// Показывает уже готовую выборку. Только из UI-потока.
    void applyRows(bool keepScroll);

    /// Вкладка может закрыться, пока запрос в работе.
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);

    /// Номер последней запрошенной выборки. Результат с чужим номером
    /// выбрасывается — см. reload().
    ///
    /// shared_ptr, а не поле: рабочий поток сверяет номер, обращаясь к объекту
    /// вкладки, и делает это вне brls::sync — то есть без всякой синхронизации
    /// с её разрушением. Счётчик живёт отдельно и переживает вкладку, как model
    /// и alive.
    std::shared_ptr<std::atomic<unsigned long long>> queryGeneration
        = std::make_shared<std::atomic<unsigned long long>>(0);

    /// Версия библиотеки, на которой построена текущая выдача. Игру могли
    /// спрятать из карточки — тогда в сетке она остаётся до пересчёта.
    unsigned seenRevision = 0;

    /// Ширина, под которую в последний раз считались кадры ячеек.
    ///
    /// RecyclerFrame::checkWidth() в borealis хранит прежнюю ширину в static —
    /// одной на все экземпляры (recycler.cpp:369). Наши два рециклера, каталог и
    /// библиотека, переписывают её друг другу, а reloadData() без поднятого
    /// флага layouted молча ничего не делает. В итоге кадры ячеек остаются
    /// посчитанными под чужую геометрию, и сетка застывает съехавшей.
    /// Отслеживаем ширину сами и пересчитываем, когда она изменилась.
    int gridWidth = 0;

    std::shared_ptr<GridModel> model;

    void buildPlayerFilter();
    void buildToggles();
    void promptSearch();
    void chooseGenre();
    void chooseSort();
    void toggleHidden();
    /// Обновляет отметки одной игры без перезапроса каталога.
    void refreshTile(const std::string& nsuid);
    void refreshToggleLabels();

    brls::Button* genreButton = nullptr;
    brls::Button* searchButton = nullptr;
    brls::Button* installedButton = nullptr;

    /// Кнопки порогов «от N». Держим списком, чтобы подсвечивать выбранный, не
    /// полагаясь на порядок детей контейнера — рядом с ними стоят и остальные
    /// чипы фильтра.
    std::vector<brls::Button*> thresholdButtons;
    brls::Button* retroButton = nullptr;
    brls::Button* hiddenButton = nullptr;

    /// nsuid игры под курсором — для «скрыть».
    std::string focusedNsuid;

    BRLS_BIND(brls::Box, playersBox, "catalog/players");
    BRLS_BIND(brls::Box, togglesBox, "catalog/toggles");
    BRLS_BIND(brls::Label, countLabel, "catalog/count");
    BRLS_BIND(brls::ProgressSpinner, spinner, "catalog/spinner");
    BRLS_BIND(brls::Button, sortButton, "catalog/sort");
    BRLS_BIND(brls::RecyclerFrame, recycler, "catalog/recycler");
    BRLS_BIND(brls::Label, emptyLabel, "catalog/empty");
};
