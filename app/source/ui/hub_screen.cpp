#include "ui/hub_screen.hpp"

HubScreen::HubScreen()
{
    this->setAxis(brls::Axis::COLUMN);
}

void HubScreen::willAppear(bool resetState)
{
    Box::willAppear(resetState);

    started   = std::chrono::steady_clock::now();
    animating = true;
}

void HubScreen::draw(NVGcontext* vg, float x, float y, float width, float height,
                     brls::Style style, brls::FrameContext* ctx)
{
    if (animating)
    {
        // Длительность берём ту же, что у затухания: два движения должны
        // закончиться вместе, иначе подъём выглядит отдельным рывком.
        const float duration = brls::Application::getStyle()["brls/animations/show"];
        const float elapsed  = std::chrono::duration<float, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();

        float progress = duration > 0.0f ? elapsed / duration : 1.0f;
        if (progress >= 1.0f)
        {
            progress  = 1.0f;
            animating = false;
        }

        // quadraticOut, как у alpha в View::show: быстро в начале, мягко в конце.
        const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
        this->setTranslationY(RISE * (1.0f - eased));
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

brls::View* HubScreen::create()
{
    return new HubScreen();
}
