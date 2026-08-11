#include "ui/text_fit.hpp"

#include <borealis.hpp>

namespace
{

/// Ширина строки тем же шрифтом, которым её нарисует Label.
float measure(const std::string& text, float fontSize)
{
    NVGcontext* vg = brls::Application::getNVGContext();
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    return nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, nullptr);
}

/// Начало символа UTF-8: у продолжающих байтов старшие биты 10xxxxxx, и резать
/// строку по ним нельзя — получится битая последовательность вместо буквы.
bool isStart(unsigned char c)
{
    return (c & 0xC0) != 0x80;
}

}  // namespace

namespace textfit
{

std::string ellipsize(const std::string& text, float maxWidth, float fontSize)
{
    if (text.empty() || measure(text, fontSize) <= maxWidth)
        return text;

    const std::string dots = "…";

    // Отрезаем по одному символу с конца, пока вместе с многоточием не влезет.
    std::string cut = text;
    while (!cut.empty())
    {
        size_t last = cut.size() - 1;
        while (last > 0 && !isStart(static_cast<unsigned char>(cut[last])))
            last--;
        cut.erase(last);

        if (measure(cut + dots, fontSize) <= maxWidth)
            return cut + dots;
    }

    return dots;
}

}  // namespace textfit
