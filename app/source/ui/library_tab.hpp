#pragma once

#include <borealis.hpp>

#include <atomic>
#include <memory>

#include "catalog.hpp"
#include "ui/game_grid.hpp"

/// Моя библиотека: слева избранное и папки, справа сетка игр выбранного списка.
/// Всё хранится на SD-карте и переживает перезапуск.
class LibraryTab : public brls::Box
{
  public:
    LibraryTab();
    ~LibraryTab() override;
    static brls::View* create();

  private:
    std::string selected;  // пусто — показываем избранное

    void rebuildSidebar();
    void showSelection();
    /// Показывает уже прочитанный список. Только из UI-потока.
    void applyGames(std::vector<Game> games);

    /// Вкладка может закрыться, пока список читается.
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);
    void promptNewFolder();
    void promptRename();
    void confirmRemove();
    /// Убрать игру из текущего списка. В макете это X на плитке —
    /// иначе за каждой правкой пришлось бы открывать карточку.
    void removeFocused();
    std::string focusedNsuid;

    // Клавиатура на Switch асинхронная: имя приходит колбэком, а не возвратом
    // из openForText, поэтому обработка вынесена отдельно.
    void onFolderNamed(const std::string& name);
    void onFolderRenamed(const std::string& name);

    std::shared_ptr<GridModel> model;

    BRLS_BIND(brls::Box, sidebar, "library/sidebar");
    BRLS_BIND(brls::RecyclerFrame, grid, "library/grid");
    BRLS_BIND(brls::Label, emptyLabel, "library/empty");
};
