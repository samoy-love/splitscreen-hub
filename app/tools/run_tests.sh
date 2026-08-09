#!/usr/bin/env bash
#
# Прогон тестов чистых функций (app/tests). Нужен обычный C++17-компилятор —
# ни devkitPro, ни консоль не требуются.
#
# На машине, где стандартной библиотеки C++ нет (например, голый msys2 из
# комплекта devkitPro), скрипт честно говорит об этом и выходит с кодом 0:
# «нечем собрать» — не то же самое, что «тесты упали». В CI компилятор есть,
# и там прогон настоящий.

set -u

cd "$(dirname "$0")/.."

CXX_CANDIDATES=("${CXX:-}" g++ clang++ c++)

pick_compiler() {
    for candidate in "${CXX_CANDIDATES[@]}"; do
        [ -z "$candidate" ] && continue
        command -v "$candidate" >/dev/null 2>&1 || continue
        # проверяем, что стандартная библиотека на месте
        if echo '#include <string>
int main(){return 0;}' | "$candidate" -x c++ -std=c++17 -o /dev/null - 2>/dev/null; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

CXX_BIN="$(pick_compiler)" || {
    echo "пропускаю тесты: рабочего C++17-компилятора не нашлось"
    exit 0
}

echo "компилятор: $CXX_BIN"
mkdir -p build-tests

status=0
for test_src in tests/test_*.cpp; do
    name="$(basename "$test_src" .cpp)"
    echo
    echo "=== $name"

    if ! "$CXX_BIN" -std=c++17 -Wall -Wextra -Werror -o "build-tests/$name" \
        "$test_src" source/format.cpp; then
        echo "не собралось: $name"
        status=1
        continue
    fi

    "./build-tests/$name" || status=1
done

exit $status
