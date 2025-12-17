#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Генерация файла со случайными числами */
void generate_file(const char *filename, long size) {
    printf("Generating file %s with size %ld bytes...\n", filename, size);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    srand(time(NULL));
    long remaining = size;
    char buffer[4096];

    while (remaining > 0) {
        int to_write = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);

        /* Заполняем буфер случайными числами */
        for (int i = 0; i < to_write / sizeof(int); i++) {
            ((int*)buffer)[i] = rand() % 1000000;
        }

        ssize_t written = write(fd, buffer, to_write);
        if (written < 0) {
            perror("write");
            close(fd);
            exit(1);
        }

        remaining -= written;
    }

    close(fd);
    printf("File generated successfully.\n");
}

/* Поиск и замена значения в файле */
long search_and_replace(const char *filename, int search_value, int replace_value, int iterations) {
    int fd = open(filename, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 0;
    }

    /* Получаем размер файла */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return 0;
    }

    long file_size = st.st_size;
    long num_ints = file_size / sizeof(int);

    printf("Searching for value %d in file with %ld integers...\n", search_value, num_ints);

    long replacements = 0;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int iter = 0; iter < iterations; iter++) {
        /* Возвращаемся в начало файла для каждой итерации */
        lseek(fd, 0, SEEK_SET);

        int buffer[4096];
        long remaining = num_ints;

        while (remaining > 0) {
            int to_read = (remaining < sizeof(buffer)/sizeof(int)) ?
                         remaining : sizeof(buffer)/sizeof(int);

            ssize_t bytes_read = read(fd, buffer, to_read * sizeof(int));
            if (bytes_read < 0) {
                perror("read");
                close(fd);
                return replacements;
            }

            int items_read = bytes_read / sizeof(int);

            /* Ищем и заменяем в буфере */
            int found_in_buffer = 0;
            for (int i = 0; i < items_read; i++) {
                if (buffer[i] == search_value) {
                    buffer[i] = replace_value;
                    found_in_buffer++;
                }
            }

            if (found_in_buffer > 0) {
                /* Возвращаемся назад и перезаписываем */
                lseek(fd, -bytes_read, SEEK_CUR);
                ssize_t written = write(fd, buffer, bytes_read);
                if (written < bytes_read) {
                    perror("write");
                    close(fd);
                    return replacements;
                }

                replacements += found_in_buffer;
            }

            remaining -= items_read;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    close(fd);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Search and replace completed:\n");
    printf("  Total replacements: %ld\n", replacements);
    printf("  Total time: %.3f seconds\n", elapsed);
    printf("  Time per iteration: %.3f seconds\n", elapsed / iterations);
    printf("  Throughput: %.0f integers/second\n",
           (num_ints * iterations) / elapsed);

    return replacements;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <filename> <size_MB> <search> <replace> [iterations=1]\n", argv[0]);
        fprintf(stderr, "  filename: file to work with\n");
        fprintf(stderr, "  size_MB: file size in megabytes\n");
        fprintf(stderr, "  search: value to search for\n");
        fprintf(stderr, "  replace: value to replace with\n");
        fprintf(stderr, "  iterations: number of times to repeat (default: 1)\n");
        return 1;
    }

    const char *filename = argv[1];
    long size_mb = atol(argv[2]);
    int search_value = atoi(argv[3]);
    int replace_value = atoi(argv[4]);
    int iterations = 1;

    if (argc >= 6) {
        iterations = atoi(argv[5]);
    }

    if (size_mb <= 0 || iterations <= 0) {
        fprintf(stderr, "Invalid parameters\n");
        return 1;
    }

    long file_size = size_mb * 1024 * 1024;

    /* Проверяем, существует ли файл */
    struct stat st;
    if (stat(filename, &st) != 0) {
        /* Файл не существует, создаем его */
        generate_file(filename, file_size);
    } else if (st.st_size != file_size) {
        printf("File exists but has wrong size. Regenerating...\n");
        generate_file(filename, file_size);
    } else {
        printf("Using existing file %s (%ld MB)\n", filename, size_mb);
    }

    /* Выполняем поиск и замену */
    search_and_replace(filename, search_value, replace_value, iterations);

    return 0;
}