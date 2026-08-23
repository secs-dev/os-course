#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>      /* PRIu64, PRId64 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* Выбор правильного заголовка для little‑endian преобразований */
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
 * Чтение вершины по индексу с использованием POSIX pread().
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
 * Обход графа-цепи из заданного файла.
 * Возвращает количество пройденных вершин (шагов) или -1 при ошибке.
 */
static int64_t traverse_chain(const char *filename)
{
    int fd = open(filename, O_RDONLY);
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

        /* Для отладки можно выводить каждую вершину, но в данном режиме выключим */
        /* printf("Vertex %" PRIu64 ": value = %ld, degree = %u\n", current, value, degree); */

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
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <num_iterations> <graph_file1> [graph_file2 ...]\n",
                argv[0]);
        return 1;
    }

    long iter_long = strtol(argv[1], NULL, 10);
    if (iter_long <= 0) {
        fprintf(stderr, "Number of iterations must be positive\n");
        return 1;
    }
    uint64_t iterations = (uint64_t)iter_long;

    int num_files = argc - 2;
    char **files = argv + 2;

    for (uint64_t i = 0; i < iterations; i++) {
        int idx = i % num_files;
        const char *fname = files[idx];

        fprintf(stderr, "Iteration %" PRIu64 "/%" PRIu64 ": traversing %s ... ",
                i + 1, iterations, fname);
        int64_t nodes = traverse_chain(fname);
        if (nodes < 0) {
            fprintf(stderr, "FAILED\n");
            return 1;
        }
        fprintf(stderr, "OK (%" PRId64 " nodes traversed)\n", nodes);
    }

    return 0;
}
