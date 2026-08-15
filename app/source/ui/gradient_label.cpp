#include "ui/gradient_label.hpp"

#include <cmath>

namespace
{

/// Цвета перелива. Зависят от темы: в светлой мятный с васильковым теряются на
/// белом фоне, и вместо них берутся те же тона, но глубокие.
struct Palette
{
    NVGcolor from;
    NVGcolor to;
    NVGcolor flash;  ///< цвет блика
};

Palette palette()
{
    const bool dark = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;

    if (dark)
        return { nvgRGB(0x3D, 0xE8, 0xC4), nvgRGB(0x69, 0x8C, 0xFF),
                 nvgRGBA(0xFF, 0xFF, 0xFF, 0xB0) };

    return { nvgRGB(0x0F, 0x8E, 0x7A), nvgRGB(0x2E, 0x4C, 0xC8),
             nvgRGBA(0x8F, 0xFF, 0xEA, 0xC0) };
}

}  // namespace

GradientLabel::GradientLabel()
    : started(std::chrono::steady_clock::now())
{
}

float GradientLabel::elapsedMs() const
{
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
        .count();
}

void GradientLabel::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx)
{
    const std::string text = this->getFullText();
    if (width <= 0 || text.empty())
        return;

    nvgFontSize(vg, this->getFontSize());
    nvgFontFaceId(vg, this->getFont());
    nvgFontQuality(vg, this->getFontQuality());
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    const Palette colors = palette();
    const float ms       = elapsedMs();
    const float baseline = y + height / 2.0f;

    // Цвета текут вдоль надписи: ось градиента вдвое шире её самой и ходит
    // по синусу на половину ширины в каждую сторону. Через буквы при этом
    // проезжает то один конец шкалы, то другой, а на синусе нет точки, где
    // движение прерывается, — петля сходится сама.
    const float phase = std::sin(ms / FLOW_PERIOD_MS * 2.0f * 3.14159265f);
    const float shift = phase * width * 0.5f;

    // Слегка наклонный: строго горизонтальный градиент на строке в один кегль
    // почти не виден, буквы просто окрашены слева направо.
    NVGpaint flow = nvgLinearGradient(vg, x + shift - width * 0.4f, y,
                                      x + shift + width * 1.4f, y + height,
                                      colors.from, colors.to);
    nvgFillPaint(vg, this->a(flow));
    nvgText(vg, x, baseline, text.c_str(), nullptr);

    // Блик: узкая светлая полоса, проходящая по надписи и снова пропадающая.
    // Пауза между проходами длиннее самого прохода — без неё это уже не блик,
    // а бегущая строка.
    const float cycle = std::fmod(ms, FLASH_PERIOD_MS) / FLASH_PERIOD_MS;
    if (cycle >= FLASH_PART)
        return;

    const float travel = cycle / FLASH_PART;
    const float band   = width * FLASH_WIDTH;

    // Начинаем и заканчиваем за краями надписи, иначе блик возникает и гаснет
    // прямо на буквах.
    const float center = x - band + travel * (width + 2.0f * band);

    // Полоса рисуется двумя половинами: nvgLinearGradient умеет только два
    // цвета, а нужен подъём к середине и спад — иначе вместо блика по надписи
    // едет светлый край.
    // Тот же цвет, но прозрачный: сводить к прозрачному белому нельзя, к краям
    // блика тогда примешивается белизна, которой в его цвете нет.
    NVGcolor clear = colors.flash;
    clear.a        = 0.0f;

    struct Half
    {
        float fromX, toX;
        NVGcolor fromColor, toColor;
    };
    const Half halves[2] = {
        { center - band / 2.0f, center, clear, colors.flash },
        { center, center + band / 2.0f, colors.flash, clear },
    };

    for (const Half& half : halves)
    {
        nvgSave(vg);
        // Без отсечения градиент за своими крайними точками продолжается
        // сплошным цветом, и вторая половина блика заливала бы всю надпись.
        nvgIntersectScissor(vg, half.fromX, y, half.toX - half.fromX, height);

        NVGpaint sheen = nvgLinearGradient(vg, half.fromX, y, half.toX, y, half.fromColor,
                                           half.toColor);
        nvgFillPaint(vg, this->a(sheen));
        nvgText(vg, x, baseline, text.c_str(), nullptr);

        nvgRestore(vg);
    }
}

brls::View* GradientLabel::create()
{
    return new GradientLabel();
}
