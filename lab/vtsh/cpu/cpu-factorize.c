#include <stdio.h>
#include <stdlib.h>

size_t count(unsigned long long** res, unsigned long long arg, size_t* len) {
  unsigned long long i = 2;
  size_t count = 0;
  while (i * i < arg) {
    if (arg % i == 0) {
      if (*len - count >= 2) {
        (*res)[count] = i;
        (*res)[count + 1] = arg / i;
        count += 2;

      } else {
        *len *= 2;
        *res = realloc(*res, *len * sizeof(unsigned long long));
        (*res)[count] = i;
        (*res)[count + 1] = arg / i;
        count += 2;
      }
    }
    i++;
  }
  if (i * i == arg) {
    if (*len - count < 1) {
      *len += 1;
      *res = realloc(*res, *len * sizeof(unsigned long long));
    }
    (*res)[count] = i;
    count++;
  }
  return count;
}

int main(int argc, char* argv[]) {
  if (argv[1] == NULL) {
    return 1;
  }
  char* end;
  unsigned long long arg = strtoull(argv[1], &end, 10);
  unsigned long long* res = malloc(sizeof(unsigned long long) * 3);
  size_t len = 3;

  size_t res_len = count(&res, arg, &len);

  for (size_t i = 0; i < res_len; i++) {
    printf("%lld ", res[i]);
  }
  printf("\n");
  free(res);
  return 0;
}