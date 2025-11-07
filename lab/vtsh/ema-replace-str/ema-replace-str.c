#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 8192
#define MAX_CHUNK_SIZE 1048576  // 1 MB
#define EXPECTED_ARGC 5

static size_t read_chunk(FILE* file, char* buffer, size_t buffer_size) {
  return fread(buffer, 1, buffer_size, file);
}

static void write_data(FILE* out, const char* data, size_t length) {
  (void)fwrite(data, 1, length, out);
}

static size_t replace_in_buffer(
    const char* buffer,
    size_t buffer_len,
    const char* old_str,
    size_t old_len,
    const char* new_str,
    size_t new_len,
    char* output,
    size_t output_size
) {
  size_t output_pos = 0;
  size_t idx = 0;

  while (idx < buffer_len) {
    if (idx + old_len <= buffer_len) {
      int match = 1;
      for (size_t j = 0; j < old_len; j++) {
        if (buffer[idx + j] != old_str[j]) {
          match = 0;
          break;
        }
      }

      if (match) {
        if (output_pos + new_len < output_size) {
          memcpy(output + output_pos, new_str, new_len);
          output_pos += new_len;
          idx += old_len;
          continue;
        }
      }
    }

    if (output_pos < output_size) {
      output[output_pos++] = buffer[idx++];
    } else {
      break;
    }
  }

  return output_pos;
}

int main(int argc, char* argv[]) {
  if (argc != EXPECTED_ARGC) {
    (void)fprintf(
        stderr,
        "Usage: %s <input_file> <output_file> <old_string> <new_string>\n",
        argv[0]
    );
    return 1;
  }

  const char* input_file = argv[1];
  const char* output_file = argv[2];
  const char* old_str = argv[3];
  const char* new_str = argv[4];

  size_t old_len = strlen(old_str);
  size_t new_len = strlen(new_str);

  if (old_len == 0) {
    (void)fprintf(stderr, "Old string cannot be empty\n");
    return 1;
  }

  if (old_len != new_len) {
    (void)fprintf(stderr, "Old and new strings must have the same length\n");
    return 1;
  }

  FILE* input = fopen(input_file, "r");
  if (!input) {
    (void)fprintf(stderr, "Cannot open input file: %s\n", input_file);
    return 1;
  }

  FILE* output = fopen(output_file, "w");
  if (!output) {
    (void)fprintf(stderr, "Cannot open output file: %s\n", output_file);
    (void)fclose(input);
    return 1;
  }

  char* input_buffer = malloc(BUFFER_SIZE);
  char* output_buffer = malloc(BUFFER_SIZE);
  if (!input_buffer || !output_buffer) {
    (void)fprintf(stderr, "Memory allocation failed\n");
    (void)fclose(input);
    (void)fclose(output);
    return 1;
  }

  size_t total_processed = 0;
  size_t chunk_remaining = MAX_CHUNK_SIZE;

  char* overlap_buffer = malloc(old_len * 2);
  size_t overlap_size = 0;

  while (!feof(input) && chunk_remaining > 0) {
    size_t read_size =
        (chunk_remaining < BUFFER_SIZE) ? chunk_remaining : BUFFER_SIZE;

    size_t bytes_read = read_chunk(input, input_buffer, read_size);
    if (bytes_read == 0 && feof(input)) {
      break;
    }

    if (overlap_size > 0) {
      char* combined_buffer = malloc(overlap_size + bytes_read);
      memcpy(combined_buffer, overlap_buffer, overlap_size);
      memcpy(combined_buffer + overlap_size, input_buffer, bytes_read);

      size_t combined_len = overlap_size + bytes_read;
      size_t output_len = replace_in_buffer(
          combined_buffer,
          combined_len,
          old_str,
          old_len,
          new_str,
          new_len,
          output_buffer,
          BUFFER_SIZE
      );

      write_data(output, output_buffer, output_len);

      if (combined_len > old_len - 1) {
        memcpy(
            overlap_buffer,
            combined_buffer + combined_len - old_len + 1,
            old_len - 1
        );
        overlap_buffer[old_len - 1] = combined_buffer[combined_len];
        overlap_size = old_len;
      }

      free(combined_buffer);
    } else {
      size_t output_len = replace_in_buffer(
          input_buffer,
          bytes_read,
          old_str,
          old_len,
          new_str,
          new_len,
          output_buffer,
          BUFFER_SIZE
      );
      write_data(output, output_buffer, output_len);

      if (bytes_read > old_len - 1) {
        memcpy(
            overlap_buffer, input_buffer + bytes_read - old_len + 1, old_len - 1
        );
        overlap_size = old_len - 1;
      }
    }

    total_processed += bytes_read;
    chunk_remaining -= bytes_read;
  }

  if (overlap_size > 0) {
    write_data(output, overlap_buffer, overlap_size);
  }

  free(input_buffer);
  free(output_buffer);
  free(overlap_buffer);
  (void)fclose(input);
  (void)fclose(output);

  (void
  )printf("Replacement completed. Processed %zu bytes.\n", total_processed);

  return 0;
}
