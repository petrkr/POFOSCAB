.PHONY: all pftd pftc upload upload-pftc clean

BUILD_DIR := build
PFTD_SRC := src/pftd/PFTD.asm
PFTD_BIN := $(BUILD_DIR)/PFTD.COM
PFTC_SRC := src/pftc/PFTC.asm
PFTC_BIN := $(BUILD_DIR)/PFTC.COM

NASM ?= nasm

# Bridge (or ESP smart-cable client) HTTP endpoint. Override HOST for a
# real ESP on the network, e.g.: make upload HOST=10.220.179.55
HOST ?= 127.0.0.1
PORT ?= 9000
# Empty by default - the bridge's own C:\ default then applies (see
# the upload target below). A trailing backslash in a Makefile
# variable is line continuation, not a literal character, so don't
# set one here; override as e.g. make upload DEST_DIR='D:\SOMEDIR\'.
DEST_DIR ?=

all: pftd pftc

pftd: $(PFTD_BIN)

pftc: $(PFTC_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

PFTD_INC := $(wildcard src/pftd/*.inc)
PFTC_INC := $(wildcard src/pftc/*.inc)

$(PFTD_BIN): $(PFTD_SRC) $(PFTD_INC) | $(BUILD_DIR)
	$(NASM) -f bin -i src/pftd/ -o $@ $<

$(PFTC_BIN): $(PFTC_SRC) $(PFTC_INC) | $(BUILD_DIR)
	$(NASM) -f bin -i src/pftc/ -o $@ $<

# Assumes the bridge (or ESP smart-cable client) is already running at
# HOST:PORT and the Portfolio is sitting in File Transfer Server mode.
# Direct curl multipart POST to the bridge's /upload endpoint (see
# mame_bridge.py's do_POST) - destDir is a query param, left off
# entirely when DEST_DIR is empty so the bridge's own C:\ default
# applies. DEST_DIR_ENC substitutes %5C for \ (DOS path separator)
# since it's going into a URL query string, not shell-quoted - Make's
# $(subst) does this without needing a subshell/sed call.
DEST_DIR_ENC = $(subst \,%5C,$(DEST_DIR))

upload: $(PFTD_BIN)
	curl -sf -F "file=@$(PFTD_BIN)" \
		"http://$(HOST):$(PORT)/upload$(if $(DEST_DIR),?destDir=$(DEST_DIR_ENC))"

# Same bridge /upload endpoint as `upload`, just for PFTC.COM - getting
# the binary onto the memory card is the same mechanism regardless of
# which wire protocol the program itself speaks once running.
upload-pftc: $(PFTC_BIN)
	curl -sf -F "file=@$(PFTC_BIN)" \
		"http://$(HOST):$(PORT)/upload$(if $(DEST_DIR),?destDir=$(DEST_DIR_ENC))"

clean:
	rm -rf $(BUILD_DIR)
