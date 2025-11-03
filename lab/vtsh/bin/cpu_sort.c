#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s algo=<nlogn|n2> count=<number> size=<array_size> [seed=<int>]\n", prog);
}

static double now_seconds(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void insertion_sort(int *arr, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        int key = arr[i];
        size_t j = i;
        while (j > 0 && arr[j - 1] > key) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = key;
    }
}

static void merge(int *arr, int *tmp, size_t left, size_t mid, size_t right) {
    size_t i = left, j = mid, k = left;
    while (i < mid && j < right) {
        if (arr[i] <= arr[j]) tmp[k++] = arr[i++];
        else tmp[k++] = arr[j++];
    }
    while (i < mid) tmp[k++] = arr[i++];
    while (j < right) tmp[k++] = arr[j++];
    for (size_t t = left; t < right; ++t) arr[t] = tmp[t];
}

static void merge_sort_impl(int *arr, int *tmp, size_t left, size_t right) {
    if (right - left <= 1) return;
    size_t mid = left + (right - left) / 2;
    merge_sort_impl(arr, tmp, left, mid);
    merge_sort_impl(arr, tmp, mid, right);
    merge(arr, tmp, left, mid, right);
}

static void merge_sort(int *arr, size_t n) {
    int *tmp = (int*)malloc(n * sizeof(int));
    if (!tmp) { perror("malloc"); exit(2); }
    merge_sort_impl(arr, tmp, 0, n);
    free(tmp);
}

static void fill_random(int *arr, size_t n, unsigned int *seed) {
    for (size_t i = 0; i < n; ++i) {
        arr[i] = (int)(rand_r(seed) & 0x7FFFFFFF);
    }
}

static bool is_sorted_non_decreasing(const int *arr, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        if (arr[i-1] > arr[i]) return false;
    }
    return true;
}

int main(int argc, char **argv) {
    enum { ALG_NLOGN, ALG_N2 } algo = ALG_NLOGN;
    uint64_t count = 0;
    size_t size = 0;
    unsigned int seed = (unsigned int)time(NULL);

    for (int i = 1; i < argc; ++i) {
        char *eq = strchr(argv[i], '=');
        if (!eq) { usage(argv[0]); return 2; }
        *eq = '\0';
        const char *k = argv[i]; const char *v = eq + 1;
        if (strcmp(k, "algo") == 0) {
            if (strcmp(v, "nlogn") == 0) algo = ALG_NLOGN;
            else if (strcmp(v, "n2") == 0) algo = ALG_N2;
            else { fprintf(stderr, "Invalid algo: %s\n", v); return 2; }
        } else if (strcmp(k, "count") == 0) {
            char *endp = NULL; unsigned long long c = strtoull(v, &endp, 10);
            if (*endp != '\0' || c == 0) { fprintf(stderr, "Invalid count: %s\n", v); return 2; }
            count = (uint64_t)c;
        } else if (strcmp(k, "size") == 0) {
            char *endp = NULL; unsigned long long s = strtoull(v, &endp, 10);
            if (*endp != '\0' || s == 0) { fprintf(stderr, "Invalid size: %s\n", v); return 2; }
            size = (size_t)s;
        } else if (strcmp(k, "seed") == 0) {
            char *endp = NULL; unsigned long long sd = strtoull(v, &endp, 10);
            if (*endp != '\0') { fprintf(stderr, "Invalid seed: %s\n", v); return 2; }
            seed = (unsigned int)sd;
        } else {
            fprintf(stderr, "Unknown arg: %s\n", k); return 2;
        }
    }

    if (count == 0 || size == 0) { usage(argv[0]); return 2; }

    int *arr = (int*)malloc(size * sizeof(int));
    if (!arr) { perror("malloc"); return 2; }

    double t0 = now_seconds();
    uint64_t ok = 0;
    for (uint64_t i = 0; i < count; ++i) {
        unsigned int iter_seed = seed + (unsigned int)i * 97u;
        fill_random(arr, size, &iter_seed);
        if (algo == ALG_N2) insertion_sort(arr, size); else merge_sort(arr, size);
        if (is_sorted_non_decreasing(arr, size)) ok++;
    }
    double t1 = now_seconds();

    free(arr);

    double elapsed = t1 - t0;
    fprintf(stderr, "done: algo=%s count=%llu size=%zu time=%.3f s it/s=%.1f ok=%llu\n",
        (algo == ALG_N2 ? "n2" : "nlogn"),
        (unsigned long long)count, size, elapsed,
        elapsed > 0 ? (double)count / elapsed : 0.0,
        (unsigned long long)ok);
    return ok == count ? 0 : 1;
}


