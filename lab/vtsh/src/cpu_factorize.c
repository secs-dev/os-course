#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t mulmod(uint64_t a, uint64_t b, uint64_t mod) {
  __uint128_t r = (__uint128_t)a * b;
  return (uint64_t)(r % mod);
}

static uint64_t powmod(uint64_t a, uint64_t e, uint64_t mod) {
  uint64_t r = 1;
  while (e) {
    if (e & 1)
      r = mulmod(r, a, mod);
    a = mulmod(a, a, mod);
    e >>= 1;
  }
  return r;
}

static bool is_prime(uint64_t n) {
  if (n < 2)
    return false;
  static const uint64_t small[] = {
      2ULL, 3ULL, 5ULL, 7ULL, 11ULL, 13ULL, 17ULL, 19ULL, 23ULL, 0};
  for (int i = 0; small[i]; ++i)
    if (n % small[i] == 0)
      return n == small[i];
  uint64_t d = n - 1, s = 0;
  while ((d & 1) == 0) {
    d >>= 1;
    s++;
  }
  const uint64_t bases[] = {2ULL, 3ULL, 5ULL, 7ULL, 11ULL, 13ULL, 17ULL, 0};
  for (int i = 0; bases[i]; ++i) {
    uint64_t a = bases[i];
    if (a % n == 0)
      continue;
    uint64_t x = powmod(a, d, n);
    if (x == 1 || x == n - 1)
      continue;
    bool cont = false;
    for (uint64_t r = 1; r < s; ++r) {
      x = mulmod(x, x, n);
      if (x == n - 1) {
        cont = true;
        break;
      }
    }
    if (!cont)
      return false;
  }
  return true;
}

static uint64_t f_rho(uint64_t x, uint64_t c, uint64_t mod) {
  return (mulmod(x, x, mod) + c) % mod;
}

static uint64_t pollard_rho(uint64_t n) {
  if ((n & 1ULL) == 0ULL)
    return 2;
  uint64_t x, y, d, c;
  for (uint64_t seed = 1; seed < 1000; ++seed) {
    x = seed;
    y = seed;
    c = seed | 1;
    d = 1;
    while (d == 1) {
      x = f_rho(x, c, n);
      y = f_rho(f_rho(y, c, n), c, n);
      uint64_t diff = x > y ? x - y : y - x;
      uint64_t a = diff, b = n;
      while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
      }
      d = a;
      if (d == n)
        break;
    }
    if (d > 1 && d < n)
      return d;
  }
  return 0;
}

static void factor_rec(uint64_t n, uint64_t* out, size_t* sz) {
  if (n == 1)
    return;
  if (is_prime(n)) {
    out[(*sz)++] = n;
    return;
  }
  uint64_t d = pollard_rho(n);
  if (d == 0 || d == n) {
    for (uint64_t p = 3; p * p <= n; p += 2) {
      if (n % p == 0) {
        factor_rec(p, out, sz);
        factor_rec(n / p, out, sz);
        return;
      }
    }
    out[(*sz)++] = n;
    return;
  }
  factor_rec(d, out, sz);
  factor_rec(n / d, out, sz);
}

static int cmp_u64(const void* a, const void* b) {
  uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
  if (x < y)
    return -1;
  if (x > y)
    return 1;
  return 0;
}

int main(int argc, char** argv) {
  uint64_t number = 0;
  int repeats = 1;
  int quiet = 0;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--number") && i + 1 < argc) {
      number = strtoull(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--repeats") && i + 1 < argc) {
      repeats = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--quiet")) {
      quiet = 1;
    } else {
      fprintf(
          stderr,
          "Usage: %s --number <uint64> [--repeats N] [--quiet]\n",
          argv[0]
      );
      return 2;
    }
  }
  if (!number || repeats <= 0) {
    fprintf(stderr, "Provide --number and positive --repeats\n");
    return 2;
  }

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  uint64_t last = 0;
  for (int r = 0; r < repeats; ++r) {
    uint64_t fac[64];
    size_t sz = 0;
    factor_rec(number, fac, &sz);
    qsort(fac, sz, sizeof(uint64_t), cmp_u64);
    last = sz ? fac[sz - 1] : 0;
    if (!quiet) {
      printf("Run %d: factors of %" PRIu64 " ->", r + 1, number);
      for (size_t i = 0; i < sz; i++)
        printf(" %" PRIu64, fac[i]);
      printf("\n");
    }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
  printf(
      "Total time: %.6f s over %d repeats. Last-largest-factor=%" PRIu64 "\n",
      dt,
      repeats,
      last
  );
  return 0;
}
