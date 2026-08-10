#include "ui/main_tabs.hpp"

#include "ui/cache_tab.hpp"
#include "ui/fonts.hpp"
#include "ui/catalog_tab.hpp"
#include "ui/library_tab.hpp"

MainTabs::MainTabs()
{
    this->inflateFromXMLRes("xml/views/main_tabs.xml");

    addTab("Каталог", new CatalogTab());
    addTab("Библиотека", new LibraryTab());
    addTab("Кэш", new CacheTab());

    select(0);
    built = true;

    // Плечевые кнопки листают вкладки, не уводя фокус наверх: с геймпада это
    // привычнее, чем возвращаться к строке вкладок каждый раз.
    //
    // hidden=true: обе подсказки съедали почти 400 точек нижней полосы, из-за
    // чего остальные наезжали на часы и индикаторы. Что вкладки переключаются
    // плечевыми, написано прямо в строке вкладок — там это и уместнее.
    this->registerAction("Вкладка влево", brls::BUTTON_LB, [this](brls::View*) {
        select((current + pages.size() - 1) % pages.size());
        return true;
    }, true);
    this->registerAction("Вкладка вправо", brls::BUTTON_RB, [this](brls::View*) {
        select((current + 1) % pages.size());
        return true;
    }, true);
}

void MainTabs::updateShortcuts()
{
    //  /  — L и R,  /  — ZL и ZR из системного шрифта.
    std::string text = " вкладки";
    if (current == 0)
        text += "    страница";
    shortcuts->setText(text);
}

void MainTabs::addTab(const std::string& label, brls::View* content)
{
    const size_t index = pages.size();

    auto* button = new brls::Button();
    button->setText(label);
    button->setFontSize(fonts::BODY);
    button->setMarginRight(8);
    button->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    button->registerClickAction([this, index](brls::View*) {
        select(index);
        return true;
    });
    tabsBox->addView(button);
    buttons.push_back(button);

    content->setVisibility(brls::Visibility::GONE);
    content->setGrow(1.0f);
    contentBox->addView(content);
    pages.push_back(content);
}

void MainTabs::select(size_t index)
{
    if (index >= pages.size())
        return;

    const bool switching = built && index != current;

    current = index;
    for (size_t i = 0; i < pages.size(); i++)
    {
        // GONE, а не INVISIBLE: невидимая вкладка всё равно занимала бы место в
        // раскладке и тянула бы содержимое вниз.
        pages[i]->setVisibility(i == index ? brls::Visibility::VISIBLE
                                           : brls::Visibility::GONE);
        buttons[i]->setStyle(i == index ? &brls::BUTTONSTYLE_PRIMARY
                                        : &brls::BUTTONSTYLE_BORDERLESS);
    }

    updateShortcuts();

    // Фокус мог остаться на плитке спрятанной вкладки — тогда нажатия уходили
    // бы в невидимое. Переводим его на открытую страницу.
    if (switching)
        brls::Application::giveFocus(pages[index]);
}

brls::View* MainTabs::create()
{
    return new MainTabs();
}
