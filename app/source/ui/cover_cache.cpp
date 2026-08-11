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

void put(const std::string& file, int texture)
{
    if (texture == 0)
        return;

    auto it = entries.find(file);
    if (it != entries.end())
    {
        // Успели загрузить дважды — пока первая загрузка шла, плитку показали
        // снова. Лишнюю текстуру освобождаем сразу, иначе она повиснет.
        nvgDeleteImage(brls::Application::getNVGContext(), texture);
        order.splice(order.begin(), order, it->second.position);
        it->second.position = order.begin();
        return;
    }

    order.push_front(file);
    entries[file] = { texture, order.begin() };

    while (static_cast<int>(entries.size()) > LIMIT)
    {
        const std::string& oldest = order.back();
        auto victim               = entries.find(oldest);
        if (victim != entries.end())
        {
            nvgDeleteImage(brls::Application::getNVGContext(), victim->second.texture);
            entries.erase(victim);
        }
        order.pop_back();
    }
}

void warm(const std::string& file)
{
    if (file.empty() || entries.count(file) || queued.count(file))
        return;

    queued.insert(file);
    const std::string path = std::string(ART_DIR) + file;

    tasks::io([file, path]() {
        std::ifstream in(path, std::ios::binary);
        if (!in.good())
            return;

        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        auto pixels = std::make_shared<asyncimage::Pixels>(
            asyncimage::decode(data.data(), data.size()));
        if (!pixels->valid())
            return;

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
}

}  // namespace covers
