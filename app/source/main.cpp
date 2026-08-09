#include <borealis.hpp>

#include <cstdlib>
#include <cstring>

#include "app_state.hpp"
#include "installed.hpp"
#include "net.hpp"
#include "tasks.hpp"
#include "ui/catalog_tab.hpp"
#include "ui/library_tab.hpp"

namespace
{

#ifdef __SWITCH__
const char* CATALOG_PATH = "romfs:/resources/catalog.db";
const char* LIBRARY_PATH = "sdmc:/switch/couch-coop/library.json";
#else
const char* CATALOG_PATH = "resources/catalog.db";
const char* LIBRARY_PATH = "library.json";
#endif

class MainActivity : public brls::Activity
{
  public:
    CONTENT_FROM_XML_RES("xml/activity/main.xml");
};

}  // namespace

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "-d") == 0)
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    if (!brls::Application::init())
    {
        brls::Logger::error("Не удалось инициализировать borealis");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("Couch co-op");
    brls::Application::setGlobalQuit(true);

    tasks::start();

    AppState& state = AppState::get();

    if (!state.catalog.open(CATALOG_PATH))
    {
        brls::Logger::error("Каталог не открылся, показывать нечего");
        return EXIT_FAILURE;
    }

    state.library.load(LIBRARY_PATH);
    state.installedTitleIds = installed::titleIds();

    // socketInitializeDefault() на Switch занимает заметное время, особенно
    // при выключенном Wi-Fi, а до первого открытия карточки сеть не нужна.
    // Инициализируем в фоне: net::isReady() до готовности вернёт false, и
    // скриншоты с трейлером просто покажут понятную подпись.
    tasks::heavy([]() { net::init(); });

    brls::Application::registerXMLView("CatalogTab", CatalogTab::create);
    brls::Application::registerXMLView("LibraryTab", LibraryTab::create);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop())
        ;

    tasks::stop();
    net::shutdown();
    return EXIT_SUCCESS;
}
