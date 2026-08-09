#include "ui/game_activity.hpp"

#include <cstdlib>

#include "app_state.hpp"
#include "net.hpp"
#include "ui/remote_image.hpp"
#include "ui/video_player.hpp"

namespace
{

/// Фирменный цвет игры приходит из eShop строкой вида "0f336f".
NVGcolor parseColor(const std::string& hex, NVGcolor fallback)
{
    if (hex.size() != 6)
        return fallback;
    char* end     = nullptr;
    long value    = std::strtol(hex.c_str(), &end, 16);
    if (end != hex.c_str() + 6)
        return fallback;
    return nvgRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
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
    c->setFontSize(15);
    c->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);

    auto* v = new brls::Label();
    v->setText(value);
    v->setFontSize(24);

    box->addView(c);
    box->addView(v);
    return box;
}

brls::Label* tag(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(16);
    label->setMarginRight(14);
    label->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    return label;
}

}  // namespace

GameActivity::GameActivity(const std::string& nsuid)
    : nsuid(nsuid)
{
}

void GameActivity::onContentAvailable()
{
    AppState& state = AppState::get();
    game            = state.catalog.byNsuid(nsuid);
    state.decorate(game);

    fillHeader();
    fillStats();
    fillTags();
    fillGenres();
    fillScreenshots();
    fillTrailerButton();

    headline->setText(game.headline);
    headline->setVisibility(game.headline.empty() ? brls::Visibility::GONE
                                                  : brls::Visibility::VISIBLE);
    description->setText(game.description);

    // у Jackbox и подобных игроки подключаются телефонами — без пояснения
    // непонятно, откуда 8 игроков при четырёх джойконах
    playersNote->setText(game.playersNote);
    playersNote->setVisibility(game.playersNote.empty() ? brls::Visibility::GONE
                                                        : brls::Visibility::VISIBLE);

    this->registerAction("Избранное", brls::BUTTON_X, [this](brls::View*) {
        toggleFavorite();
        return true;
    });
    this->registerAction("В папку", brls::BUTTON_Y, [this](brls::View*) {
        chooseFolder();
        return true;
    });
}

void GameActivity::fillHeader()
{
    header->setBackgroundColor(
        parseColor(game.backgroundColor, brls::Application::getTheme()["brls/background"]));

    if (!game.boxArtFile.empty())
        cover->setImageFromRes("art/" + game.boxArtFile);

    title->setText(game.title);

    std::string sub = game.publisher;
    if (game.releaseYear)
        sub += " · " + std::to_string(game.releaseYear);
    // Оценка и согласие обзоров: без источника и числа голосов цифра ничего
    // не значит, поэтому показываем их вместе.
    if (game.rating > 0)
    {
        sub += " · " + std::to_string(game.rating) + "%";
        if (game.ratingVotes > 0)
            sub += " из " + std::to_string(game.ratingVotes);
        if (!game.ratingSource.empty())
            sub += ", " + game.ratingSource;
    }
    if (game.topMentions > 0)
        sub += " · в подборках: " + std::to_string(game.topMentions);
    subtitle->setText(sub);

    refreshHeaderMarks();
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

void GameActivity::fillGenres()
{
    std::vector<std::string> list = AppState::get().catalog.genresOf(game.nsuid);
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

void GameActivity::fillScreenshots()
{
    shotsBox->clearViews();

    // Скриншотов в каталоге 20 тысяч — в romfs они не влезут, поэтому грузятся
    // из сети и оседают в кэше на SD. Первые четыре: остальные всё равно не
    // видны без прокрутки, а трафик тратят.
    std::vector<std::string> urls = AppState::get().catalog.screenshots(game.nsuid);
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

    for (const std::string& url : urls)
    {
        auto* shot = new RemoteImage();
        shot->setWidth(230);
        shot->setHeight(130);
        shot->setCornerRadius(6);
        shot->setMarginRight(10);
        shot->setScalingType(brls::ImageScalingType::FILL);
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

void GameActivity::fillTrailerButton()
{
    // Ролики есть у 61% игр (2139 из 3480), у остальных кнопка не должна
    // появляться вовсе — но по ТЗ просят именно disabled с подписью, так
    // понятнее, что раздел вообще существует.
    std::vector<std::string> videos = AppState::get().catalog.videos(game.nsuid);
    trailerUrl = videos.empty() ? std::string() : videos.front();

    if (!net::isReady())
    {
        trailerButton->setText("Трейлер недоступен (нет сети)");
        trailerButton->setState(brls::ButtonState::DISABLED);
        return;
    }

    if (trailerUrl.empty())
    {
        trailerButton->setText("Трейлер недоступен");
        trailerButton->setState(brls::ButtonState::DISABLED);
        return;
    }

    trailerButton->setText("▶ Смотреть трейлер");
    trailerButton->setState(brls::ButtonState::ENABLED);
    trailerButton->registerClickAction([this](brls::View*) {
        openTrailer();
        return true;
    });
}

void GameActivity::openTrailer()
{
    if (trailerUrl.empty())
        return;
    brls::Application::pushActivity(new VideoPlayerActivity(trailerUrl));
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
    AppState& state = AppState::get();
    std::vector<std::string> names = state.library.folderNames();

    // Список показывает, где игра уже лежит, и позволяет завести папку прямо
    // отсюда: раньше при отсутствии папок мы просто отфутболивали во вкладку
    // «Моя библиотека», а отметок не было вовсе — приходилось помнить.
    std::vector<std::string> items;
    items.reserve(names.size() + 1);
    for (const std::string& name : names)
        items.push_back((state.library.inFolder(name, game.nsuid) ? "* " : "   ") + name);
    items.push_back("+ Новая папка…");

    auto* dropdown = new brls::Dropdown(
        "В какую папку", items,
        [this, names](int selected) {
            if (selected < 0 || selected > static_cast<int>(names.size()))
                return;

            if (selected == static_cast<int>(names.size()))
            {
                promptNewFolder();
                return;
            }

            AppState& s = AppState::get();
            s.library.toggleInFolder(names[selected], game.nsuid);
            brls::Application::notify(s.library.inFolder(names[selected], game.nsuid)
                                          ? "Добавлено в «" + names[selected] + "»"
                                          : "Убрано из «" + names[selected] + "»");
            refreshHeaderMarks();
        },
        0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void GameActivity::promptNewFolder()
{
    brls::Application::getImeManager()->openForText(
        [this](const std::string& name) {
            if (name.empty())
                return;
            AppState& s = AppState::get();
            s.library.createFolder(name);
            s.library.toggleInFolder(name, game.nsuid);
            brls::Application::notify("Добавлено в «" + name + "»");
            refreshHeaderMarks();
        },
        "Название папки", "Например: «Вечер с друзьями»", 32);
}
