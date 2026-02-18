#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vtsh.h"

static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    int saw_eof = 0;

    while (n + 1 < cap) {
        char c = '\0';
        ssize_t r = read(fd, &c, 1);
        if (r == 0) {
            saw_eof = 1;
            break;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;
        buf[n++] = c;
    }

    buf[n] = '\0';

    if (n + 1 >= cap) {
        char c = '\0';
        ssize_t r = 0;
        do {
            r = read(fd, &c, 1);
        } while (r > 0 && c != '\n');
    }

    if (n == 0 && saw_eof) return 0;
    return (ssize_t)n;
}

int main() {
    char command[256];

    if (getenv("VT_TEST_MODE")) {
        vtsh_set_test_mode(1);
    }

    while (1) {
        printf("%s", vtsh_prompt());
        fflush(stdout);

        ssize_t r = read_line(STDIN_FILENO, command, sizeof(command));
        if (r <= 0) break;
        vtsh_execute(command);
    }

    return 0;
}
