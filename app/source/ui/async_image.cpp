#include "ui/async_image.hpp"

#include <utility>

// Реализация лежит внутри nanovg (STB_IMAGE_IMPLEMENTATION в nanovg.c) и
// экспортируется наружу, поэтому нам нужны только объявления.
#include <stb_image.h>

namespace asyncimage
{

Pixels::Pixels(Pixels&& other) noexcept
    : width(other.width)
    , height(other.height)
    , rgba(other.rgba)
{
    other.width  = 0;
    other.height = 0;
    other.rgba   = nullptr;
}

Pixels& Pixels::operator=(Pixels&& other) noexcept
{
    if (this != &other)
    {
        if (rgba)
            stbi_image_free(rgba);
        width        = other.width;
        height       = other.height;
        rgba         = other.rgba;
        other.width  = 0;
        other.height = 0;
        other.rgba   = nullptr;
    }
    return *this;
}

Pixels::~Pixels()
{
    if (rgba)
        stbi_image_free(rgba);
}

Pixels decode(const unsigned char* data, size_t size)
{
    Pixels out;
    if (!data || size == 0)
        return out;

    int channels = 0;
    out.rgba     = stbi_load_from_memory(data, static_cast<int>(size), &out.width, &out.height,
                                         &channels, 4);
    if (!out.rgba)
    {
        out.width  = 0;
        out.height = 0;
        brls::Logger::warning("image: не разобралось изображение ({} Б): {}", size,
                              stbi_failure_reason() ? stbi_failure_reason() : "причина неизвестна");
    }
    return out;
}

int upload(const Pixels& pixels)
{
    if (!pixels.valid())
        return 0;

    NVGcontext* vg = brls::Application::getNVGContext();
    int texture    = nvgCreateImageRGBA(vg, pixels.width, pixels.height, 0, pixels.rgba);
    if (texture == 0)
        brls::Logger::error("image: не создалась текстура {}x{}", pixels.width, pixels.height);
    return texture;
}

void apply(brls::Image* image, const Pixels& pixels)
{
    if (!image)
        return;

    // innerSetImage сам освобождает прежнюю текстуру вида
    if (int texture = upload(pixels))
        image->innerSetImage(texture);
}

}  // namespace asyncimage
