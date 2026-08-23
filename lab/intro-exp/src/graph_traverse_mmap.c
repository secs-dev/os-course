#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

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

static int no_cache_mode = 0;

/*
 * Обход графа-цепи с использованием mmap.
 * Если write_mode != 0, значение каждой посещённой вершины инкрементируется.
 * Возвращает количество обработанных вершин или -1 при ошибке.
 */
static int64_t traverse_chain_mmap(const char *filename, int write_mode)
{
    int oflags = write_mode ? O_RDWR : O_RDONLY;
    int fd = open(filename, oflags);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    size_t file_size = st.st_size;
    if (file_size < HEADER_SIZE) {
        fprintf(stderr, "File too small: %s\n", filename);
        close(fd);
        return -1;
    }

    int prot = PROT_READ;
    if (write_mode)
        prot |= PROT_WRITE;

    unsigned char *data = mmap(NULL, file_size, prot, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    close(fd);  /* после mmap дескриптор больше не нужен */

    /* Подсказки ядру при включённом --no-cache */
    if (no_cache_mode) {
#if defined(__linux__) || defined(__APPLE__)
        if (madvise(data, file_size, MADV_SEQUENTIAL) != 0)
            perror("madvise(MADV_SEQUENTIAL)");
#endif
    }

    Header *hdr = (Header*)data;
    if (memcmp(hdr->magic, MAGIC, MAGIC_LEN) != 0) {
        fprintf(stderr, "Invalid magic in %s\n", filename);
        munmap(data, file_size);
        return -1;
    }

    uint64_t node_count   = le64toh(hdr->node_count);
    uint32_t record_size  = le32toh(hdr->record_size);
    uint32_t fan_out      = le32toh(hdr->fan_out);
    uint64_t root_index   = le64toh(hdr->root_index);

    if (fan_out != 1) {
        fprintf(stderr, "fan_out = %u (expected 1) in %s\n", fan_out, filename);
        munmap(data, file_size);
        return -1;
    }

    if (record_size != 24)
        fprintf(stderr, "Warning: record_size=%u (expected 24) in %s\n", record_size, filename);

    if (root_index >= node_count) {
        fprintf(stderr, "root_index %" PRIu64 " >= node_count %" PRIu64 " in %s\n",
                root_index, node_count, filename);
        munmap(data, file_size);
        return -1;
    }

    uint64_t current = root_index;
    uint64_t steps = 0;

    while (1) {
        off_t offset = HEADER_SIZE + current * record_size;
        if (offset + 24 > (off_t)file_size) {
            fprintf(stderr, "Vertex %" PRIu64 " out of file bounds\n", current);
            munmap(data, file_size);
            return -1;
        }

        unsigned char *p = data + offset;

        int64_t value;
        memcpy(&value, p, 8);
        value = le64toh(value);

        uint32_t degree;
        memcpy(&degree, p + 8, 4);
        degree = le32toh(degree);

        uint64_t child = 0;
        if (degree > 0) {
            memcpy(&child, p + 16, 8);
            child = le64toh(child);
        }

        if (write_mode) {
            int64_t new_value = value + 1;
            uint64_t new_le = htole64((uint64_t)new_value);
            memcpy(p, &new_le, 8);   /* изменение в отображённой памяти */
        }

        if (degree == 0)
            break;

        if (child >= node_count) {
            fprintf(stderr, "child %" PRIu64 " out of range in %s\n", child, filename);
            munmap(data, file_size);
            return -1;
        }

        current = child;
        steps++;
        if (steps > node_count) {
            fprintf(stderr, "Possible cycle in %s\n", filename);
            munmap(data, file_size);
            return -1;
        }
    }

    /* Освобождаем страницы из кэша (если включено) */
    if (no_cache_mode) {
#if defined(__linux__) || defined(__APPLE__)
        if (madvise(data, file_size, MADV_DONTNEED) != 0)
            perror("madvise(MADV_DONTNEED)");
#endif
    }

    munmap(data, file_size);
    return (int64_t)(steps + 1);
}

int main(int argc, char **argv)
{
    int write_mode = 0;
    int iter_pos = 1;

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
        fprintf(stderr, "Usage: %s [--write] [--no-cache] <num_iterations> <graph_file1> ...\n"
                        "  --write     : update vertex values (write load)\n"
                        "  --no-cache  : use madvise to reduce caching\n", argv[0]);
        return 1;
    }

    long iter = strtol(argv[iter_pos], NULL, 10);
    if (iter <= 0) {
        fprintf(stderr, "iterations must be positive\n");
        return 1;
    }
    uint64_t iterations = (uint64_t)iter;

    int num_files = argc - iter_pos - 1;
    char **files = argv + iter_pos + 1;

    for (uint64_t i = 0; i < iterations; i++) {
        int idx = i % num_files;
        const char *fname = files[idx];

        fprintf(stderr, "Iteration %" PRIu64 "/%" PRIu64 " (%s): traversing %s ... ",
                i + 1, iterations, write_mode ? "write" : "read", fname);
        int64_t nodes = traverse_chain_mmap(fname, write_mode);
        if (nodes < 0) {
            fprintf(stderr, "FAILED\n");
            return 1;
        }
        fprintf(stderr, "OK (%" PRId64 " nodes processed)\n", nodes);
    }
    return 0;
}
