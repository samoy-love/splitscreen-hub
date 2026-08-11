#pragma once

#include <borealis.hpp>

#include <atomic>
#include <memory>

#include "catalog.hpp"

/// Карточка игры: шапка красится в фирменный цвет игры, плитки с числом
/// игроков, размером и языками, описание и скриншоты.
class GameActivity : public brls::Activity
{
  public:
    /// Карточка с уже дочитанными деталями: экран рисуется сразу целиком.
    ///
    /// Открывает её GameActivity::open() — она читает базу в рабочем потоке и
    /// только потом создаёт активность. Собирать экран после открытия нельзя:
    /// вместо появления карточки зритель видел, как она складывается из кусков.
    GameActivity(Game full, std::vector<std::string> genreList,
                 std::vector<std::string> shots, std::vector<std::string> videos);

    /// Читает детали и открывает карточку, когда всё готово.
    static void open(const Game& brief);

    // «xml/» подставляет сама borealis, см. main.cpp
    CONTENT_FROM_XML_RES("activity/game.xml");

    void onContentAvailable() override;

    ~GameActivity() override;

  private:
    /// Карточку могли закрыть, пока грузилась обложка.
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);
    std::string nsuid;
    Game game;

    /// Всё, что нужно экрану, — известно до его создания.
    std::vector<std::string> genreList;
    std::vector<std::string> shots;
    std::vector<std::string> videos;
    std::string trailerUrl;

    // сколько скриншотов ещё грузится и сколько из них не удалось
    int shotsPending = 0;
    int shotsFailed  = 0;

    void fillHeader();
    /// Читает и разбирает обложку в рабочем потоке.
    void loadCoverAsync(const std::string& file);
    std::string loadedArt;  ///< какую обложку уже загрузили
    void fillStats();
    void fillTags();
    void fillGenres(const std::vector<std::string>& list);
    /// Звезда и список папок в шапке — состояние, а не кнопки.
    void refreshHeaderMarks();
    /// Заполняет полосу снимками, но не больше указанного числа мест.
    void fillScreenshots(std::vector<std::string> urls, int slots);
    void chooseFolder();

    BRLS_BIND(brls::Box, header, "game/header");
    BRLS_BIND(brls::Image, cover, "game/cover");
    BRLS_BIND(brls::Label, title, "game/title");
    BRLS_BIND(brls::Label, subtitle, "game/subtitle");
    BRLS_BIND(brls::Box, statsBox, "game/stats");
    BRLS_BIND(brls::Label, headline, "game/headline");
    BRLS_BIND(brls::Label, description, "game/description");
    BRLS_BIND(brls::Box, tagsBox, "game/tags");
    BRLS_BIND(brls::Label, playersNote, "game/players_note");
    BRLS_BIND(brls::Label, star, "game/star");
    BRLS_BIND(brls::Label, folders, "game/folders");
    BRLS_BIND(brls::Label, genres, "game/genres");
    BRLS_BIND(brls::Box, shotsBox, "game/shots");
    BRLS_BIND(brls::ScrollingFrame, scroller, "game/scroll");
    BRLS_BIND(brls::Label, shotsHint, "game/shots_hint");
    /// Кнопка трейлера создаётся кодом и встаёт первой в полосе медиа рядом со
    /// скриншотами, поэтому её нет в разметке.
    brls::Button* trailerButton = nullptr;

    /// Ставит плитки трейлеров и возвращает, сколько мест они заняли.
    int fillTrailerButton(std::vector<std::string> videos);
    void openTrailer();
};
