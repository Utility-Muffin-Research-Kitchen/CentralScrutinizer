SHELL := /bin/bash

CC ?= aarch64-buildroot-linux-gnu-gcc
STRIP ?= aarch64-buildroot-linux-gnu-strip
BUILD_DIR ?= build/mlp1
CATASTROPHE_DIR ?= /workspace/Catastrophe

SRC := $(shell find src -name '*.c' -print | sort) third_party/qrcodegen.c third_party/civetweb/src/civetweb.c $(CATASTROPHE_DIR)/include/cjson/cJSON.c
HDR := $(shell find include -name '*.h' -print | sort) third_party/civetweb/include/civetweb.h $(CATASTROPHE_DIR)/include/catastrophe.h $(CATASTROPHE_DIR)/include/catastrophe_widgets.h
INCLUDES := -Iinclude -Ithird_party/civetweb/include -I$(CATASTROPHE_DIR)/include -I$(CATASTROPHE_DIR)/include/cjson
PKG_CONFIG ?= pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf SDL2_image)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf SDL2_image)

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-DPLATFORM_MLP1 -DNO_SSL -DCS_ENABLE_CATASTROPHE_UI -DUSE_WEBSOCKET -DHAVE_POLL \
	-ffunction-sections -fdata-sections \
	$(INCLUDES) $(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -Wl,--gc-sections -lsqlite3 -lpthread -lutil -lm

.PHONY: all clean

all: $(BUILD_DIR)/central-scrutinizer

$(BUILD_DIR)/central-scrutinizer: $(SRC) $(HDR)
	@mkdir -p "$(BUILD_DIR)"
	$(CC) $(CFLAGS) -o "$@" $(SRC) $(LDFLAGS)
	$(STRIP) "$@"

clean:
	rm -rf "$(BUILD_DIR)"
