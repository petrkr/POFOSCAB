.PHONY: all pftd clean

BUILD_DIR := build
PFTD_SRC := src/pftd/PFTD.asm
PFTD_BIN := $(BUILD_DIR)/PFTD.COM

NASM ?= nasm

all: pftd

pftd: $(PFTD_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PFTD_BIN): $(PFTD_SRC) | $(BUILD_DIR)
	$(NASM) -f bin -i src/pftd/ -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
