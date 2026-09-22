#ifndef GRAPH_IO_H
#define GRAPH_IO_H

/* Include after defining _GNU_SOURCE and _FILE_OFFSET_BITS=64. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* One file, one reusable scratch allocation; no cached file data.
 * The caller must exclude concurrent writers and truncation. */
typedef struct {
    int fd;
    off_t size;
    size_t alignment;       /* Zero for ordinary I/O (including F_NOCACHE). */
    unsigned char *buffer;
} GraphFile;

#ifdef __linux__
static int graph_setup_direct_io(GraphFile *file, const struct stat *st)
{
    size_t alignment = 0;
#if defined(STATX_DIOALIGN) && defined(AT_EMPTY_PATH)
    struct statx sx;
    if (statx(file->fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &sx) == 0 &&
        (sx.stx_mask & STATX_DIOALIGN)) {
        size_t memory = sx.stx_dio_mem_align;
        size_t offset = sx.stx_dio_offset_align;
        if (!memory || !offset || (memory & (memory - 1)) ||
            (offset & (offset - 1))) {
            errno = EOPNOTSUPP;
            return -1;
        }
        /* The larger power of two satisfies both alignment requirements. */
        alignment = memory > offset ? memory : offset;
    }
#endif
    if (!alignment) {
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0 || st->st_blksize <= 0 ||
            (uintmax_t)st->st_blksize > SIZE_MAX) {
            errno = EOPNOTSUPP;
            return -1;
        }
        alignment = (size_t)page;
        if (alignment < (size_t)st->st_blksize)
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
    void *buffer;
    int error = posix_memalign(&buffer, alignment, alignment);
    if (error) {
        errno = error;
        return -1;
    }
    file->alignment = alignment;
    file->buffer = buffer;
    return 0;
}
#endif

static int graph_open(GraphFile *file, const char *path, int writing, int no_cache)
{
    *file = (GraphFile){ .fd = -1 };
#if !defined(__linux__) && !defined(__APPLE__)
    if (no_cache) {
        fprintf(stderr, "--no-cache is supported only on Linux and macOS\n");
        errno = EOPNOTSUPP;
        return -1;
    }
#endif
    int flags = writing ? O_RDWR : O_RDONLY;
#ifdef __linux__
    if (no_cache)
        flags |= O_DIRECT;
#endif
    file->fd = open(path, flags);
    if (file->fd < 0)
        goto fail;

    struct stat st;
    if (fstat(file->fd, &st) < 0)
        goto fail;
    if (!S_ISREG(st.st_mode) || st.st_size < 0) {
        errno = EINVAL;
        goto fail;
    }
    file->size = st.st_size;
#ifdef __APPLE__
    if (no_cache && fcntl(file->fd, F_NOCACHE, 1) < 0)
        goto fail;
#elif defined(__linux__)
    if (no_cache && graph_setup_direct_io(file, &st) < 0)
        goto fail;
#endif
    return 0;

fail:
    {
        int error = errno;
        if (no_cache)
            perror("--no-cache open/setup failed (no buffered fallback)");
        if (file->fd >= 0)
            close(file->fd);
        file->fd = -1;
        errno = error;
        return -1;
    }
}

static int graph_close(GraphFile *file)
{
    free(file->buffer);
    file->buffer = NULL;
    int result = close(file->fd);
    file->fd = -1;
    return result;
}

static int graph_seek(GraphFile *file, off_t offset)
{
    off_t result;
    do {
        result = lseek(file->fd, offset, SEEK_SET);
    } while (result == (off_t)-1 && errno == EINTR);
    return result == (off_t)-1 ? -1 : 0;
}

/* Retry interruptions, but never continue an unexpected short transfer.
 * For the last direct-I/O block, expected is the number of bytes before EOF. */
static int graph_transfer_once(GraphFile *file, void *data, size_t size,
                               size_t expected, int writing)
{
    ssize_t result;
    do {
        result = writing ? write(file->fd, data, size) : read(file->fd, data, size);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        return -1;
    if ((size_t)result != expected) {
        errno = EIO;
        return -1;
    }
    return 0;
}

#ifdef __linux__
static int graph_direct_transfer(GraphFile *file, off_t offset,
                                 unsigned char *data, size_t size, int writing)
{
    const size_t block = file->alignment;
    while (size > 0) {
        size_t skip = (size_t)((uintmax_t)offset % block);
        off_t start = offset - (off_t)skip;
        if ((uintmax_t)start > INT64_MAX - (uintmax_t)block) {
            errno = EOVERFLOW;
            return -1;
        }
        size_t available = block;
        if ((uintmax_t)(file->size - start) < block)
            available = (size_t)(file->size - start);
        size_t count = block - skip;
        if (count > size)
            count = size;

        /* Always reload, including before writes: preserve neighboring data. */
        if (graph_seek(file, start) < 0 ||
            graph_transfer_once(file, file->buffer, block, available, 0) < 0)
            return -1;
        if (writing) {
            memset(file->buffer + available, 0, block - available);
            memcpy(file->buffer + skip, data, count);
            if (graph_seek(file, start) < 0)
                return -1;
            int result = graph_transfer_once(file, file->buffer, block, block, 1);
            int error = result < 0 ? errno : 0;
            /* Even a failed/short write may have extended the final block. */
            if (available < block) {
                int rc;
                do {
                    rc = ftruncate(file->fd, file->size);
                } while (rc < 0 && errno == EINTR);
                if (rc < 0) {
                    error = errno;
                    perror("Failed to restore graph EOF");
                }
            }
            if (error) {
                errno = error;
                return -1;
            }
        } else {
            memcpy(data, file->buffer + skip, count);
        }
        offset += (off_t)count;
        data += count;
        size -= count;
    }
    return 0;
}
#endif

static int graph_transfer(GraphFile *file, off_t offset, void *data,
                          size_t size, int writing)
{
    if (offset < 0 || offset > file->size || size > SSIZE_MAX ||
        (uintmax_t)size > (uintmax_t)(file->size - offset)) {
        errno = EIO;
        return -1;
    }
    if (!size)
        return 0;
#ifdef __linux__
    if (file->alignment) {
        int result = graph_direct_transfer(file, offset, data, size, writing);
        if (result < 0) {
            int error = errno;
            perror("--no-cache direct I/O failed (no buffered fallback)");
            errno = error;
        }
        return result;
    }
#endif
    if (graph_seek(file, offset) < 0)
        return -1;
    return graph_transfer_once(file, data, size, size, writing);
}

static int graph_read(GraphFile *file, off_t offset, void *data, size_t size)
{
    return graph_transfer(file, offset, data, size, 0);
}

static int graph_write(GraphFile *file, off_t offset, const void *data, size_t size)
{
    return graph_transfer(file, offset, (void *)data, size, 1);
}

#endif
