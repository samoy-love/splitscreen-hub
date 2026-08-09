#include "net.hpp"

#include <borealis.hpp>
#include <sys/stat.h>
#include <dirent.h>

#include <algorithm>
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

namespace
{

#ifdef __SWITCH__
const char* CACHE_DIR = "sdmc:/switch/couch-coop/cache";
#else
const char* CACHE_DIR = "cache";
#endif

/// Кэш не должен разрастаться бесконечно: скриншотов 20 тысяч, а роликов
/// 2621 штука по несколько мегабайт — без лимита SD-карта рано или поздно
/// закончится. Держим суммарный размер каталога кэша под этим порогом,
/// выкидывая самые старые файлы (по mtime) при превышении.
constexpr long long CACHE_LIMIT_BYTES = 700LL * 1024 * 1024;

bool ready = false;
bool initDone = false;   // init() отработал — успешно или нет
std::mutex readyMutex;
std::condition_variable readyCv;

/// Полный обход кэша — это opendir плюс stat на каждый файл, а после часа
/// пролистывания карточек файлов там тысячи. Делать его на каждой записи
/// расточительно, поэтому считаем записанные байты и проверяем, только когда
/// с прошлой проверки набежало заметно.
/// Три попытки на картинку: этого хватает на короткий провал связи и
/// не растягивает ожидание, если сети нет совсем.
constexpr int FETCH_ATTEMPTS = 3;

constexpr long long BYTES_BETWEEN_SWEEPS = 32LL * 1024 * 1024;
long long bytesSinceSweep = 0;

void enforceCacheLimit()
{
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

    for (const auto& e : entries)
    {
        if (total <= CACHE_LIMIT_BYTES)
            break;
        if (std::remove(e.path.c_str()) == 0)
            total -= e.size;
    }
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
            return;
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out.good())
            return;
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());

    bytesSinceSweep += static_cast<long long>(data.size());
    if (bytesSinceSweep >= BYTES_BETWEEN_SWEEPS)
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
    if (R_FAILED(socketInitializeDefault()))
    {
        brls::Logger::warning("Сеть недоступна, скриншоты грузиться не будут");
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
    ::mkdir("sdmc:/switch/couch-coop", 0777);
    ::mkdir(CACHE_DIR, 0777);

    ready = true;
    {
        std::lock_guard<std::mutex> lock(readyMutex);
        initDone = true;
    }
    readyCv.notify_all();
}

bool waitReady(int milliseconds)
{
    std::unique_lock<std::mutex> lock(readyMutex);
    readyCv.wait_for(lock, std::chrono::milliseconds(milliseconds),
                     [] { return initDone; });
    return ready;
}

void shutdown()
{
    if (!ready)
        return;
    curl_global_cleanup();
#ifdef __SWITCH__
    socketExit();
#endif
    ready = false;
}

bool isReady()
{
    return ready;
}

std::string cachePath(const std::string& url)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016zx.img", std::hash<std::string> {}(url));
    return std::string(CACHE_DIR) + "/" + name;
}

void trimCache()
{
    bytesSinceSweep = 0;
    enforceCacheLimit();
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "couch-coop/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

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
        return cached;

    // сеть поднимается в фоне: даём ей время, вместо того чтобы молча сдаться
    if (!ready && !waitReady(5000))
        return {};

    long status     = 0;
    CURLcode result = CURLE_OK;

    for (int attempt = 0; attempt < FETCH_ATTEMPTS; attempt++)
    {
        // нарастающая пауза: при обрыве Wi-Fi мгновенный повтор бесполезен
        if (attempt > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(400 * attempt));

        std::vector<unsigned char> data = fetchOnce(url, status, result);
        if (!data.empty())
        {
            writeFile(path, data);
            return data;
        }

        if (!worthRetrying(status, result))
            break;
    }

    brls::Logger::debug("Не скачалось (curl {}, http {}): {}",
                        static_cast<int>(result), status, url);
    return {};
}

}  // namespace net
