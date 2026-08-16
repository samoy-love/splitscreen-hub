#include "updater.hpp"

#include <borealis.hpp>
#include <borealis/extern/nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __SWITCH__
#include <mbedtls/sha256.h>
#include <switch.h>
#endif

#include "format.hpp"
#include "net.hpp"
#include "tasks.hpp"

namespace
{

/// Публикует publish-file.sh из deploy-kit, см. .deploy-kit/nro.env.
const char* MANIFEST_URL = "https://samoy.love/splitscreen-hub/SplitScreenHub.nro.json";
const char* BASE_URL     = "https://samoy.love/splitscreen-hub/";

/// Куда кладём, если argv[0] не пришёл: стандартное место homebrew.
const char* DEFAULT_SELF = "sdmc:/switch/SplitScreenHub.nro";

std::atomic_bool installing { false };

/// sha256 файла шестнадцатеричной строкой; пусто, если считать нечем.
std::string fileSha256(const std::string& path)
{
#ifdef __SWITCH__
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return {};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    static unsigned char buf[64 * 1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        mbedtls_sha256_update_ret(&ctx, buf, n);
    std::fclose(f);
    unsigned char out[32];
    mbedtls_sha256_finish_ret(&ctx, out);
    mbedtls_sha256_free(&ctx);
    char hex[65];
    for (int i = 0; i < 32; i++)
        std::snprintf(hex + i * 2, 3, "%02x", out[i]);
    return std::string(hex, 64);
#else
    (void)path;
    return {};
#endif
}

}  // namespace

namespace updater
{

const char* currentVersion()
{
    return APP_VERSION;
}

std::string selfPath()
{
#ifdef __SWITCH__
    // hbloader передаёт argv одной строкой: путь к .nro первым словом,
    // возможно в кавычках.
    if (envHasArgv())
    {
        std::string argv = static_cast<const char*>(envGetArgv());
        std::string first;
        if (!argv.empty() && argv[0] == '"')
        {
            const auto end = argv.find('"', 1);
            first          = argv.substr(1, end == std::string::npos ? std::string::npos : end - 1);
        }
        else
            first = argv.substr(0, argv.find(' '));
        if (first.size() > 4 && first.compare(first.size() - 4, 4, ".nro") == 0)
            return first;
    }
    return DEFAULT_SELF;
#else
    return {};
#endif
}

void check(std::function<void(bool, const Info&, const std::string&)> onResult)
{
    tasks::io([onResult]() {
        Info info;
        bool available = false;
        std::string message;

        const std::vector<unsigned char> body = net::fetchFresh(MANIFEST_URL);
        if (body.empty())
            message = "offline";
        else
        {
            try
            {
                const auto j  = nlohmann::json::parse(body.begin(), body.end());
                info.version  = j.value("version", "");
                info.sha256   = j.value("sha256", "");
                info.size     = j.value("size", 0LL);
                info.url      = std::string(BASE_URL) + j.value("file", "SplitScreenHub.nro");
                available     = !info.version.empty()
                    && fmtx::compareVersions(info.version, APP_VERSION) > 0;
            }
            catch (const std::exception& e)
            {
                message = e.what();
                brls::Logger::error("updater: манифест не разобрался: {}", e.what());
            }
        }

        brls::sync([onResult, available, info, message]() { onResult(available, info, message); });
    });
}

void install(const Info& info, std::function<void(float)> onProgress,
             std::function<void(bool, const std::string&)> onDone)
{
    bool expected = false;
    if (!installing.compare_exchange_strong(expected, true))
        return;

    tasks::heavy([info, onProgress, onDone]() {
        auto finish = [onDone](bool ok, std::string message) {
            installing = false;
            brls::sync([onDone, ok, message]() { onDone(ok, message); });
        };

        const std::string self = selfPath();
        if (self.empty())
            return finish(false, "no self path");
        const std::string tmp = self + ".new";

        int lastPercent       = -1;
        const bool downloaded = net::downloadToFile(info.url, tmp, [&](long long got, long long total) {
            const long long denom = total > 0 ? total : info.size;
            const float part      = denom > 0 ? static_cast<float>(got) / static_cast<float>(denom) : 0.f;
            const int percent     = static_cast<int>(part * 100);
            if (percent != lastPercent)  // не заваливать UI-поток на каждом чанке
            {
                lastPercent = percent;
                brls::sync([onProgress, part]() { onProgress(part > 1.f ? 1.f : part); });
            }
            return true;
        });
        if (!downloaded)
            return finish(false, "download");

        // Сумма — единственное, что отличает целый файл от оборванного на
        // полпути или подменённого по дороге: TLS мы не проверяем (см. net).
        if (!info.sha256.empty())
        {
            const std::string got = fileSha256(tmp);
            if (!got.empty() && got != info.sha256)
            {
                std::remove(tmp.c_str());
                brls::Logger::error("updater: сумма не сошлась: {} вместо {}", got, info.sha256);
                return finish(false, "checksum");
            }
        }

        // FAT не умеет rename поверх существующего файла: сначала убрать
        // старый. Окно между remove и rename есть, но .new уже целый и лежит
        // рядом — в худшем случае его можно переименовать руками.
        std::remove(self.c_str());
        if (std::rename(tmp.c_str(), self.c_str()) != 0)
            return finish(false, "rename");

        finish(true, info.version);
    });
}

}  // namespace updater
