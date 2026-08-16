#pragma once

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

/// Картинка, которая подгружается из сети в фоне.
///
/// Загрузка идёт в отдельном потоке, а в UI картинка ставится через brls::sync —
/// borealis не потокобезопасен. Пока грузится, на месте картинки стоит
/// прямоугольник-заглушка, интерфейс при этом не замирает.
class RemoteImage : public brls::Image
{
  public:
    RemoteImage();
    ~RemoteImage() override;

    void load(const std::string& url);

    /// Вызывается в UI-потоке, когда загрузка закончилась: true — картинка
    /// показана, false — не получилось. Без этого карточка не знала, когда
    /// убирать подпись «Скриншоты загружаются…».
    std::function<void(bool ok)> onDone;

  private:
    /// Пользователь может уйти с карточки раньше, чем скачается картинка.
    /// Флаг живёт дольше самой вьюхи, и колбэк по нему понимает, что писать
    /// уже некуда.
    std::shared_ptr<std::atomic_bool> alive;
};
