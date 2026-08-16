#pragma once

#include <borealis.hpp>

/// Шкалы оформления: кегли и отступы.
///
/// Кегли задаются только отсюда: разница в одну-две точки на глаз не читается
/// как замысел, зато читается как небрежность, поэтому произвольных размеров
/// в разметке и коде нет.
///
/// Ступеней намеренно четыре, и между ними заметный шаг: если два текста разного
/// назначения, разница должна быть видна, а если одного — размер обязан
/// совпадать.
///
/// Разметка ссылается на те же значения через `@style/hub/font/...` — то есть
/// число живёт ровно в одном месте и разъехаться не может.
namespace fonts
{

/// Подписи, чипы фильтра, вторичный текст. Меньше на телевизоре уже не читается.
constexpr float CAPTION = 15.0f;

/// Основной текст: описания, названия в списках, пункты меню.
constexpr float BODY = 17.0f;

/// Акцент: ведущая строка карточки, заметные значения, статус поверх видео.
constexpr float ACCENT = 20.0f;

/// Заголовок экрана или карточки. На экране он один.
constexpr float TITLE = 26.0f;

/// Публикует шкалу под именами `hub/font/...`, чтобы на неё могла ссылаться
/// разметка. Звать до разбора первого XML: значения `@style/...` подставляются
/// один раз при инфляции.
inline void registerMetrics()
{
    brls::Style style = brls::getStyle();
    style.addMetric("hub/font/caption", CAPTION);
    style.addMetric("hub/font/body", BODY);
    style.addMetric("hub/font/accent", ACCENT);
    style.addMetric("hub/font/title", TITLE);
}

/// Делает основным шрифтом системный шрифт консоли, а не файл из romfs.
/// Звать сразу после brls::Application::createWindow() и до первого Label.
/// На десктопе ничего не делает — там borealis сам берёт Inter из своих
/// ресурсов.
void useConsoleFont();

}  // namespace fonts

/// Единая шкала отступов.
///
/// Правило то же, что у кеглей: отступы берутся только отсюда, иначе экраны
/// с разными полями выглядят как собранные из разных приложений.
namespace space
{

/// Между тесно связанными элементами: подпись и её значение.
constexpr float XS = 4.0f;
/// Между соседними элементами одной группы: чипы в ряду.
constexpr float S = 8.0f;
/// Между группами внутри экрана: блоками карточки.
constexpr float M = 14.0f;
/// Между крупными разделами.
constexpr float L = 24.0f;
/// Поля экрана от края. Одинаковые у всех экранов — это и задаёт общую рамку.
constexpr float SCREEN = 30.0f;

inline void registerMetrics()
{
    brls::Style style = brls::getStyle();
    style.addMetric("hub/space/xs", XS);
    style.addMetric("hub/space/s", S);
    style.addMetric("hub/space/m", M);
    style.addMetric("hub/space/l", L);
    style.addMetric("hub/space/screen", SCREEN);
}

}  // namespace space
