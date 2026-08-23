#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>      /* PRIu64, PRId64 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__APPLE__)
#  include <sys/endian.h>
#else
#  include <endian.h>
#endif

#define HEADER_SIZE     64
#define MAGIC           "GCACHEG1"
#define MAGIC_LEN       8

/* Заголовок файла (little‑endian, без выравнивания) */
typedef struct {
    char     magic[8];
    uint32_t version;
    uint64_t node_count;
    uint32_t record_size;
    uint32_t fan_out;
    uint32_t page_size;
    uint32_t back_prob_permille;
    uint64_t seed;
    uint64_t root_index;
    uint64_t min_step_nodes;
    uint32_t flags;
} __attribute__((packed)) Header;

/*
 * Чтение вершины по индексу.
 * Буфер фиксирован (24 байта), т.к. при fan_out == 1 запись всегда 24 байта.
 * Возвращает 0 при успехе, -1 при ошибке.
 */
static int read_vertex(int fd, uint64_t index, size_t record_size,
                       int64_t *value, uint32_t *degree, uint64_t *child)
{
    off_t offset = HEADER_SIZE + index * record_size;
    unsigned char buf[24];  /* 8 (value) + 4 (degree) + 4 (reserved) + 8 (child) */

    ssize_t n = pread(fd, buf, sizeof(buf), offset);
    if (n != sizeof(buf))
        return -1;

    memcpy(value, buf, 8);
    *value = le64toh(*value);

    memcpy(degree, buf + 8, 4);
    *degree = le32toh(*degree);

    if (*degree > 0) {
        memcpy(child, buf + 16, 8);
        *child = le64toh(*child);
    }

    return 0;
}

/*
 * Запись нового значения вершины (только поле value) обратно в файл.
 * Возвращает 0 при успехе, -1 при ошибке.
 */
static int write_value(int fd, uint64_t index, size_t record_size, int64_t new_value)
{
    off_t offset = HEADER_SIZE + index * record_size;  /* начало записи — поле value */
    uint64_t value_le = htole64((uint64_t)new_value);  /* new_value — int64_t, приводим к uint64_t */
    ssize_t n = pwrite(fd, &value_le, sizeof(value_le), offset);
    return (n == sizeof(value_le)) ? 0 : -1;
}

/*
 * Обход графа-цепи.
 * Если write_mode != 0, то значение каждой посещённой вершины обновляется (инкремент).
 * Возвращает количество пройденных вершин или -1 при ошибке.
 */
static int64_t traverse_chain(const char *filename, int write_mode)
{
    int flags = write_mode ? O_RDWR : O_RDONLY;
    int fd = open(filename, flags);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    /* Чтение заголовка */
    Header header;
    ssize_t n = read(fd, &header, sizeof(header));
    if (n != sizeof(header)) {
        fprintf(stderr, "Failed to read header from %s\n", filename);
        close(fd);
        return -1;
    }

    /* Проверка магического числа */
    if (memcmp(header.magic, MAGIC, MAGIC_LEN) != 0) {
        fprintf(stderr, "Invalid magic number in %s\n", filename);
        close(fd);
        return -1;
    }

    uint64_t node_count   = le64toh(header.node_count);
    uint32_t record_size  = le32toh(header.record_size);
    uint32_t fan_out      = le32toh(header.fan_out);
    uint64_t root_index   = le64toh(header.root_index);

    if (fan_out != 1) {
        fprintf(stderr, "Error: fan_out = %u in %s, expected 1\n", fan_out, filename);
        close(fd);
        return -1;
    }

    if (record_size != 24) {
        fprintf(stderr, "Warning: record_size = %u in %s, expected 24. Continuing.\n",
                record_size, filename);
    }

    if (root_index >= node_count) {
        fprintf(stderr, "Error: root_index %" PRIu64 " >= node_count %" PRIu64 " in %s\n",
                root_index, node_count, filename);
        close(fd);
        return -1;
    }

    uint64_t current = root_index;
    uint64_t steps = 0;

    while (1) {
        int64_t  value;
        uint32_t degree;
        uint64_t child;

        if (read_vertex(fd, current, record_size, &value, &degree, &child) != 0) {
            fprintf(stderr, "Error reading vertex at index %" PRIu64 " in %s\n",
                    current, filename);
            close(fd);
            return -1;
        }

        /* Если включён режим записи, обновляем значение */
        if (write_mode) {
            int64_t new_value = value + 1;   /* простой инкремент – создаём нагрузку на запись */
            if (write_value(fd, current, record_size, new_value) != 0) {
                fprintf(stderr, "Error writing value at index %" PRIu64 " in %s\n",
                        current, filename);
                close(fd);
                return -1;
            }
            /* Для отладки можно вывести обновление, но в бенчмарке лучше молчать */
        }

        if (degree == 0)
            break;

        if (child >= node_count) {
            fprintf(stderr, "Error: child %" PRIu64 " out of range in %s\n",
                    child, filename);
            close(fd);
            return -1;
        }

        current = child;
        steps++;

        if (steps > node_count) {
            fprintf(stderr, "Possible cycle detected in %s\n", filename);
            close(fd);
            return -1;
        }
    }

    close(fd);
    return (int64_t)(steps + 1);  /* +1 за корневую вершину */
}

int main(int argc, char **argv)
{
    int write_mode = 0;
    int iter_pos = 1;   /* позиция аргумента с числом итераций */

    /* Проверяем, не передан ли флаг --write */
    if (argc >= 2 && strcmp(argv[1], "--write") == 0) {
        write_mode = 1;
        iter_pos = 2;
    }

    if (argc - iter_pos < 2) {   /* нужно как минимум число итераций и один файл */
        fprintf(stderr, "Usage: %s [--write] <num_iterations> <graph_file1> [graph_file2 ...]\n",
                argv[0]);
        fprintf(stderr, "  --write    : update vertex values (write load)\n");
        fprintf(stderr, "  otherwise : read-only traversal\n");
        return 1;
    }

    long iter_long = strtol(argv[iter_pos], NULL, 10);
    if (iter_long <= 0) {
        fprintf(stderr, "Number of iterations must be positive\n");
        return 1;
    }
    uint64_t iterations = (uint64_t)iter_long;

    int num_files = argc - iter_pos - 1;
    char **files = argv + iter_pos + 1;

    for (uint64_t i = 0; i < iterations; i++) {
        int idx = i % num_files;
        const char *fname = files[idx];

        fprintf(stderr, "Iteration %" PRIu64 "/%" PRIu64 " (%s): traversing %s ... ",
                i + 1, iterations, write_mode ? "write" : "read", fname);
        int64_t nodes = traverse_chain(fname, write_mode);
        if (nodes < 0) {
            fprintf(stderr, "FAILED\n");
            return 1;
        }
        fprintf(stderr, "OK (%" PRId64 " nodes processed)\n", nodes);
    }

    return 0;
}
