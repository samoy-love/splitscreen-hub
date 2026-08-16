#pragma once

#include <borealis.hpp>

#include <string>
#include <vector>

class RemoteImage;

/// Полноэкранный просмотр скриншотов с перелистыванием.
///
/// В карточке снимки показываются полосой по 228 точек — разглядеть на них
/// ничего нельзя, а открыть было невозможно вовсе: фокус доставался только
/// кнопке трейлера. Здесь снимок занимает весь экран, а влево-вправо
/// переключают его на соседний.
class GalleryActivity : public brls::Activity
{
  public:
    GalleryActivity(std::vector<std::string> urls, size_t start);

    // «xml/» подставляет сама borealis, см. main.cpp
    CONTENT_FROM_XML_RES("activity/gallery.xml");

    void onContentAvailable() override;

  private:
    void show(size_t index);

    std::vector<std::string> urls;
    size_t current = 0;

    /// Картинку пересоздаём на каждый снимок: RemoteImage сам следит за своим
    /// временем жизни, и так проще, чем гасить прошлую загрузку.
    RemoteImage* image = nullptr;

    BRLS_BIND(brls::Box, holder, "gallery/holder");
    BRLS_BIND(brls::Label, status, "gallery/status");
    BRLS_BIND(brls::Label, counter, "gallery/counter");
};
