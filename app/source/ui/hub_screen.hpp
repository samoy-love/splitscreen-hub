#pragma once

#include <borealis.hpp>

#include <chrono>

/// Корневой контейнер экрана с анимацией появления.
///
/// borealis умеет только затухание: View::show меняет прозрачность и всё.
/// Системное меню Switch к затуханию добавляет короткий подъём — содержимое
/// приходит чуть снизу и встаёт на место. Разница мелкая, но именно она
/// отличает «экран сменился» от «кадр подменили».
///
/// Слайдом borealis это сделать нельзя: TransitionAnimation::SLIDE_LEFT
/// поддерживается только у AppletFrame, а у обычного View
/// getShowAnimationDuration на него вызывает fatal() — плеер и галерея, у
/// которых корень это Box, падали бы при открытии.
///
/// Поэтому смещение считаем сами: время берём от первого кадра после появления,
/// сглаживание — та же квадратичная кривая, что у затухания в borealis, чтобы
/// оба движения шли согласованно.
class HubScreen : public brls::Box
{
  public:
    HubScreen();

    static brls::View* create();

    void willAppear(bool resetState = false) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    /// Насколько экран приподнимается в начале. Больше — и это уже не
    /// «появился», а «выехал»; меньше — незаметно.
    static constexpr float RISE = 22.0f;

    std::chrono::steady_clock::time_point started;
    bool animating = false;
};
