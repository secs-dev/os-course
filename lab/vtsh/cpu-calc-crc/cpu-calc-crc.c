#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FRAGMENTS 1000
#define MAX_FRAGMENT_LENGTH 10000
#define CRC_TABLE_SIZE 256
#define CRC_POLYNOMIAL 0xEDB88320U
#define CRC_BITS_PER_BYTE 8
#define CRC_INIT_VALUE 0xFFFFFFFFU
#define CRC_MASK 0xFFU
#define MIN_FRAGMENT_LENGTH 100
#define ASCII_START 32  // пробел
#define ASCII_PRINTABLE_COUNT 95
#define BYTE_SHIFT 8U
#define BASE_10 10

// const NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint32_t crc_table[CRC_TABLE_SIZE];

static void init_crc_table() {
  uint32_t polynomial = CRC_POLYNOMIAL;
  for (int i = 0; i < CRC_TABLE_SIZE; i++) {
    // подаю байт i на вход и 8 раз прогоняю
    // сдвиг/полином
    uint32_t crc = (uint32_t)i;
    for (int j = 0; j < CRC_BITS_PER_BYTE; j++) {
      if (crc & 1U) {
        // если младший бит 1, то после сдвига нужно применить полином
        crc = (crc >> 1U) ^ polynomial;
      } else {
        // иначе просто сдвиг вправо
        crc >>= 1U;
      }
    }
    crc_table[i] = crc;
  }
}

static uint32_t calculate_crc32(const char* data, size_t length) {
  // стартуем с 0xFFFFFFFF для CRC-32:
  uint32_t crc = CRC_INIT_VALUE;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = (uint8_t)data[i];
    // основной шаг CRC:
    // 1) смешиваем текущий CRC с новым байтом (crc ^ byte)
    // 2) берём младшие 8 бит как индекс в таблицу (это остаток от деления)
    // 3) сдвигаем crc на байт и подставляем предрасчитанный остаток
    crc = (crc >> BYTE_SHIFT) ^ crc_table[(crc ^ byte) & CRC_MASK];
  }
  // финальная инверсия по стандарту CRC-32 
  return crc ^ CRC_INIT_VALUE;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    (void)fprintf(stderr, "Usage: %s <iterations> <fragment_count>\n", argv[0]);
    return 1;
  }

  char* endptr = NULL;
  errno = 0;
  long iterations_long = strtol(argv[1], &endptr, BASE_10);
  if (errno != 0 || *endptr != '\0' || iterations_long <= 0) {
    (void)fprintf(stderr, "Invalid iterations parameter\n");
    return 1;
  }

  errno = 0;
  endptr = NULL;
  long fragment_count_long = strtol(argv[2], &endptr, BASE_10);
  if (errno != 0 || *endptr != '\0' || fragment_count_long <= 0) {
    (void)fprintf(stderr, "Invalid fragment_count parameter\n");
    return 1;
  }

  int iterations = (int)iterations_long;
  int fragment_count = (int)fragment_count_long;

  if (iterations <= 0 || fragment_count <= 0) {
    (void)fprintf(stderr, "Invalid parameters\n");
    return 1;
  }

  init_crc_table();

  char** fragments = (char**)malloc((size_t)fragment_count * sizeof(char*));
  if (!fragments) {
    (void)fprintf(stderr, "Memory allocation failed\n");
    return 1;
  }

  // NOLINTNEXTLINE
  srand((unsigned int)time(NULL));

  for (int i = 0; i < fragment_count; i++) {
    fragments[i] = (char*)malloc(MAX_FRAGMENT_LENGTH);
    if (!fragments[i]) {
      (void)fprintf(stderr, "Memory allocation failed\n");
      return 1;
    }

    // NOLINTNEXTLINE
    int length = (rand() % (MAX_FRAGMENT_LENGTH - MIN_FRAGMENT_LENGTH)) +
                 MIN_FRAGMENT_LENGTH;
    for (int j = 0; j < length; j++) {
      // NOLINTNEXTLINE
      fragments[i][j] = (char)(ASCII_START + (rand() % ASCII_PRINTABLE_COUNT));
    }
    fragments[i][length] = '\0';
  }

  uint32_t final_crc = CRC_INIT_VALUE;

  printf(
      "Starting CRC calculation with %d iterations and %d fragments\n",
      iterations,
      fragment_count
  );

  for (int iter = 0; iter < iterations; iter++) {
    char* concatenated =
        (char*)malloc((size_t)fragment_count * MAX_FRAGMENT_LENGTH);
    if (!concatenated) {
      (void)fprintf(stderr, "Memory allocation failed\n");
      return 1;
    }

    concatenated[0] = '\0';

    for (int i = 0; i < fragment_count; i++) {
      // NOLINTNEXTLINE
      int random_index = rand() % fragment_count;
      strcat(concatenated, fragments[random_index]);
    }

    size_t len = strlen(concatenated);

    uint32_t crc = calculate_crc32(concatenated, len);

    final_crc ^= crc;

    free(concatenated);
  }

  printf("Final CRC: 0x%08X\n", final_crc);

  for (int i = 0; i < fragment_count; i++) {
    free(fragments[i]);
  }
  free((void*)fragments);

  return 0;
}
