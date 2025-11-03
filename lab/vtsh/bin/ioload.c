#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	bool is_write;              
	size_t block_size;          
	uint64_t block_count;       
	char file_path[PATH_MAX];   
	uint64_t range_begin;       
	uint64_t range_end;         
	bool use_direct;            
	bool random_access;         
} io_config;

static void print_usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s rw=<read|write> block_size=<bytes> block_count=<n> file=<path> "
		"[range=<begin>-<end>] [direct=<on|off>] [type=<sequence|random>]\n",
		prog);
}

static int parse_bool_on_off(const char *v, bool *out) {
	if (strcmp(v, "on") == 0) { *out = true; return 0; }
	if (strcmp(v, "off") == 0) { *out = false; return 0; }
	return -1;
}

static int parse_type(const char *v, bool *random_access) {
	if (strcmp(v, "sequence") == 0) { *random_access = false; return 0; }
	if (strcmp(v, "random") == 0) { *random_access = true; return 0; }
	return -1;
}

static int parse_range(const char *v, uint64_t *begin, uint64_t *end) {
	char *dash = strchr(v, '-');
	if (!dash) return -1;
	char *endptr = NULL;
	*dash = '\0';
	unsigned long long b = strtoull(v, &endptr, 10);
	if (*endptr != '\0') return -1;
	unsigned long long e = strtoull(dash + 1, &endptr, 10);
	if (*endptr != '\0') return -1;
	*begin = b; *end = e; return 0;
}

static int parse_rw(const char *v, bool *is_write) {
	if (strcmp(v, "read") == 0) { *is_write = false; return 0; }
	if (strcmp(v, "write") == 0) { *is_write = true; return 0; }
	return -1;
}

static double now_seconds(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static size_t get_alignment(void) {
	long p = sysconf(_SC_PAGESIZE);
	if (p <= 0) p = 4096;
	return (size_t)p;
}

static void *aligned_alloc_or_die(size_t alignment, size_t size) {
	void *ptr = NULL;
	int rc = posix_memalign(&ptr, alignment, size);
	if (rc != 0) {
		fprintf(stderr, "posix_memalign: %s\n", strerror(rc));
		exit(2);
	}
	memset(ptr, 0xA5, size); 
	return ptr;
}

int main(int argc, char **argv) {
	io_config cfg = {0};
	cfg.range_begin = 0; cfg.range_end = 0;
	cfg.use_direct = false;
	cfg.random_access = false;

	for (int i = 1; i < argc; ++i) {
		char *arg = argv[i];
		char *eq = strchr(arg, '=');
		if (!eq) { fprintf(stderr, "Invalid arg: %s\n", arg); print_usage(argv[0]); return 2; }
		*eq = '\0';
		const char *key = arg; const char *val = eq + 1;
		if (strcmp(key, "rw") == 0) {
			if (parse_rw(val, &cfg.is_write) != 0) { fprintf(stderr, "Invalid rw: %s\n", val); return 2; }
		} else if (strcmp(key, "block_size") == 0) {
			char *endptr = NULL; unsigned long long v = strtoull(val, &endptr, 10);
			if (*endptr != '\0' || v == 0) { fprintf(stderr, "Invalid block_size: %s\n", val); return 2; }
			cfg.block_size = (size_t)v;
		} else if (strcmp(key, "block_count") == 0) {
			char *endptr = NULL; unsigned long long v = strtoull(val, &endptr, 10);
			if (*endptr != '\0' || v == 0) { fprintf(stderr, "Invalid block_count: %s\n", val); return 2; }
			cfg.block_count = (uint64_t)v;
		} else if (strcmp(key, "file") == 0) {
			if (strlen(val) >= sizeof(cfg.file_path)) { fprintf(stderr, "file path too long\n"); return 2; }
			strcpy(cfg.file_path, val);
		} else if (strcmp(key, "range") == 0) {
			uint64_t b=0,e=0; if (parse_range((char*)val, &b, &e) != 0) { fprintf(stderr, "Invalid range: %s\n", val); return 2; }
			cfg.range_begin = b; cfg.range_end = e;
		} else if (strcmp(key, "direct") == 0) {
			if (parse_bool_on_off(val, &cfg.use_direct) != 0) { fprintf(stderr, "Invalid direct: %s\n", val); return 2; }
		} else if (strcmp(key, "type") == 0) {
			if (parse_type(val, &cfg.random_access) != 0) { fprintf(stderr, "Invalid type: %s\n", val); return 2; }
		} else {
			fprintf(stderr, "Unknown key: %s\n", key); print_usage(argv[0]); return 2;
		}
	}

	if (cfg.block_size == 0 || cfg.block_count == 0 || cfg.file_path[0] == '\0') {
		print_usage(argv[0]);
		return 2;
	}

	int flags = cfg.is_write ? O_WRONLY | O_CREAT : O_RDONLY;
	if (cfg.use_direct) flags |= O_DIRECT;
	int fd = open(cfg.file_path, flags, 0666);
	if (fd < 0) { perror("open"); return 1; }

	struct stat st;
	if (fstat(fd, &st) != 0) { perror("fstat"); close(fd); return 1; }
	uint64_t file_size = (uint64_t)st.st_size;

	uint64_t range_begin = cfg.range_begin;
	uint64_t range_end = cfg.range_end;
    if (range_begin == 0 && range_end == 0) {
            range_begin = 0;
            if (cfg.is_write) {
                    range_end = (uint64_t)cfg.block_size * cfg.block_count;
            } else {
                    range_end = file_size;
            }
    }
	if (range_end < range_begin) {
		fprintf(stderr, "range end < begin\n"); close(fd); return 2;
	}

	size_t alignment = cfg.use_direct ? get_alignment() : sizeof(void*);
	if (cfg.use_direct) {
		if (cfg.block_size % alignment != 0) {
			fprintf(stderr, "With O_DIRECT, block_size must be multiple of %zu bytes\n", alignment);
			close(fd); return 2;
		}
		if (range_begin % alignment != 0) {
			fprintf(stderr, "With O_DIRECT, range begin must be aligned to %zu bytes\n", alignment);
			close(fd); return 2;
		}
	}

	uint64_t usable_bytes = (range_end > range_begin) ? (range_end - range_begin) : 0;
	if (usable_bytes < cfg.block_size) {
		fprintf(stderr, "range too small for one block\n"); close(fd); return 2;
	}
    uint64_t blocks_in_range = usable_bytes / cfg.block_size;
    if (!cfg.is_write) {
            if (!cfg.random_access && cfg.block_count > blocks_in_range) {
                    cfg.block_count = blocks_in_range;
            }
    }

	void *buffer = aligned_alloc_or_die(alignment, cfg.block_size);

	double t0 = now_seconds();
	uint64_t completed = 0;
	for (uint64_t i = 0; i < cfg.block_count; ++i) {
		uint64_t block_index;
		if (cfg.random_access) {
			static uint64_t state = 0x9E3779B97F4A7C15ULL;
			state = state * 2862933555777941757ULL + 3037000493ULL;
			block_index = (state >> 33) % blocks_in_range;
		} else {
			block_index = i % blocks_in_range;
		}
		off_t offset = (off_t)(range_begin + block_index * cfg.block_size);
		ssize_t nbytes = 0;
		if (cfg.is_write) {
			nbytes = pwrite(fd, buffer, cfg.block_size, offset);
		} else {
			nbytes = pread(fd, buffer, cfg.block_size, offset);
		}
		if (nbytes < 0) { perror(cfg.is_write ? "pwrite" : "pread"); break; }
		if ((size_t)nbytes != cfg.block_size) {
			break;
		}
		completed++;
	}
	double t1 = now_seconds();

	close(fd);
	free(buffer);

	double seconds = t1 - t0;
	uint64_t total_bytes = completed * (uint64_t)cfg.block_size;
	double mb = (double)total_bytes / (1024.0 * 1024.0);
	double mbps = seconds > 0 ? (mb / seconds) : 0.0;
	fprintf(stderr, "done: blocks=%llu bytes=%llu time=%.3f s throughput=%.3f MiB/s\n",
		(unsigned long long)completed,
		(unsigned long long)total_bytes, seconds, mbps);
	return completed == cfg.block_count ? 0 : 1;
}


