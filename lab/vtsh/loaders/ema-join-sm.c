#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORD_SIZE 8

typedef struct {
    int id;
    char word[WORD_SIZE + 1];
} Row;

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <left> <right> <out> <repeats>\n", prog);
}

static int cmp_row(const void *a, const void *b) {
    const Row *ra = a;
    const Row *rb = b;
    return (ra->id > rb->id) - (ra->id < rb->id);
}

static Row *read_table(const char *filename, int *count) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        exit(1);
    }

    if (fscanf(f, "%d", count) != 1 || *count < 0) {
        fprintf(stderr, "Invalid file format: %s\n", filename);
        exit(1);
    }

    Row *rows = (Row *)malloc(sizeof(Row) * (size_t)(*count));
    if (!rows) {
        perror("malloc");
        exit(1);
    }

    for (int i = 0; i < *count; i++) {
        if (fscanf(f, "%d %8s", &rows[i].id, rows[i].word) != 2) {
            fprintf(stderr, "Invalid row in %s\n", filename);
            exit(1);
        }
    }

    fclose(f);
    return rows;
}

static int merge_join(Row *A, int nA, Row *B, int nB, FILE *out) {
    int i = 0, j = 0;
    int result_count = 0;

    if (out) fprintf(out, "0\n");

    while (i < nA && j < nB) {
        if (A[i].id < B[j].id) {
            i++;
        } else if (A[i].id > B[j].id) {
            j++;
        } else {
            int id = A[i].id;
            int i2 = i;
            int j2 = j;

            while (i2 < nA && A[i2].id == id) i2++;
            while (j2 < nB && B[j2].id == id) j2++;

            for (int x = i; x < i2; x++) {
                for (int y = j; y < j2; y++) {
                    if (out) {
                        fprintf(out, "%d %s %s\n", id, A[x].word, B[y].word);
                    }
                    result_count++;
                }
            }

            i = i2;
            j = j2;
        }
    }

    if (out) {
        fseek(out, 0, SEEK_SET);
        fprintf(out, "%d\n", result_count);
    }
    return result_count;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        usage(argv[0]);
        return 1;
    }

    int repeats = atoi(argv[4]);
    if (repeats <= 0) {
        usage(argv[0]);
        return 1;
    }

    for (int r = 0; r < repeats; r++) {
        int nA, nB;
        Row *A = read_table(argv[1], &nA);
        Row *B = read_table(argv[2], &nB);

        qsort(A, nA, sizeof(Row), cmp_row);
        qsort(B, nB, sizeof(Row), cmp_row);

        FILE *out = NULL;
        if (r == repeats - 1) {
            out = fopen(argv[3], "w");
            if (!out) {
                perror("fopen out");
                free(A);
                free(B);
                return 1;
            }
        }

        merge_join(A, nA, B, nB, out);

        if (out) fclose(out);
        free(A);
        free(B);
    }

    return 0;
}
