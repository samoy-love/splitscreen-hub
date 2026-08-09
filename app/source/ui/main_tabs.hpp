#pragma once

#include <borealis.hpp>

#include <vector>

/// Корневой экран: строка вкладок сверху и содержимое под ней.
///
/// Заменяет brls::TabFrame, у которого вкладки живут в постоянном левом
/// сайдбаре шириной 410 точек. Здесь вкладки занимают только высоту строки, а
/// вся ширина экрана достаётся сетке игр.
///
/// Вкладки создаются один раз и переключаются видимостью. TabFrame пересоздавал
/// вкладку при каждом переходе, а это перезапрос каталога на 3489 игр — больше
/// полусекунды на каждое нажатие.
class MainTabs : public brls::Box
{
  public:
    MainTabs();

    static brls::View* create();

  private:
    void addTab(const std::string& label, brls::View* content);
    void select(size_t index);

    std::vector<brls::Button*> buttons;
    std::vector<brls::View*> pages;
    size_t current = 0;

    /// Все страницы созданы. До этого момента фокус переводить некуда: вкладки
    /// ещё не в иерархии.
    bool built = false;

    BRLS_BIND(brls::Box, tabsBox, "main/tabs");
    BRLS_BIND(brls::Box, contentBox, "main/content");
};
