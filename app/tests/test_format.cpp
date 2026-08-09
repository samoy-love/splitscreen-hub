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
    const std::string got = fmtx::formatSize(bytes);
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

    std::printf("\n%s\n", failures ? "ЕСТЬ ПРОВАЛЫ" : "все проверки прошли");
    return failures ? 1 : 0;
}
