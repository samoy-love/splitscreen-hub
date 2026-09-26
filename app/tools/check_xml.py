"""Проверка разметки против того, что borealis действительно понимает.

Неизвестный атрибут borealis не игнорирует — она бросает исключение при разборе
файла, то есть приложение падает на консоли при открытии соответствующего
экрана. Ловить это прогоном сборки на железе дорого: один круг стоит несколько
минут, а ошибка видна только после того, как пользователь дойдёт до экрана.

Скрипт собирает список зарегистрированных атрибутов прямо из исходников
borealis (обычные registerXXXXMLAttribute и макрос BRLS_REGISTER_ENUM_XML_ATTRIBUTE),
разворачивает наследование и сверяет с нашей разметкой.

Запуск: python tools/check_xml.py (из любого каталога — пути считаются от
самого скрипта).
Код возврата 1, если что-то не сходится.
"""

import glob
import io
import os
import re
import sys

# Разметка и сообщения — на русском, а в выводе встречаются ещё и символы вроде
# «★» из самих XML. Консоль Windows по умолчанию берёт cp866, и печать такой
# строки роняет проверку с UnicodeEncodeError — то есть инструмент падает не
# из-за разметки, а из-за собственного вывода.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# Пути — от расположения скрипта, а не от текущего каталога: запуск не из
# app/ находил ноль XML-файлов и честно сообщал, что разметка в порядке.
APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(APP, "lib", "borealis", "library", "lib")
XML_GLOB = os.path.join(APP, "resources", "xml", "**", "*.xml")

# Класс -> файл, где он регистрирует свои атрибуты.
FILES = {
    "View": f"{LIB}/core/view.cpp",
    "Box": f"{LIB}/core/box.cpp",
    "Label": f"{LIB}/views/label.cpp",
    "Image": f"{LIB}/views/image.cpp",
    "Button": f"{LIB}/views/button.cpp",
    "Rectangle": f"{LIB}/views/rectangle.cpp",
    "Header": f"{LIB}/views/header.cpp",
    "Hint": f"{LIB}/views/hint.cpp",
    "Sidebar": f"{LIB}/views/sidebar.cpp",
    "AppletFrame": f"{LIB}/views/applet_frame.cpp",
    "ScrollingFrame": f"{LIB}/views/scrolling_frame.cpp",
    "RecyclerFrame": f"{LIB}/views/recycler.cpp",
    "TabFrame": f"{LIB}/views/tab_frame.cpp",
    "ProgressSpinner": f"{LIB}/views/progress_spinner.cpp",
}

PARENT = {
    "Box": "View",
    "Label": "View",
    "Image": "View",
    "Rectangle": "View",
    "Button": "Box",
    "Header": "Box",
    "Hint": "Box",
    "Sidebar": "Box",
    "AppletFrame": "Box",
    "ScrollingFrame": "Box",
    "RecyclerFrame": "ScrollingFrame",
    "TabFrame": "AppletFrame",
    "ProgressSpinner": "View",
}

# Наши собственные теги из registerXMLView.
OURS = {
    "WrapBox": "Box",
    "CatalogTab": "Box",
    "LibraryTab": "Box",
    "SettingsTab": "Box",
    "HubScreen": "Box",
    "MainTabs": "Box",
    # Наследует Label, а не Box: атрибуты text и fontSize приходят от него.
    "GradientLabel": "Label",
}


def registered(path):
    if not os.path.exists(path):
        return set()
    text = io.open(path, encoding="utf-8", errors="ignore").read()
    names = set(re.findall(r'XMLAttribute\(\s*"([^"]+)"', text))
    names |= set(re.findall(r'BRLS_REGISTER_ENUM_XML_ATTRIBUTE\(\s*\n?\s*"([^"]+)"', text))
    return names


def main():
    # Проверка, которой нечего проверять, должна падать, а не зеленеть: без
    # подмодуля borealis или без разметки успех ничего не значит.
    missing = [path for path in FILES.values() if not os.path.exists(path)]
    if missing:
        print("нет исходников borealis — подмодуль не выкачан?")
        for path in missing:
            print("  " + os.path.relpath(path, APP).replace(os.sep, "/"))
        return 1
    xml_files = sorted(glob.glob(XML_GLOB, recursive=True))
    if not xml_files:
        print(f"не найдено ни одного XML: {os.path.relpath(XML_GLOB, APP)}")
        return 1

    own = {cls: registered(path) for cls, path in FILES.items()}

    def allowed(cls):
        result = set(own.get(cls, ()))
        while cls in PARENT:
            cls = PARENT[cls]
            result |= own.get(cls, set())
        return result

    problems = []

    # Кегль задаётся только через шкалу из source/ui/fonts.hpp. Числом он тут
    # уже накапливался — двенадцать разных значений, на каждом экране свои, и
    # разница в точку читалась как небрежность, а не как замысел.
    for path in xml_files:
        text = io.open(path, encoding="utf-8").read()
        for match in re.finditer(r'fontSize="(\d[^"]*)"', text):
            line = text[: match.start()].count("\n") + 1
            problems.append((path, line, "fontSize", "число вместо @style/hub/font/...",
                             match.group(1)))

    for path in xml_files:
        text = io.open(path, encoding="utf-8").read()
        for match in re.finditer(r'<([\w:]+)((?:[^>"]|"[^"]*")*?)/?>', text):
            tag, attr_text = match.group(1), match.group(2)
            cls = tag.split(":")[-1] if tag.startswith("brls:") else OURS.get(tag)
            if cls is None or cls not in FILES:
                continue  # тег, о котором мы ничего не знаем, — не наше дело
            known = allowed(cls) | {"id"}
            for name, value in re.findall(r'([\w]+)\s*=\s*"([^"]*)"', attr_text):
                if name not in known:
                    line = text[: match.start()].count("\n") + 1
                    problems.append((path, line, tag, name, value))

    for path, line, tag, name, value in problems:
        path = os.path.relpath(path, APP).replace(os.sep, "/")
        if tag == "fontSize":
            print(f'{path}:{line}  fontSize="{value}" — {name}')
        else:
            print(f'{path}:{line}  <{tag}> не понимает {name}="{value}"')

    if problems:
        print(f"\nнайдено проблем: {len(problems)}")
        return 1

    print(f"разметка в порядке, файлов: {len(xml_files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
