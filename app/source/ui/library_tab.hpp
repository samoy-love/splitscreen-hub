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

    /// Сверяет версию библиотеки и перечитывает раздел, если она изменилась.
    /// Вызывается из отрисовки, а её не бывает у спрятанной вкладки — поэтому
    /// проверка не стоит ничего, пока вкладка не на экране.
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    void rebuildSidebar();

    /// Откладывает перестройку на следующий кадр. Нужна там, где состав папок
    /// меняется из обработчика: удалять вид, пока его обработчик выполняется,
    /// нельзя — borealis обращается к нему после возврата.
    void scheduleSidebar();

    /// Обновляет подсветку и счётчики, не пересоздавая кнопки. Смена раздела
    /// меняет ровно это, а пересоздание убивало кнопку вместе с фокусом.
    void refreshSidebar();

    /// Строка списка разделов.
    ///
    /// Не Button: у него одна подпись, и счётчик приходилось приписывать к
    /// названию через пробелы — в столбец числа из-за этого не выстраивались.
    /// Здесь название и счётчик — отдельные надписи, разнесённые по краям.
    struct Entry
    {
        std::string name;
        brls::Box* row     = nullptr;
        brls::Label* label = nullptr;
        brls::Label* count = nullptr;
    };

    std::vector<Entry> entries;

    /// Версия библиотеки, на которой построено то, что сейчас на экране.
    /// Вкладки создаются один раз при запуске и живут всё время работы: без
    /// сверки список остаётся таким, каким был на старте.
    unsigned seenRevision = 0;

    /// Ширина, под которую в последний раз считались кадры ячеек.
    ///
    /// RecyclerFrame::checkWidth() в borealis хранит прежнюю ширину в static —
    /// одной на все экземпляры (recycler.cpp:369). Наши два рециклера, каталог и
    /// библиотека, переписывают её друг другу, а reloadData() без поднятого
    /// флага layouted молча ничего не делает. В итоге кадры ячеек остаются
    /// посчитанными под чужую геометрию, и сетка застывает съехавшей.
    /// Отслеживаем ширину сами и пересчитываем, когда она изменилась.
    int gridWidth = 0;

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
