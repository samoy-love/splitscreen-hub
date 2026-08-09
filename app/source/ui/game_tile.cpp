#include "ui/game_tile.hpp"

#include <fstream>

#include <borealis.hpp>

#include "tasks.hpp"

namespace
{
#ifdef __SWITCH__
const char* ART_DIR = "romfs:/resources/art/";
#else
const char* ART_DIR = "resources/art/";
#endif
}  // namespace

GameTile::GameTile()
    : alive(std::make_shared<std::atomic_bool>(true))
{
    this->inflateFromXMLRes("xml/views/game_tile.xml");

    // Действие вешаем один раз в конструкторе. Если регистрировать его в
    // setGame, при переиспользовании плитки обработчики накапливались бы.
    this->registerClickAction([this](brls::View*) {
        if (onSelect && !nsuid.empty())
            onSelect(nsuid);
        return true;
    });
}

GameTile::~GameTile()
{
    *alive = false;
}

void GameTile::loadCover(const std::string& file)
{
    pendingArt = file;
    cover->clear();

    auto flag = alive;
    auto self = this;
    const std::string path = std::string(ART_DIR) + file;

    tasks::io([flag, self, path, file]() {
        std::ifstream in(path, std::ios::binary);
        if (!in.good())
            return;
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        if (data.empty() || !*flag)
            return;

        brls::sync([flag, self, file, data = std::move(data)]() {
            // плитку могли переиспользовать под другую игру, пока читали файл
            if (!*flag || self->pendingArt != file)
                return;
            self->cover->setImageFromMem(data.data(), static_cast<int>(data.size()));
        });
    });
}

void GameTile::setGame(const Game& game)
{
    nsuid = game.nsuid;

    if (!game.boxArtFile.empty())
        loadCover(game.boxArtFile);
    else
    {
        pendingArt.clear();
        cover->clear();
    }

    name->setText(game.title);

    // точное число игроков на одном экране; игр без него в базе нет
    players->setText(std::to_string(game.sameScreenMax));

    star->setVisibility(game.favorite ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    installedMark->setVisibility(game.installed ? brls::Visibility::VISIBLE
                                                : brls::Visibility::GONE);
}

void GameTile::setOnSelect(std::function<void(const std::string&)> callback)
{
    onSelect = std::move(callback);
}

brls::View* GameTile::create()
{
    return new GameTile();
}
