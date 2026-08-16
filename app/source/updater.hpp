#pragma once

#include <functional>
#include <string>

/// Обновление приложения с сервера выкатки.
///
/// Рядом с SplitScreenHub.nro на samoy.love лежит SplitScreenHub.nro.json —
/// манифест, который кладёт publish-file.sh из deploy-kit при каждой выкатке:
/// версия, размер и sha256. Приложение сравнивает версию с той, что вшита в
/// сборку (APP_VERSION из CMakeLists), качает новый .nro во временный файл
/// рядом с собой, сверяет сумму и подменяет себя. hbloader загружает .nro в
/// память целиком, поэтому подмена работающего файла безопасна; вступит в
/// силу при следующем запуске.
///
/// Все колбэки зовутся в UI-потоке через brls::sync.
namespace updater
{

struct Info
{
    std::string version;
    std::string url;
    std::string sha256;
    long long size = 0;
};

/// Версия работающей сборки — та же, что в nacp.
const char* currentVersion();

/// Спрашивает манифест в фоне. onResult получает найденное обновление или
/// nullptr, если его нет либо сервер недоступен (available=false и ошибка в
/// message во втором случае).
void check(std::function<void(bool available, const Info& info, const std::string& message)> onResult);

/// Качает и подменяет .nro. onProgress — доля 0..1, onDone — итог с текстом
/// для пользователя. Одновременно идёт не больше одной установки.
void install(const Info& info, std::function<void(float)> onProgress,
             std::function<void(bool ok, const std::string& message)> onDone);

/// Путь к собственному .nro (argv[0] от hbloader) или пусто на десктопе.
std::string selfPath();

}  // namespace updater
