#include "ui/remote_image.hpp"

#include <borealis.hpp>

#include <memory>

#include "net.hpp"
#include "tasks.hpp"
#include "ui/async_image.hpp"

RemoteImage::RemoteImage()
    : alive(std::make_shared<std::atomic_bool>(true))
{
    this->setBackgroundColor(brls::Application::getTheme()["brls/background"]);
}

RemoteImage::~RemoteImage()
{
    *alive = false;
}

void RemoteImage::load(const std::string& url)
{
    auto flag = alive;
    auto self = this;

    brls::Logger::debug("image: запрошено {}", url);

    tasks::io([flag, self, url]() {
        std::vector<unsigned char> data = net::fetch(url);
        if (!*flag)
        {
            brls::Logger::verbose("image: получатель закрылся, бросаем {}", url);
            return;
        }
        if (data.empty())
            brls::Logger::warning("image: пусто после загрузки {}", url);

        // Скриншоты весят по 200–350 КБ, и их разбор в UI-потоке съедал по
        // несколько кадров на каждый — открытие карточки заметно подвисало.
        auto pixels = std::make_shared<asyncimage::Pixels>(
            asyncimage::decode(data.data(), data.size()));
        if (!*flag)
            return;

        brls::sync([flag, self, pixels]() {
            if (!*flag)
                return;  // карточку успели закрыть
            const bool ok = pixels->valid();
            if (ok)
                asyncimage::apply(self, *pixels);
            if (self->onDone)
                self->onDone(ok);
        });
    });
}
