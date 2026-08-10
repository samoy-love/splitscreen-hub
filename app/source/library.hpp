#pragma once

#include <map>
#include <string>
#include <vector>

/// Личная библиотека: избранное и папки. Живёт на SD-карте и переживает
/// перезапуск приложения.
///
/// Файл пишется через временный и os-replace, иначе выдернутая посреди записи
/// консоль оставит обрезанный JSON и библиотека потеряется целиком.
class Library
{
  public:
    bool load(const std::string& path);
    bool save() const;

    bool isFavorite(const std::string& nsuid) const;
    void toggleFavorite(const std::string& nsuid);
    const std::vector<std::string>& favorites() const { return favs; }

    std::vector<std::string> folderNames() const;
    const std::vector<std::string>& folder(const std::string& name) const;
    bool inFolder(const std::string& name, const std::string& nsuid) const;

    void createFolder(const std::string& name);
    void renameFolder(const std::string& from, const std::string& to);
    void removeFolder(const std::string& name);
    void toggleInFolder(const std::string& name, const std::string& nsuid);

    /// Скрытые игры: не показываются в каталоге, пока не включить их фильтром.
    /// Нужно, чтобы убрать из выдачи то, во что играть точно не будешь, —
    /// удалить игру из каталога в romfs нельзя, он только для чтения.
    bool isHidden(const std::string& nsuid) const;
    void toggleHidden(const std::string& nsuid);
    const std::vector<std::string>& hiddenGames() const { return hidden; }

    /// Сколько всего записей — для подписи на вкладке.
    size_t size() const;

  private:
    std::string file;
    std::vector<std::string> favs;
    std::map<std::string, std::vector<std::string>> folders;
    std::vector<std::string> hidden;

    static const std::vector<std::string> empty;
};
