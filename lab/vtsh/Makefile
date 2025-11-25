CC      ?= gcc
CFLAGS  ?= -std=gnu17 -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wundef -g -O0
LDFLAGS ?= 

BIN_DIR := bin

all: $(BIN_DIR)/mysh $(BIN_DIR)/proc-clone3 $(BIN_DIR)/cpu-factorize $(BIN_DIR)/ema-replace-str

$(BIN_DIR)/mysh: src/mysh.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN_DIR)/proc-clone3: src/proc_clone3.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE $< -o $@

$(BIN_DIR)/cpu-factorize: src/cpu_factorize.c
	$(CC) $(CFLAGS) $< -o $@ -lm

$(BIN_DIR)/ema-replace-str: src/ema_replace_str.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE $< -o $@

clean:
	rm -rf $(BIN_DIR)/*.o $(BIN_DIR)/mysh $(BIN_DIR)/proc-clone3 $(BIN_DIR)/cpu-factorize $(BIN_DIR)/ema-replace-str

release:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=gnu17 -O3 -DNDEBUG'
	@echo 'Built with -O3'