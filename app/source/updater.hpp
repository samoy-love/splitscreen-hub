#pragma once

#include <functional>
#include <string>

/// Обновление приложения с сервера выкатки.
///
/// Рядом с SplitScreenHub.nro на samoy.love лежит SplitScreenHub.nro.json —
/// манифест, который кладёт publish-file.sh из deploy-kit при каждой выкатке:
/// версия, размер и sha256. Приложение сравнивает версию с той, что вшита в
/// сборку (APP_VERSION из CMakeLists), качает новый .nro во временный файл
/// рядом с собой и сверяет сумму.
///
/// Подменить себя на ходу нельзя: libnx монтирует romfs из собственного .nro
/// (romfsInit в userAppInit) и держит файл открытым до самого выхода, поэтому
/// remove/rename поверх работающего файла на консоли отказывают. Скачанное
/// лежит рядом как <self>.new c меткой <self>.new.ok, а подмена делается в
/// два момента, когда файл никем не занят: при выходе по кнопке «Перезапустить»
/// (romfs уже размонтирован нами) и при следующем старте, если до выхода не
/// дошло. В обоих случаях сразу после подмены приложение перезапускается через
/// envSetNextLoad — пользователь видит короткий рестарт, а не ручной запуск.
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

/// Ход закачки: сколько получено, сколько всего (0 — неизвестно), средняя
/// скорость с начала закачки и оценка остатка (−1 — пока не посчитать).
struct Progress
{
    long long received = 0;
    long long total    = 0;
    double bytesPerSec = 0;
    int etaSeconds     = -1;
};

/// Версия работающей сборки — та же, что в nacp.
const char* currentVersion();

/// Спрашивает манифест в фоне. onResult получает найденное обновление или
/// nullptr, если его нет либо сервер недоступен (available=false и ошибка в
/// message во втором случае).
void check(std::function<void(bool available, const Info& info, const std::string& message)> onResult);

/// Качает и сверяет .nro; результат ждёт подмены рядом с приложением. onDone
/// получает ok и текст: версию при успехе, код ошибки при провале
/// («download», «checksum», «no self path»). Одновременно идёт не больше
/// одной закачки.
void install(const Info& info, std::function<void(const Progress&)> onProgress,
             std::function<void(bool ok, const std::string& message)> onDone);

/// Скачано и сверено, ждёт перезапуска.
bool hasPending();

/// Подменяет .nro скачанным и просит hbloader перезапустить приложение.
/// Звать, когда romfs можно размонтировать: перед выходом из main или в самом
/// его начале. Возвращает false и причину, если подмена не удалась —
/// приложение при этом остаётся прежним и работоспособным.
bool applyPending(std::string& error);

/// Убирает хвосты прошлых обновлений: <self>.old после удачного старта новой
/// версии и <self>.new без метки — обрыв или сборка, которая ещё не умела
/// подменять себя при перезапуске.
void cleanupLeftovers();

/// Путь к собственному .nro (argv[0] от hbloader) или пусто на десктопе.
std::string selfPath();

}  // namespace updater
