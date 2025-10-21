#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct table {
  int* id;
  char** string;
  size_t len;
};

struct element {
  int id;
  char* str;
};

uint32_t simple_hash(int x) {
  return (uint32_t)(x * 2654435761u);
}

struct table read_table(FILE* file) {
  size_t len;
  fscanf(file, "%zu", &len);
  struct table table = {
      .id = malloc(sizeof(int) * len),
      .string = malloc(sizeof(char*) * len),
      .len = len
  };
  char* str;
  int id;
  for (int i = 0; i < len; i++) {
    fscanf(file, "%d", &table.id[i]);
    str = malloc(sizeof(char) * 8);
    fscanf(file, "%s", str);
    table.string[i] = str;
    // table.id[i] = id;
    // table.string[i] = str;
  }
  return table;
}

void print_table(struct table* table) {
  for (int i = 0; i < table->len; i++) {
    printf("%d %s\n", table->id[i], table->string[i]);
  }
  printf("\n");
}

void free_table(struct table* table) {
  for (int i = 0; i < table->len; i++) {
    free(table->string[i]);
  }
  free(table->string);
  free(table->id);
}

struct element* hashtable_creating(struct table* table) {
  struct element* hashtable = calloc(table->len, sizeof(struct element));

  for (size_t i = 0; i < table->len; i++) {
    struct element element = {.id = table->id[i], .str = table->string[i]};
    uint32_t num = simple_hash(table->id[i]) % table->len;

    if (hashtable[num].id == 0) {
      hashtable[num] = element;
    } else {
      uint32_t j = num;

      while (hashtable[j].id != 0) {
        j++;
        if (j == table->len) {
          j = 0;
        }
      }

      hashtable[j] = element;
    }
  }
  return hashtable;
}

char* compare(struct element* element, struct element* hashtable, size_t* len) {
  uint32_t num = simple_hash(element->id) % *len;
  if (hashtable[num].id != 0) {
    if (hashtable[num].id == element->id) {
      return hashtable[num].str;
    }
    uint32_t j = num;
    while (hashtable[j].id != element->id) {
      j++;
      if (j >= *len) {
        j = 0;
      }
      if (j == num) {
        return NULL;
      }
    }
    return hashtable[j].str;
  }
  return NULL;
}

int main(int arg, char* args[]) {
  if (args[1] == NULL || args[2] == NULL) {
    return 1;
  }

  FILE* fptr;

  fptr = fopen(args[1], "r");
  if (fptr == NULL) {
    printf("Can not open file %s\n", args[1]);
    return 1;
  }

  struct table table1 = read_table(fptr);
  // print_table(&table1);
  fclose(fptr);

  fptr = fopen(args[2], "r");
  if (fptr == NULL) {
    printf("Can not open file %s\n", args[2]);
    return 1;
  }

  struct table table2 = read_table(fptr);
  // print_table(&table2);
  fclose(fptr);

  struct element* hashtable = hashtable_creating(&table1);

  //   for (int i = 0; i < table1.len; i++) {
  //     printf("%d %d %s\n", i, hashtable[i].id, hashtable[i].str);
  //   }

  char* res_filename = "result.txt";
  fptr = fopen(res_filename, "w+");

  for (size_t i = 0; i < table2.len; i++) {
    struct element element = {.id = table2.id[i], .str = table2.string[i]};
    char* res_str = compare(&element, hashtable, &table1.len);
    if (res_str != NULL) {
      fprintf(fptr, "%s %s\n", res_str, element.str);
      printf("%s %s\n", res_str, element.str);
    }
  }

  return 0;
}