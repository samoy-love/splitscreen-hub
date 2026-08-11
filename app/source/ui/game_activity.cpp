#include "ui/game_activity.hpp"

#include <cstdlib>

#include <fstream>
#include <memory>

#include "app_state.hpp"
#include "format.hpp"
#include "perf.hpp"
#include "ui/folder_picker.hpp"
#include "ui/fonts.hpp"
#include "net.hpp"
#include "tasks.hpp"
#include "ui/async_image.hpp"
#include "ui/gallery_activity.hpp"
#include "ui/remote_image.hpp"
#include "ui/video_player.hpp"

using namespace brls::literals;

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

/// Ширина карточки характеристики. Фиксированная, а не растянутая: с grow=1
/// четыре факта разъезжались на все 1220 точек, и между «12.1 ГБ» и «11»
/// оказывалось триста точек пустоты — взгляд шёл через весь экран ради четырёх
/// коротких значений.
constexpr float STAT_WIDTH = 200;

/// Полоса медиа: одинаковые плитки и зазор между ними.
///
/// Ширина у трейлера и снимка теперь одна. Раньше кнопка трейлера была 190×128
/// сплошным бирюзовым прямоугольником — самым ярким пятном экрана, — а снимки
/// 228×128, и полоса читалась как «две кнопки и картинки» вместо одного ряда
/// превью.
constexpr float MEDIA_WIDTH = 228;
constexpr float MEDIA_HEIGHT = 128;
constexpr float MEDIA_GAP = 10;

/// Сколько плиток влезает в строку. Экран 1280 минус поля по 30 с каждой
/// стороны — 1220 точек; пять плиток занимают 1190. Раньше числа трейлеров и
/// снимков задавались порознь (до трёх и до четырёх), и при двух трейлерах
/// полоса требовала 1352 точки: последний снимок обрезался краем экрана, а
/// доскроллить до него было нечем — прокрутка на карточке вертикальная.
constexpr int MEDIA_SLOTS = 5;

brls::Box* statTile(const std::string& caption, const std::string& value)
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setWidth(STAT_WIDTH);
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

/// Метка-чип: подложка и обычный цвет текста.
///
/// Раньше это была подпись цветом text_disabled без фона. Тем же цветом в теме
/// покрашены недоступные элементы, поэтому «Есть русский» читалось как
/// отключённая кнопка, а не как факт об игре.
brls::Box* tag(const std::string& text)
{
    auto* box = new brls::Box(brls::Axis::ROW);
    box->setHeight(26);
    box->setPaddingLeft(10);
    box->setPaddingRight(10);
    box->setMarginRight(8);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setCornerRadius(13);
    box->setBackgroundColor(nvgRGBA(255, 255, 255, 20));

    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(fonts::CAPTION);
    label->setVerticalAlign(brls::VerticalAlign::CENTER);
    box->addView(label);
    return box;
}

}  // namespace

GameActivity::GameActivity(Game full, std::vector<std::string> genreList,
                           std::vector<std::string> shots, std::vector<std::string> videos)
    : nsuid(full.nsuid)
    , genreList(std::move(genreList))
    , shots(std::move(shots))
    , videos(std::move(videos))
{
    game = std::move(full);
}

void GameActivity::open(const Game& brief)
{
    // Четыре запроса к базе — в рабочем потоке, и только потом открываем экран.
    // Задержка в единицы миллисекунд незаметна, а вот сборка карточки по частям
    // на глазах заметна очень.
    const std::string nsuid = brief.nsuid;

    tasks::io([nsuid]() {
        perf::Scope timer("детали карточки");

        AppState& state = AppState::get();
        Game full       = state.catalog.byNsuid(nsuid);
        state.decorate(full);

        std::vector<std::string> genreList = state.catalog.genresOf(nsuid);
        std::vector<std::string> shots     = state.catalog.screenshots(nsuid);
        std::vector<std::string> videos    = state.catalog.videos(nsuid);

        if (full.nsuid.empty())
        {
            // Молча ничего не делать нельзя: нажатие на плитку выглядело как
            // зависшее приложение.
            brls::Logger::error("карточка: игра {} не найдена в каталоге", nsuid);
            brls::sync([]() { brls::Application::notify("hub/game/not_found"_i18n); });
            return;
        }

        brls::sync([full = std::move(full), genreList = std::move(genreList),
                    shots = std::move(shots), videos = std::move(videos)]() mutable {
            // Без затухания. borealis рисует стек экранов до первого
            // непрозрачного, а экран считается прозрачным ровно пока идёт
            // анимация появления, — то есть всё это время каталог и карточка
            // смешиваются друг с другом, и на экране видны обе разметки сразу.
            // Движение даёт подъём HubScreen, смешивать кадры для этого не надо.
            brls::Application::pushActivity(
                new GameActivity(std::move(full), std::move(genreList), std::move(shots),
                                 std::move(videos)),
                brls::TransitionAnimation::NONE);
        });
    });
}

GameActivity::~GameActivity()
{
    // Обложка и скриншоты могут догружаться в рабочем потоке; без флага их
    // возврат через brls::sync обратился бы к уничтоженной карточке.
    *alive = false;
}

void GameActivity::onContentAvailable()
{
    // Те же кнопки, что в каталоге: X кладёт в списки, Y прячет. Раньше X здесь
    // значил «избранное», а там «в папку» — одна кнопка с двумя смыслами.
    this->registerAction("hub/action/to_folder"_i18n, brls::BUTTON_X, [this](brls::View*) {
        chooseFolder();
        return true;
    });
    this->registerAction("hub/action/hide"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        AppState& state     = AppState::get();
        const bool wasShown = !state.library.isHidden(nsuid);
        state.library.toggleHidden(nsuid);
        game.hidden = !wasShown;
        brls::Application::notify(wasShown ? "hub/filter/hidden_done"_i18n
                                           : "hub/filter/shown_done"_i18n);
        refreshHeaderMarks();
        return true;
    });

    // Всё содержимое уже прочитано в GameActivity::open(), до создания экрана.
    // Здесь только раскладка — она укладывается в один кадр, и карточка
    // появляется целиком.
    perf::Scope timer("сборка карточки");

    fillHeader();
    fillStats();
    fillTags();
    fillGenres(genreList);

    // Порядок важен: полоса медиа очищается один раз здесь, затем в неё встают
    // кнопки трейлеров, и только после — скриншоты.
    shotsBox->clearViews();
    const int used = fillTrailerButton(videos);
    fillScreenshots(shots, MEDIA_SLOTS - used);

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
        sub += " · " + brls::getStr("hub/game/in_toplists", game.topMentions);
    subtitle->setText(sub);

    refreshHeaderMarks();
}

void GameActivity::loadCoverAsync(const std::string& file)
{
    // fillHeader() вызывается дважды — сразу из brief и потом из полной строки.
    // Файл в обоих случаях один и тот же, а читать и разбирать его повторно
    // значит зря потратить чтение с romfs и разбор JPEG на каждое открытие.
    if (file == loadedArt)
        return;
    loadedArt = file;

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

    folders->setText(in.empty() ? "" : brls::getStr("hub/game/in_lists", in));
    folders->setVisibility(in.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void GameActivity::fillGenres(const std::vector<std::string>& list)
{
    // Жанры в базе русские — это значения, а не подписи. При английском
    // интерфейсе их переводит genreLabel().
    std::string joined;
    for (const std::string& g : list)
        joined += (joined.empty() ? "" : " · ") + Catalog::genreLabel(g);

    genres->setText(joined);
    genres->setVisibility(joined.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void GameActivity::fillStats()
{
    statsBox->clearViews();

    // Диапазон печатаем только когда он есть. «2–2» читается как опечатка:
    // у половины игр каталога нижняя и верхняя границы совпадают.
    std::string players = game.sameScreenMin == game.sameScreenMax
        ? std::to_string(game.sameScreenMax)
        : std::to_string(game.sameScreenMin) + "–" + std::to_string(game.sameScreenMax);
    statsBox->addView(statTile("hub/game/players"_i18n, players));

    // Единицы — из i18n, как в разделе кэша: та же величина не должна
    // называться по-разному на двух экранах.
    std::string size = fmtx::formatSize(game.romSizeBytes, "hub/unit/gb"_i18n.c_str(),
                                  "hub/unit/mb"_i18n.c_str());
    statsBox->addView(statTile("hub/game/size"_i18n,
                               size.empty() ? "hub/game/unknown"_i18n : size));

    int langs = languageCount(game.languages);
    statsBox->addView(statTile("hub/game/languages"_i18n,
                               langs ? std::to_string(langs) : "hub/game/unknown"_i18n));

    // Столбца «Статус» здесь больше нет. Полезен только положительный ответ, а
    // он приходился на одну игру из полусотни — остальные сорок девять отдавали
    // четверть строки под слово «Не установлена». Установленные помечены чипом
    // среди прочих меток, ниже.
}

void GameActivity::fillTags()
{
    tagsBox->clearViews();

    if (game.installed)
        tagsBox->addView(tag("hub/game/installed"_i18n));
    if (game.hasRussian)
        tagsBox->addView(tag("hub/game/has_russian"_i18n));
    if (game.hasOnline)
        tagsBox->addView(tag("hub/game/has_online"_i18n));
    if (game.hasDemo)
        tagsBox->addView(tag("hub/game/has_demo"_i18n));
    // настольный режим есть почти у всех, поэтому интересно обратное
    if (game.noTabletop)
        tagsBox->addView(tag("hub/game/no_tabletop"_i18n));
}

void GameActivity::fillScreenshots(std::vector<std::string> urls, int slots)
{
    // Полосу очищает onContentAvailable до вызова fillTrailerButton(), чтобы
    // кнопка трейлера встала первой и не была снесена этой очисткой.

    // Скриншотов в каталоге 20 тысяч — в romfs они не влезут, поэтому грузятся
    // из сети и оседают в кэше на SD. Первые четыре: остальные всё равно не
    // видны без прокрутки, а трафик тратят.
    // В полосе показываем четыре, но в галерею отдаём все: листать там есть
    // куда, а трафик тратится только на открытый снимок.
    // В полосе — сколько осталось мест после трейлеров, в галерею отдаём все:
    // листать там есть куда, а трафик тратится только на открытый снимок.
    std::vector<std::string> all = urls;
    if (slots < 0)
        slots = 0;
    if (urls.size() > static_cast<size_t>(slots))
        urls.resize(slots);

    // Видимость подписи не трогаем нигде: место под неё зарезервировано в
    // разметке, а пустой текст его не отдаёт. Иначе исчезновение «Загружаю
    // снимки…» подбрасывало вверх описание, которое в этот момент читают.
    if (urls.empty())
    {
        shotsHint->setText("");
        return;
    }

    shotsPending = static_cast<int>(urls.size());
    shotsFailed  = 0;

    // О самой загрузке сообщать словами не нужно: пустые слоты стоят на своих
    // местах и заполняются на глазах. Подпись остаётся для того, что по слотам
    // не видно, — что снимки не пришли и почему.
    shotsHint->setText("");

    for (size_t i = 0; i < urls.size(); i++)
    {
        const std::string& url = urls[i];

        auto* shot = new RemoteImage();
        shot->setWidth(MEDIA_WIDTH);
        shot->setHeight(MEDIA_HEIGHT);
        shot->setCornerRadius(6);
        shot->setMarginRight(MEDIA_GAP);
        shot->setScalingType(brls::ImageScalingType::FILL);

        // Фон-заглушка. Слот занимает своё место с первого кадра, и снимок,
        // пришедший из сети через полсекунды, не появляется из пустоты.
        shot->setBackgroundColor(nvgRGBA(255, 255, 255, 18));

        // Снимок теперь фокусируется и открывается: раньше фокус доставался
        // только кнопке трейлера, и разглядеть скриншоты было нечем.
        shot->setFocusable(true);
        shot->registerClickAction([this, all, i](brls::View*) {
            brls::Application::pushActivity(new GalleryActivity(all, i),
                                            brls::TransitionAnimation::NONE);
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
                shotsHint->setText("");
            else
                shotsHint->setText(net::isReady() ? "hub/game/shots_partial"_i18n
                                                  : "hub/game/shots_offline"_i18n);
        };
        shot->load(url);
        shotsBox->addView(shot);
    }
}

int GameActivity::fillTrailerButton(std::vector<std::string> videos)
{
    // Ролики есть у 61% игр (2139 из 3489), у остальных кнопка не должна
    // появляться вовсе — но по ТЗ просят именно disabled с подписью, так
    // понятнее, что раздел вообще существует.
    // У большинства игр ролик один, но у 295 их несколько — раньше все, кроме
    // первого, были недоступны вовсе. Показываем кнопку на каждый, но не больше
    // трёх: дальше полоса медиа вытеснила бы скриншоты.
    // Не больше двух: третий трейлер вытеснил бы из полосы все снимки, а
    // показать игру они умеют лучше.
    if (videos.size() > 2)
        videos.resize(2);

    trailerUrl = videos.empty() ? std::string() : videos.front();

    // Состояние сети здесь не спрашиваем: net::init() идёт в фоне, и карточка,
    // открытая в первые секунды после запуска, навсегда получала «нет сети» —
    // до самого закрытия, хотя сеть успевала подняться. Проверка перенесена в
    // момент нажатия, где можно и подождать.

    if (videos.empty())
    {
        trailerButton = new brls::Button();
        trailerButton->setFontSize(fonts::CAPTION);
        trailerButton->setWidth(MEDIA_WIDTH);
        trailerButton->setHeight(MEDIA_HEIGHT);
        trailerButton->setMarginRight(MEDIA_GAP);
        trailerButton->setText("hub/game/no_trailer"_i18n);
        trailerButton->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        trailerButton->setState(brls::ButtonState::DISABLED);
        shotsBox->addView(trailerButton);
        return 1;
    }

    for (size_t i = 0; i < videos.size(); i++)
    {
        auto* button = new brls::Button();
        button->setFontSize(fonts::CAPTION);
        button->setWidth(MEDIA_WIDTH);
        button->setHeight(MEDIA_HEIGHT);
        button->setMarginRight(MEDIA_GAP);

        // Тёмная плитка вместо заливки цветом акцента: в ряду превью трейлер —
        // такой же кадр, а не призыв к действию. Стиль ставим первым, свой фон
        // поверх него — иначе стиль затрёт цвет.
        button->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        button->setBackgroundColor(nvgRGBA(255, 255, 255, 20));
        button->setCornerRadius(6);
        // Номер печатаем только когда роликов правда несколько: «▶ Трейлер 1»
        // при единственном ролике обещает продолжение, которого нет.
        button->setText(videos.size() > 1 ? brls::getStr("hub/game/trailer_n", i + 1)
                                          : "hub/game/trailer"_i18n);

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

    return static_cast<int>(videos.size());
}

void GameActivity::openTrailer()
{
    if (trailerUrl.empty())
        return;

    // Сеть могла ещё не подняться, если нажали сразу после запуска. Ждём её
    // короткое время в рабочем потоке — блокировать UI ради этого нельзя.
    if (net::isReady())
    {
        brls::Application::pushActivity(new VideoPlayerActivity(trailerUrl),
                                        brls::TransitionAnimation::NONE);
        return;
    }

    // Подпись возвращаем в любом случае: на ветке отказа она оставалась
    // «Соединяемся…» до самого закрытия карточки.
    const std::string caption = trailerButton->getText();
    trailerButton->setText("hub/game/connecting"_i18n);

    auto flag       = alive;
    auto* self      = this;
    std::string url = trailerUrl;

    tasks::io([flag, self, url, caption]() {
        const bool ok = net::waitReady(5000);
        brls::sync([flag, self, url, caption, ok]() {
            if (*flag && self->trailerButton)
                self->trailerButton->setText(caption);

            if (ok)
                brls::Application::pushActivity(new VideoPlayerActivity(url),
                                                brls::TransitionAnimation::NONE);
            else
                brls::Application::notify("hub/game/no_network"_i18n);
        });
    });
}


void GameActivity::chooseFolder()
{
    folders::pick(game.nsuid, [this]() { refreshHeaderMarks(); });
}

