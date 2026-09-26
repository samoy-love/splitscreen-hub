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

/// Прежняя сборка на время подмены. Имя кончается на .nro нарочно: hbmenu
/// показывает только такие файлы, и если питание пропадёт между двумя
/// переименованиями в applyPending(), приложение останется в меню хотя бы
/// под этим именем — а запущенное оттуда, само вернёт себе основное (см.
/// recoverInterruptedSwap).
const char* BACKUP_SUFFIX = ".old.nro";

bool endsWith(const std::string& s, const std::string& tail)
{
    return s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string oldPath(const std::string& self)
{
    return endsWith(self, ".nro") ? self.substr(0, self.size() - 4) + BACKUP_SUFFIX
                                  : self + ".old";
}

/// Так резервную копию называли прежние сборки: hbmenu её не видел.
std::string legacyOldPath(const std::string& self) { return self + ".old"; }

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

/// sha256 файла шестнадцатеричной строкой в нижнем регистре; пусто, если
/// считать нечем или файл не прочитался до конца.
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
    // Ошибка чтения посреди файла дала бы сумму его начала — это не «не
    // сошлось», а «не посчитали», и ответ должен быть пустым.
    const bool readFailed = std::ferror(f) != 0;
    std::fclose(f);
    if (readFailed)
    {
        mbedtls_sha256_free(&ctx);
        return {};
    }
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

/// Сумма из манифеста в том виде, в каком её считает fileSha256(): ровно 64
/// шестнадцатеричных знака в нижнем регистре. Пусто, если это не sha256 —
/// регистр в манифесте нам не указ, а вот обрезанная или чужая строка должна
/// остановить обновление, а не пройти сравнение по случайности.
std::string normalizeSha256(const std::string& value)
{
    if (value.size() != 64)
        return {};
    std::string out(value);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'F')
            c = static_cast<char>(c - 'A' + 'a');
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return {};
    }
    return out;
}

/// Метка «сверено»: одна строка «sha256=<64 знака> size=<байт>». Ключи в
/// самой строке — чтобы метку, записанную прежними сборками (там сумма могла
/// оказаться пустой, и строка начиналась с пробела), нельзя было прочитать
/// как годную: такая не разбирается, и файл при подмене считается чужим.
bool writeMark(const std::string& path, const std::string& sha, long long size)
{
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f)
        return false;
    const bool written = std::fprintf(f, "sha256=%s size=%lld\n", sha.c_str(), size) > 0;
    return std::fclose(f) == 0 && written;
}

bool readMark(const std::string& path, std::string& sha, long long& size)
{
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f)
        return false;
    char line[160] = {};
    const bool got = std::fgets(line, sizeof line, f) != nullptr;
    std::fclose(f);
    if (!got)
        return false;

    char hex[65] = {};
    long long n  = -1;
    int consumed = 0;
    if (std::sscanf(line, "sha256=%64[0-9a-f] size=%lld%n", hex, &n, &consumed) != 2)
        return false;
    // После размера — только перевод строки: хвост означает, что метка чужая
    // или испорчена.
    for (const char* rest = line + consumed; *rest; rest++)
        if (*rest != '\n' && *rest != '\r')
            return false;

    sha  = normalizeSha256(hex);
    size = n;
    return !sha.empty() && size > 0;
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
        // Поэтому без неё не ставим ничего: ни когда её нет в манифесте, ни
        // когда её не удалось посчитать. Прежде в обоих случаях проверка
        // молча пропускалась, и непроверенный файл уходил в подмену.
        auto reject = [&tmp, &finish](const std::string& code) {
            std::remove(tmp.c_str());
            finish(false, code);
        };

        const std::string expected = normalizeSha256(info.sha256);
        if (expected.empty())
        {
            brls::Logger::error("updater: в манифесте нет годной суммы: «{}»", info.sha256);
            return reject("no checksum");
        }

        // Размер из манифеста сверяем отдельно и раньше суммы: оборванная
        // закачка видна сразу, без чтения всего файла.
        const long long got = fileSize(tmp);
        if (info.size > 0 && got != info.size)
        {
            brls::Logger::error("updater: размер не сошёлся: {} вместо {}", got, info.size);
            return reject("checksum");
        }

        const std::string actual = fileSha256(tmp);
        if (actual.empty())
        {
            brls::Logger::error("updater: сумму {} посчитать не удалось", tmp);
            return reject("hash");
        }
        if (actual != expected)
        {
            brls::Logger::error("updater: сумма не сошлась: {} вместо {}", actual, expected);
            return reject("checksum");
        }

        // Метка «сверено»: без неё файл .new при старте считается обрывком и
        // удаляется. Внутри — сумма и размер, размер сверяется ещё раз перед
        // самой подменой.
        if (!writeMark(okPath(self), actual, got))
        {
            std::remove(okPath(self).c_str());
            return reject("mark");
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

    std::string sha;
    long long expected = -1;
    if (!readMark(ok, sha, expected) || fileSize(tmp) != expected)
    {
        // Метка есть, а файл не тот — недописан или подменён; или метка не
        // читается, то есть сверка не доказана. Не рискуем.
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

    // Подмена сорвалась, и файл снова на своём месте: возвращаем romfs. При
    // старте без него приложение не прочло бы каталог и закрылось бы с
    // ошибкой вместо того, чтобы работать прежней версией.
    auto remount = []() {
#ifdef __SWITCH__
        romfsInit();
#endif
    };

    // FAT не переименовывает поверх существующего: старую сборку сначала
    // убираем с дороги под именем .old.nro — она же и путь отката, если
    // подмена сорвётся на полпути; при удачном старте новой версии её удалит
    // cleanupLeftovers(). Между двумя rename основного файла нет вовсе, и
    // выключение консоли в этот момент оставило бы в hbmenu только копию —
    // поэтому у неё имя, которое hbmenu показывает.
    std::remove(old.c_str());
    if (std::rename(self.c_str(), old.c_str()) != 0)
    {
        error = std::string("rename self: ") + std::strerror(errno);
        remount();
        return false;
    }
    if (std::rename(tmp.c_str(), self.c_str()) != 0)
    {
        error = std::string("rename new: ") + std::strerror(errno);
        if (std::rename(old.c_str(), self.c_str()) == 0)
            remount();
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

bool recoverInterruptedSwap()
{
#ifdef __SWITCH__
    // Запущены не из резервной копии — восстанавливать нечего.
    const std::string self = selfPath();
    if (!endsWith(self, BACKUP_SUFFIX))
        return false;

    // Основной файл на месте: копию запустили руками, например потому, что
    // новая версия не стартует. Это законный откат — просто работаем.
    const std::string main = self.substr(0, self.size() - std::strlen(BACKUP_SUFFIX)) + ".nro";
    if (exists(main))
        return false;

    // Подмену прервали между двумя rename: основного файла нет, а мы — его
    // прежняя сборка. Возвращаем себе основное имя и перезапускаемся уже
    // оттуда; скачанное обновление с меткой лежит рядом с основным именем, и
    // перезапущенная сборка сама доведёт подмену до конца. Свой .nro держит
    // открытым romfs, поэтому сначала отпускаем его.
    romfsExit();
    if (std::rename(self.c_str(), main.c_str()) != 0)
    {
        brls::Logger::error("updater: не удалось вернуть {} на место {}: {}", self, main,
                            std::strerror(errno));
        romfsInit();
        return false;
    }

    // Без envSetNextLoad перезапуска не будет, но и продолжать нельзя: файла,
    // из которого смонтирован romfs, под прежним именем уже нет. Человек
    // запустит приложение из hbmenu — теперь под обычным именем.
    if (envHasNextLoad())
    {
        const std::string argv = "\"" + main + "\"";
        envSetNextLoad(main.c_str(), argv.c_str());
    }
    return true;
#else
    return false;
#endif
}

void cleanupLeftovers()
{
    const std::string self = selfPath();
    if (self.empty())
        return;
    // Копию, из которой нас запустили, не трогаем: удалить работающий файл
    // консоль не даст, а если это откат, копия ещё пригодится.
    if (!endsWith(self, BACKUP_SUFFIX))
        std::remove(oldPath(self).c_str());
    std::remove(legacyOldPath(self).c_str());
    if (exists(newPath(self)) && !exists(okPath(self)))
        std::remove(newPath(self).c_str());
}

}  // namespace updater
