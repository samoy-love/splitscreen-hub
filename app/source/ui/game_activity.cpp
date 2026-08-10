#include "ui/game_activity.hpp"

#include <cstdlib>

#include <fstream>
#include <memory>

#include "app_state.hpp"
#include "format.hpp"
#include "ui/folder_picker.hpp"
#include "ui/fonts.hpp"
#include "net.hpp"
#include "tasks.hpp"
#include "ui/async_image.hpp"
#include "ui/gallery_activity.hpp"
#include "ui/remote_image.hpp"
#include "ui/video_player.hpp"

namespace
{

#ifdef __SWITCH__
const char* ART_DIR = "romfs:/art/";
#else
const char* ART_DIR = "resources/art/";
#endif

/// Фирменный цвет игры приходит из eShop строкой вида "0f336f".
NVGcolor parseColor(const std::string& hex, NVGcolor fallback)
{
    unsigned char r = 0, g = 0, b = 0;
    return fmtx::parseHexColor(hex, r, g, b) ? nvgRGB(r, g, b) : fallback;
}

brls::Box* statTile(const std::string& caption, const std::string& value)
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setGrow(1.0f);
    box->setMarginRight(10);
    box->setPadding(10);
    box->setCornerRadius(6);
    box->setBackgroundColor(brls::Application::getTheme()["brls/background"]);

    auto* c = new brls::Label();
    c->setText(caption);
    c->setFontSize(fonts::CAPTION);
    c->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);

    auto* v = new brls::Label();
    v->setText(value);
    v->setFontSize(fonts::ACCENT);

    box->addView(c);
    box->addView(v);
    return box;
}

brls::Label* tag(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(fonts::CAPTION);
    label->setMarginRight(14);
    label->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    return label;
}

}  // namespace

GameActivity::GameActivity(const std::string& nsuid)
    : nsuid(nsuid)
{
}

GameActivity::GameActivity(const Game& brief)
    : nsuid(brief.nsuid)
    , brief(brief)
{
}

GameActivity::~GameActivity()
{
    // Обложка и скриншоты могут догружаться в рабочем потоке; без флага их
    // возврат через brls::sync обратился бы к уничтоженной карточке.
    *alive = false;
}

void GameActivity::onContentAvailable()
{
    this->registerAction("Избранное", brls::BUTTON_X, [this](brls::View*) {
        toggleFavorite();
        return true;
    });
    this->registerAction("В папку", brls::BUTTON_Y, [this](brls::View*) {
        chooseFolder();
        return true;
    });

    // Название и обложка известны заранее — их каталог уже прочитал для плитки.
    // Показываем сразу, чтобы экран открылся с содержимым, а не пустым: раньше
    // всё, включая заголовок, ждало четырёх запросов к базе.
    if (!brief.nsuid.empty())
    {
        game = brief;
        fillHeader();
    }

    // Остальное — описание, жанры, ссылки на медиа — это ещё четыре запроса, и
    // выполнять их в кадре открытия значило отдать этот кадр целиком под них.
    auto flag        = alive;
    auto* self       = this;
    std::string want = nsuid;

    tasks::io([flag, self, want]() {
        AppState& state = AppState::get();

        Game full = state.catalog.byNsuid(want);
        state.decorate(full);
        std::vector<std::string> genres = state.catalog.genresOf(want);
        std::vector<std::string> shots  = state.catalog.screenshots(want);
        std::vector<std::string> videos = state.catalog.videos(want);

        if (!*flag)
            return;

        brls::sync([flag, self, full = std::move(full), genres = std::move(genres),
                    shots = std::move(shots), videos = std::move(videos)]() mutable {
            if (!*flag)
                return;
            self->applyDetails(std::move(full), std::move(genres), std::move(shots),
                               std::move(videos));
        });
    });
}

void GameActivity::applyDetails(Game full, std::vector<std::string> genreList,
                                std::vector<std::string> shots,
                                std::vector<std::string> videos)
{
    if (full.nsuid.empty())
    {
        brls::Logger::error("карточка: игра {} не найдена в каталоге", nsuid);
        return;
    }

    game = std::move(full);
    brls::Logger::info("карточка: открыта «{}» ({}), игроков до {}", game.title, nsuid,
                       game.sameScreenMax);

    fillHeader();
    fillStats();
    fillTags();
    fillGenres(genreList);

    // Порядок важен: полоса медиа очищается один раз здесь, затем в неё встают
    // кнопки трейлеров, и только после — скриншоты.
    shotsBox->clearViews();
    fillTrailerButton(videos);
    fillScreenshots(shots);

    headline->setText(game.headline);
    headline->setVisibility(game.headline.empty() ? brls::Visibility::GONE
                                                  : brls::Visibility::VISIBLE);
    description->setText(game.description);

    // у Jackbox и подобных игроки подключаются телефонами — без пояснения
    // непонятно, откуда 8 игроков при четырёх джойконах
    playersNote->setText(game.playersNote);
    playersNote->setVisibility(game.playersNote.empty() ? brls::Visibility::GONE
                                                        : brls::Visibility::VISIBLE);
}

void GameActivity::fillHeader()
{
    header->setBackgroundColor(
        parseColor(game.backgroundColor, brls::Application::getTheme()["brls/background"]));

    // setImageFromRes читает файл и разбирает JPEG прямо здесь, в UI-потоке.
    // Для шапки это лишние кадры на каждом открытии карточки.
    if (!game.boxArtFile.empty())
        loadCoverAsync(game.boxArtFile);

    title->setText(game.title);

    std::string sub = game.publisher;
    if (game.releaseYear)
        sub += " · " + std::to_string(game.releaseYear);
    if (game.topMentions > 0)
        sub += " · в подборках: " + std::to_string(game.topMentions);
    subtitle->setText(sub);

    refreshHeaderMarks();
}

void GameActivity::loadCoverAsync(const std::string& file)
{
    auto flag              = alive;
    auto* target           = cover.getView();
    const std::string path = std::string(ART_DIR) + file;

    tasks::io([flag, target, path]() {
        std::ifstream in(path, std::ios::binary);
        if (!in.good())
        {
            brls::Logger::warning("карточка: обложка не открылась — {}", path);
            return;
        }
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        auto pixels = std::make_shared<asyncimage::Pixels>(
            asyncimage::decode(data.data(), data.size()));
        if (!pixels->valid() || !*flag)
            return;

        brls::sync([flag, target, pixels]() {
            if (*flag)
                asyncimage::apply(target, *pixels);
        });
    });
}

void GameActivity::refreshHeaderMarks()
{
    star->setVisibility(game.favorite ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    std::string in;
    for (const std::string& name : AppState::get().library.folderNames())
        if (AppState::get().library.inFolder(name, game.nsuid))
            in += (in.empty() ? "" : ", ") + name;

    folders->setText(in.empty() ? "" : "в папках: " + in);
    folders->setVisibility(in.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void GameActivity::fillGenres(const std::vector<std::string>& list)
{
    std::string joined;
    for (const std::string& g : list)
        joined += (joined.empty() ? "" : " · ") + g;

    genres->setText(joined);
    genres->setVisibility(joined.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void GameActivity::fillStats()
{
    statsBox->clearViews();

    std::string players = std::to_string(game.sameScreenMin) + "–"
        + std::to_string(game.sameScreenMax);
    statsBox->addView(statTile("На одном экране", players));

    std::string size = formatSize(game.romSizeBytes);
    statsBox->addView(statTile("Размер", size.empty() ? "—" : size));

    int langs = languageCount(game.languages);
    statsBox->addView(statTile("Языки", langs ? std::to_string(langs) : "—"));

    statsBox->addView(statTile("Статус", game.installed ? "Установлена" : "Не установлена"));
}

void GameActivity::fillTags()
{
    tagsBox->clearViews();

    if (game.hasRussian)
        tagsBox->addView(tag("Есть русский"));
    if (game.hasOnline)
        tagsBox->addView(tag("Есть и онлайн"));
    if (game.hasDemo)
        tagsBox->addView(tag("Есть демо"));
    // настольный режим есть почти у всех, поэтому интересно обратное
    if (game.noTabletop)
        tagsBox->addView(tag("Без настольного режима"));
}

void GameActivity::fillScreenshots(std::vector<std::string> urls)
{
    // Полосу очищает onContentAvailable до вызова fillTrailerButton(), чтобы
    // кнопка трейлера встала первой и не была снесена этой очисткой.

    // Скриншотов в каталоге 20 тысяч — в romfs они не влезут, поэтому грузятся
    // из сети и оседают в кэше на SD. Первые четыре: остальные всё равно не
    // видны без прокрутки, а трафик тратят.
    // В полосе показываем четыре, но в галерею отдаём все: листать там есть
    // куда, а трафик тратится только на открытый снимок.
    std::vector<std::string> all = urls;
    if (urls.size() > 4)
        urls.resize(4);

    if (urls.empty())
    {
        shotsHint->setVisibility(brls::Visibility::GONE);
        return;
    }

    shotsPending = static_cast<int>(urls.size());
    shotsFailed  = 0;
    shotsHint->setText("Скриншоты загружаются…");
    shotsHint->setVisibility(brls::Visibility::VISIBLE);

    for (size_t i = 0; i < urls.size(); i++)
    {
        const std::string& url = urls[i];

        auto* shot = new RemoteImage();
        shot->setWidth(228);
        shot->setHeight(128);
        shot->setCornerRadius(6);
        shot->setMarginRight(10);
        shot->setScalingType(brls::ImageScalingType::FILL);

        // Снимок теперь фокусируется и открывается: раньше фокус доставался
        // только кнопке трейлера, и разглядеть скриншоты было нечем.
        shot->setFocusable(true);
        shot->registerClickAction([this, all, i](brls::View*) {
            brls::Application::pushActivity(new GalleryActivity(all, i),
                                            brls::TransitionAnimation::FADE);
            return true;
        });
        shot->addGestureRecognizer(new brls::TapGestureRecognizer(shot));
        shot->onDone = [this](bool ok) {
            shotsFailed += !ok;
            if (--shotsPending > 0)
                return;
            // все ответили: либо убираем подпись, либо честно говорим, что
            // не загрузилось — раньше она висела на экране вечно
            if (shotsFailed == 0)
                shotsHint->setVisibility(brls::Visibility::GONE);
            else
                shotsHint->setText(net::isReady()
                                       ? "Часть скриншотов не загрузилась"
                                       : "Скриншоты недоступны — нет сети");
        };
        shot->load(url);
        shotsBox->addView(shot);
    }
}

void GameActivity::fillTrailerButton(std::vector<std::string> videos)
{
    // Ролики есть у 61% игр (2139 из 3489), у остальных кнопка не должна
    // появляться вовсе — но по ТЗ просят именно disabled с подписью, так
    // понятнее, что раздел вообще существует.
    // У большинства игр ролик один, но у 295 их несколько — раньше все, кроме
    // первого, были недоступны вовсе. Показываем кнопку на каждый, но не больше
    // трёх: дальше полоса медиа вытеснила бы скриншоты.
    if (videos.size() > 3)
        videos.resize(3);

    trailerUrl = videos.empty() ? std::string() : videos.front();

    // Состояние сети здесь не спрашиваем: net::init() идёт в фоне, и карточка,
    // открытая в первые секунды после запуска, навсегда получала «нет сети» —
    // до самого закрытия, хотя сеть успевала подняться. Проверка перенесена в
    // момент нажатия, где можно и подождать.

    if (videos.empty())
    {
        trailerButton = new brls::Button();
        trailerButton->setFontSize(fonts::CAPTION);
        trailerButton->setWidth(190);
        trailerButton->setHeight(128);
        trailerButton->setMarginRight(10);
        trailerButton->setText("Трейлера нет");
        trailerButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        trailerButton->setState(brls::ButtonState::DISABLED);
        shotsBox->addView(trailerButton);
        return;
    }

    for (size_t i = 0; i < videos.size(); i++)
    {
        auto* button = new brls::Button();
        button->setFontSize(fonts::CAPTION);
        button->setWidth(190);
        button->setHeight(128);
        button->setMarginRight(10);
        button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        // Номер печатаем только когда роликов правда несколько: «▶ Трейлер 1»
        // при единственном ролике обещает продолжение, которого нет.
        button->setText(videos.size() > 1
                            ? "▶ Трейлер " + std::to_string(i + 1)
                            : "▶ Трейлер");

        const std::string url = videos[i];
        button->registerClickAction([this, url](brls::View*) {
            trailerUrl = url;
            openTrailer();
            return true;
        });
        shotsBox->addView(button);

        if (i == 0)
            trailerButton = button;  // на него меняем подпись, пока ждём сеть
    }
}

void GameActivity::openTrailer()
{
    if (trailerUrl.empty())
        return;

    // Сеть могла ещё не подняться, если нажали сразу после запуска. Ждём её
    // короткое время в рабочем потоке — блокировать UI ради этого нельзя.
    if (net::isReady())
    {
        brls::Application::pushActivity(new VideoPlayerActivity(trailerUrl));
        return;
    }

    trailerButton->setText("Подключаемся…");
    std::string url = trailerUrl;
    tasks::io([url]() {
        const bool ok = net::waitReady(5000);
        brls::sync([url, ok]() {
            if (ok)
                brls::Application::pushActivity(new VideoPlayerActivity(url));
            else
                brls::Application::notify("Нет сети — трейлер не открыть");
        });
    });
}

void GameActivity::toggleFavorite()
{
    AppState& state = AppState::get();
    state.library.toggleFavorite(game.nsuid);
    game.favorite = state.library.isFavorite(game.nsuid);
    refreshHeaderMarks();
}

void GameActivity::chooseFolder()
{
    folders::pick(game.nsuid, [this]() { refreshHeaderMarks(); });
}

