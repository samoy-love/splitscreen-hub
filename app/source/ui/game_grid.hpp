#pragma once

#include <borealis.hpp>

#include <functional>
#include <memory>
#include <vector>

#include "catalog.hpp"

/// Данные сетки. Живут отдельно от вкладки и во владении обоих: и вкладки, и
/// источника данных RecyclerFrame.
///
/// Хранить вектор внутри вкладки нельзя: при переключении вкладок borealis
/// уничтожает старую до того, как новая заполнит свою, и строки читали бы
/// освобождённую память — обложки с именами из мусорных байтов и падение по
/// std::bad_alloc.
struct GridModel
{
    std::vector<Game> games;
    /// Игру передаём целиком: карточке хватит её на заголовок и обложку,
    /// и открытие не ждёт чтения подробностей из каталога.
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

    /// Модель задаётся один раз при создании строки, а не на каждую привязку:
    /// иначе обработчики выбора и фокуса копировались бы в каждую из семи
    /// плиток на каждой прокрутке — четырнадцать копий std::function на ряд.
    void bind(std::shared_ptr<GridModel> model, brls::RecyclerFrame* owner);

    /// Ширина плитки и зазор между ними — из game_tile.xml.
    static constexpr int TILE_WIDTH = 160;
    static constexpr int TILE_GAP   = 14;

    /// Высота строки: плитка 218 плюс зазор до следующей. В 218 заложены две
    /// строки подписи — с одной название обрезалось на середине слова у каждой
    /// третьей игры.
    static constexpr int HEIGHT = 232;

    /// Сколько плиток шириной TILE_WIDTH влезет в заданную ширину.
    static int columnsFor(int availableWidth);

    void setGames(size_t from);

    /// Не выпускает фокус из сетки вверх, пока строка не первая.
    ///
    /// RecyclerFrame ищет строку выше среди уже созданных ячеек. При быстром
    /// листании она ещё не создана, поиск не находит ничего и передаёт запрос
    /// наружу — фокус улетал в шапку с фильтрами посреди прокрутки. Здесь такой
    /// ответ отклоняется: подождать один кадр правильнее, чем прыгать через
    /// весь экран.
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

  private:
    std::vector<brls::Box*> slots;
    std::shared_ptr<GridModel> model;

    /// Номер строки в списке и сетка, которой она принадлежит, — по ним
    /// понятно, можно ли отдавать фокус наружу.
    size_t rowIndex             = 0;
    brls::RecyclerFrame* owner  = nullptr;
};

/// Настраивает RecyclerFrame на показ игр и возвращает модель, которую надо
/// наполнить и после этого позвать refreshGameGrid().
std::shared_ptr<GridModel> attachGameGrid(brls::RecyclerFrame* recycler, int columns);

/// Перестраивает сетку под новое содержимое модели.
///
/// keepScroll оставляет позицию прокрутки на месте. Сброс в начало верен, когда
/// поменялся фильтр — список стал другим, и показывать его с середины незачем.
/// Но когда пользователь просто спрятал игру, список тот же, и отброс к началу
/// заставляет искать место заново.
void refreshGameGrid(brls::RecyclerFrame* recycler, bool keepScroll = false);
