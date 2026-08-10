#include "ui/gallery_activity.hpp"

#include "net.hpp"
#include "ui/remote_image.hpp"

using namespace brls::literals;

GalleryActivity::GalleryActivity(std::vector<std::string> urls, size_t start)
    : urls(std::move(urls))
    , current(start)
{
}

void GalleryActivity::onContentAvailable()
{
    if (urls.empty())
    {
        status->setText("hub/gallery/none"_i18n);
        return;
    }

    if (current >= urls.size())
        current = 0;

    this->registerAction("hub/action/close"_i18n, brls::BUTTON_B, [this](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::FADE);
        return true;
    });
    this->registerAction("hub/action/prev_shot"_i18n, brls::BUTTON_LEFT, [this](brls::View*) {
        show((current + urls.size() - 1) % urls.size());
        return true;
    });
    this->registerAction("hub/action/next_shot"_i18n, brls::BUTTON_RIGHT, [this](brls::View*) {
        show((current + 1) % urls.size());
        return true;
    });

    show(current);
}

void GalleryActivity::show(size_t index)
{
    current = index;

    counter->setText(brls::getStr("hub/gallery/counter", current + 1, urls.size()));
    status->setText("hub/gallery/loading"_i18n);
    status->setVisibility(brls::Visibility::VISIBLE);

    holder->clearViews();

    image = new RemoteImage();
    image->setWidthPercentage(100.0f);
    image->setHeightPercentage(100.0f);

    // Единственное, что может взять фокус на этом экране. Без этого действиям
    // активности не на чём сработать и стрелки не листали бы — ровно та же
    // ошибка, что была с поверхностью видео. Рамку прячем: подсвечивать
    // единственный элемент незачем.
    image->setFocusable(true);
    image->setHideHighlight(true);
    // FIT, а не FILL: снимок надо увидеть целиком, а не обрезанным по краям.
    image->setScalingType(brls::ImageScalingType::FIT);
    image->onDone = [this](bool ok) {
        status->setText(ok ? ""
                           : (net::isReady() ? "hub/gallery/failed"_i18n
                                             : "hub/gallery/offline"_i18n));
        status->setVisibility(ok ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    };
    holder->addView(image);
    image->load(urls[current]);

    // clearViews() уничтожил прежнюю картинку вместе с фокусом — возвращаем его
    // на новую, иначе после первого перелистывания стрелки перестают работать.
    brls::Application::giveFocus(image);
}
