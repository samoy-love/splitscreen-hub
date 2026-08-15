#pragma once

#include <borealis.hpp>

#include <chrono>

/// Корневой контейнер экрана с анимацией появления и ухода.
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
///
/// Уход — зеркало появления. borealis перед закрытием экрана зовёт
/// willDisappear и только потом гасит активность, отводя на это те же 300 мс,
/// — этого хватает, чтобы содержимое успело осесть вниз, пока проступает
/// каталог под ним. Без этого закрытие выглядело обрывом: экран просто
/// растворялся, стоя на месте.
class HubScreen : public brls::Box
{
  public:
    HubScreen();

    static brls::View* create();

    void willAppear(bool resetState = false) override;
    void willDisappear(bool resetState = false) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    /// Насколько экран приподнимается в начале. Больше — и это уже не
    /// «появился», а «выехал»; меньше — незаметно.
    static constexpr float RISE = 22.0f;

    /// Уход короче подъёма и без выдержки: экран, который закрывают, уже не
    /// разглядывают, и долгий уход читается как задержка на нажатие B.
    static constexpr float SINK = 14.0f;

    enum class Phase
    {
        Idle,
        Appearing,
        Disappearing,
    };

    Phase phase = Phase::Idle;
    std::chrono::steady_clock::time_point started;

    /// Доля пройденного от начала фазы, от 0 до 1. Единица означает, что
    /// движение закончилось и фазу пора гасить.
    float progress() const;
};
