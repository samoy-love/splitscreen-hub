#include "ui/game_tile.hpp"

#include <fstream>
#include <memory>

#include <borealis.hpp>

#include "tasks.hpp"
#include "ui/async_image.hpp"

namespace
{
#ifdef __SWITCH__
// корень romfs — это содержимое build/resources, см. CATALOG_PATH в main.cpp
const char* ART_DIR = "romfs:/art/";
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

    // Касание по плитке. Без распознавателя жестов тач по играм не работал
    // вовсе: registerClickAction отвечает только за кнопку A.
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

    // Подписка тоже одна на всё время жизни плитки: рециклер переиспользует
    // плитки, и подписка в setGame копилась бы на каждой прокрутке.
    this->getFocusEvent()->subscribe([this](brls::View*) {
        if (onFocus && !nsuid.empty())
            onFocus(nsuid);
    });
}

void GameTile::setOnFocus(std::function<void(const std::string&)> callback)
{
    onFocus = std::move(callback);
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
        {
            brls::Logger::warning("tile: обложка не открылась — {}", path);
            return;
        }
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        if (data.empty())
        {
            brls::Logger::warning("tile: обложка пуста — {}", path);
            return;
        }
        if (!*flag)
            return;  // плитку успели убрать, пока читали файл

        // Разбор JPEG раньше шёл в UI-потоке и на каждой строке сетки съедал
        // несколько кадров. Здесь он в рабочем потоке, а в UI-поток уходят уже
        // готовые пиксели.
        auto pixels = std::make_shared<asyncimage::Pixels>(
            asyncimage::decode(data.data(), data.size()));
        if (!pixels->valid() || !*flag)
            return;

        brls::sync([flag, self, file, pixels]() {
            // плитку могли переиспользовать под другую игру, пока декодировали
            if (!*flag || self->pendingArt != file)
                return;
            asyncimage::apply(self->cover, *pixels);
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

