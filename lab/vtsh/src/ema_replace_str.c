#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static double elapsed_sec(struct timespec a, struct timespec b) {
  long sec = b.tv_sec - a.tv_sec;
  long nsec = b.tv_nsec - a.tv_nsec;
  if (nsec < 0) {
    sec--;
    nsec += 1000000000L;
  }
  return (double)sec + (double)nsec / 1e9;
}

static int generate_file(const char* path, size_t size_mb) {
  size_t total = size_mb * 1024ULL * 1024ULL;
  int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  if (ftruncate(fd, (off_t)total) != 0) {
    perror("ftruncate");
    close(fd);
    return 1;
  }
  char* ptr = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }
  unsigned x = 123456789u;
  for (size_t i = 0; i < total; i++) {
    x = x * 1103515245u + 12345u;
    unsigned r = (x >> 16) & 0x7fff;
    char c;
    if (r % 97 == 0)
      c = '\n';
    else if (r % 23 == 0)
      c = ' ';
    else
      c = 'a' + (r % 26);
    ptr[i] = c;
  }
  if (msync(ptr, total, MS_SYNC) != 0)
    perror("msync");
  munmap(ptr, total);
  close(fd);
  fprintf(stderr, "Generated %zu MB at %s\n", size_mb, path);
  return 0;
}

static size_t replace_once(
    char* buf,
    size_t n,
    size_t start,
    size_t end,
    const char* from,
    const char* to
) {
  size_t fl = strlen(from);
  size_t cnt = 0;
  if (end > n)
    end = n;
  if (start > end || fl == 0 || fl > n)
    return 0;
  for (size_t i = start; i + fl <= end;) {
    if (memcmp(buf + i, from, fl) == 0) {
      memcpy(buf + i, to, fl);
      cnt++;
      i += fl;
    } else {
      i++;
    }
  }
  return cnt;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(
        stderr,
        "Usage:\n  %s --generate <path> <size_mb>\n  %s --file <path> --from "
        "<s> --to <s> [--repeats N] [--range L-H]\n",
        argv[0],
        argv[0]
    );
    return 2;
  }
  if (!strcmp(argv[1], "--generate")) {
    if (argc != 4) {
      fprintf(stderr, "Usage: %s --generate <path> <size_mb>\n", argv[0]);
      return 2;
    }
    const char* path = argv[2];
    size_t szmb = (size_t)strtoull(argv[3], NULL, 10);
    return generate_file(path, szmb);
  }

  const char *path = NULL, *from = NULL, *to = NULL;
  int repeats = 1;
  size_t range_l = 0, range_h = (size_t)-1;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--file") && i + 1 < argc)
      path = argv[++i];
    else if (!strcmp(argv[i], "--from") && i + 1 < argc)
      from = argv[++i];
    else if (!strcmp(argv[i], "--to") && i + 1 < argc)
      to = argv[++i];
    else if (!strcmp(argv[i], "--repeats") && i + 1 < argc)
      repeats = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--range") && i + 1 < argc) {
      char* dash = strchr(argv[i + 1], '-');
      if (!dash) {
        fprintf(stderr, "--range expects L-H\n");
        return 2;
      }
      *dash = '\0';
      range_l = (size_t)strtoull(argv[i + 1], NULL, 10);
      range_h = (size_t)strtoull(dash + 1, NULL, 10);
      i++;
    }
  }
  if (!path || !from || !to || repeats <= 0) {
    fprintf(
        stderr,
        "Usage: %s --file <path> --from <s> --to <s> [--repeats N] [--range "
        "L-H]\n",
        argv[0]
    );
    return 2;
  }
  if (strlen(from) != strlen(to)) {
    fprintf(stderr, "--from and --to must have same length\n");
    return 2;
  }

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    perror("fstat");
    close(fd);
    return 1;
  }
  size_t n = (size_t)st.st_size;
  char* ptr = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  size_t total = 0;
  for (int r = 0; r < repeats; ++r) {
    size_t cnt = replace_once(
        ptr, n, range_l, range_h == 0 ? (size_t)-1 : range_h, from, to
    );
    total += cnt;
    if (msync(ptr, n, MS_SYNC) != 0)
      perror("msync");
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double dt = elapsed_sec(t0, t1);
  printf(
      "Replaced %zu occurrences in %.6f s (repeats=%d)\n", total, dt, repeats
  );

  munmap(ptr, n);
  close(fd);
  return 0;
}
