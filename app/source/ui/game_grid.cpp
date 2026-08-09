#include "ui/game_grid.hpp"

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
        row->setGames(*model, static_cast<size_t>(indexPath.row) * columns);
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

void GameRow::setGames(const GridModel& model, size_t from)
{
    for (size_t i = 0; i < slots.size(); i++)
    {
        size_t idx = from + i;
        auto* tile = static_cast<GameTile*>(slots[i]);

        if (idx >= model.games.size())
        {
            // хвост последней строки: место занимаем, но фокус туда не пускаем
            tile->setVisibility(brls::Visibility::INVISIBLE);
            tile->setFocusable(false);
            continue;
        }

        tile->setVisibility(brls::Visibility::VISIBLE);
        tile->setGame(model.games[idx]);
        tile->setFocusable(true);
        tile->setOnSelect(model.onSelect);
        tile->setOnFocus(model.onFocus);
    }
}

std::shared_ptr<GridModel> attachGameGrid(brls::RecyclerFrame* recycler, int columns)
{
    auto model = std::make_shared<GridModel>();

    recycler->estimatedRowHeight = GameRow::HEIGHT;
    recycler->registerCell("Row", [columns]() { return new GameRow(columns); });
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
