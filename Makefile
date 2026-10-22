.PHONY: all pftd pftc upload-pftd upload-pftc clean

BUILD_DIR := build
PFTD_SRC := src/pftd/PFTD.asm
PFTD_BIN := $(BUILD_DIR)/PFTD.COM

POFO_LIB_SRC := src/pofo/tinylib.c src/pofo/pofo_box.c src/pofo/pofo_dialog.c src/pofo/pofo_edit.c src/pofo/pofo_menu.c src/pofo/pofo_screen.c

# start.c must stay first: it is the .COM entry point (see the file).
PFTC_SRC := src/pftc/start.c src/pftc/pftc.c
PFTC_SUPPORT_SRC := $(POFO_LIB_SRC) src/pofo/smartcable.c src/pftc/pftc_proto.c src/pftc/gui_core.c src/pftc/valid.c src/pftc/gui.c src/pftc/gui_iface.c src/pftc/gui_ip.c
PFTC_HEADERS := src/pofo/pofo.h src/pofo/tinylib.h src/pofo/smartcable.h src/pftc/pftc_proto.h src/pftc/gui_core.h src/pftc/gui.h src/pftc/valid.h src/pftc/gui_shared.h src/pftc/version.h src/pftc/build_id.h
PFTC_BIN := $(BUILD_DIR)/PFTC.COM

NASM ?= nasm
BCC ?= bcc

BCC_INCLUDES := -Isrc/pofo -Isrc/pftc
# Smaller calling convention for the whole PFTC build: -C-c = caller
# saves registers, -C-f = first argument passed in AX. bcc only hands
# these to the code generator, not to its separate preprocessor pass,
# so the matching macros are defined here by hand - the hand-written
# asm in tinylib.c/valid.c/pofo_screen.c picks up its arguments by
# them. libdos.a is built for the plain stack convention, which is why
# PFTC links nothing from it that takes arguments (see tinylib.h).
BCC_CODEGEN := -C-c -C-f -D__CALLER_SAVES__=1 -D__FIRST_ARG_IN_AX__=1
# -x: no libdos crt0, src/pftc/start.c is the startup instead.
BCC_LINK := -x

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

$(PFTD_BIN): $(PFTD_SRC) $(PFTD_INC) | $(BUILD_DIR)
	$(NASM) -f bin -i src/pftd/ -o $@ $<

$(PFTC_BIN): $(PFTC_SRC) $(PFTC_SUPPORT_SRC) $(PFTC_HEADERS) | $(BUILD_DIR)
	$(BCC) -Md -O $(BCC_CODEGEN) $(BCC_LINK) $(BCC_INCLUDES) -o $@ $(PFTC_SRC) $(PFTC_SUPPORT_SRC)

# Assumes the bridge (or ESP smart-cable client) is already running at
# HOST:PORT and the Portfolio is sitting in File Transfer Server mode.
# Direct curl multipart POST to the bridge's /uploadFile endpoint (see
# mame_bridge.py's do_POST) - destDir is a query param, left off
# entirely when DEST_DIR is empty so the bridge's own C:\ default
# applies. DEST_DIR_ENC substitutes %5C for \ (DOS path separator)
# since it's going into a URL query string, not shell-quoted - Make's
# $(subst) does this without needing a subshell/sed call.
DEST_DIR_ENC = $(subst \,%5C,$(DEST_DIR))

upload-pftd: $(PFTD_BIN)
	curl -sf -F "file=@$(PFTD_BIN)" \
		"http://$(HOST):$(PORT)/uploadFile$(if $(DEST_DIR),?destDir=$(DEST_DIR_ENC))"

upload-pftc: $(PFTC_BIN)
	curl -sf -F "file=@$(PFTC_BIN)" \
		"http://$(HOST):$(PORT)/uploadFile$(if $(DEST_DIR),?destDir=$(DEST_DIR_ENC))"
clean:
	rm -rf $(BUILD_DIR)
