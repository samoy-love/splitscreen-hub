#include "library.hpp"

#include <borealis.hpp>
#include <borealis/extern/nlohmann/json.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

const std::vector<std::string> Library::empty;

namespace
{

/// Создаёт все каталоги пути, кроме последнего элемента.
/// При первом запуске sdmc:/switch/splitscreen-hub/ ещё не существует.
void ensureParentDir(const std::string& path)
{
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos)
    {
        std::string dir = path.substr(0, pos);
        if (dir.empty() || dir.back() == ':')
            continue;
        ::mkdir(dir.c_str(), 0777);  // уже существует — вернёт EEXIST, это нормально
    }
}

}  // namespace

bool Library::load(const std::string& path)
{
    file = path;

    std::ifstream in(file);
    if (!in.good())
    {
        // основного файла нет — возможно, консоль выключили ровно между
        // переименованиями в save(); тогда целая копия лежит рядом
        const std::string backup = file + ".bak";
        std::ifstream fallback(backup);
        if (fallback.good())
        {
            brls::Logger::warning("Библиотека восстановлена из {}", backup);
            std::rename(backup.c_str(), file.c_str());
            in.close();
            in.open(file);
        }
    }
    if (!in.good())
        return true;  // библиотеки ещё нет — это нормально при первом запуске

    try
    {
        json j;
        in >> j;
        favs   = j.value("favorites", std::vector<std::string>{});
        hidden = j.value("hidden", std::vector<std::string>{});
        lang   = j.value("language", std::string("en"));
        for (auto& [name, items] : j.value("folders", json::object()).items())
            folders[name] = items.get<std::vector<std::string>>();
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Библиотека повреждена, начинаем с пустой: {}", e.what());
        favs.clear();
        folders.clear();
        hidden.clear();
        return false;
    }

    brls::Logger::info("библиотека: загружена из {} — избранного {}, папок {}, скрыто {}", file,
                       favs.size(), folders.size(), hidden.size());
    return true;
}

bool Library::save() const
{
    if (file.empty())
        return false;

    json j;
    j["favorites"] = favs;
    j["hidden"]    = hidden;
    j["language"]  = lang;
    j["folders"]   = json::object();
    for (const auto& [name, items] : folders)
        j["folders"][name] = items;

    ensureParentDir(file);

    // пишем во временный файл и заменяем им основной: обрыв питания посреди
    // записи не должен уносить всю библиотеку
    const std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.good())
        {
            brls::Logger::error("Не удалось записать библиотеку в {}", tmp);
            return false;
        }
        out << j.dump(1);
        if (!out.good())
            return false;
    }

    // На FAT переименование поверх существующего файла не работает, поэтому
    // просто снести старый и переименовать нельзя: между двумя вызовами файла
    // не существует вовсе, и выдернутая в этот момент консоль унесла бы всю
    // библиотеку. Держим предыдущую версию под .bak до успешной замены.
    const std::string backup = file + ".bak";
    std::remove(backup.c_str());
    const bool hadFile = std::rename(file.c_str(), backup.c_str()) == 0;

    if (std::rename(tmp.c_str(), file.c_str()) != 0)
    {
        brls::Logger::error("Не удалось заменить файл библиотеки");
        if (hadFile)
            std::rename(backup.c_str(), file.c_str());  // возвращаем как было
        return false;
    }

    std::remove(backup.c_str());
    return true;
}

bool Library::isFavorite(const std::string& nsuid) const
{
    return std::find(favs.begin(), favs.end(), nsuid) != favs.end();
}

void Library::toggleFavorite(const std::string& nsuid)
{
    auto it = std::find(favs.begin(), favs.end(), nsuid);
    if (it == favs.end())
        favs.push_back(nsuid);
    else
        favs.erase(it);
    save();
}

void Library::setLanguage(const std::string& code)
{
    if (lang == code)
        return;
    lang = code;
    save();
}

bool Library::isHidden(const std::string& nsuid) const
{
    return std::find(hidden.begin(), hidden.end(), nsuid) != hidden.end();
}

void Library::toggleHidden(const std::string& nsuid)
{
    auto it = std::find(hidden.begin(), hidden.end(), nsuid);
    if (it == hidden.end())
        hidden.push_back(nsuid);
    else
        hidden.erase(it);
    save();
}

std::vector<std::string> Library::folderNames() const
{
    std::vector<std::string> names;
    names.reserve(folders.size());
    for (const auto& [name, _] : folders)
        names.push_back(name);
    return names;
}

const std::vector<std::string>& Library::folder(const std::string& name) const
{
    auto it = folders.find(name);
    return it == folders.end() ? empty : it->second;
}

bool Library::inFolder(const std::string& name, const std::string& nsuid) const
{
    const auto& items = folder(name);
    return std::find(items.begin(), items.end(), nsuid) != items.end();
}

void Library::createFolder(const std::string& name)
{
    if (name.empty() || folders.count(name))
        return;
    folders[name];
    save();
}

void Library::renameFolder(const std::string& from, const std::string& to)
{
    if (to.empty() || from == to || !folders.count(from) || folders.count(to))
        return;
    folders[to] = folders[from];
    folders.erase(from);
    save();
}

void Library::removeFolder(const std::string& name)
{
    if (folders.erase(name))
        save();
}

void Library::toggleInFolder(const std::string& name, const std::string& nsuid)
{
    auto it = folders.find(name);
    if (it == folders.end())
        return;
    auto& items = it->second;
    auto pos    = std::find(items.begin(), items.end(), nsuid);
    if (pos == items.end())
        items.push_back(nsuid);
    else
        items.erase(pos);
    save();
}

size_t Library::size() const
{
    size_t n = favs.size();
    for (const auto& [_, items] : folders)
        n += items.size();
    return n;
}
