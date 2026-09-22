#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE          /* для O_DIRECT на Linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define le32toh(x) OSSwapLittleToHostInt32(x)
#  define le64toh(x) OSSwapLittleToHostInt64(x)
#  define htole64(x) OSSwapHostToLittleInt64(x)
#  include <sys/fcntl.h>
#else
#  include <endian.h>
#endif

#ifdef __linux__
#  include <linux/fs.h>      /* для O_DIRECT */
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
static off_t graph_size;
#ifdef __linux__
static size_t dio_alignment;

static int setup_direct_io(int fd, const struct stat *st)
{
    size_t alignment = 0;
#if defined(STATX_DIOALIGN) && defined(AT_EMPTY_PATH)
    struct statx sx;
    if (statx(fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &sx) == 0 &&
        (sx.stx_mask & STATX_DIOALIGN)) {
        if (!sx.stx_dio_mem_align || !sx.stx_dio_offset_align) {
            errno = EOPNOTSUPP;
            return -1;
        }
        alignment = sx.stx_dio_mem_align;
        if (alignment < sx.stx_dio_offset_align)
            alignment = sx.stx_dio_offset_align;
        /* A common power-of-two alignment satisfies both constraints. */
        if ((sx.stx_dio_mem_align & (sx.stx_dio_mem_align - 1)) ||
            (sx.stx_dio_offset_align & (sx.stx_dio_offset_align - 1))) {
            errno = EOPNOTSUPP;
            return -1;
        }
    }
#endif
    if (!alignment) {
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0 || st->st_blksize <= 0) {
            errno = EOPNOTSUPP;
            return -1;
        }
        alignment = (size_t)page;
        if (alignment < (uintmax_t)st->st_blksize)
            alignment = (size_t)st->st_blksize;
        if (alignment % (size_t)page || alignment % (size_t)st->st_blksize) {
            errno = EOPNOTSUPP;
            return -1;
        }
    }
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    if ((alignment & (alignment - 1)) || alignment > SSIZE_MAX) {
        errno = EOPNOTSUPP;
        return -1;
    }
    dio_alignment = alignment;
    return 0;
}

static int seek_to(int fd, off_t offset)
{
    off_t result;
    do {
        result = lseek(fd, offset, SEEK_SET);
    } while (result == (off_t)-1 && errno == EINTR);
    return result == (off_t)-1 ? -1 : 0;
}

/* Each operation owns its bounce buffer: no data is cached between requests.
 * RMW assumes no concurrent writers/truncation, like the traversal itself. */
static int direct_transfer(int fd, off_t offset, void *data, size_t size, int writing)
{
    size_t skip = (size_t)((uintmax_t)offset % dio_alignment);
    off_t start = offset - (off_t)skip;
    if (size > SIZE_MAX - skip ||
        size + skip > SIZE_MAX - (dio_alignment - 1)) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t length = (skip + size + dio_alignment - 1) & ~(dio_alignment - 1);
    if (length > SSIZE_MAX || (uintmax_t)start > INT64_MAX - (uintmax_t)length) {
        errno = EOVERFLOW;
        return -1;
    }
    void *buffer;
    int error = posix_memalign(&buffer, dio_alignment, length);
    if (error) {
        errno = error;
        return -1;
    }
    memset(buffer, 0, length);
    size_t needed = length;
    if ((uintmax_t)(graph_size - start) < needed)
        needed = (size_t)(graph_size - start);
    size_t done = 0;
    if (seek_to(fd, start) < 0) {
        error = errno;
        goto out;
    }
    while (done < needed) {
        ssize_t n = read(fd, (char *)buffer + done, length - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            error = n < 0 ? errno : EIO;
            goto out;
        }
        done += (size_t)n;
        if (done < needed && done % dio_alignment) {
            error = EIO; /* Cannot retry an unaligned short direct transfer. */
            goto out;
        }
    }
    if (!writing) {
        memcpy(data, (char *)buffer + skip, size);
        goto out;
    }
    memcpy((char *)buffer + skip, data, size);
    done = 0;
    if (seek_to(fd, start) < 0) {
        error = errno;
        goto out;
    }
    while (done < length) {
        ssize_t n = write(fd, (char *)buffer + done, length - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            error = n < 0 ? errno : EIO;
            break;
        }
        done += (size_t)n;
        if (done < length && done % dio_alignment) {
            error = EIO;
            break;
        }
    }
    /* Restore EOF even after a failed/partial tail write. */
    if ((uintmax_t)start + length > (uintmax_t)graph_size) {
        int rc;
        do {
            rc = ftruncate(fd, graph_size);
        } while (rc < 0 && errno == EINTR);
        if (rc < 0) {
            error = errno;
            perror("Failed to restore graph EOF");
        }
    }
out:
    free(buffer);
    if (error) {
        errno = error;
        return -1;
    }
    return 0;
}
#endif

static int transfer(int fd, off_t offset, void *data, size_t size, int writing)
{
    if (offset < 0 || offset > graph_size ||
        (uintmax_t)size > (uintmax_t)(graph_size - offset)) {
        errno = EIO;
        return -1;
    }
#ifdef __linux__
    if (no_cache_mode) {
        int rc = direct_transfer(fd, offset, data, size, writing);
        if (rc < 0)
            perror("--no-cache direct I/O failed (no buffered fallback)");
        return rc;
    }
#endif
    off_t pos;
    do {
        pos = lseek(fd, offset, SEEK_SET);
    } while (pos == (off_t)-1 && errno == EINTR);
    if (pos == (off_t)-1)
        return -1;
    size_t done = 0;
    while (done < size) {
        ssize_t n = writing ? write(fd, (char *)data + done, size - done)
                            : read(fd, (char *)data + done, size - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (n == 0)
                errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

/*
 * Открытие файла с учётом режима (read-only или read-write) и опции no-cache.
 * Возвращает дескриптор или -1 при ошибке.
 */
static int open_graph_file(const char *filename, int write_mode)
{
#if !defined(__linux__) && !defined(__APPLE__)
    if (no_cache_mode) {
        fprintf(stderr, "--no-cache is supported only on Linux and macOS\n");
        errno = EOPNOTSUPP;
        return -1;
    }
#endif
    int flags = write_mode ? O_RDWR : O_RDONLY;
#ifdef __linux__
    if (no_cache_mode)
        flags |= O_DIRECT;
#endif

    int fd = open(filename, flags);
    if (fd == -1) {
        if (no_cache_mode)
            perror("--no-cache open failed (no buffered fallback)");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0)
        goto fail;
    if (!S_ISREG(st.st_mode) || st.st_size < HEADER_SIZE) {
        errno = EINVAL;
        goto fail;
    }
    graph_size = st.st_size;

#if defined(__APPLE__)
    if (no_cache_mode) {
        /* Отключаем кэширование на macOS */
        if (fcntl(fd, F_NOCACHE, 1) == -1)
            goto fail;
    }
#elif defined(__linux__)
    if (no_cache_mode && setup_direct_io(fd, &st) < 0)
        goto fail;
#endif

    return fd;
fail:
    {
        int saved = errno;
        if (no_cache_mode)
            perror("--no-cache setup failed (no buffered fallback)");
        close(fd);
        errno = saved;
        return -1;
    }
}

static int vertex_offset(uint64_t index, size_t record_size, off_t *offset)
{
    if (!record_size || index > ((uintmax_t)graph_size - HEADER_SIZE) / record_size) {
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
static int read_vertex(int fd, uint64_t index, size_t record_size,
                       int64_t *value, uint32_t *degree, uint64_t *child)
{
    off_t offset;
    if (vertex_offset(index, record_size, &offset) < 0)
        return -1;

    unsigned char buf[24];
    if (transfer(fd, offset, buf, sizeof(buf), 0) < 0)
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
    off_t offset;
    if (vertex_offset(index, record_size, &offset) < 0)
        return -1;

    uint64_t value_le = htole64((uint64_t)new_value);
    return transfer(fd, offset, &value_le, sizeof(value_le), 1);
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
    if (transfer(fd, 0, &header, sizeof(header), 0) < 0) {
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

    if (record_size < 24 || node_count > INT64_MAX ||
        node_count > ((uintmax_t)graph_size - HEADER_SIZE) / record_size) {
        fprintf(stderr, "Invalid record size, node count, or truncated graph in %s\n", filename);
        close(fd);
        return -1;
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
            int64_t new_value = (int64_t)((uint64_t)value + UINT64_C(1));
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

        if (steps >= node_count) {
            fprintf(stderr, "Possible cycle detected in %s\n", filename);
            close(fd);
            return -1;
        }
    }

    if (close(fd) < 0) {
        perror("close graph");
        return -1;
    }
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
        fprintf(stderr, "  --no-cache  : disable system cache (O_DIRECT on Linux, F_NOCACHE on macOS; unsupported elsewhere)\n");
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
