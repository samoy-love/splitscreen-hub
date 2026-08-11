#!/usr/bin/env bash
#
# Прогон тестов чистых функций (app/tests). Нужен обычный C++17-компилятор —
# ни devkitPro, ни консоль не требуются.
#
# На Windows запускать из msys2, который идёт в комплекте с devkitPro:
#
#   /c/devkitPro/msys2/usr/bin/bash -lc "cd /c/…/app && bash tools/run_tests.sh"
#
# Из Git Bash не получится: там /usr указывает на каталог самого Git, и
# компилятор msys2 не находит собственные заголовки C++ — выглядит это так,
# будто стандартной библиотеки нет вовсе.
#
# Если рабочего компилятора действительно нет, скрипт говорит об этом и выходит
# с кодом 2: это не успех и не провал тестов, а «проверить не удалось».

set -u

cd "$(dirname "$0")/.."

CXX_CANDIDATES=("${CXX:-}" g++ clang++ c++)

pick_compiler() {
    # Пробный вывод — во временный файл, а не в /dev/null: под Cygwin и msys2
    # запись исполняемого файла туда не проходит, и рабочий компилятор
    # объявлялся негодным.
    local probe_out
    probe_out="$(mktemp -u)".exe

    for candidate in "${CXX_CANDIDATES[@]}"; do
        [ -z "$candidate" ] && continue
        command -v "$candidate" >/dev/null 2>&1 || continue
        # проверяем, что стандартная библиотека на месте
        if echo '#include <string>
int main(){ return (int)std::string("x").size() - 1; }' \
            | "$candidate" -x c++ -std=c++17 -o "$probe_out" - 2>/dev/null; then
            rm -f "$probe_out"
            echo "$candidate"
            return 0
        fi
    done

    rm -f "$probe_out"
    return 1
}

# Код возврата 2, а не 0. Без компилятора тесты не выполнялись, и выдавать это
# за успех нельзя: в CI на образе без g++ получалась зелёная галочка при том,
# что не проверено ничего. Отличать от провала самих тестов (1) полезно —
# сборочный скрипт может решить, считать ли это фатальным.
CXX_BIN="$(pick_compiler)" || {
    echo "ОШИБКА: рабочего C++17-компилятора не нашлось, тесты не запускались" >&2
    exit 2
}

echo "компилятор: $CXX_BIN"
mkdir -p build-tests

status=0
for test_src in tests/test_*.cpp; do
    name="$(basename "$test_src" .cpp)"
    echo
    echo "=== $name"

    # Каждый тест собирается со своими модулями: имя файла определяет набор.
    # Ни один из них не тянет borealis или romfs — это чистые функции.
    case "$name" in
        test_query) deps="source/catalog_query.cpp" ;;
        *)          deps="source/format.cpp" ;;
    esac

    if ! "$CXX_BIN" -std=c++17 -Wall -Wextra -Werror -Isource \
        -o "build-tests/$name" "$test_src" $deps; then
        echo "не собралось: $name"
        status=1
        continue
    fi

    "./build-tests/$name" || status=1
done

exit $status
