#include "ui/hub_screen.hpp"

namespace
{

/// Доля времени появления, за которую содержимое набирает полную непрозрачность.
///
/// Меньше единицы нарочно: движение и проявление, заканчиваясь вместе, дают
/// вялый выход из полупрозрачности на последних кадрах, когда экран уже почти
/// стоит на месте. Текст проступает раньше, а доезжает уже сплошным.
constexpr float FADE_PART = 0.65f;

/// Быстро в начале, мягко в конце — та же кривая, что у alpha в View::show.
float easeOut(float t)
{
    return 1.0f - (1.0f - t) * (1.0f - t);
}

/// Зеркальная ей: с места трогается медленно и разгоняется. Уход с ней выглядит
/// движением прочь, а не оседанием.
float easeIn(float t)
{
    return t * t;
}

}  // namespace

HubScreen::HubScreen()
{
    this->setAxis(brls::Axis::COLUMN);
}

void HubScreen::willAppear(bool resetState)
{
    Box::willAppear(resetState);

    started = std::chrono::steady_clock::now();
    phase   = Phase::Appearing;

    // Первый кадр рисуется уже смещённым и почти прозрачным. Без этого экран
    // успевал мигнуть на месте: draw() выставит те же значения, но только
    // когда до него дойдёт очередь.
    //
    // Не 0.0f: borealis зовёт draw() только при alpha > 0 (View::frame), а
    // именно draw() дальше поднимает alpha до 1 — с нуля она осталась бы
    // нулевой навсегда, и экран не появлялся бы вовсе.
    this->setTranslationY(RISE);
    this->setAlpha(0.001f);
}

void HubScreen::willDisappear(bool resetState)
{
    Box::willDisappear(resetState);

    started = std::chrono::steady_clock::now();
    phase   = Phase::Disappearing;
}

float HubScreen::progress() const
{
    // Длительность берём ту же, что у затухания: два движения должны
    // закончиться вместе, иначе подъём выглядит отдельным рывком.
    const float duration = brls::Application::getStyle()["brls/animations/show"];
    if (duration <= 0.0f)
        return 1.0f;

    const float elapsed
        = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
              .count();

    return elapsed >= duration ? 1.0f : elapsed / duration;
}

void HubScreen::draw(NVGcontext* vg, float x, float y, float width, float height,
                     brls::Style style, brls::FrameContext* ctx)
{
    if (phase == Phase::Appearing)
    {
        const float t = progress();

        this->setTranslationY(RISE * (1.0f - easeOut(t)));
        this->setAlpha(t >= FADE_PART ? 1.0f : easeOut(t / FADE_PART));

        if (t >= 1.0f)
            phase = Phase::Idle;
    }
    else if (phase == Phase::Disappearing)
    {
        // Прозрачность здесь не трогаем: активность гасит сама borealis, а два
        // затухания на одном содержимом перемножились бы, и экран пропадал бы
        // вдвое быстрее движения.
        const float t = progress();
        this->setTranslationY(SINK * easeIn(t));

        if (t >= 1.0f)
            phase = Phase::Idle;
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

brls::View* HubScreen::create()
{
    return new HubScreen();
}
