#pragma once

#include <cstdint>
#include <map>
#include <mutex>
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

    bool isFavorite(const std::string& nsuid) const;
    void toggleFavorite(const std::string& nsuid);

    /// Копия, а не ссылка: список читают рабочие потоки, а меняет UI-поток.
    /// Отдавать наружу ссылку на вектор, который в это время может расти, —
    /// прямой путь к чтению освобождённой памяти.
    std::vector<std::string> favorites() const;

    std::vector<std::string> folderNames() const;
    std::vector<std::string> folder(const std::string& name) const;
    bool inFolder(const std::string& name, const std::string& nsuid) const;

    void createFolder(const std::string& name);
    /// false, если имя занято или пусто — тогда на экране ничего менять нельзя.
    bool renameFolder(const std::string& from, const std::string& to);
    void removeFolder(const std::string& name);
    void toggleInFolder(const std::string& name, const std::string& nsuid);

    /// Скрытые игры: не показываются в каталоге, пока не включить их фильтром.
    /// Нужно, чтобы убрать из выдачи то, во что играть точно не будешь, —
    /// удалить игру из каталога в romfs нельзя, он только для чтения.
    bool isHidden(const std::string& nsuid) const;
    void toggleHidden(const std::string& nsuid);
    std::vector<std::string> hiddenGames() const;

    /// Язык интерфейса и текстов о играх. Хранится здесь же: это единственный
    /// файл пользовательских данных, и заводить второй ради одной строки
    /// незачем.
    const std::string& language() const { return lang; }
    void setLanguage(const std::string& code);

    /// Номер версии, растущий при каждом изменении.
    ///
    /// Вкладки создаются один раз при запуске и дальше только показываются и
    /// прячутся: конструктор библиотеки отработал, когда избранного ещё не было,
    /// и переключение на неё ничего не перечитывало. По этому счётчику экраны
    /// замечают, что данные под ними изменились.
    unsigned revision() const { return rev; }

  private:
    /// Библиотеку читают рабочие потоки (decorate() на каждой игре выдачи), а
    /// меняет UI-поток. Без мьютекса std::find по favs шёл бы одновременно с
    /// push_back в него же.
    mutable std::mutex mutex;

    /// Пишет файл на карту. Зовётся из рабочего потока, копию данных получает
    /// готовой — под мьютексом её собирает saveLater().
    static void writeFile(const std::string& path, const std::string& json, uint64_t snapshot);
    /// Собирает JSON под мьютексом и отправляет запись в рабочий поток.
    void saveLater() const;

    std::string file;
    std::vector<std::string> favs;
    std::map<std::string, std::vector<std::string>> folders;
    std::vector<std::string> hidden;
    /// Пусто — язык не выбирали. Тогда его берут из языка консоли: русский
    /// только если консоль русская, иначе английский. Как только человек
    /// выбрал язык руками, здесь появляется код и система больше не спрашивается.
    std::string lang;
    /// mutable: saveLater() помечена const, а она — единственная точка, через
    /// которую проходят все изменения.
    mutable unsigned rev = 0;

};
