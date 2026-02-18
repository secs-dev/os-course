#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <size> <repeats> [seed]\n", prog);
}

int main(int argc, char *argv[]) {
    int n = 200;
    int repeats = 1;
    unsigned int seed = 1;

    if (argc > 1) {
        n = atoi(argv[1]);
        if (n <= 0) { usage(argv[0]); return 1; }
    }
    if (argc > 2) {
        repeats = atoi(argv[2]);
        if (repeats <= 0) { usage(argv[0]); return 1; }
    }
    if (argc > 3) {
        seed = (unsigned int)strtoul(argv[3], NULL, 10);
    }
    if (argc > 4) {
        usage(argv[0]);
        return 1;
    }

    srand(seed);

    size_t total = (size_t)n * (size_t)n;
    double *A = (double *)malloc(sizeof(double) * total);
    double *B = (double *)malloc(sizeof(double) * total);
    double *C = (double *)malloc(sizeof(double) * total);
    if (!A || !B || !C) {
        perror("malloc");
        free(A);
        free(B);
        free(C);
        return 1;
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            size_t idx = (size_t)i * (size_t)n + (size_t)j;
            A[idx] = (double)(rand() % 1000) / 100.0;
            B[idx] = (double)(rand() % 1000) / 100.0;
        }
    }

    double checksum = 0.0;
    for (int r = 0; r < repeats; r++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                double sum = 0.0;
                for (int k = 0; k < n; k++) {
                    sum += A[(size_t)i * (size_t)n + (size_t)k] *
                           B[(size_t)k * (size_t)n + (size_t)j];
                }
                C[(size_t)i * (size_t)n + (size_t)j] = sum;
            }
        }
        checksum += C[(size_t)(r % n) * (size_t)n + (size_t)((r * 7) % n)];
    }

    printf("sum=%.6f\n", checksum);

    free(A);
    free(B);
    free(C);
    return 0;
}
