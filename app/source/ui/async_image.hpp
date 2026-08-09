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
    int width              = 0;
    int height             = 0;
    unsigned char* rgba    = nullptr;

    Pixels()                         = default;
    Pixels(const Pixels&)            = delete;
    Pixels& operator=(const Pixels&) = delete;
    Pixels(Pixels&& other) noexcept;
    Pixels& operator=(Pixels&& other) noexcept;
    ~Pixels();

    bool valid() const { return rgba && width > 0 && height > 0; }
};

/// Разбирает JPEG или PNG. Можно звать из любого потока.
Pixels decode(const unsigned char* data, size_t size);

/// Заливает пиксели в текстуру вида. Только из UI-потока: трогает контекст
/// nanovg, который не потокобезопасен.
void apply(brls::Image* image, const Pixels& pixels);

}  // namespace asyncimage
