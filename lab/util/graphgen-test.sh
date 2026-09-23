#!/bin/bash

set -e

TIME_FIELDS='Elapsed|User time|System time|Voluntary context switches|Involuntary context switches|Minor.*page faults|Major.*page faults'

# Компилируем обходчики графа
mkdir -p out
gcc -Wall -O2 ../intro-exp/src/graph_traverse.c -o out/graph_traverse
gcc -Wall -O2 ../intro-exp/src/graph_traverse_mmap.c -o out/graph_traverse_mmap

# Генерируем графы с разной локальностью
mkdir -p graphs

# Для чистоты эксперимента меняем только локальность графа

# Тест 1: высокая локальность
echo "===== Генерация graph-0-1 ====="
python3 graphgen.py \
    -s 64M \
    --seed 427 \
    --topology chain \
    --min-step-pages 0 \
    --max-step-pages 1 \
    -o graphs/graph-0-1.bin | head -n 15

# Тест 2: средняя локальность
echo "===== Генерация graph-0-10 ====="
python3 graphgen.py \
    -s 64M \
    --seed 427 \
    --topology chain \
    --min-step-pages 0 \
    --max-step-pages 10 \
    -o graphs/graph-0-10.bin | head -n 15

# Тест 3: без ограничения сверху
echo "===== Генерация graph-0-none ====="
python3 graphgen.py \
    -s 64M \
    --seed 427 \
    --topology chain \
    --min-step-pages 0 \
    -o graphs/graph-0-none.bin | head -n 15

# Тест 4: минимальный шаг 2 страницы, без ограничения сверху
echo "===== Генерация graph-2-none ====="
python3 graphgen.py \
    -s 64M \
    --seed 427 \
    --topology chain \
    --min-step-pages 2 \
    -o graphs/graph-2-none.bin | head -n 15


for traverser in graph_traverse graph_traverse_mmap; do
    echo "===== ОБХОДЧИК: $traverser ====="

    echo "===== graph-0-1: высокая локальность ====="
    sudo sync
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
    /usr/bin/time -v taskset -c 2 \
        ./out/$traverser 1 graphs/graph-0-1.bin \
        2>&1 | grep -E "$TIME_FIELDS"

    echo "===== graph-0-10: средняя локальность ====="
    sudo sync
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
    /usr/bin/time -v taskset -c 2 \
        ./out/$traverser 1 graphs/graph-0-10.bin \
        2>&1 | grep -E "$TIME_FIELDS"

    echo "===== graph-0-none: без ограничения сверху ====="
    sudo sync
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
    /usr/bin/time -v taskset -c 2 \
        ./out/$traverser 1 graphs/graph-0-none.bin \
        2>&1 | grep -E "$TIME_FIELDS"

    echo "===== graph-2-none: минимальный шаг 2 страницы ====="
    sudo sync
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
    /usr/bin/time -v taskset -c 2 \
        ./out/$traverser 1 graphs/graph-2-none.bin \
        2>&1 | grep -E "$TIME_FIELDS"
done

# Возвращаем состояние окружения
rm -rf out
rm -rf graphs