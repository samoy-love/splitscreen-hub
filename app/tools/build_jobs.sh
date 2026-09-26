#!/bin/sh
# Печатает, сколько параллельных задач компилятора выдержит память машины.
#
# make -j$(nproc) и ninja по умолчанию считают только ядра. На своих
# раннерах (класс heavy) ядер много, а потолок памяти — 4 ГиБ на весь раннер
# вместе с docker. Компиляция borealis и FFmpeg по задаче на ядро выбирала
# этот потолок, ядро убивало процессы раннера, и GitHub видел только «runner
# lost communication» посреди сборки — три выкатки подряд.
#
# Задаче даём 1 ГиБ (C++ с шаблонами borealis — самые тяжёлые единицы) и
# ещё гигабайт оставляем раннеру и docker. Считать нужно СНАРУЖИ контейнера:
# внутри docker /proc/meminfo показывает память хоста, а не лимит раннера.
# Уже заданный BUILD_JOBS не пересчитывается — так число задач доезжает в
# контейнер и его можно задать руками.

if [ -n "${BUILD_JOBS:-}" ]; then
    echo "$BUILD_JOBS"
    exit 0
fi

cpus=$(nproc 2>/dev/null || echo 1)

# Свободная память в КиБ: MemAvailable (под lxcfs — уже с учётом лимита
# контейнера), а если виден cgroup v2 с лимитом — не больше остатка до него.
mem_kb=$(awk '/^MemAvailable:/ { print $2 }' /proc/meminfo 2>/dev/null)
mem_kb=${mem_kb:-0}
if [ -r /sys/fs/cgroup/memory.max ] && [ -r /sys/fs/cgroup/memory.current ]; then
    max=$(cat /sys/fs/cgroup/memory.max)
    if [ "$max" != "max" ]; then
        left_kb=$(( (max - $(cat /sys/fs/cgroup/memory.current)) / 1024 ))
        if [ "$mem_kb" -eq 0 ] || [ "$left_kb" -lt "$mem_kb" ]; then
            mem_kb=$left_kb
        fi
    fi
fi

if [ "$mem_kb" -le 0 ]; then
    # Память не узнали — лучше как раньше, чем собирать в одну задачу.
    echo "$cpus"
    exit 0
fi

by_mem=$(( mem_kb / 1048576 - 1 ))
[ "$by_mem" -lt 1 ] && by_mem=1
[ "$by_mem" -lt "$cpus" ] && echo "$by_mem" || echo "$cpus"
