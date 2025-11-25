#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <linux/sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_clone3
#error "Your libc/kernel headers do not provide SYS_clone3"
#endif

static double elapsed_sec(struct timespec a, struct timespec b) {
  long sec = b.tv_sec - a.tv_sec;
  long nsec = b.tv_nsec - a.tv_nsec;
  if (nsec < 0) {
    sec--;
    nsec += 1000000000L;
  }
  return (double)sec + (double)nsec / 1e9;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
    return 2;
  }
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  struct clone_args args = {0};
  args.flags = SIGCHLD;  

  pid_t pid = (pid_t)syscall(SYS_clone3, &args, sizeof(args));
  if (pid < 0) {
    perror("clone3");
    return 1;
  }
  if (pid == 0) {
    execvp(argv[1], &argv[1]);
    perror("execvp");
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
    return 1;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double dt = elapsed_sec(t0, t1);
  if (WIFEXITED(status)) {
    printf(
        "clone3: pid=%d exit=%d time=%.6f s\n", pid, WEXITSTATUS(status), dt
    );
    return WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    printf(
        "clone3: pid=%d signaled=%d time=%.6f s\n", pid, WTERMSIG(status), dt
    );
    return 128 + WTERMSIG(status);
  } else {
    printf("clone3: pid=%d status=0x%x time=%.6f s\n", pid, status, dt);
    return 1;
  }
}
