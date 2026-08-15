#include "net.hpp"

#include <borealis.hpp>
#include <sys/stat.h>
#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <cstdio>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <curl/curl.h>

#include "tasks.hpp"
#include "perf.hpp"

namespace
{

#ifdef __SWITCH__
const char* CACHE_DIR = "sdmc:/switch/splitscreen-hub/cache";
#else
const char* CACHE_DIR = "cache";
#endif

/// Кэш не должен разрастаться бесконечно: скриншотов 20 тысяч, а роликов
/// 2621 штука по несколько мегабайт — без лимита SD-карта рано или поздно
/// закончится. Держим суммарный размер каталога кэша под этим порогом,
/// выкидывая самые старые файлы (по mtime) при превышении.
constexpr long long CACHE_LIMIT_BYTES = 700LL * 1024 * 1024;

/// Пишется в heavy-потоке, читается из трёх io-потоков и из UI — обычный bool
/// здесь был гонкой.
std::atomic_bool ready { false };
std::atomic_bool initDone { false };  // init() отработал — успешно или нет

/// Сокеты закрывает тот, кто их открыл. Если их подняла borealis, наш socketExit
/// оборвал бы связь у всего приложения.
std::atomic_bool weOwnSockets { false };
std::mutex readyMutex;
std::condition_variable readyCv;

/// Три попытки на картинку: этого хватает на короткий провал связи и
/// не растягивает ожидание, если сети нет совсем.
constexpr int FETCH_ATTEMPTS = 3;

/// Потолок на размер одного ответа. Обложки и скриншоты укладываются в единицы
/// мегабайт, а без предела ответ льётся в память сколько дадут.
constexpr curl_off_t MAX_RESPONSE_BYTES = 32LL * 1024 * 1024;

/// Полный обход кэша — это opendir плюс stat на каждый файл, а после часа
/// пролистывания карточек файлов там тысячи. Делать его на каждой записи
/// расточительно, поэтому считаем записанные байты и проверяем, только когда
/// с прошлой проверки набежало заметно.
constexpr long long BYTES_BETWEEN_SWEEPS = 32LL * 1024 * 1024;

/// Инкрементируется из всех io-потоков сразу.
std::atomic<long long> bytesSinceSweep { 0 };

/// Чистка кэша из двух потоков одновременно удаляла бы файлы друг у друга и
/// сыпала предупреждениями о неудавшемся remove.
std::mutex sweepMutex;

void enforceCacheLimit()
{
    std::lock_guard<std::mutex> sweepLock(sweepMutex);

    DIR* dir = ::opendir(CACHE_DIR);
    if (!dir)
        return;

    struct Entry
    {
        std::string path;
        time_t mtime;
        long long size;
    };
    std::vector<Entry> entries;
    long long total = 0;

    struct dirent* ent;
    while ((ent = ::readdir(dir)) != nullptr)
    {
        const std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        // недокачанные файлы (.tmp/.part) не трогаем — их удалит сам загрузчик
        if (name.size() > 4 && (name.substr(name.size() - 4) == ".tmp"
                || name.substr(name.size() - 5) == ".part"))
            continue;

        const std::string path = std::string(CACHE_DIR) + "/" + name;
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0)
            continue;

        entries.push_back({path, st.st_mtime, static_cast<long long>(st.st_size)});
        total += st.st_size;
    }
    ::closedir(dir);

    if (total <= CACHE_LIMIT_BYTES)
        return;

    std::sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });

    const long long was = total;
    int removed         = 0;
    for (const auto& e : entries)
    {
        if (total <= CACHE_LIMIT_BYTES)
            break;
        if (std::remove(e.path.c_str()) == 0)
        {
            total -= e.size;
            removed++;
        }
        else
        {
            brls::Logger::warning("net: не удалось удалить из кеша {}", e.path);
        }
    }

    brls::Logger::info("net: чистка кеша — удалено файлов {}, было {} МБ, стало {} МБ", removed,
                       was / (1024 * 1024), total / (1024 * 1024));
}

size_t onData(void* chunk, size_t size, size_t count, void* userdata)
{
    auto* out         = static_cast<std::vector<unsigned char>*>(userdata);
    const size_t total = size * count;
    auto* bytes        = static_cast<unsigned char*>(chunk);
    out->insert(out->end(), bytes, bytes + total);
    return total;
}

std::vector<unsigned char> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good())
        return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
}

void writeFile(const std::string& path, const std::vector<unsigned char>& data)
{
    // через временный файл: оборванная запись не должна оставить битую картинку,
    // которую мы потом будем считать валидным кэшем
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good())
        {
            brls::Logger::warning("net: не удалось создать {} (место на SD?)", tmp);
            return;
        }
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out.good())
        {
            brls::Logger::warning("net: обрыв записи {} Б в {}", data.size(), tmp);
            return;
        }
    }
    // rename поверх существующего файла на FAT не работает, поэтому старый
    // сначала убираем. Промежуток без файла здесь безвреден: это кэш, и
    // отсутствующая картинка просто скачается заново.
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        brls::Logger::warning("net: не удалось положить в кэш {}", path);
        std::remove(tmp.c_str());
        return;
    }

    if (bytesSinceSweep.fetch_add(static_cast<long long>(data.size())) + (long long)data.size()
        >= BYTES_BETWEEN_SWEEPS)
    {
        bytesSinceSweep = 0;
        enforceCacheLimit();
    }
}

}  // namespace

namespace net
{

void init()
{
#ifdef __SWITCH__
    // borealis поднимает сокеты сама в userAppInit, до входа в main. Наш вызов
    // приходит вторым и возвращает LibnxError_AlreadyInitialized — это не
    // отказ, а признак того, что сеть уже готова. Раньше мы принимали его за
    // ошибку и выключали загрузку картинок совсем.
    Result socketResult = socketInitializeDefault();
    const bool alreadyUp =
        R_MODULE(socketResult) == Module_Libnx && R_DESCRIPTION(socketResult) == LibnxError_AlreadyInitialized;

    if (alreadyUp)
    {
        socketResult = 0;
        weOwnSockets = false;
    }
    else
    {
        weOwnSockets = R_SUCCEEDED(socketResult);
    }

    if (R_FAILED(socketResult))
    {
        brls::Logger::warning("Сеть недоступна (socketInitializeDefault = 0x{:x}), "
                              "скриншоты грузиться не будут",
                              (unsigned)socketResult);
        {
            std::lock_guard<std::mutex> lock(readyMutex);
            initDone = true;
        }
        readyCv.notify_all();
        return;
    }
#endif

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        brls::Logger::warning("curl не инициализировался, скриншоты грузиться не будут");
        {
            std::lock_guard<std::mutex> lock(readyMutex);
            initDone = true;
        }
        readyCv.notify_all();
        return;
    }

    ::mkdir("sdmc:/switch", 0777);
    ::mkdir("sdmc:/switch/splitscreen-hub", 0777);
    ::mkdir(CACHE_DIR, 0777);

    ready = true;
    {
        std::lock_guard<std::mutex> lock(readyMutex);
        initDone = true;
    }
    readyCv.notify_all();
    brls::Logger::info("net: готова, кеш в {}", CACHE_DIR);
}

bool waitReady(int milliseconds)
{
    std::unique_lock<std::mutex> lock(readyMutex);
    // Условие включает и завершение работы: иначе io-поток, начавший ждать сеть
    // прямо перед выходом, держал бы tasks::stop() все пять секунд.
    readyCv.wait_for(lock, std::chrono::milliseconds(milliseconds),
                     [] { return initDone.load() || tasks::shuttingDown(); });
    return ready.load() && !tasks::shuttingDown();
}

void wakeWaiters()
{
    readyCv.notify_all();
}

void shutdown()
{
    // Сокеты закрываем и тогда, когда сеть так и не поднялась: curl мог не
    // инициализироваться уже после успешного socketInitializeDefault(), и
    // ранний выход по !ready оставлял бы наши сокеты открытыми.
    if (ready.exchange(false))
        curl_global_cleanup();

#ifdef __SWITCH__
    if (weOwnSockets.exchange(false))
        socketExit();
#endif
}

bool isReady()
{
    return ready;
}

std::string cachePath(const std::string& url, const char* suffix)
{
    char name[40];
    std::snprintf(name, sizeof(name), "%016zx%s", std::hash<std::string> {}(url), suffix);
    return std::string(CACHE_DIR) + "/" + name;
}

CacheStats cacheStats()
{
    CacheStats stats;
    stats.limitBytes = CACHE_LIMIT_BYTES;

    DIR* dir = ::opendir(CACHE_DIR);
    if (!dir)
        return stats;

    struct dirent* ent;
    while ((ent = ::readdir(dir)) != nullptr)
    {
        const std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;

        struct stat st {};
        if (::stat((std::string(CACHE_DIR) + "/" + name).c_str(), &st) != 0)
            continue;

        stats.files++;
        stats.bytes += st.st_size;
    }
    ::closedir(dir);
    return stats;
}

int clearCache()
{
    DIR* dir = ::opendir(CACHE_DIR);
    if (!dir)
        return 0;

    std::vector<std::string> victims;
    struct dirent* ent;
    while ((ent = ::readdir(dir)) != nullptr)
    {
        const std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        // .part пишет прямо сейчас открытый трейлер: удалять его — значит
        // молча лишить пользователя кэша уже идущей загрузки
        if (name.size() > 5 && name.substr(name.size() - 5) == ".part")
            continue;

        victims.push_back(std::string(CACHE_DIR) + "/" + name);
    }
    ::closedir(dir);

    // удаляем после обхода: правка каталога во время readdir даёт
    // неопределённое поведение
    int removed = 0;
    for (const std::string& path : victims)
        removed += std::remove(path.c_str()) == 0;

    bytesSinceSweep = 0;
    return removed;
}


/// Одна попытка. Код ответа отдаём наружу, чтобы вызывающий решил, имеет ли
/// смысл повторять.
std::vector<unsigned char> fetchOnce(const std::string& url, long& status, CURLcode& result)
{
    std::vector<unsigned char> data;
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result = CURLE_FAILED_INIT;
        return data;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "splitscreen-hub/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    // FOLLOWLOCATION без ограничений — это цепочка любой длины и переход на
    // любую схему, включая file://. Разрешаем только http(s) и пять шагов.
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

    // Обрываем закачку сразу при закрытии приложения, не досиживая таймаут:
    // выход занимал до минуты, если io-поток начал качать перед самым концом.
    // Заодно ограничиваем размер ответа — памяти в applet-режиме мало.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &data);
    curl_easy_setopt(
        curl, CURLOPT_XFERINFOFUNCTION,
        +[](void* userdata, curl_off_t, curl_off_t now, curl_off_t, curl_off_t) -> int {
            if (tasks::shuttingDown())
                return 1;
            auto* out = static_cast<std::vector<unsigned char>*>(userdata);
            return (now > MAX_RESPONSE_BYTES || out->size() > MAX_RESPONSE_BYTES) ? 1 : 0;
        });

    result = curl_easy_perform(curl);
    status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || status != 200)
        data.clear();
    return data;
}

/// 404 повторять бессмысленно, обрыв и временную ошибку сервера — стоит.
bool worthRetrying(long status, CURLcode result)
{
    if (result != CURLE_OK)
        return true;
    return status == 0 || status == 408 || status == 429 || status >= 500;
}

std::vector<unsigned char> fetch(const std::string& url)
{
    const std::string path = cachePath(url);

    std::vector<unsigned char> cached = readFile(path);
    if (!cached.empty())
    {
        return cached;
    }

    // сеть поднимается в фоне: даём ей время, вместо того чтобы молча сдаться
    if (!ready && !waitReady(5000))
    {
        brls::Logger::warning("net: сеть не поднялась за 5 с, пропускаем {}", url);
        return {};
    }

    long status     = 0;
    CURLcode result = CURLE_OK;

    for (int attempt = 0; attempt < FETCH_ATTEMPTS; attempt++)
    {
        if (tasks::shuttingDown())
            return {};

        // Нарастающая пауза: при обрыве Wi-Fi мгновенный повтор бесполезен.
        // Спим короткими отрезками, чтобы выход из приложения не ждал её конца.
        for (int slept = 0; attempt > 0 && slept < 400 * attempt; slept += 50)
        {
            if (tasks::shuttingDown())
                return {};
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        perf::Scope one("");
        std::vector<unsigned char> data = fetchOnce(url, status, result);
        perf::count(perf::Counter::NetFetches);
        perf::count(perf::Counter::NetMs, (long long)one.elapsedMs());

        if (!data.empty())
        {
            writeFile(path, data);
            return data;
        }

        brls::Logger::warning("net: попытка {} из {} не удалась (curl {} «{}», http {}): {}",
                              attempt + 1, FETCH_ATTEMPTS, static_cast<int>(result),
                              curl_easy_strerror(result), status, url);

        if (!worthRetrying(status, result))
            break;
    }

    brls::Logger::error("Не скачалось (curl {} «{}», http {}): {}", static_cast<int>(result),
                        curl_easy_strerror(result), status, url);
    return {};
}

}  // namespace net
