#pragma once

#include <borealis.hpp>

#include <cstddef>

/// Раскодирование картинок вне UI-потока.
///
/// brls::Image::setImageFromMem разбирает JPEG прямо там, где вызван. При
/// прокрутке сетки это происходит в UI-потоке по нескольку раз на строку, и
/// кадры проседают. Здесь разбор вынесен в рабочий поток, а в UI-потоке
/// остаётся только загрузка готовых пикселей в текстуру — это дёшево.
namespace asyncimage
{

/// Раскодированное изображение. Владеет буфером, освобождает его сам.
struct Pixels
{
    int width           = 0;
    int height          = 0;
    unsigned char* rgba = nullptr;

    Pixels()                         = default;
    Pixels(const Pixels&)            = delete;
    Pixels& operator=(const Pixels&) = delete;
    Pixels(Pixels&& other) noexcept;
    Pixels& operator=(Pixels&& other) noexcept;
    ~Pixels();

    bool valid() const { return rgba && width > 0 && height > 0; }
};

/// Разбирает JPEG или PNG. Можно звать из любого потока.
///
/// Всегда четыре канала. Класть непрозрачные картинки в текстуру GL_RGB —
/// заманчиво (треть памяти), но наружу nanovg умеет только RGBA, а свой
/// glBindTexture в обход неё сбивает её же кэш привязки
/// (NANOVG_GL_USE_STATE_FILTER в nanovg_gl.h): библиотека считает нужную
/// текстуру уже привязанной, пропускает привязку и рисует тем, что осталось в
/// контексте. На экране это белые плитки и полосы от атласа шрифта.
Pixels decode(const unsigned char* data, size_t size);

/// Создаёт текстуру из пикселей и возвращает её. Ноль — не получилось.
/// Только из UI-потока: трогает контекст nanovg, который не потокобезопасен.
int upload(const Pixels& pixels);

/// Заливает пиксели в текстуру вида. Владение текстурой остаётся у вида.
void apply(brls::Image* image, const Pixels& pixels);

}  // namespace asyncimage
