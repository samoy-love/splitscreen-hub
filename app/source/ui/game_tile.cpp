#include "ui/game_tile.hpp"

#include <cmath>
#include <fstream>
#include <memory>

#include <borealis.hpp>

#include "tasks.hpp"
#include "ui/async_image.hpp"
#include "ui/cover_cache.hpp"
#include "perf.hpp"

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
            onSelect(game);
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

    // Текстурой владеет кэш, а не вид: одну и ту же обложку показывают разные
    // плитки, и освобождать её по своему усмотрению нельзя.
    cover->setFreeTexture(false);

    if (int cached = covers::find(file))
    {
        perf::count(perf::Counter::CoverFromCache);
        cover->innerSetImage(cached);
        cover->setAlpha(1.0f);
        fading = false;
        return;
    }

    cover->clear();

    auto flag = alive;
    auto self = this;
    const std::string path = std::string(ART_DIR) + file;

    // В начало очереди: эта плитка на экране прямо сейчас, а в хвосте очереди
    // ждут предзагрузки соседних строк и запросы, чьи плитки уже уехали.
    tasks::ioFront([flag, self, path, file]() {
        // Пока задача ждала очереди, строку могли переиспользовать под другую
        // игру: при быстрой прокрутке таких задач набирается больше, чем плиток
        // на экране. Читать и разбирать файл ради выброшенного результата
        // бессмысленно — проверяем до чтения, а не после.
        if (!*flag || self->pendingArt != file)
        {
            perf::count(perf::Counter::CoverDropped);
            return;
        }

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
        if (!*flag || self->pendingArt != file)
        {
            perf::count(perf::Counter::CoverDropped);
            return;
        }

        // Разбор JPEG раньше шёл в UI-потоке и на каждой строке сетки съедал
        // несколько кадров. Здесь он в рабочем потоке, а в UI-поток уходят уже
        // готовые пиксели.
        perf::Scope decode("");
        auto pixels = std::make_shared<asyncimage::Pixels>(
            asyncimage::decode(data.data(), data.size()));
        perf::count(perf::Counter::CoverFromDisk);
        perf::count(perf::Counter::CoverDecodeMs, (long long)decode.elapsedMs());

        if (!pixels->valid() || !*flag)
        {
            perf::count(perf::Counter::CoverDropped);
            return;
        }

        brls::sync([flag, self, file, pixels]() {
            // Кладём в кэш в любом случае: даже если плитку успели занять под
            // другую игру, работа уже сделана и пригодится при возврате.
            const int texture = asyncimage::upload(*pixels);
            covers::put(file, texture);

            if (*flag && self->pendingArt == file && texture)
            {
                self->cover->innerSetImage(texture);
                // Обложка, прочитанная с карты, появляется плавно: мгновенная
                // подмена на прокрутке читается как мигание.
                self->cover->setAlpha(0.0f);
                self->coverShown = std::chrono::steady_clock::now();
                self->fading     = true;
            }
        });
    });
}

void GameTile::setGame(const Game& game)
{
    // Рециклер часто отдаёт строке те же самые игры — например, когда список
    // перерисовывается без смены фильтра. Тогда делать нечего: обложка уже
    // показана, подписи те же.
    if (nsuid == game.nsuid && game.favorite == this->game.favorite
        && game.hidden == this->game.hidden && game.installed == this->game.installed)
        return;

    this->game = game;
    nsuid      = game.nsuid;

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

    // Скрытая игра видна только когда включён её фильтр, и там она должна
    // отличаться от обычных — иначе непонятно, что именно ты вернул на экран.
    this->setAlpha(game.hidden ? 0.45f : 1.0f);

    star->setVisibility(game.favorite ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    installedMark->setVisibility(game.installed ? brls::Visibility::VISIBLE
                                                : brls::Visibility::GONE);
}

void GameTile::draw(NVGcontext* vg, float x, float y, float width, float height,
                    brls::Style style, brls::FrameContext* ctx)
{
    if (fading)
    {
        constexpr float FADE_MS = 160.0f;
        const float elapsed     = std::chrono::duration<float, std::milli>(
                                  std::chrono::steady_clock::now() - coverShown)
                                  .count();

        float t = elapsed / FADE_MS;
        if (t >= 1.0f)
        {
            t      = 1.0f;
            fading = false;
        }
        cover->setAlpha(t);
    }

    // Подъём под курсором. Плавно, а не скачком: рамку выделения borealis рисует
    // сама, но она одинаковая у кнопки и у плитки, и лёгкое движение отличает
    // «выбрана игра» от «выбран чип фильтра».
    const float target = this->isFocused() ? LIFT : 0.0f;
    if (lift != target)
    {
        lift += (target - lift) * 0.35f;   // экспоненциальное приближение
        if (std::abs(target - lift) < 0.2f)
            lift = target;
        this->setTranslationY(-lift);
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

void GameTile::setOnSelect(std::function<void(const Game&)> callback)
{
    onSelect = std::move(callback);
}

