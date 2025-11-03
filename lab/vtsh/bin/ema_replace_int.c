#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint64_t count;            
    size_t size;               
    int32_t needle;            
    int32_t replacement;       
    char path[PATH_MAX];       
    bool generate;             
} config_t;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s count=<iterations> size=<ints> needle=<int> replacement=<int> file=<path> [gen=<on|off>]\n",
        prog);
}

static double now_seconds(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int parse_on_off(const char *v, bool *out) {
    if (strcmp(v, "on") == 0) { *out = true; return 0; }
    if (strcmp(v, "off") == 0) { *out = false; return 0; }
    return -1;
}

static int generate_file(const char *path, size_t n, unsigned int seed) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { perror("open"); return -1; }
    int32_t *buf = (int32_t*)malloc(n * sizeof(int32_t));
    if (!buf) { perror("malloc"); close(fd); return -1; }
    for (size_t i = 0; i < n; ++i) {
        buf[i] = (int32_t)(rand_r(&seed));
    }
    size_t bytes = n * sizeof(int32_t);
    ssize_t w = write(fd, buf, bytes);
    if (w < 0 || (size_t)w != bytes) { perror("write"); free(buf); close(fd); return -1; }
    free(buf);
    if (fsync(fd) != 0) { perror("fsync"); close(fd); return -1; }
    close(fd);
    return 0;
}

static int search_and_replace_once(int fd, size_t nints, int32_t needle, int32_t replacement, uint64_t *replaced_out) {
    const size_t chunk = 4096 / (sizeof(int32_t)) * (sizeof(int32_t));
    int32_t *buf = (int32_t*)malloc(chunk);
    if (!buf) { perror("malloc"); return -1; }
    uint64_t replaced = 0;
    size_t processed = 0;
    while (processed < nints) {
        size_t remain = nints - processed;
        size_t to_read_ints = remain;
        size_t max_ints_in_chunk = chunk / sizeof(int32_t);
        if (to_read_ints > max_ints_in_chunk) to_read_ints = max_ints_in_chunk;
        off_t off = (off_t)(processed * sizeof(int32_t));
        ssize_t r = pread(fd, buf, to_read_ints * sizeof(int32_t), off);
        if (r < 0) { perror("pread"); free(buf); return -1; }
        if ((size_t)r != to_read_ints * sizeof(int32_t)) { /* short read */ break; }
        bool any = false;
        for (size_t i = 0; i < to_read_ints; ++i) {
            if (buf[i] == needle) { buf[i] = replacement; replaced++; any = true; }
        }
        if (any) {
            ssize_t w = pwrite(fd, buf, to_read_ints * sizeof(int32_t), off);
            if (w < 0 || (size_t)w != to_read_ints * sizeof(int32_t)) { perror("pwrite"); free(buf); return -1; }
        }
        processed += to_read_ints;
    }
    free(buf);
    if (replaced_out) *replaced_out = replaced;
    return 0;
}

int main(int argc, char **argv) {
    config_t cfg = {0};
    cfg.generate = true;

    for (int i = 1; i < argc; ++i) {
        char *eq = strchr(argv[i], '=');
        if (!eq) { usage(argv[0]); return 2; }
        *eq = '\0';
        const char *k = argv[i]; const char *v = eq + 1;
        if (strcmp(k, "count") == 0) {
            char *e=NULL; unsigned long long c=strtoull(v,&e,10);
            if (*e!='\0' || c==0) { fprintf(stderr, "Invalid count: %s\n", v); return 2; }
            cfg.count = (uint64_t)c;
        } else if (strcmp(k, "size") == 0) {
            char *e=NULL; unsigned long long s=strtoull(v,&e,10);
            if (*e!='\0' || s==0) { fprintf(stderr, "Invalid size: %s\n", v); return 2; }
            cfg.size = (size_t)s;
        } else if (strcmp(k, "needle") == 0) {
            char *e=NULL; long v32=strtol(v,&e,10);
            if (*e!='\0') { fprintf(stderr, "Invalid needle: %s\n", v); return 2; }
            cfg.needle = (int32_t)v32;
        } else if (strcmp(k, "replacement") == 0) {
            char *e=NULL; long v32=strtol(v,&e,10);
            if (*e!='\0') { fprintf(stderr, "Invalid replacement: %s\n", v); return 2; }
            cfg.replacement = (int32_t)v32;
        } else if (strcmp(k, "file") == 0) {
            if (strlen(v) >= sizeof(cfg.path)) { fprintf(stderr, "file path too long\n"); return 2; }
            strcpy(cfg.path, v);
        } else if (strcmp(k, "gen") == 0) {
            if (parse_on_off(v, &cfg.generate) != 0) { fprintf(stderr, "Invalid gen: %s\n", v); return 2; }
        } else {
            fprintf(stderr, "Unknown arg: %s\n", k); usage(argv[0]); return 2;
        }
    }

    if (cfg.count == 0 || cfg.size == 0 || cfg.path[0] == '\0') { usage(argv[0]); return 2; }

    if (cfg.generate) {
        unsigned int seed = (unsigned int)time(NULL);
        if (generate_file(cfg.path, cfg.size, seed) != 0) return 1;
    } else {
        struct stat st;
        if (stat(cfg.path, &st) != 0) { perror("stat"); return 1; }
        size_t expect_bytes = cfg.size * sizeof(int32_t);
        if ((size_t)st.st_size < expect_bytes) {
            fprintf(stderr, "file too small: have=%lld need=%zu\n", (long long)st.st_size, expect_bytes);
            return 2;
        }
    }

    int fd = open(cfg.path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    double t0 = now_seconds();
    uint64_t total_replaced = 0;
    for (uint64_t i = 0; i < cfg.count; ++i) {
        uint64_t replaced = 0;
        if (search_and_replace_once(fd, cfg.size, cfg.needle, cfg.replacement, &replaced) != 0) { close(fd); return 1; }
        total_replaced += replaced;
    }
    if (fsync(fd) != 0) { perror("fsync");}
    double t1 = now_seconds();
    close(fd);

    double sec = t1 - t0;
    fprintf(stderr, "done: count=%llu size=%zu needle=%d replacement=%d replaced=%llu time=%.3f s it/s=%.1f\n",
        (unsigned long long)cfg.count, cfg.size, (int)cfg.needle, (int)cfg.replacement,
        (unsigned long long)total_replaced, sec, sec>0? (double)cfg.count/sec : 0.0);
    return 0;
}


