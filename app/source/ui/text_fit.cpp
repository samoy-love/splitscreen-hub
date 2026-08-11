#include "ui/text_fit.hpp"

#include <borealis.hpp>

#include <vector>

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

/// Высота текста, разложенного по строкам в заданную ширину, и высота одной
/// строки — теми же настройками, какими его нарисует Label.
void measureBox(const std::string& text, float maxWidth, float fontSize, float lineHeight,
                float& height, float& single)
{
    NVGcontext* vg = brls::Application::getNVGContext();
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, lineHeight);

    nvgTextMetrics(vg, nullptr, nullptr, &single);

    float bounds[4] = {};
    nvgTextBoxBounds(vg, 0, 0, maxWidth, text.c_str(), nullptr, bounds);
    height = bounds[3] - bounds[1];
}

/// Начало символа UTF-8: у продолжающих байтов старшие биты 10xxxxxx, и резать
/// строку по ним нельзя — получится битая последовательность вместо буквы.
bool isStart(unsigned char c)
{
    return (c & 0xC0) != 0x80;
}

/// Смещения начал символов плюс конец строки.
std::vector<size_t> charStarts(const std::string& text)
{
    std::vector<size_t> starts;
    starts.reserve(text.size() + 1);
    for (size_t i = 0; i < text.size(); i++)
        if (isStart(static_cast<unsigned char>(text[i])))
            starts.push_back(i);
    starts.push_back(text.size());
    return starts;
}

}  // namespace

namespace textfit
{

std::string ellipsize(const std::string& text, float maxWidth, float fontSize)
{
    if (text.empty() || measure(text, fontSize) <= maxWidth)
        return text;

    const std::string dots = "…";

    // Границы символов считаем один раз: строка в UTF-8, и резать её по байтам
    // нельзя.
    const std::vector<size_t> starts = charStarts(text);

    // Двоичный поиск по длине вместо отрезания по одному символу. Прежний
    // вариант звал nvgTextBounds по три-четыре десятка раз на длинное название,
    // и так на каждую из семи плиток в строке при каждой прокрутке; здесь
    // вызовов шесть-семь.
    size_t low = 0, high = starts.size() - 1;
    while (low < high)
    {
        const size_t mid = (low + high + 1) / 2;
        if (measure(text.substr(0, starts[mid]) + dots, fontSize) <= maxWidth)
            low = mid;
        else
            high = mid - 1;
    }

    return low == 0 ? dots : text.substr(0, starts[low]) + dots;
}

std::string ellipsizeHeight(const std::string& text, float maxWidth, float fontSize,
                            float lineHeight, float maxHeight)
{
    if (text.empty() || maxHeight <= 0)
        return text;

    float height = 0, single = 0;
    measureBox(text, maxWidth, fontSize, lineHeight, height, single);

    const float limit = maxHeight;
    if (height <= limit)
        return text;

    const std::string dots = "…";
    const std::vector<size_t> starts = charStarts(text);

    // Двоичный поиск по длине: чем короче текст, тем меньше строк он занимает,
    // так что условие монотонно и делить пополам можно.
    size_t low = 0, high = starts.size() - 1;
    while (low < high)
    {
        const size_t mid = (low + high + 1) / 2;
        measureBox(text.substr(0, starts[mid]) + dots, maxWidth, fontSize, lineHeight, height,
                   single);
        if (height <= limit)
            low = mid;
        else
            high = mid - 1;
    }

    return low == 0 ? dots : text.substr(0, starts[low]) + dots;
}

}  // namespace textfit
