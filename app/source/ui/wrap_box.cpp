#include "ui/wrap_box.hpp"

// borealis.hpp тянет yoga не целиком: объявления самого переноса лежат здесь
#include <yoga/Yoga.h>

WrapBox::WrapBox()
{
    // Перенос включён сразу: тег заводился ровно ради него, и отдельный атрибут
    // только добавил бы способ получить бесполезный контейнер.
    YGNodeStyleSetFlexWrap(this->ygNode, YGWrapWrap);
}

brls::View* WrapBox::create()
{
    return new WrapBox();
}
