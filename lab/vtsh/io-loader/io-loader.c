#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_SIZE 4096

struct params {
  char rw;
  long block_size;
  long block_count;
  char* file;
  long range_min;
  long range_max;
  bool direct;
  char type;
};

void random_bytes(void* buf, size_t size) {
  unsigned char* p = buf;
  for (size_t i = 0; i < size; i++)
    p[i] = (rand() % (126 - 32 + 1)) + 32;
}

struct params fill_params(char** args) {
  char* del = "=";
  char* end;
  struct params params = {0};
  for (size_t i = 1; i < 8; i++) {
    char* param = strtok(args[i], del);
    char* value = strtok(NULL, del);

    if (strcmp(param, "rw") == 0) {
      if (strcmp(value, "read") == 0) {
        params.rw = 'r';
      } else if (strcmp(value, "write") == 0) {
        params.rw = 'w';
      }
    }

    if (strcmp(param, "block_size") == 0) {
      long val = strtol(value, &end, 10);
      if (value != end && errno != ERANGE) {
        params.block_size = val;
      }
    }

    if (strcmp(param, "block_count") == 0) {
      long val = strtol(value, &end, 10);
      if (value != end && errno != ERANGE) {
        params.block_count = val;
      }
    }

    if (strcmp(param, "file") == 0) {
      params.file = value;
    }

    if (strcmp(param, "range") == 0) {
      char* range_del = "-";
      char* range_min = strtok(value, range_del);
      char* range_max = strtok(NULL, range_del);

      long min = strtol(range_min, &end, 10);
      if (range_min != end && errno != ERANGE) {
        params.range_min = min;
      }

      long max = strtol(range_max, &end, 10);
      if (range_max != end && errno != ERANGE) {
        params.range_max = max;
      }
    }

    if (strcmp(param, "direct") == 0) {
      if (strcmp(value, "on") == 0) {
        params.direct = true;
      } else if (strcmp(value, "off") == 0) {
        params.direct = false;
      }
    }

    if (strcmp(param, "type") == 0) {
      if (strcmp(value, "sequence") == 0) {
        params.type = 's';
      } else if (strcmp(value, "random") == 0) {
        params.type = 'r';
      }
    }
  }

  return params;
}

void print_params(const struct params* p) {
  printf("rw: %c\n", p->rw);
  printf("block_size: %ld\n", p->block_size);
  printf("block_count: %ld\n", p->block_count);
  printf("file: %s\n", p->file ? p->file : "(null)");
  printf("range_min: %ld\n", p->range_min);
  printf("range_max: %ld\n", p->range_max);
  printf("direct: %s\n", p->direct ? "true" : "false");
  printf("type: %c\n", p->type);
}

bool is_valid(const struct params* p) {
  if (p->rw == 0) {
    return false;
  }
  if (p->block_size < 1) {
    return false;
  }
  if (p->block_count < 1) {
    return false;
  }
  if (p->file == NULL) {
    return false;
  }
  if (p->range_min < 0) {
    return false;
  }
  if (p->range_max < 0) {
    return false;
  }
  if ((p->range_min != 0) && (p->range_min >= p->range_max)) {
    return false;
  }
  if (p->type == 0) {
    return false;
  }
  return true;
}

bool check_range(struct params* params) {
  if (params->range_max - params->range_min + 1 <
      params->block_size * params->block_count) {
    printf("Range is smaller than you need to read\n");
    return false;
  }
  return true;
}

bool sequense_read(int file_descr, long block_size) {
  char buf[block_size + 1];
  int res = read(file_descr, &buf, block_size);
  if (res != -1) {
    buf[block_size] = '\0';
    printf("%s\n", buf);
  } else {
    printf("%s\n", strerror(errno));
  }
  return (res != -1);
}

bool random_read(
    int file_descr,
    long block_size,
    long block_count,
    long range_min,
    long range_max
) {
  char buf[block_size + 1];
  int position;

  if (range_min == 0 && range_max == 0) {
    long max = block_size * block_count - block_size;
    position = rand() % max;
  } else {
    position = range_min + rand() % range_max;
  }

  off_t offset_res = lseek(file_descr, position, SEEK_SET);

  if (offset_res == -1)
    return false;

  int res = read(file_descr, &buf, block_size);
  if (res != -1) {
    buf[block_size] = '\0';
    printf("%s\n", buf);
  } else {
    printf("%s\n", strerror(errno));
  }
  return (res != -1);
}

bool sequense_write(int file_descr, long block_size) {
  char buf[block_size];
  random_bytes(buf, block_size);
  int res = write(file_descr, &buf, block_size);
  if (res != -1) {
    printf("Succesfull writing\n");
  } else {
    printf("%s\n", strerror(errno));
  }
  return (res != -1);
}

bool random_write(
    int file_descr,
    long block_size,
    long block_count,
    long range_min,
    long range_max
) {
  char buf[block_size + 1];
  random_bytes(&buf, block_size);
  int position;
  if (range_min == 0 && range_max == 0) {
    long max = block_size * block_count - block_size;
    position = rand() % max;
  } else {
    position = range_min + rand() % range_max;
  }

  off_t offset_res = lseek(file_descr, position, SEEK_SET);

  if (offset_res == -1)
    return false;

  int res = write(file_descr, &buf, block_size);
  if (res != -1) {
    printf("Succesfull writing\n");
  } else {
    printf("%s\n", strerror(errno));
  }
  return (res != -1);
}

bool rw_simple(int file_descr, struct params* params) {
  if (params->rw == 'r') {
    if (params->range_min == 0 && params->range_max == 0) {
      if (params->type == 's') {
        for (long i = 0; i < params->block_count; i++) {
          bool res = sequense_read(file_descr, params->block_size);
          if (!res)
            return false;
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          bool res = random_read(
              file_descr, params->block_size, params->block_count, 0, 0
          );
          if (!res)
            return false;
        }
      }

    } else {
      if (!check_range(params)) {
        return false;
      }

      if (params->type == 's') {
        off_t offset_res = lseek(file_descr, params->range_min, SEEK_SET);

        if (offset_res == -1)
          return false;
        for (long i = 0; i < params->block_count; i++) {
          bool res = sequense_read(file_descr, params->block_size);
          if (!res)
            return false;
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          bool res = random_read(
              file_descr,
              params->block_size,
              params->block_count,
              params->range_min,
              params->range_max
          );
          if (!res)
            return false;
        }
      }
    }
  } else {
    if (params->range_min == 0 && params->range_max == 0) {
      if (params->type == 's') {
        for (long i = 0; i < params->block_count; i++) {
          bool res = sequense_write(file_descr, params->block_size);
          if (!res)
            return false;
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          bool res = random_write(
              file_descr, params->block_size, params->block_count, 0, 0
          );
          if (!res)
            return false;
        }
      }

    } else {
      if (!check_range(params)) {
        return false;
      }
      if (params->type == 's') {
        off_t offset_res = lseek(file_descr, params->range_min, SEEK_SET);

        if (offset_res == -1)
          return false;
        for (long i = 0; i < params->block_count; i++) {
          bool res = sequense_write(file_descr, params->block_size);
          if (!res)
            return false;
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          bool res = random_write(
              file_descr,
              params->block_size,
              params->block_count,
              params->range_min,
              params->range_max
          );
          if (!res)
            return false;
        }
      }
    }
  }
  return true;
}

bool check_range_direct(struct params* params) {
  int min = params->range_min;
  if (params->range_min % params->block_size != 0) {
    min = ((params->range_min / params->block_size) + 1) * params->block_size;
  }
  int count = params->range_max - min + 1;
  if (params->range_max % params->block_size != 0) {
    int max = (params->range_min / params->block_size) * params->block_size;
    count = (max - min) / params->block_size;
  }
  return (count >= params->block_count);
}

bool direct_sequense_read(
    int file_descr, void** buf, long block_size, off_t offset
) {
  ssize_t n = pread(file_descr, *buf, block_size, offset);
  if (n < 0) {
    fprintf(stderr, "pread failed: %s\n", strerror(errno));
  } else {
    printf("Read %zd bytes from offset %ld\n", n, (long)offset);
  }
}

bool direct_random_read(
    int file_descr, void** buf, long block_size, int range_min, int range_max
) {
  struct stat st;
  if (fstat(file_descr, &st) != 0) {
    perror("fstat");
    return false;
  }
  off_t fsize = st.st_size;

  long max = fsize / block_size;
  off_t offset;

  if (range_max == 0 && range_min == 0) {
    offset = (rand() % max) * block_size;
  } else {
    offset = (range_min + rand() % range_max) * block_size;
  }

  ssize_t n = pread(file_descr, *buf, block_size, offset);
  if (n < 0) {
    fprintf(stderr, "pread failed: %s\n", strerror(errno));
  } else {
    printf("Read %zd bytes from offset %ld\n", n, (long)offset);
    return false;
  }
  return true;
}

bool direct_sequense_write(
    int file_descr, void** buf, long block_size, off_t offset
) {
  random_bytes(*buf, block_size);
  ssize_t n = pwrite(file_descr, *buf, block_size, offset);
  if (n < 0) {
    fprintf(stderr, "pwrite failed: %s\n", strerror(errno));
    return false;
  } else {
    printf("Write %zd bytes\n", n);
    return true;
  }
}

bool direct_random_write(
    int file_descr, void** buf, long block_size, int range_min, int range_max
) {
  struct stat st;
  if (fstat(file_descr, &st) != 0) {
    perror("fstat");
    return false;
  }
  off_t fsize = st.st_size;

  long max = fsize / block_size;
  off_t offset;

  if (range_max == 0 && range_min == 0) {
    offset = (rand() % max) * block_size;
  } else {
    offset = (range_min + rand() % range_max) * block_size;
  }

  random_bytes(*buf, block_size);
  ssize_t n = pwrite(file_descr, *buf, block_size, offset);
  if (n < 0) {
    fprintf(stderr, "pwrite failed: %s\n", strerror(errno));
  } else {
    printf("Write %zd bytes\n", n);
  }
  return true;
}

bool rw_direct(int file_descr, struct params* params) {
  if (params->block_size % BLOCK_SIZE != 0) {
    printf("Block size must be a multiple of 512\n");
    return false;
  }

  void* buf;
  int res = posix_memalign(&buf, BLOCK_SIZE, params->block_size);
  if (res != 0) {
    printf("posix_memalign failed: %s\n", strerror(res));
    return false;
  }

  off_t offset;

  if (params->rw == 'r') {
    if (params->range_min == 0 && params->range_max == 0) {
      if (params->type == 's') {
        for (long i = 0; i < params->block_count; i++) {
          offset = i * params->block_size;
          direct_sequense_read(file_descr, &buf, params->block_size, offset);
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          direct_random_read(file_descr, &buf, params->block_size, 0, 0);
        }
      }
    } else {
      if (!check_range_direct(params))
        return false;
      off_t min =
          ((params->range_min / params->block_size) + 1) * params->block_size;
      if (params->type == 's') {
        for (long i = 0; i < params->block_count; i++) {
          offset = min + i * params->block_size;
          direct_sequense_read(file_descr, &buf, params->block_size, offset);
        }
      } else {
        int min = (params->range_min / params->block_size) + 1;
        int max = params->range_max / params->block_size;
        for (long i = 0; i < params->block_count; i++) {
          direct_random_read(file_descr, &buf, params->block_size, min, max);
        }
      }
    }
  } else {
    if (params->range_min == 0 && params->range_max == 0) {
      if (params->type == 's') {
        for (long i = 0; i < params->block_count; i++) {
          offset = i * params->block_size;
          bool res = direct_sequense_write(
              file_descr, &buf, params->block_size, offset
          );
          if (!res)
            return false;
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          direct_random_write(file_descr, &buf, params->block_size, 0, 0);
        }
      }
    } else {
      if (!check_range_direct(params))
        return false;
      off_t min =
          ((params->range_min / params->block_size) + 1) * params->block_size;
      if (params->type == 's') {
        for (long i = 0; i < params->block_count; i++) {
          offset = min + i * params->block_size;
          bool res = direct_sequense_write(
              file_descr, &buf, params->block_size, offset
          );
          if (!res)
            return false;
        }
      } else {
        for (long i = 0; i < params->block_count; i++) {
          int min = (params->range_min / params->block_size) + 1;
          int max = params->range_max / params->block_size;
          direct_random_write(file_descr, &buf, params->block_size, 0, 0);
        }
      }
    }
  }

  return true;
}

int main(int arg, char** args) {
  srand(time(NULL));

  if (args[1] == NULL || args[2] == NULL || args[3] == NULL ||
      args[4] == NULL || args[5] == NULL || args[6] == NULL ||
      args[7] == NULL) {
    printf("Not enough args\n");
    return 1;
  }

  struct params params = fill_params(args);
  print_params(&params);

  if (!is_valid(&params)) {
    printf("Invalid args\n");
    return 1;
  }

  int flags = 0;
  if (params.rw == 'r')
    flags = O_RDONLY;
  if (params.rw == 'w')
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  if (params.direct)
    flags |= O_DIRECT;

  mode_t mode = 0644;
  int file_descr = open(params.file, flags, mode);
  if (file_descr < 0) {
    printf("%s\n", strerror(errno));
    return 1;
  }

  if (params.direct) {
    rw_direct(file_descr, &params);
  } else {
    rw_simple(file_descr, &params);
  }

  close(file_descr);
  return 0;
}