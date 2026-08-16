#pragma once

#include <borealis.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "catalog.hpp"

/// Плитка игры в сетке: обложка, бейдж с числом игроков, звезда у избранного
/// и подпись. Обложка грузится из romfs, а текстурой владеет кэш covers, а не
/// вид: плитка только закрепляет за собой нужную обложку (pin), пока показывает
/// её, и отпускает при переиспользовании — так память не течёт даже на быстром
/// скролле по всему каталогу.
class GameTile : public brls::Box
{
  public:
    GameTile();
    ~GameTile() override;

    void setGame(const Game& game);

    /// Что делать по нажатию A. Плитка отдаёт свою копию Game, а не ссылку в
    /// вектор каталога: список пересобирается на каждую смену фильтра, и ссылка
    /// на элемент после этого повиснет. Копия нужна карточке, чтобы нарисовать
    /// заголовок и обложку сразу, не дожидаясь чтения подробностей из каталога.
    void setOnSelect(std::function<void(const Game&)> callback);

    /// Кто получил фокус. Библиотеке это нужно, чтобы понимать, к чему
    /// относится «убрать»: к игре под курсором или к папке в списке слева.
    void setOnFocus(std::function<void(const std::string&)> callback);

    /// Плавное проявление обложки и подъём при фокусе. Обе анимации короткие и
    /// считаются по времени: borealis умеет анимировать только прозрачность
    /// целых видов, а нам нужно двигать плитку и гасить одну картинку внутри.
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    /// Когда обложка появилась — от этого момента идёт проявление.
    std::chrono::steady_clock::time_point coverShown;
    bool fading = false;

    /// Насколько плитка приподнимается под курсором.
    static constexpr float LIFT = 5.0f;
    float lift = 0.0f;

    /// Обложка читается с romfs в фоновом потоке, а в UI-поток отдаются
    /// готовые байты. Иначе шесть файловых чтений на строку происходили бы
    /// прямо в кадре отрисовки, и прокрутка дёргалась бы на каждой новой
    /// строке. Флаг живёт дольше плитки: она переиспользуется рециклером,
    /// и к моменту ответа может показывать уже другую игру.
    std::shared_ptr<std::atomic_bool> alive;
    std::string pendingArt;

    /// Поколение обложки: растёт при каждой смене pendingArt. Рабочий поток
    /// сверяет только его — читать оттуда саму строку pendingArt нельзя, её в
    /// это время переписывает UI-поток. shared_ptr, как и alive: задача может
    /// пережить плитку.
    std::shared_ptr<std::atomic<uint64_t>> artGeneration;
    void loadCover(const std::string& file);

    Game game;
    std::string nsuid;
    std::function<void(const Game&)> onSelect;
    std::function<void(const std::string&)> onFocus;

    BRLS_BIND(brls::Image, cover, "tile/cover");
    BRLS_BIND(brls::Label, players, "tile/players");
    BRLS_BIND(brls::Label, name, "tile/name");
    BRLS_BIND(brls::Label, star, "tile/star");
    BRLS_BIND(brls::Box, installedMark, "tile/installed");
};
