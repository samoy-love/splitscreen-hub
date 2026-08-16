#include "library.hpp"

#include "tasks.hpp"

#include <borealis.hpp>
#include <borealis/extern/nlohmann/json.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>

using json = nlohmann::json;


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

/// tasks::io — пул из нескольких потоков, и два быстрых изменения подряд
/// отправляют туда две записи, которые иначе писали бы один и тот же .tmp и
/// переименовывали его друг у друга из-под рук. Мьютекс их сериализует, а
/// номер снимка не даёт более раннему снимку лечь поверх позднего, если пул
/// выполнит задачи не в порядке постановки. Других мьютексов writeFile не
/// берёт, поэтому взаимной блокировки с Library::mutex быть не может.
std::mutex writeMutex;
std::atomic<uint64_t> snapshotCounter{0};
uint64_t lastWritten = 0;  // под writeMutex

}  // namespace

bool Library::load(const std::string& path)
{
    file = path;

    std::ifstream in(file);
    if (!in.good())
    {
        // основного файла нет — возможно, консоль выключили ровно между
        // переименованиями в writeFile(); тогда целая копия лежит рядом
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
        lang   = j.value("language", std::string());
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

void Library::saveLater() const
{
    // Счётчик версии растёт здесь: через эту точку проходит любое изменение.
    rev++;

    if (file.empty())
        return;

    // JSON собираем под мьютексом и в UI-потоке — это микросекунды на паре
    // сотен строк. На карту его пишет рабочий поток: ofstream и два rename по
    // SD в кадре — единственное, что у нас ещё оставалось от носителя в
    // отрисовке, при том что всё остальное давно вынесено в tasks::io.
    std::string payload;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex);

        json j;
        j["favorites"] = favs;
        j["hidden"]    = hidden;
        j["language"]  = lang;
        j["folders"]   = json::object();
        for (const auto& [name, items] : folders)
            j["folders"][name] = items;

        payload = j.dump(1);
        path    = file;
    }

    const uint64_t snapshot = ++snapshotCounter;
    tasks::io([path, payload, snapshot]() { writeFile(path, payload, snapshot); });
}

void Library::writeFile(const std::string& path, const std::string& json, uint64_t snapshot)
{
    std::lock_guard<std::mutex> lock(writeMutex);
    if (snapshot < lastWritten)
        return;
    lastWritten = snapshot;

    ensureParentDir(path);

    // пишем во временный файл и заменяем им основной: обрыв питания посреди
    // записи не должен уносить всю библиотеку
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.good())
        {
            brls::Logger::error("Не удалось записать библиотеку в {}", tmp);
            return;
        }
        out << json;
        if (!out.good())
        {
            brls::Logger::error("Библиотека записалась не полностью");
            return;
        }
    }

    // На FAT переименование поверх существующего файла не работает, поэтому
    // просто снести старый и переименовать нельзя: между двумя вызовами файла
    // не существует вовсе, и выдернутая в этот момент консоль унесла бы всю
    // библиотеку. Держим предыдущую версию под .bak до успешной замены.
    const std::string backup = path + ".bak";
    std::remove(backup.c_str());
    const bool hadFile = std::rename(path.c_str(), backup.c_str()) == 0;

    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        brls::Logger::error("Не удалось заменить файл библиотеки");
        if (hadFile)
            std::rename(backup.c_str(), path.c_str());  // возвращаем как было
        return;
    }

    std::remove(backup.c_str());
}

bool Library::isFavorite(const std::string& nsuid) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return std::find(favs.begin(), favs.end(), nsuid) != favs.end();
}

void Library::toggleFavorite(const std::string& nsuid)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find(favs.begin(), favs.end(), nsuid);
        if (it == favs.end())
            favs.push_back(nsuid);
        else
            favs.erase(it);
    }
    saveLater();
}

void Library::setLanguage(const std::string& code)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (lang == code)
            return;
        lang = code;
    }
    saveLater();
}

bool Library::isHidden(const std::string& nsuid) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return std::find(hidden.begin(), hidden.end(), nsuid) != hidden.end();
}

void Library::toggleHidden(const std::string& nsuid)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find(hidden.begin(), hidden.end(), nsuid);
        if (it == hidden.end())
            hidden.push_back(nsuid);
        else
            hidden.erase(it);
    }
    saveLater();
}

std::vector<std::string> Library::favorites() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return favs;
}

std::vector<std::string> Library::hiddenGames() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return hidden;
}

std::vector<std::string> Library::folderNames() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> names;
    names.reserve(folders.size());
    for (const auto& [name, _] : folders)
        names.push_back(name);
    return names;
}

std::vector<std::string> Library::folder(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = folders.find(name);
    return it == folders.end() ? std::vector<std::string>() : it->second;
}

bool Library::inFolder(const std::string& name, const std::string& nsuid) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = folders.find(name);
    if (it == folders.end())
        return false;
    return std::find(it->second.begin(), it->second.end(), nsuid) != it->second.end();
}

void Library::createFolder(const std::string& name)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (name.empty() || folders.count(name))
            return;
        folders[name];
    }
    saveLater();
}

bool Library::renameFolder(const std::string& from, const std::string& to)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Занятое имя — отказ, и знать об этом должен вызывающий: экран
        // библиотеки иначе переключался на несуществующую папку и показывал
        // пустой раздел, а фокус оставался на удалённой строке.
        if (to.empty() || from == to || !folders.count(from) || folders.count(to))
            return false;
        folders[to] = folders[from];
        folders.erase(from);
    }
    saveLater();
    return true;
}

void Library::removeFolder(const std::string& name)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!folders.erase(name))
            return;
    }
    saveLater();
}

void Library::toggleInFolder(const std::string& name, const std::string& nsuid)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = folders.find(name);
        if (it == folders.end())
            return;
        auto& items = it->second;
        auto pos    = std::find(items.begin(), items.end(), nsuid);
        if (pos == items.end())
            items.push_back(nsuid);
        else
            items.erase(pos);
    }
    saveLater();
}
