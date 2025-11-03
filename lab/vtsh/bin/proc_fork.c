#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *prog) {
	fprintf(stderr, "Usage: %s count=<number>\n", prog);
}

static double now_seconds(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
	uint64_t count = 0;
	for (int i = 1; i < argc; ++i) {
		char *eq = strchr(argv[i], '=');
		if (!eq) { usage(argv[0]); return 2; }
		*eq = '\0';
		const char *k = argv[i]; const char *v = eq + 1;
		if (strcmp(k, "count") == 0) {
			char *endp = NULL; unsigned long long c = strtoull(v, &endp, 10);
			if (*endp != '\0' || c == 0) { fprintf(stderr, "Invalid count: %s\n", v); return 2; }
			count = (uint64_t)c;
		} else {
			fprintf(stderr, "Unknown arg: %s\n", k); return 2;
		}
	}
	if (count == 0) { usage(argv[0]); return 2; }

	double t0 = now_seconds();
	uint64_t launched = 0, waited = 0;
	for (uint64_t i = 0; i < count; ++i) {
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			break;
		}
		if (pid == 0) {
			_exit(0);
		}
		launched++;
		int st = 0;
		if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); break; }
		waited++;
	}
	double t1 = now_seconds();

	double elapsed = t1 - t0;
	fprintf(stderr, "done: forked=%llu waited=%llu time=%.3f s rate=%.0f forks/s\n",
		(unsigned long long)launched, (unsigned long long)waited, elapsed,
		elapsed > 0 ? (double)waited / elapsed : 0.0);
	return (waited == count) ? 0 : 1;
}


