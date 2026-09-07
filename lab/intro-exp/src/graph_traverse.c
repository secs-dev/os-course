#define _GNU_SOURCE          /* для O_DIRECT на Linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__APPLE__)
#  include <sys/endian.h>
#  include <sys/fcntl.h>
#else
#  include <endian.h>
#endif

#ifdef __linux__
#  include <linux/fs.h>      /* для O_DIRECT */
#endif

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  define O_RDONLY _O_RDONLY
#  define O_RDWR   _O_RDWR
#  define open     _open
#  define close    _close
#  define lseek    _lseeki64
#  define read     _read
#  define write    _write
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

static int no_cache_mode = 0;

/*
 * Открытие файла с учётом режима (read-only или read-write) и опции no-cache.
 * Возвращает дескриптор или -1 при ошибке.
 */
static int open_graph_file(const char *filename, int write_mode)
{
    int flags = write_mode ? O_RDWR : O_RDONLY;
#ifdef __linux__
    if (no_cache_mode)
        flags |= O_DIRECT;
#endif

    int fd = open(filename, flags);
    if (fd == -1)
        return -1;

#if defined(__APPLE__)
    if (no_cache_mode) {
        /* Отключаем кэширование на macOS */
        int set = 1;
        if (fcntl(fd, F_NOCACHE, &set) == -1) {
            close(fd);
            return -1;
        }
    }
#elif defined(__linux__)
    /* Для Linux O_DIRECT уже установлен, ничего дополнительно не нужно */
#elif defined(_WIN32)
    /* Для Windows используем CreateFile с FILE_FLAG_NO_BUFFERING */
    if (no_cache_mode) {
        /* Закрываем POSIX-дескриптор и открываем через WinAPI */
        close(fd);
        HANDLE h = CreateFileA(filename,
                               write_mode ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_FLAG_NO_BUFFERING,
                               NULL);
        if (h == INVALID_HANDLE_VALUE)
            return -1;
        /* Преобразуем HANDLE в дескриптор C (это не portable) */
        fd = _open_osfhandle((intptr_t)h, flags);
        if (fd == -1) {
            CloseHandle(h);
            return -1;
        }
    }
#endif

    return fd;
}

/*
 * Чтение вершины с использованием lseek + read (без pread).
 * Буфер фиксирован (24 байта).
 */
static int read_vertex(int fd, uint64_t index, size_t record_size,
                       int64_t *value, uint32_t *degree, uint64_t *child)
{
    off_t offset = HEADER_SIZE + index * record_size;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        return -1;

    unsigned char buf[24];
    ssize_t n = read(fd, buf, sizeof(buf));
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
 * Запись нового значения вершины (только поле value) с использованием lseek + write.
 */
static int write_value(int fd, uint64_t index, size_t record_size, int64_t new_value)
{
    off_t offset = HEADER_SIZE + index * record_size;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        return -1;

    uint64_t value_le = htole64((uint64_t)new_value);
    ssize_t n = write(fd, &value_le, sizeof(value_le));
    return (n == sizeof(value_le)) ? 0 : -1;
}

/*
 * Обход графа-цепи.
 * Если write_mode != 0, то значение каждой посещённой вершины обновляется (инкремент).
 * Возвращает количество пройденных вершин или -1 при ошибке.
 */
static int64_t traverse_chain(const char *filename, int write_mode)
{
    int fd = open_graph_file(filename, write_mode);
    if (fd == -1) {
        perror("open_graph_file");
        return -1;
    }

    /* Чтение заголовка */
    Header header;
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        close(fd);
        return -1;
    }
    ssize_t n = read(fd, &header, sizeof(header));
    if (n != sizeof(header)) {
        fprintf(stderr, "Failed to read header from %s\n", filename);
        close(fd);
        return -1;
    }

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

        if (write_mode) {
            int64_t new_value = value + 1;
            if (write_value(fd, current, record_size, new_value) != 0) {
                fprintf(stderr, "Error writing value at index %" PRIu64 " in %s\n",
                        current, filename);
                close(fd);
                return -1;
            }
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
    return (int64_t)(steps + 1);
}

int main(int argc, char **argv)
{
    int write_mode = 0;
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
        fprintf(stderr, "  --no-cache  : disable system cache (O_DIRECT on Linux, F_NOCACHE on macOS, FILE_FLAG_NO_BUFFERING on Windows)\n");
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
