#pragma once

#include <string>

/// Чистые преобразования данных каталога в то, что видит пользователь.
///
/// Вынесены в отдельный файл без зависимостей нарочно: так их можно собрать и
/// проверить обычным g++ на машине разработчика, не поднимая ни borealis, ни
/// SQLite, ни тулчейн Switch. Тесты — в tests/test_format.cpp.
namespace fmtx
{

/// Размер картриджа человеку: «4.7 ГБ», «512 МБ». Пустая строка, если размер
/// неизвестен — в базе это ноль.
std::string formatSize(long long bytes);

/// Сколько языков в строке вида "ru,en,ja". Ноль, если строка пустая.
int languageCount(const std::string& languages);

/// Фирменный цвет игры приходит из eShop строкой вида "0f336f".
/// Возвращает false, если строка не шестизначное шестнадцатеричное число, —
/// тогда вызывающий берёт цвет темы.
bool parseHexColor(const std::string& hex, unsigned char& r, unsigned char& g,
                   unsigned char& b);

}  // namespace fmtx
