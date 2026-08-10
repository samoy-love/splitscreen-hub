#include "ui/cover_cache.hpp"

#include <list>
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

void clear()
{
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& [file, entry] : entries)
        nvgDeleteImage(vg, entry.texture);
    entries.clear();
    order.clear();
}

}  // namespace covers
