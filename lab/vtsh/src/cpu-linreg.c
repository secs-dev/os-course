#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    double x;
    double y;
} Point;

/* Линейная регрессия: y = a*x + b */
void linear_regression(Point *points, int n, double *a, double *b) {
    double sum_x = 0.0, sum_y = 0.0;
    double sum_xx = 0.0, sum_xy = 0.0;

    for (int i = 0; i < n; i++) {
        sum_x += points[i].x;
        sum_y += points[i].y;
        sum_xx += points[i].x * points[i].x;
        sum_xy += points[i].x * points[i].y;
    }

    double denom = n * sum_xx - sum_x * sum_x;
    if (fabs(denom) < 1e-10) {
        *a = 0.0;
        *b = sum_y / n;
        return;
    }

    *a = (n * sum_xy - sum_x * sum_y) / denom;
    *b = (sum_y * sum_xx - sum_x * sum_xy) / denom;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <count> <min> <max> [iterations=1]\n", argv[0]);
        fprintf(stderr, "  count: number of random points\n");
        fprintf(stderr, "  min: minimum value for random numbers\n");
        fprintf(stderr, "  max: maximum value for random numbers\n");
        fprintf(stderr, "  iterations: number of times to repeat (default: 1)\n");
        return 1;
    }

    int n = atoi(argv[1]);
    double min_val = atof(argv[2]);
    double max_val = atof(argv[3]);
    int iterations = 1;

    if (argc >= 5) {
        iterations = atoi(argv[4]);
    }

    if (n <= 0 || min_val >= max_val || iterations <= 0) {
        fprintf(stderr, "Invalid parameters\n");
        return 1;
    }

    srand(time(NULL));

    Point *points = malloc(n * sizeof(Point));
    if (!points) {
        perror("malloc");
        return 1;
    }

    printf("Running linear regression %d times with %d points each...\n", iterations, n);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int iter = 0; iter < iterations; iter++) {
        /* Генерируем случайные точки */
        for (int i = 0; i < n; i++) {
            points[i].x = min_val + (max_val - min_val) * (rand() / (double)RAND_MAX);
            points[i].y = min_val + (max_val - min_val) * (rand() / (double)RAND_MAX);
        }

        /* Вычисляем линейную регрессию */
        double a, b;
        linear_regression(points, n, &a, &b);

        /* Вычисляем среднеквадратичную ошибку */
        double mse = 0.0;
        for (int i = 0; i < n; i++) {
            double y_pred = a * points[i].x + b;
            double error = y_pred - points[i].y;
            mse += error * error;
        }
        mse /= n;

        if (iterations == 1) {
            printf("Linear regression result:\n");
            printf("  y = %.6f * x + %.6f\n", a, b);
            printf("  Mean squared error: %.6f\n", mse);
            printf("  R^2: %.6f\n", 1.0 - mse);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\nPerformance:\n");
    printf("  Total time: %.3f seconds\n", elapsed);
    printf("  Time per iteration: %.3f seconds\n", elapsed / iterations);
    printf("  Points processed: %d\n", n * iterations);
    printf("  Points per second: %.0f\n", (n * iterations) / elapsed);

    free(points);
    return 0;
}