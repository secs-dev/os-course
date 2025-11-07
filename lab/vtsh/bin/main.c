#include <stdio.h>
#include <vtsh.h>
#include <vtsh_utils.h>

int main() {
  vtsh_setup_signal_handlers();
  (void)setvbuf(stdin, NULL, _IONBF, 0);
  print_prompt();
  vtsh_loop();
  return 0;
}
