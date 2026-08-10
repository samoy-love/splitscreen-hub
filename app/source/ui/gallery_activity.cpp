#include "ui/gallery_activity.hpp"

#include "net.hpp"
#include "ui/remote_image.hpp"

GalleryActivity::GalleryActivity(std::vector<std::string> urls, size_t start)
    : urls(std::move(urls))
    , current(start)
{
}

void GalleryActivity::onContentAvailable()
{
    if (urls.empty())
    {
        status->setText("Скриншотов нет");
        return;
    }

    if (current >= urls.size())
        current = 0;

    this->registerAction("Закрыть", brls::BUTTON_B, [this](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::FADE);
        return true;
    });
    this->registerAction("Предыдущий", brls::BUTTON_LEFT, [this](brls::View*) {
        show((current + urls.size() - 1) % urls.size());
        return true;
    });
    this->registerAction("Следующий", brls::BUTTON_RIGHT, [this](brls::View*) {
        show((current + 1) % urls.size());
        return true;
    });

    show(current);
}

void GalleryActivity::show(size_t index)
{
    current = index;

    counter->setText(std::to_string(current + 1) + " из " + std::to_string(urls.size()));
    status->setText("Загрузка…");
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
                           : (net::isReady() ? "Не удалось загрузить снимок"
                                             : "Нет сети"));
        status->setVisibility(ok ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    };
    holder->addView(image);
    image->load(urls[current]);

    // clearViews() уничтожил прежнюю картинку вместе с фокусом — возвращаем его
    // на новую, иначе после первого перелистывания стрелки перестают работать.
    brls::Application::giveFocus(image);
}
