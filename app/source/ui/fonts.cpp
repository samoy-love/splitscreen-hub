#include "ui/fonts.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace fonts
{

void useConsoleFont()
{
#ifdef __SWITCH__
    // borealis на консоли рисует всё шрифтом под именем FONT_CHINESE_SIMPLIFIED:
    // это либо romfs:/font/font.ttf, либо китайский системный, у которого
    // кириллица и цифры полноширинные. Системный «стандартный» шрифт (тот же
    // UD Shin Go, что и во всём интерфейсе консоли) borealis загружает как
    // FONT_REGULAR, но основным не делает. Класть его копию в romfs нельзя —
    // шрифт принадлежит Nintendo и Morisawa; брать у консоли в рантайме
    // можно — на это и рассчитан pl:u.
    //
    // Поэтому подменяем запись FONT_CHINESE_SIMPLIFIED системным шрифтом до
    // первого Label: Application::getDefaultFont() запоминает id при первом
    // вызове. Цепочку запасных шрифтов (CJK, значки консоли, Material)
    // навешиваем на новый id заново — nanovg привязывает её к id, а не к имени.
    PlFontData standard {};
    if (R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard)))
        return;

    NVGcontext* vg = brls::Application::getNVGContext();
    const int chinese = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
    if (!brls::Application::loadFontFromMemory(brls::FONT_CHINESE_SIMPLIFIED,
                                               standard.address, standard.size, false))
        return;
    const int main = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);

    auto fallback = [&](int id) {
        if (id != brls::FONT_INVALID && id != main)
            nvgAddFallbackFontId(vg, main, id);
    };
    fallback(chinese);
    for (const std::string& name : { brls::FONT_CHINESE_SIMPLIFIED_EXT, brls::FONT_CHINESE_TRADITIONAL,
                                     brls::FONT_KOREAN_REGULAR, brls::FONT_SWITCH_ICONS,
                                     brls::FONT_MATERIAL_ICONS })
        fallback(brls::Application::getFont(name));
#endif
}

}  // namespace fonts
