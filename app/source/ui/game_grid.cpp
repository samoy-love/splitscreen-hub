#include "ui/game_grid.hpp"

#include "ui/cover_cache.hpp"
#include "ui/game_tile.hpp"

namespace
{

class GridDataSource : public brls::RecyclerDataSource
{
  public:
    GridDataSource(std::shared_ptr<GridModel> model, int columns)
        : model(std::move(model))
        , columns(columns)
    {
    }

    int numberOfRows(brls::RecyclerFrame*, int) override
    {
        const size_t count = model->games.size();
        if (count == 0)
            return 0;
        return static_cast<int>((count + columns - 1) / columns);
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath indexPath) override
    {
        auto* row = static_cast<GameRow*>(recycler->dequeueReusableCell("Row"));
        row->setGames(static_cast<size_t>(indexPath.row) * columns);
        return row;
    }

  private:
    // Держим модель за shared_ptr: источник живёт столько же, сколько
    // RecyclerFrame, и переживает вкладку при её пересоздании.
    std::shared_ptr<GridModel> model;
    int columns;
};

}  // namespace

GameRow::GameRow(int columns)
{
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setHeight(HEIGHT);
    this->setPaddingBottom(TILE_GAP);

    for (int i = 0; i < columns; i++)
    {
        auto* tile = new GameTile();
        tile->setMarginRight(TILE_GAP);
        this->addView(tile);
        slots.push_back(tile);
    }
}

int GameRow::columnsFor(int availableWidth)
{
    // n плиток и n-1 зазоров между ними
    int n = (availableWidth + TILE_GAP) / (TILE_WIDTH + TILE_GAP);
    return n < 1 ? 1 : n;
}

void GameRow::bind(std::shared_ptr<GridModel> model, brls::RecyclerFrame* owner)
{
    this->model = std::move(model);
    this->owner = owner;

    // Обработчики одни и те же для всех игр — ставим их раз и навсегда.
    for (brls::Box* slot : slots)
    {
        auto* tile = static_cast<GameTile*>(slot);
        tile->setOnSelect(this->model->onSelect);
        tile->setOnFocus(this->model->onFocus);
    }
}

void GameRow::setGames(size_t from)
{
    if (!model)
        return;

    rowIndex = slots.empty() ? 0 : from / slots.size();

    for (size_t i = 0; i < slots.size(); i++)
    {
        size_t idx = from + i;
        auto* tile = static_cast<GameTile*>(slots[i]);

        if (idx >= model->games.size())
        {
            // хвост последней строки: место занимаем, но фокус туда не пускаем
            tile->setVisibility(brls::Visibility::INVISIBLE);
            tile->setFocusable(false);
            continue;
        }

        tile->setVisibility(brls::Visibility::VISIBLE);
        tile->setFocusable(true);
        tile->setGame(model->games[idx]);
    }

    // Обложки следующей строки читаем заранее: к моменту, когда до неё дойдёт
    // прокрутка, они уже в кэше, и вместо пустых плиток сразу видны картинки.
    const size_t next = from + slots.size();
    for (size_t i = 0; i < slots.size() && next + i < model->games.size(); i++)
        covers::warm(model->games[next + i].boxArtFile);
}

namespace
{

/// Лежит ли вид внутри поддерева предка.
bool isInside(brls::View* view, brls::View* ancestor)
{
    for (brls::View* p = view; p != nullptr; p = p->getParent())
        if (p == ancestor)
            return true;
    return false;
}

}  // namespace

brls::View* GameRow::getNextFocus(brls::FocusDirection direction, brls::View* currentView)
{
    brls::View* next = Box::getNextFocus(direction, currentView);

    // Наверх из первой строки — это выход к фильтрам, так и задумано. Из любой
    // другой ответ за пределами сетки означает, что строку выше просто не успели
    // создать: остаёмся на месте.
    if (direction == brls::FocusDirection::UP && rowIndex > 0 && owner && next
        && !isInside(next, owner))
        return currentView;

    return next;
}

std::shared_ptr<GridModel> attachGameGrid(brls::RecyclerFrame* recycler, int columns)
{
    auto model = std::make_shared<GridModel>();

    recycler->estimatedRowHeight = GameRow::HEIGHT;
    recycler->registerCell("Row", [columns, model, recycler]() {
        auto* row = new GameRow(columns);
        row->bind(model, recycler);
        return row;
    });
    recycler->setDataSource(new GridDataSource(model, columns));

    return model;
}

void refreshGameGrid(brls::RecyclerFrame* recycler)
{
    recycler->reloadData();

    // Без явного сброса прокрутка оставалась там же, где была до смены фильтра,
    // и первая строка оказывалась выше видимой области — выглядело это как
    // пропавший верхний ряд игр.
    recycler->setContentOffsetY(0.0f, false);
}
