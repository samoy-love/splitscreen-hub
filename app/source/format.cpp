#include "format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fmtx
{

bool sizeInGigabytes(long long bytes, double& value)
{
    value = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (value >= 1.0)
        return true;
    value = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return false;
}

std::string formatSize(long long bytes, const char* gigabytes, const char* megabytes)
{
    if (bytes <= 0)
        return "";

    double value = 0;
    const bool gb = sizeInGigabytes(bytes, value);

    char buf[32];
    std::snprintf(buf, sizeof(buf), gb ? "%.1f %s" : "%.0f %s", value,
                  gb ? gigabytes : megabytes);
    return buf;
}

int languageCount(const std::string& languages)
{
    if (languages.empty())
        return 0;
    int n = 1;
    for (char c : languages)
        n += c == ',';
    return n;
}

bool parseHexColor(const std::string& hex, unsigned char& r, unsigned char& g, unsigned char& b)
{
    if (hex.size() != 6)
        return false;

    unsigned value = 0;
    for (char c : hex)
    {
        unsigned digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<unsigned>(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<unsigned>(c - 'A') + 10;
        else
            return false;  // strtol пропускал бы «0x», пробелы и знак

        value = value * 16 + digit;
    }

    r = static_cast<unsigned char>((value >> 16) & 0xFF);
    g = static_cast<unsigned char>((value >> 8) & 0xFF);
    b = static_cast<unsigned char>(value & 0xFF);
    return true;
}

namespace
{

/// Канал из sRGB в линейный свет. Экран отдаёт не то, что записано в байте:
/// зависимость примерно степенная, и без её обращения синий фон считался бы
/// светлее, чем он есть.
double linearize(unsigned char channel)
{
    const double v = channel / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

/// Цвет светлеет к белому на долю k, тон при этом сохраняется: все каналы
/// идут к 255 пропорционально оставшемуся до него расстоянию.
void lighten(unsigned char& r, unsigned char& g, unsigned char& b, double k)
{
    auto mix = [k](unsigned char c) {
        return static_cast<unsigned char>(c + (255.0 - c) * k + 0.5);
    };
    r = mix(r);
    g = mix(g);
    b = mix(b);
}

/// То же к чёрному: каналы умножаются на общий множитель, поэтому их отношение,
/// то есть тон, не меняется.
void darken(unsigned char& r, unsigned char& g, unsigned char& b, double k)
{
    auto mix = [k](unsigned char c) { return static_cast<unsigned char>(c * (1.0 - k)); };
    r = mix(r);
    g = mix(g);
    b = mix(b);
}

}  // namespace

double relativeLuminance(unsigned char r, unsigned char g, unsigned char b)
{
    return 0.2126 * linearize(r) + 0.7152 * linearize(g) + 0.0722 * linearize(b);
}

double contrastRatio(double luminanceA, double luminanceB)
{
    const double lighter = luminanceA > luminanceB ? luminanceA : luminanceB;
    const double darker  = luminanceA > luminanceB ? luminanceB : luminanceA;
    return (lighter + 0.05) / (darker + 0.05);
}

bool readableOn(unsigned char& r, unsigned char& g, unsigned char& b)
{
    // Светлый текст — белый, тёмный — не чистый чёрный, а тот же графит, что у
    // меток на кнопках: чёрный на цветной заливке режет глаз.
    const double lightText = relativeLuminance(0xFF, 0xFF, 0xFF);
    const double darkText  = relativeLuminance(0x16, 0x18, 0x1C);

    double background     = relativeLuminance(r, g, b);
    const bool preferDark = contrastRatio(background, darkText) > contrastRatio(background, lightText);
    const double text     = preferDark ? darkText : lightText;

    // Шаг мелкий: цвет должен отойти ровно настолько, чтобы текст стало видно,
    // и ни оттенком больше. Сорока шагов по 4% хватает, чтобы дойти от любого
    // цвета до края шкалы, — цикл всегда завершается.
    for (int step = 0; step < 40 && contrastRatio(background, text) < MIN_CONTRAST; step++)
    {
        // Уводим фон от текста: под тёмным текстом фон светлеет, под светлым —
        // темнеет. Тон при этом сохраняется, меняется только светлота.
        if (preferDark)
            lighten(r, g, b, 0.04);
        else
            darken(r, g, b, 0.04);

        background = relativeLuminance(r, g, b);
    }

    return preferDark;
}

int compareVersions(const std::string& a, const std::string& b)
{
    auto parse = [](const std::string& v) {
        std::vector<int> out;
        std::size_t i = 0;
        if (i < v.size() && (v[i] == 'v' || v[i] == 'V'))
            ++i;
        int cur = 0;
        bool digit = false;
        for (; i <= v.size(); ++i)
        {
            const char c = i < v.size() ? v[i] : '.';
            if (c >= '0' && c <= '9')
            {
                cur   = cur * 10 + (c - '0');
                digit = true;
            }
            else if (c == '.')
            {
                out.push_back(digit ? cur : 0);
                cur   = 0;
                digit = false;
            }
            else
                break;  // хвост вроде «-beta» не участвует
        }
        if (digit)
            out.push_back(cur);
        return out;
    };
    const std::vector<int> x = parse(a), y = parse(b);
    for (std::size_t i = 0; i < std::max(x.size(), y.size()); ++i)
    {
        const int l = i < x.size() ? x[i] : 0;
        const int r = i < y.size() ? y[i] : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

}  // namespace fmtx
