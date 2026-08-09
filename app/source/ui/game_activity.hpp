#pragma once

#include <borealis.hpp>

#include "catalog.hpp"

/// Карточка игры: шапка красится в фирменный цвет игры, плитки с числом
/// игроков, размером и языками, описание и скриншоты.
class GameActivity : public brls::Activity
{
  public:
    explicit GameActivity(const std::string& nsuid);

    // «xml/» подставляет сама borealis, см. main.cpp
    CONTENT_FROM_XML_RES("activity/game.xml");

    void onContentAvailable() override;

  private:
    std::string nsuid;
    Game game;
    std::string trailerUrl;

    // сколько скриншотов ещё грузится и сколько из них не удалось
    int shotsPending = 0;
    int shotsFailed  = 0;

    void fillHeader();
    void fillStats();
    void fillTags();
    void fillGenres();
    /// Звезда и список папок в шапке — состояние, а не кнопки.
    void refreshHeaderMarks();
    void fillScreenshots();
    void toggleFavorite();
    void chooseFolder();
    void promptNewFolder();

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
    BRLS_BIND(brls::Label, shotsHint, "game/shots_hint");
    /// Кнопка трейлера создаётся кодом и встаёт первой в полосе медиа рядом со
    /// скриншотами, поэтому её нет в разметке.
    brls::Button* trailerButton = nullptr;

    void fillTrailerButton();
    void openTrailer();
};
