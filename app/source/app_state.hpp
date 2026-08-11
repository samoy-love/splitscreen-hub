#pragma once

#include <set>
#include <string>

#include "catalog.hpp"
#include "library.hpp"

/// Общее состояние приложения: каталог, библиотека и список установленных игр.
/// Всё это читается один раз при старте и живёт до выхода.
struct AppState
{
    Catalog catalog;
    Library library;
    std::set<std::string> installedTitleIds;

    Filter filter;

    static AppState& get();

    /// Проставляет флаги installed и favorite — каталог о них не знает.
    void decorate(std::vector<Game>& games) const;
    void decorate(Game& game) const;

    /// Сколько из подходящих под фильтр игр уже стоит на консоли.
    int installedCount(const std::vector<Game>& games) const;
};

/// Человекочитаемый размер: 3.4 ГБ.

/// Число языков вместо их списка — в плитку список не влезет.
int languageCount(const std::string& languages);
