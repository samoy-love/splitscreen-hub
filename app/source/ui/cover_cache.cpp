#include "ui/cover_cache.hpp"

#include <memory>

#include "tasks.hpp"
#include "ui/async_image.hpp"

#include <fstream>
#include <list>
#include <unordered_set>
#include <unordered_map>

namespace
{

/// Список в порядке использования: спереди — то, что трогали последним.
std::list<std::string> order;

struct Entry
{
    int texture;
    std::list<std::string>::iterator position;
};

std::unordered_map<std::string, Entry> entries;

/// Уже поставленные в очередь на предзагрузку: без этого каждая прокрутка
/// заказывала бы одни и те же файлы заново.
std::unordered_set<std::string> queued;

/// Показываемые сейчас обложки и число плиток на каждую. Вытеснению не
/// подлежат.
std::unordered_map<std::string, int> pinned;

#ifdef __SWITCH__
const char* ART_DIR = "romfs:/art/";
#else
const char* ART_DIR = "resources/art/";
#endif

}  // namespace

namespace covers
{

int find(const std::string& file)
{
    auto it = entries.find(file);
    if (it == entries.end())
        return 0;

    // Освежаем: вытеснять будем то, что дольше всего не показывали.
    order.splice(order.begin(), order, it->second.position);
    it->second.position = order.begin();
    return it->second.texture;
}

int put(const std::string& file, int texture)
{
    if (texture == 0)
        return 0;

    auto it = entries.find(file);
    if (it != entries.end())
    {
        // Успели загрузить дважды — пока первая загрузка шла, ту же обложку
        // заказали снова. Лишнюю текстуру освобождаем сразу, иначе она
        // повиснет, а наружу отдаём ту, что осталась в кэше.
        nvgDeleteImage(brls::Application::getNVGContext(), texture);
        order.splice(order.begin(), order, it->second.position);
        it->second.position = order.begin();
        return it->second.texture;
    }

    order.push_front(file);
    entries[file] = { texture, order.begin() };

    // Вытесняем с хвоста, пропуская показываемые. Плиток на экране пара
    // десятков, так что до предела очередь дойти не успевает; но если вдруг
    // всё занято — просто перестаём вытеснять, лишняя текстура дешевле
    // серого прямоугольника вместо обложки.
    auto cur = order.end();
    while (static_cast<int>(entries.size()) > LIMIT && cur != order.begin())
    {
        --cur;
        if (pinned.count(*cur))
            continue;  // защищена: идём дальше к началу очереди

        auto victim = entries.find(*cur);
        if (victim != entries.end())
        {
            nvgDeleteImage(brls::Application::getNVGContext(), victim->second.texture);
            entries.erase(victim);
        }
        // erase возвращает следующий элемент; --cur на следующем витке встанет
        // на тот, что был перед удалённым.
        cur = order.erase(cur);
    }

    return texture;
}

void pin(const std::string& file)
{
    if (!file.empty())
        pinned[file]++;
}

void unpin(const std::string& file)
{
    auto it = pinned.find(file);
    if (it == pinned.end())
        return;
    if (--it->second <= 0)
        pinned.erase(it);
}

void warm(const std::string& file)
{
    if (file.empty() || entries.count(file) || queued.count(file))
        return;

    queued.insert(file);
    const std::string path = std::string(ART_DIR) + file;

    tasks::io([file, path]() {
        // Отметку о заказе снимаем при любом исходе. Раньше ранние выходы —
        // файл не открылся, JPEG не разобрался — оставляли её навсегда, и
        // предзагрузка этой обложки за весь сеанс больше не повторялась.
        auto release = [file]() { brls::sync([file]() { queued.erase(file); }); };

        std::ifstream in(path, std::ios::binary);
        if (!in.good())
        {
            release();
            return;
        }

        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        auto pixels = std::make_shared<asyncimage::Pixels>(
            asyncimage::decode(data.data(), data.size()));
        if (!pixels->valid())
        {
            release();
            return;
        }

        brls::sync([file, pixels]() {
            queued.erase(file);
            put(file, asyncimage::upload(*pixels));
        });
    });
}

void clear()
{
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& [file, entry] : entries)
        nvgDeleteImage(vg, entry.texture);
    entries.clear();
    order.clear();
    queued.clear();
    pinned.clear();
}

}  // namespace covers
