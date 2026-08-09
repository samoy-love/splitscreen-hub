#pragma once

#include <borealis.hpp>

#include "catalog.hpp"

/// Строка сетки: несколько плиток в ряд. RecyclerFrame в borealis умеет
/// переиспользовать только строки, поэтому сетка собирается как список строк —
/// иначе 3468 обложек оказались бы в памяти одновременно.
class GameRow : public brls::RecyclerCell
{
  public:
    GameRow();

    /// Четыре, а не шесть: слева у TabFrame постоянный сайдбар шириной 410,
    /// так что контенту остаётся 870 минус паддинги — около 820 точек.
    /// Плитка занимает 192 плюс 12 отступа, шесть колонок требовали 1152 и
    /// две последние уезжали за экран вместе с фокусом.
    static constexpr int COLUMNS = 4;

    void setGames(const std::vector<Game>& games, size_t from,
                  const std::function<void(const std::string&)>& onSelect);

  private:
    std::vector<brls::Box*> slots;
};

/// Вкладка каталога: фильтр «от N игроков» и сетка обложек.
class CatalogTab : public brls::Box
{
  public:
    CatalogTab();
    static brls::View* create();

    /// Перечитывает выборку из базы и обновляет сетку.
    void reload();

  private:
    std::vector<Game> games;

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

    BRLS_BIND(brls::Box, playersBox, "catalog/players");
    BRLS_BIND(brls::Box, togglesBox, "catalog/toggles");
    BRLS_BIND(brls::Label, countLabel, "catalog/count");
    BRLS_BIND(brls::RecyclerFrame, recycler, "catalog/recycler");
    BRLS_BIND(brls::Label, emptyLabel, "catalog/empty");
};
