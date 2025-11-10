#include "vtsh_signals.h"

#include <signal.h>

static void ignore_signal(int sig) {
  (void)sig;
}

void vtsh_setup_signal_handlers(void) {
  (void)signal(SIGINT, ignore_signal);   // прерывание с клавиатуры (Ctrl+C)
  (void)signal(SIGTERM, ignore_signal);  // завершения процесса (kill)
}
