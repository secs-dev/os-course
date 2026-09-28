#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE          /* для O_DIRECT на Linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "graph_io.h"

#if defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define le32toh(x) OSSwapLittleToHostInt32(x)
#  define le64toh(x) OSSwapLittleToHostInt64(x)
#  define htole64(x) OSSwapHostToLittleInt64(x)
#elif defined(__FreeBSD__)
#  include <sys/endian.h>
#else
#  include <endian.h>
#endif

#define HEADER_SIZE     40
#define MAGIC           "GCACHEG1"
#define MAGIC_LEN       8

/* Заголовок файла (little‑endian, без выравнивания) */
typedef struct {
    char     magic[8];
    uint32_t version;
    uint64_t node_count;
    uint32_t record_size;
    uint32_t fan_out;
    uint64_t root_index;
    uint32_t flags;
} __attribute__((packed)) Header;

static int vertex_offset(const GraphFile *file, uint64_t index, size_t record_size, off_t *offset)
{
    if (!record_size || index > ((uintmax_t)file->size - HEADER_SIZE) / record_size) {
        errno = EOVERFLOW;
        return -1;
    }
    *offset = (off_t)(HEADER_SIZE + index * record_size);
    return 0;
}

/*
 * Чтение вершины с использованием lseek + read (без pread).
 * Буфер фиксирован (24 байта).
 */
static int read_vertex(GraphFile *file, uint64_t index, size_t record_size,
                       int64_t *value, uint32_t *degree, uint64_t *child)
{
    off_t offset;
    if (vertex_offset(file, index, record_size, &offset) < 0)
        return -1;

    unsigned char buf[24];
    if (graph_read(file, offset, buf, sizeof(buf)) < 0)
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
 * Запись нового значения вершины (только поле value) с использованием lseek + write.
 */
static int write_value(GraphFile *file, uint64_t index, size_t record_size, int64_t new_value)
{
    off_t offset;
    if (vertex_offset(file, index, record_size, &offset) < 0)
        return -1;

    uint64_t value_le = htole64((uint64_t)new_value);
    return graph_write(file, offset, &value_le, sizeof(value_le));
}

/*
 * Обход графа-цепи.
 * Если write_mode != 0, то значение каждой посещённой вершины обновляется (инкремент).
 * Возвращает количество пройденных вершин или -1 при ошибке.
 */
static int64_t traverse_chain(const char *filename, int write_mode, int no_cache_mode)
{
    GraphFile file;
    if (graph_open(&file, filename, write_mode, no_cache_mode) < 0) {
        perror("graph_open");
        return -1;
    }

    /* Чтение заголовка */
    Header header;
    if (graph_read(&file, 0, &header, sizeof(header)) < 0) {
        fprintf(stderr, "Failed to read header from %s\n", filename);
        graph_close(&file);
        return -1;
    }

    if (memcmp(header.magic, MAGIC, MAGIC_LEN) != 0) {
        fprintf(stderr, "Invalid magic number in %s\n", filename);
        graph_close(&file);
        return -1;
    }

    uint64_t node_count   = le64toh(header.node_count);
    uint32_t record_size  = le32toh(header.record_size);
    uint32_t fan_out      = le32toh(header.fan_out);
    uint64_t root_index   = le64toh(header.root_index);

    if (fan_out != 1) {
        fprintf(stderr, "Error: fan_out = %u in %s, expected 1\n", fan_out, filename);
        graph_close(&file);
        return -1;
    }

    if (record_size < 24 || node_count > INT64_MAX ||
        node_count > ((uintmax_t)file.size - HEADER_SIZE) / record_size) {
        fprintf(stderr, "Invalid record size, node count, or truncated graph in %s\n", filename);
        graph_close(&file);
        return -1;
    }

    if (root_index >= node_count) {
        fprintf(stderr, "Error: root_index %" PRIu64 " >= node_count %" PRIu64 " in %s\n",
                root_index, node_count, filename);
        graph_close(&file);
        return -1;
    }

    uint64_t current = root_index;
    uint64_t steps = 0;

    while (1) {
        int64_t  value;
        uint32_t degree;
        uint64_t child;

        if (read_vertex(&file, current, record_size, &value, &degree, &child) != 0) {
            fprintf(stderr, "Error reading vertex at index %" PRIu64 " in %s\n",
                    current, filename);
            graph_close(&file);
            return -1;
        }

        if (write_mode) {
            int64_t new_value = (int64_t)((uint64_t)value + UINT64_C(1));
            if (write_value(&file, current, record_size, new_value) != 0) {
                fprintf(stderr, "Error writing value at index %" PRIu64 " in %s\n",
                        current, filename);
                graph_close(&file);
                return -1;
            }
        }

        if (degree == 0)
            break;

        if (child >= node_count) {
            fprintf(stderr, "Error: child %" PRIu64 " out of range in %s\n",
                    child, filename);
            graph_close(&file);
            return -1;
        }

        current = child;
        steps++;

        if (steps >= node_count) {
            fprintf(stderr, "Possible cycle detected in %s\n", filename);
            graph_close(&file);
            return -1;
        }
    }

    if (graph_close(&file) < 0) {
        perror("close graph");
        return -1;
    }
    return (int64_t)(steps + 1);
}

int main(int argc, char **argv)
{
    int write_mode = 0;
    int no_cache_mode = 0;
    int iter_pos = 1;

    /* Разбор аргументов: сначала обрабатываем флаги --write и --no-cache */
    while (iter_pos < argc) {
        if (strcmp(argv[iter_pos], "--write") == 0) {
            write_mode = 1;
            iter_pos++;
        } else if (strcmp(argv[iter_pos], "--no-cache") == 0) {
            no_cache_mode = 1;
            iter_pos++;
        } else {
            break;
        }
    }

    if (argc - iter_pos < 2) {
        fprintf(stderr, "Usage: %s [--write] [--no-cache] <num_iterations> <graph_file1> [graph_file2 ...]\n",
                argv[0]);
        fprintf(stderr, "  --write     : update vertex values (write load)\n");
        fprintf(stderr, "  --no-cache  : request cache bypass/minimization (Linux O_DIRECT, macOS F_NOCACHE, FreeBSD O_DIRECT advisory; unsupported elsewhere)\n");
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
        int64_t nodes = traverse_chain(fname, write_mode, no_cache_mode);
        if (nodes < 0) {
            fprintf(stderr, "FAILED\n");
            return 1;
        }
        fprintf(stderr, "OK (%" PRId64 " nodes processed)\n", nodes);
    }

    return 0;
}
