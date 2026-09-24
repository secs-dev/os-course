#!/bin/bash

set -e

# Компилируем обходчик графа
mkdir -p out
gcc -Wall -O2 ../../intro-exp/src/graph_traverse.c -o out/graph_traverse

# Генерируем графы
mkdir -p graphs

# Высокая локальность
echo "===== Генерация graph-0-1 ====="
python3 ../graphgen.py \
    -s 64M \
    --seed 427 \
    --topology chain \
    --min-step-pages 0 \
    --max-step-pages 1 \
    -o graphs/graph-0-1.bin | head -n 15

# Низкая локальность
echo "===== Генерация graph-2-none ====="
python3 ../graphgen.py \
    -s 64M \
    --seed 427 \
    --topology chain \
    --min-step-pages 2 \
    -o graphs/graph-2-none.bin | head -n 15



ITERATIONS=1
CPU=2
N=20

TRAVERSER="./out/graph_traverse"
graphs=(graphs/graph-0-1.bin graphs/graph-2-none.bin)

# Директория для сырых результатов
RESULTS_DIR="result"
CSV_FILE="$RESULTS_DIR/results.csv"

mkdir -p "$RESULTS_DIR"

drop_caches(){
    sudo sync
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
}

echo "timestamp,graph,run_id,wall_time_s,user_time_s,sys_time_s,vol_ctx,invol_ctx,minflt,majflt" > "$CSV_FILE"

for i in $(seq 1 $N); do
    for g in "${graphs[@]}"; do
        echo "Iteration $i / $N : $g"

        drop_caches
        ts=$(date +%s)

        /usr/bin/time -v taskset -c $CPU "$TRAVERSER" "$ITERATIONS" "$g" \
            > /dev/null 2> /tmp/time_out.$$

        wall=$(grep "Elapsed (wall clock)" /tmp/time_out.$$ | awk -F': ' '{print $2}')
        user=$(grep "User time" /tmp/time_out.$$ | awk '{print $4}')
        sys=$(grep "System time" /tmp/time_out.$$ | awk '{print $4}')
        volc=$(grep -i "voluntary context switches" /tmp/time_out.$$ | head -1 | awk '{print $4}')
        invc=$(grep -i "involuntary context switches" /tmp/time_out.$$ | awk '{print $4}')
        minf=$(grep "Minor (reclaiming a frame) page faults" /tmp/time_out.$$ | awk '{print $NF}')
        majf=$(grep "Major (requiring I/O) page faults" /tmp/time_out.$$ | awk '{print $NF}')

        echo "$ts,$g,$i,$wall,$user,$sys,$volc,$invc,$minf,$majf" >> "$CSV_FILE"

        rm -f /tmp/time_out.$$
    done
done

python3 statistics.py result/results.csv

# Возвращаем состояние окружения
rm -rf out
rm -rf graphs