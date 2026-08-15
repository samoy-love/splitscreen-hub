// Тесты чистых функций из source/format.cpp.
//
// Собираются обычным g++ на машине разработчика и не требуют ни консоли, ни
// тулчейна Switch: см. tools/run_tests.sh. Проверять это на железе — минуты
// на прогон ради функций, которые считаются за микросекунды.

#include "../source/format.hpp"

#include <cstdio>
#include <string>

namespace
{

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "ok  " : "FAIL", what, detail.empty() ? "" : " — ",
                detail.c_str());
    if (!ok)
        failures++;
}

void expectSize(long long bytes, const std::string& expected)
{
    // Единицы задаёт вызывающий — в приложении они приходят из i18n. В тесте
    // подставляем русские: ожидания в проверках ниже написаны под них.
    const std::string got = fmtx::formatSize(bytes, "ГБ", "МБ");
    check(("formatSize(" + std::to_string(bytes) + ")").c_str(), got == expected,
          "получили «" + got + "», ждали «" + expected + "»");
}

void expectLangs(const std::string& input, int expected)
{
    const int got = fmtx::languageCount(input);
    check(("languageCount(\"" + input + "\")").c_str(), got == expected,
          std::to_string(got) + " вместо " + std::to_string(expected));
}

void expectColor(const std::string& hex, bool valid, int r = 0, int g = 0, int b = 0)
{
    unsigned char gr = 0, gg = 0, gb = 0;
    const bool ok = fmtx::parseHexColor(hex, gr, gg, gb);

    if (!valid)
    {
        check(("parseHexColor(\"" + hex + "\") отвергнут").c_str(), !ok);
        return;
    }

    check(("parseHexColor(\"" + hex + "\")").c_str(),
          ok && gr == r && gg == g && gb == b,
          std::to_string(gr) + "," + std::to_string(gg) + "," + std::to_string(gb));
}

/// Главное свойство readableOn: что бы ни пришло из eShop, подпись на шапке
/// после неё читается. Проверяем именно контраст, а не конкретные числа, —
/// иначе тест сломается от любой правки шага подгонки, ничего не сказав о сути.
void expectReadable(const std::string& hex)
{
    unsigned char r = 0, g = 0, b = 0;
    fmtx::parseHexColor(hex, r, g, b);

    const unsigned char wasR = r, wasG = g, wasB = b;
    const bool dark          = fmtx::readableOn(r, g, b);

    const double text = dark ? fmtx::relativeLuminance(0x16, 0x18, 0x1C)
                             : fmtx::relativeLuminance(0xFF, 0xFF, 0xFF);
    const double ratio = fmtx::contrastRatio(fmtx::relativeLuminance(r, g, b), text);

    char detail[160];
    std::snprintf(detail, sizeof(detail), "%02x%02x%02x → %02x%02x%02x, текст %s, контраст %.2f",
                  wasR, wasG, wasB, r, g, b, dark ? "тёмный" : "светлый", ratio);

    check(("readableOn(\"" + hex + "\")").c_str(), ratio >= fmtx::MIN_CONTRAST, detail);
}

/// Цвета, на которых хватает выбора текста, обязаны остаться нетронутыми:
/// фирменный цвет игры — это продолжение её обложки, и трогать его без нужды
/// нельзя.
void expectKept(const std::string& hex)
{
    unsigned char r = 0, g = 0, b = 0;
    fmtx::parseHexColor(hex, r, g, b);

    const unsigned char wasR = r, wasG = g, wasB = b;
    fmtx::readableOn(r, g, b);

    check(("readableOn(\"" + hex + "\") не трогает цвет").c_str(),
          r == wasR && g == wasG && b == wasB);
}

}  // namespace

int main()
{
    std::printf("formatSize:\n");
    expectSize(0, "");             // размер неизвестен — в базе это ноль
    expectSize(-1, "");            // мусор в данных не должен рисоваться
    expectSize(512LL * 1024 * 1024, "512 МБ");
    expectSize(2LL * 1024 * 1024 * 1024, "2.0 ГБ");
    // ровно на границе гигабайта берётся ветка «ГБ», а не «1024 МБ»
    expectSize(1024LL * 1024 * 1024, "1.0 ГБ");
    expectSize(1024LL * 1024 * 1024 - 1, "1024 МБ");

    std::printf("\nlanguageCount:\n");
    expectLangs("", 0);
    expectLangs("ru", 1);
    expectLangs("ru,en", 2);
    expectLangs("ru,en,ja,de,fr", 5);

    std::printf("\nparseHexColor:\n");
    expectColor("0f336f", true, 0x0f, 0x33, 0x6f);
    expectColor("FFFFFF", true, 255, 255, 255);
    expectColor("000000", true, 0, 0, 0);
    expectColor("", false);
    expectColor("0f336", false);      // пять символов
    expectColor("0f336ff", false);    // семь
    expectColor("0x3366", false);     // strtol проглатывал бы префикс
    expectColor("00 336f", false);    // и пробел тоже
    expectColor("gggggg", false);

    std::printf("\nrelativeLuminance:\n");
    check("чёрный — ноль", fmtx::relativeLuminance(0, 0, 0) == 0.0);
    check("белый — единица", fmtx::relativeLuminance(255, 255, 255) == 1.0);
    // Не среднее каналов: глаз втрое чувствительнее к зелёному, чем к красному,
    // и вчетверо — чем к синему.
    check("зелёный светлее красного",
          fmtx::relativeLuminance(0, 255, 0) > fmtx::relativeLuminance(255, 0, 0));
    check("красный светлее синего",
          fmtx::relativeLuminance(255, 0, 0) > fmtx::relativeLuminance(0, 0, 255));
    check("чёрное на белом — 21",
          fmtx::contrastRatio(0.0, 1.0) > 20.9 && fmtx::contrastRatio(0.0, 1.0) < 21.1);
    check("цвет сам с собой — 1", fmtx::contrastRatio(0.3, 0.3) == 1.0);

    std::printf("\nreadableOn — текст читается на любом цвете:\n");
    expectReadable("0f336f");  // тёмно-синий, обычный случай
    expectReadable("ffffff");  // белая обложка — на ней и падал белый текст
    expectReadable("000000");
    expectReadable("f7e7c3");  // песочный, Animal Crossing
    expectReadable("ff0000");  // средний по яркости: не годится ни тот, ни другой
    expectReadable("e63946");
    expectReadable("00c2ff");
    expectReadable("7f7f7f");  // ровно середина шкалы — худший возможный случай
    expectReadable("ffff00");  // самый светлый из насыщенных
    expectReadable("0000ff");  // самый тёмный из насыщенных

    std::printf("\nreadableOn — цвет остаётся прежним, где это возможно:\n");
    expectKept("0f336f");
    expectKept("ffffff");
    expectKept("000000");
    expectKept("f7e7c3");

    std::printf("\n%s\n", failures ? "ЕСТЬ ПРОВАЛЫ" : "все проверки прошли");
    return failures ? 1 : 0;
}
