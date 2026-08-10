#include <borealis.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <exception>
#include <mutex>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "app_state.hpp"
#include "installed.hpp"
#include "net.hpp"
#include "tasks.hpp"
#include "ui/cache_tab.hpp"
#include "ui/catalog_tab.hpp"
#include "ui/gallery_activity.hpp"
#include "ui/library_tab.hpp"
#include "ui/main_tabs.hpp"
#include "ui/wrap_box.hpp"

namespace
{

// nacptool собирает romfs из содержимого build/resources, поэтому корень romfs —
// это сама папка resources: внутри лежат catalog.db, art/, xml/ без лишнего
// уровня. Путь с «resources/» внутри romfs не существует.
#ifdef __SWITCH__
const char* CATALOG_PATH = "romfs:/catalog.db";
const char* LIBRARY_PATH = "sdmc:/switch/splitscreen-hub/library.json";
#else
const char* CATALOG_PATH = "resources/catalog.db";
const char* LIBRARY_PATH = "library.json";
#endif

/// Журнал загрузки на SD-карте.
///
/// Приложение падало на консоли до появления первого кадра, и увидеть причину
/// было нечем: обычный лог borealis уходит в никуда, а необработанное
/// исключение превращается в abort, то есть в безымянную «произошла ошибка».
/// Поэтому пишем прямо в файл и сбрасываем после каждой строки — если
/// следующая не появилась, значит упали именно на этом шаге.
std::FILE* bootLog = nullptr;

/// Пишут и рабочие потоки тоже, а borealis блокирует только свой собственный
/// вывод — файл нужно защитить отдельно.
std::mutex logMutex;

std::chrono::steady_clock::time_point bootStarted;

void step(const char* what)
{
    if (!bootLog)
        return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - bootStarted)
                  .count();
    std::lock_guard<std::mutex> lock(logMutex);
    std::fprintf(bootLog, "[%7lld ms] %s\n", (long long)ms, what);
    std::fflush(bootLog);
}

/// Сколько памяти отдано процессу и сколько уже занято. В режиме апплета лимит
/// заметно ниже, чем при подмене игры, и разница объясняет отказы, которые
/// иначе выглядят необъяснимо.
void logMemory(const char* when)
{
#ifdef __SWITCH__
    u64 used = 0, total = 0;
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    if (bootLog)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        std::fprintf(bootLog, "           память %s: занято %llu МБ из %llu МБ, режим апплета %d\n",
                     when, (unsigned long long)(used / (1024 * 1024)),
                     (unsigned long long)(total / (1024 * 1024)), (int)appletGetAppletType());
        std::fflush(bootLog);
    }
#else
    (void)when;
#endif
}

void openBootLog()
{
#ifdef __SWITCH__
    ::mkdir("sdmc:/switch", 0777);
    ::mkdir("sdmc:/switch/splitscreen-hub", 0777);
    bootLog = std::fopen("sdmc:/switch/splitscreen-hub/boot.log", "w");
#else
    bootLog = std::fopen("boot.log", "w");
#endif
    bootStarted = std::chrono::steady_clock::now();
    step("start");
    logMemory("на старте");

    // Вместо setLogOutput() подписываемся на событие: borealis сбрасывает буфер
    // только под MinGW, а на консоли несброшенный хвост теряется ровно при
    // падении — то есть именно тогда, когда он и нужен. Здесь пишем сами и
    // сбрасываем каждую строку. Так в файл попадает всё, что приложение и сама
    // borealis отправляют в brls::Logger, из любого потока.
    brls::Logger::setThreadSafeLogging(true);

    // INFO, а не DEBUG: на отладочном уровне borealis пишет строку на каждую
    // созданную и уничтоженную ячейку списка, а мы — на каждую поставленную
    // задачу. При прокрутке это восемь строк на ряд, и каждая идёт на SD.
    // Отладочный уровень включается ключом -d.
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Logger::getLogEvent()->subscribe(
        [](brls::Logger::TimePoint now, brls::LogLevel level, std::string message)
        {
            if (!bootLog)
                return;

            static const char* names[] = { "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE" };
            const char* name           = names[(int)level <= 4 ? (int)level : 4];

            // Время от старта: настенные часы на консоли мало что говорят, а
            // разница между строками сразу показывает, что тормозит.
            static const auto started = std::chrono::steady_clock::now();
            auto ms                   = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - started)
                          .count();

            std::lock_guard<std::mutex> lock(logMutex);
            std::fprintf(bootLog, "[%7lld ms] %-7s %s\n", (long long)ms, name, message.c_str());

            // Сбрасываем на карту только то, что важно увидеть после падения.
            // Раньше сбрасывалась каждая строка, и при прокрутке сетки запись на
            // SD шла из UI-потока по нескольку раз на ряд — интерфейс от этого
            // заметно дёргался. Остальное дойдёт буфером или ближайшей ошибкой.
            if (level <= brls::LogLevel::LOG_WARNING)
                std::fflush(bootLog);
        });
}

class MainActivity : public brls::Activity
{
  public:
    // Без «xml/» в начале: createFromXMLResource подставляет его сам. У
    // inflateFromXMLRes, наоборот, префикс нужно писать руками — в borealis эти
    // два пути устроены по-разному.
    CONTENT_FROM_XML_RES("activity/main.xml");
};

}  // namespace

int main(int argc, char* argv[])
{
    openBootLog();

    // Любое исключение здесь иначе уходит в std::terminate и превращается в
    // системную ошибку без объяснений. Ловим, чтобы записать причину.
    try
    {
        for (int i = 1; i < argc; i++)
            if (std::strcmp(argv[i], "-d") == 0)
                brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

        // Интерфейс написан по-русски целиком, поэтому и подсказки borealis
        // должны быть русскими. С LOCALE_AUTO на английской консоли выходила
        // смесь: наши строки по-русски, «OK» и «Back» — по-английски.
        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_RU;

        step("borealis init");
        if (!brls::Application::init())
        {
            step("FAILED: borealis init");
            return EXIT_FAILURE;
        }

        // Нижняя полоса подсказок переполнялась: подсказки налезали на
        // индикаторы Wi-Fi, батареи и часы, и не читалось ни то, ни другое.
        // Размер шрифта самих подсказок задан в borealis числом и нам
        // недоступен (она подключена подмодулем), поэтому ужимаем отступы и
        // сокращаем число подсказок — см. hidden=true в экранах.
        //
        // Метрики надо задать до разбора разметки: значения @style/... в XML
        // подставляются один раз при инфляции.
        brls::Style style = brls::getStyle();
        style.addMetric("brls/hints/footer_margin_sides", 16.0f);
        style.addMetric("brls/hints/footer_padding_sides", 10.0f);
        style.addMetric("brls/applet_frame/footer_height", 64.0f);

        // Переходы между экранами по умолчанию длятся 200 мс и на глаз читаются
        // как рывок. Чуть длиннее — и смена экрана выглядит движением, а не
        // подменой кадра.
        style.addMetric("brls/animations/show", 300.0f);

        step("create window");
        brls::Application::createWindow("SplitScreen Hub");
        brls::Application::setGlobalQuit(true);

        step("threads");
        tasks::start();

        // Ранние выходы ниже и любое исключение иначе оставили бы рабочие потоки
        // работать дальше, пока процесс уже разрушается: они продолжают
        // выполнять освобождённый код, и вместо честного кода возврата консоль
        // получает падение самой Atmosphere. Повторный вызов stop() безвреден.
        struct WorkersGuard
        {
            ~WorkersGuard()
            {
                // Порядок важен: сначала поднимаем флаг остановки и будим тех,
                // кто ждёт сеть, и только потом джойним потоки — иначе stop()
                // простоял бы на этом ожидании свои пять секунд.
                tasks::requestStop();
                net::wakeWaiters();
                tasks::stop();
            }
        } workersGuard;

        step("open catalog");
        AppState& state = AppState::get();
        if (!state.catalog.open(CATALOG_PATH))
        {
            if (bootLog)
            {
                std::fprintf(bootLog, "FAILED: catalog (%s): %s\n", CATALOG_PATH,
                             state.catalog.lastError.c_str());

                // Отличаем «файла нет в romfs» от «файл есть, но sqlite его не
                // берёт»: без этого причина неотличима, а перебирать варианты
                // сборками по 77 МБ дорого.
                struct stat st {};
                if (::stat(CATALOG_PATH, &st) != 0)
                    std::fprintf(bootLog, "  stat: errno=%d %s\n", errno, std::strerror(errno));
                else
                    std::fprintf(bootLog, "  stat: size=%lld\n", (long long)st.st_size);

                std::FILE* probe = std::fopen(CATALOG_PATH, "rb");
                if (!probe)
                {
                    std::fprintf(bootLog, "  fopen: errno=%d %s\n", errno, std::strerror(errno));
                }
                else
                {
                    char head[16] = {};
                    size_t got    = std::fread(head, 1, sizeof(head), probe);
                    std::fclose(probe);
                    std::fprintf(bootLog, "  fopen ok, read %zu: %.15s\n", got, head);
                }

                // Что вообще смонтировано под romfs — вдруг корень пуст.
                if (DIR* dir = ::opendir("romfs:/"))
                {
                    while (struct dirent* e = ::readdir(dir))
                        std::fprintf(bootLog, "  romfs: %s\n", e->d_name);
                    ::closedir(dir);
                }
                else
                {
                    std::fprintf(bootLog, "  opendir romfs:/ errno=%d %s\n", errno,
                                 std::strerror(errno));
                }
                std::fflush(bootLog);
            }
            return EXIT_FAILURE;
        }

        step("library");
        state.library.load(LIBRARY_PATH);

        step("installed titles");
        state.installedTitleIds = installed::titleIds();

        // socketInitializeDefault() на Switch занимает заметное время, особенно
        // при выключенном Wi-Fi, а до первого открытия карточки сеть не нужна.
        step("net (background)");
        tasks::heavy([]() { net::init(); });

        step("register views");
        // WrapBox регистрируем первым: он встречается внутри разметки вкладок,
        // и к моменту их разбора тег уже должен быть известен.
        brls::Application::registerXMLView("WrapBox", WrapBox::create);
        brls::Application::registerXMLView("MainTabs", MainTabs::create);
        brls::Application::registerXMLView("CatalogTab", CatalogTab::create);
        brls::Application::registerXMLView("LibraryTab", LibraryTab::create);
        brls::Application::registerXMLView("CacheTab", CacheTab::create);

        step("main activity");
        brls::Application::pushActivity(new MainActivity());

        logMemory("перед главным циклом");

        step("main loop");
        while (brls::Application::mainLoop())
            ;

        step("shutdown");
        tasks::stop();
        net::shutdown();
        step("done");
    }
    catch (const std::exception& e)
    {
        if (bootLog)
        {
            std::fprintf(bootLog, "EXCEPTION: %s\n", e.what());
            std::fflush(bootLog);
        }
        return EXIT_FAILURE;
    }
    catch (...)
    {
        step("EXCEPTION: unknown");
        return EXIT_FAILURE;
    }

    if (bootLog)
    {
        // Подписка на события лога живёт дольше main и захватывает этот
        // указатель: любая строка из статических деструкторов borealis писала бы
        // в закрытый файл.
        std::FILE* toClose = bootLog;
        bootLog            = nullptr;
        std::fclose(toClose);
    }
    return EXIT_SUCCESS;
}
