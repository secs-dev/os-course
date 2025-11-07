BUILD_DIR ?= build
iterations ?= 3
fragments ?= 5
input ?= input.txt
output ?= output.txt
old ?= foo
new ?= bar

.PHONY: all configure build shell cpu ema test clean
.SILENT: all clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR)

shell: 
	./$(BUILD_DIR)/bin/vtsh

cpu: 
	./$(BUILD_DIR)/cpu-calc-crc/cpu-calc-crc $(iterations) $(fragments)

ema: 
	./$(BUILD_DIR)/ema-replace-str/ema-replace-str $(input) $(output) $(old) $(new)

test:
	cd test && python3 -m unittest discover

clean:
	rm -rf $(BUILD_DIR)

