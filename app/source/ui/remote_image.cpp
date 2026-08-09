#include "ui/remote_image.hpp"

#include <borealis.hpp>

#include "net.hpp"
#include "tasks.hpp"

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

        brls::sync([flag, self, data = std::move(data)]() {
            if (!*flag)
                return;  // карточку успели закрыть
            const bool ok = !data.empty();
            if (ok)
                self->setImageFromMem(data.data(), static_cast<int>(data.size()));
            if (self->onDone)
                self->onDone(ok);
        });
    });
}
