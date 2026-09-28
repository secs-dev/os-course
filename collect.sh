#!/usr/bin/env bash
set -euo pipefail

N=10
ITER=10
WARMUP=2
GRAPH="graph-rand.bin"

MODE="${1:-}"

case "$MODE" in
    readwrite)
        OUT="results_readwrite.csv"
        METHODS=("read" "write")
        ;;
    mmap)
        OUT="results_mmap.csv"
        METHODS=("mmap_read" "mmap_write")
        ;;
    *)
        echo "Usage: $0 {readwrite|mmap}"
        exit 1
        ;;
esac

echo "timestamp,graph,run_id,method,iterations,wall_time_s,page_faults,vol_ctx,invol_ctx" > "$OUT"

BACKUP="${GRAPH}.original"
cp "$GRAPH" "$BACKUP"

# ------------------------------------------------------------
# Initial cache preparation
# ------------------------------------------------------------

sudo purge >/dev/null 2>&1

# Warm the file cache.
cat "$GRAPH" > /dev/null
cat "$GRAPH" > /dev/null

echo "Cache warmed."
echo "Mode: $MODE"
echo "N=$N, ITER=$ITER, WARMUP=$WARMUP"
echo

# ------------------------------------------------------------
# Function: run one command
# ------------------------------------------------------------

run_measurement() {
    local method="$1"
    local output_csv="$2"
    local run_id="$3"
    local save_result="$4"

    case "$method" in
        read)
            CMD=(./out/graph_traverse "$ITER" "$GRAPH")
            ;;
        write)
            cp "$BACKUP" "$GRAPH"
            cat "$GRAPH" > /dev/null
            CMD=(./out/graph_traverse --write "$ITER" "$GRAPH")
            ;;
        mmap_read)
            CMD=(./out/graph_traverse_mmap "$ITER" "$GRAPH")
            ;;
        mmap_write)
            cp "$BACKUP" "$GRAPH"
            cat "$GRAPH" > /dev/null
            CMD=(./out/graph_traverse_mmap --write "$ITER" "$GRAPH")
            ;;
        *)
            echo "Unknown method: $method"
            exit 1
            ;;
    esac

    TMP=$(mktemp)

    /usr/bin/time -l "${CMD[@]}" > /dev/null 2>"$TMP"

    wall=$(awk '$2 == "real" {print $1}' "$TMP" | tr ',' '.')
    page_faults=$(awk '$2 == "page" && $3 == "faults" {print $1}' "$TMP")
    vol_ctx=$(awk '$2 == "voluntary" && $3 == "context" && $4 == "switches" {print $1}' "$TMP")
    invol_ctx=$(awk '$2 == "involuntary" && $3 == "context" && $4 == "switches" {print $1}' "$TMP")

    echo "$wall s | faults=$page_faults | voluntary=$vol_ctx | involuntary=$invol_ctx"

    if [[ "$save_result" == "yes" ]]; then
        timestamp=$(date +%s)

        echo "$timestamp,$GRAPH,$run_id,$method,$ITER,$wall,$page_faults,$vol_ctx,$invol_ctx" >> "$output_csv"
    fi

    rm -f "$TMP"
}

# ------------------------------------------------------------
# Warm-up runs
# ------------------------------------------------------------

echo "=== WARM-UP ==="

for method in "${METHODS[@]}"; do
    for w in $(seq 1 "$WARMUP"); do
        echo "Warm-up $w/$WARMUP — $method"
        run_measurement "$method" "$OUT" 0 "no"
    done
done

echo
echo "=== MEASUREMENTS ==="

# ------------------------------------------------------------
# Final measurements
# ------------------------------------------------------------

for i in $(seq 1 "$N"); do
    for method in "${METHODS[@]}"; do
        echo "RUN $i/$N — $method"

        run_measurement "$method" "$OUT" "$i" "yes"
    done
done

rm -f "$BACKUP"

echo
echo "Done."
echo "Results saved to $OUT"