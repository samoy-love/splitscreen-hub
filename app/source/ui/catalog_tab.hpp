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

    /// Перечитывает выборку из базы и обновляет сетку. Сам запрос уходит в
    /// рабочий поток, результат возвращается в UI через applyRows().
    void reload();

    ~CatalogTab() override;

  private:
    /// Показывает уже готовую выборку. Только из UI-потока.
    void applyRows();

    /// Вкладка может закрыться, пока запрос в работе.
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);

    std::shared_ptr<GridModel> model;

    void buildPlayerFilter();
    void buildToggles();
    void promptSearch();
    void chooseGenre();
    void cycleSort();
    void refreshToggleLabels();

    brls::Button* genreButton = nullptr;
    brls::Button* sortButton  = nullptr;
    brls::Button* searchButton = nullptr;
    brls::Button* installedButton = nullptr;

    /// Кнопки порогов «от N». Держим списком, чтобы подсвечивать выбранный, не
    /// полагаясь на порядок детей контейнера — рядом с ними теперь и остальные
    /// чипы фильтра.
    std::vector<brls::Button*> thresholdButtons;
    brls::Label* countLabel = nullptr;

    BRLS_BIND(brls::Box, togglesBox, "catalog/toggles");
    BRLS_BIND(brls::RecyclerFrame, recycler, "catalog/recycler");
    BRLS_BIND(brls::Label, emptyLabel, "catalog/empty");
};
