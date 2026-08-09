#pragma once

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <memory>

#include "catalog.hpp"

/// Плитка игры в сетке: обложка, бейдж с числом игроков, звезда у избранного
/// и подпись. Обложка грузится из romfs — при переиспользовании плитки
/// borealis сам освобождает старую текстуру, поэтому память не течёт даже на
/// быстром скролле по всему каталогу.
class GameTile : public brls::Box
{
  public:
    GameTile();
    ~GameTile() override;

    void setGame(const Game& game);

    /// Что делать по нажатию A. Плитка хранит nsuid, а не ссылку на Game:
    /// список игр пересобирается при каждой смене фильтра, и ссылка на элемент
    /// вектора после этого повиснет.
    void setOnSelect(std::function<void(const std::string&)> callback);

    static brls::View* create();

  private:
    /// Обложка читается с romfs в фоновом потоке, а в UI-поток отдаются
    /// готовые байты. Иначе шесть файловых чтений на строку происходили бы
    /// прямо в кадре отрисовки, и прокрутка дёргалась бы на каждой новой
    /// строке. Флаг живёт дольше плитки: она переиспользуется рециклером,
    /// и к моменту ответа может показывать уже другую игру.
    std::shared_ptr<std::atomic_bool> alive;
    std::string pendingArt;
    void loadCover(const std::string& file);

    std::string nsuid;
    std::function<void(const std::string&)> onSelect;

    BRLS_BIND(brls::Image, cover, "tile/cover");
    BRLS_BIND(brls::Label, players, "tile/players");
    BRLS_BIND(brls::Label, name, "tile/name");
    BRLS_BIND(brls::Label, star, "tile/star");
    BRLS_BIND(brls::Box, installedMark, "tile/installed");
};
