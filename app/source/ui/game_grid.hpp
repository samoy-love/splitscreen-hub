#pragma once

#include <borealis.hpp>

#include <functional>
#include <memory>
#include <vector>

#include "catalog.hpp"

/// Данные сетки. Живут отдельно от вкладки и во владении обоих: и вкладки, и
/// источника данных RecyclerFrame.
///
/// Раньше источник читал глобальный указатель на вектор внутри вкладки. При
/// переключении вкладок borealis успевала уничтожить старую до того, как новая
/// заполнит свой вектор, и строки читали освобождённую память: в журнале это
/// выглядело как обложки с именами из мусорных байтов, а заканчивалось падением
/// по std::bad_alloc.
struct GridModel
{
    std::vector<Game> games;
    /// Игру передаём целиком: карточке хватит её на заголовок и обложку,
    /// и открытие не ждёт запроса к базе.
    std::function<void(const Game&)> onSelect;
    /// Необязательный: библиотеке нужно знать, на какой игре стоит курсор.
    std::function<void(const std::string&)> onFocus;
};

/// Строка сетки: несколько плиток в ряд.
///
/// RecyclerFrame в borealis переиспользует именно строки, поэтому сетка
/// собирается как список строк — иначе 3489 обложек оказались бы в памяти
/// одновременно.
class GameRow : public brls::RecyclerCell
{
  public:
    explicit GameRow(int columns);

    /// Модель задаётся один раз при создании строки. Раньше обработчики
    /// выбора и фокуса копировались в каждую из семи плиток на каждую
    /// привязку — четырнадцать копий std::function на ряд при прокрутке.
    void bind(std::shared_ptr<GridModel> model);

    /// Ширина плитки и зазор между ними — из game_tile.xml.
    static constexpr int TILE_WIDTH = 160;
    static constexpr int TILE_GAP   = 14;

    /// Высота плитки (198) плюс зазор между строками.
    static constexpr int HEIGHT = 212;

    /// Сколько плиток шириной TILE_WIDTH влезет в заданную ширину.
    static int columnsFor(int availableWidth);

    void setGames(size_t from);

  private:
    std::vector<brls::Box*> slots;
    std::shared_ptr<GridModel> model;
};

/// Настраивает RecyclerFrame на показ игр и возвращает модель, которую надо
/// наполнить и после этого позвать refreshGameGrid().
std::shared_ptr<GridModel> attachGameGrid(brls::RecyclerFrame* recycler, int columns);

/// Перерисовывает сетку и возвращает её к первой строке.
void refreshGameGrid(brls::RecyclerFrame* recycler);
