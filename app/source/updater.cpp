#include "updater.hpp"

#include <borealis.hpp>
#include <borealis/extern/nlohmann/json.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

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

/// Скачанный .nro и его метка «сверено» рядом с приложением.
std::string newPath(const std::string& self) { return self + ".new"; }
std::string okPath(const std::string& self) { return self + ".new.ok"; }
std::string oldPath(const std::string& self) { return self + ".old"; }

bool exists(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

long long fileSize(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 ? static_cast<long long>(st.st_size) : -1;
}

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

void install(const Info& info, std::function<void(const Progress&)> onProgress,
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
        const std::string tmp = newPath(self);
        std::remove(okPath(self).c_str());  // прежняя метка не должна пережить новую закачку

        // Скорость — средняя с начала закачки, а не мгновенная: по Wi-Fi
        // консоли поток рваный, и мгновенная цифра прыгала бы в разы каждую
        // долю секунды, а ETA вместе с ней. Средняя даёт спокойные числа и
        // честную оценку остатка. В UI — не чаще четырёх раз в секунду.
        using clock                = std::chrono::steady_clock;
        const auto started         = clock::now();
        auto lastReport            = started;
        const bool downloaded      = net::downloadToFile(info.url, tmp, [&](long long got, long long total) {
            const auto now = clock::now();
            const long long denom = total > 0 ? total : info.size;
            const bool done = denom > 0 && got >= denom;
            if (!done && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count() < 250)
                return true;
            lastReport = now;

            Progress p;
            p.received = got;
            p.total    = denom;
            const double seconds = std::chrono::duration<double>(now - started).count();
            if (seconds > 0.5 && got > 0)
            {
                p.bytesPerSec = static_cast<double>(got) / seconds;
                if (denom > got)
                    p.etaSeconds = static_cast<int>(static_cast<double>(denom - got) / p.bytesPerSec + 0.5);
                else if (denom > 0)
                    p.etaSeconds = 0;
            }
            brls::sync([onProgress, p]() { onProgress(p); });
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

        // Метка «сверено»: без неё файл .new при старте считается обрывком и
        // удаляется. Внутри — сумма и размер, размер сверяется ещё раз перед
        // самой подменой.
        if (FILE* ok = std::fopen(okPath(self).c_str(), "w"))
        {
            std::fprintf(ok, "%s %lld\n", info.sha256.c_str(), fileSize(tmp));
            std::fclose(ok);
        }
        else
        {
            std::remove(tmp.c_str());
            return finish(false, "mark");
        }

        finish(true, info.version);
    });
}

bool hasPending()
{
    const std::string self = selfPath();
    return !self.empty() && exists(newPath(self)) && exists(okPath(self));
}

bool applyPending(std::string& error)
{
    const std::string self = selfPath();
    if (self.empty() || !hasPending())
    {
        error = "nothing";
        return false;
    }
    const std::string tmp = newPath(self), ok = okPath(self), old = oldPath(self);

    long long expected = -1;
    if (FILE* f = std::fopen(ok.c_str(), "r"))
    {
        char sha[80] = {};
        if (std::fscanf(f, "%79s %lld", sha, &expected) != 2)
            expected = -1;
        std::fclose(f);
    }
    if (expected <= 0 || fileSize(tmp) != expected)
    {
        // Метка есть, а файл не тот — недописан или подменён. Не рискуем.
        std::remove(tmp.c_str());
        std::remove(ok.c_str());
        error = "size";
        return false;
    }

#ifdef __SWITCH__
    // Именно это держит наш .nro открытым. Второй romfsExit из userAppExit
    // при выходе безвреден: размонтировать нечего, он просто вернёт ошибку.
    romfsExit();
#endif

    // FAT не переименовывает поверх существующего: старую сборку сначала
    // убираем с дороги под именем .old — она же и путь отката, если подмена
    // сорвётся на полпути; при удачном старте новой версии её удалит
    // cleanupLeftovers().
    std::remove(old.c_str());
    if (std::rename(self.c_str(), old.c_str()) != 0)
    {
        error = std::string("rename self: ") + std::strerror(errno);
        return false;
    }
    if (std::rename(tmp.c_str(), self.c_str()) != 0)
    {
        error = std::string("rename new: ") + std::strerror(errno);
        std::rename(old.c_str(), self.c_str());
        return false;
    }
    std::remove(ok.c_str());

#ifdef __SWITCH__
    // Сразу запустить новую версию: hbloader после выхода загрузит указанный
    // .nro. Без этого пользователю пришлось бы возвращаться в hbmenu.
    if (envHasNextLoad())
    {
        const std::string argv = "\"" + self + "\"";
        envSetNextLoad(self.c_str(), argv.c_str());
    }
#endif
    return true;
}

void cleanupLeftovers()
{
    const std::string self = selfPath();
    if (self.empty())
        return;
    std::remove(oldPath(self).c_str());
    if (exists(newPath(self)) && !exists(okPath(self)))
        std::remove(newPath(self).c_str());
}

}  // namespace updater
