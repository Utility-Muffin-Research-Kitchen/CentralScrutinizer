SHELL := /bin/bash

APP_NAME := central-scrutinizer
LEAF_PAK_DIR_NAME := CentralScrutinizer.pak
BUILD_DIR := build
MLP1_TOOLCHAIN_IMAGE ?= ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:local
WORKSPACE_ROOT ?= $(abspath ..)
CATASTROPHE_DIR ?= $(WORKSPACE_ROOT)/Catastrophe
ADB ?= adb
SDL_AVAILABLE := $(shell pkg-config --exists sdl2 SDL2_ttf SDL2_image 2>/dev/null && echo 1 || echo 0)
ifeq ($(SDL_AVAILABLE),1)
	SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image)
	SDL_LDFLAGS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image)
	MAC_UI_CFLAGS := -DCS_ENABLE_CATASTROPHE_UI $(SDL_CFLAGS)
	MAC_UI_LDFLAGS := $(SDL_LDFLAGS) -framework Cocoa
else
	MAC_UI_CFLAGS :=
	MAC_UI_LDFLAGS :=
endif
SRC_COMMON := src/build_info.c src/paths.c src/auth.c src/session.c src/platforms.c src/states.c src/dotclean.c src/library.c src/uploads.c src/file_ops.c src/settings.c src/keep_awake.c src/ui.c
SRC_SERVER := src/daemon.c src/terminal.c src/app.c src/server.c src/routes_status.c src/routes_auth.c src/routes_helpers.c src/routes_library.c src/routes_states.c src/routes_logs.c src/routes_upload.c src/routes_file_ops.c src/routes_tools.c src/routes_jawaka.c src/jawaka_ipc.c third_party/civetweb/src/civetweb.c
SRC_VENDOR := third_party/qrcodegen.c $(CATASTROPHE_DIR)/include/cjson/cJSON.c
SRC_APP := src/main.c $(SRC_COMMON) $(SRC_SERVER) $(SRC_VENDOR)
COMMON_INCLUDES := -Iinclude -Ithird_party/civetweb/include -I$(CATASTROPHE_DIR)/include -I$(CATASTROPHE_DIR)/include/cjson -DUSE_WEBSOCKET
SQLITE_LDFLAGS ?= -lsqlite3
WEB_DEPS_STAMP := web/node_modules/next/package.json

.PHONY: all mac mlp1 package package-platform package-mlp1 do-package-leaf clean test-native test-native-all test-smoke test-all web-install web-test web-build preview preview-clear-port

mac:
	@mkdir -p $(BUILD_DIR)/mac
	cc -std=gnu11 -O0 -g -DPLATFORM_MAC -DNO_SSL -DHAVE_POLL $(COMMON_INCLUDES) $(MAC_UI_CFLAGS) \
		-o $(BUILD_DIR)/mac/$(APP_NAME) $(SRC_APP) $(MAC_UI_LDFLAGS) $(SQLITE_LDFLAGS) -lm -lpthread

all: mlp1

mlp1:
	@mkdir -p $(BUILD_DIR)/mlp1
	docker run --rm \
		-v "$(WORKSPACE_ROOT)":/workspace \
		-w /workspace/CentralScrutinizer \
		$(MLP1_TOOLCHAIN_IMAGE) \
		make -f ports/mlp1/Makefile BUILD_DIR=/workspace/CentralScrutinizer/$(BUILD_DIR)/mlp1

test-native:
	@if [ -z "$(strip $(TEST))" ]; then \
		echo "ERROR: make test-native requires TEST=tests/native/test_build_info.c" >&2; \
		exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)/tests
	cc -std=gnu11 -O0 -g -DPLATFORM_MAC -DNO_SSL $(if $(TEST_SERVER),-DCS_TESTING,) $(COMMON_INCLUDES) \
		-o $(BUILD_DIR)/tests/$(notdir $(TEST:.c=)) \
		$(TEST) $(SRC_COMMON) $(if $(TEST_SERVER),$(SRC_SERVER) $(SRC_VENDOR),) $(SQLITE_LDFLAGS) -lm -lpthread
	@./$(BUILD_DIR)/tests/$(notdir $(TEST:.c=))
	@echo "PASS $(TEST)"

test-native-all:
	@set -e; \
	for test_file in tests/native/test_*.c; do \
		echo "RUN native $$test_file"; \
		$(MAKE) test-native TEST=$$test_file TEST_SERVER=1; \
	done

test-smoke:
	@for script in tests/smoke/test_*.sh; do \
		[ "$$(basename "$$script")" = "helpers.sh" ] || bash "$$script"; \
	done

test-all:
	@$(MAKE) test-native-all
	@$(MAKE) web-test
	@$(MAKE) test-smoke

$(WEB_DEPS_STAMP): web/package.json web/package-lock.json
	npm --prefix web ci

web-install: $(WEB_DEPS_STAMP)

web-test: $(WEB_DEPS_STAMP)
	npm --prefix web test -- --run
	npm --prefix web run test:e2e

web-build: $(WEB_DEPS_STAMP)
	rm -rf web/out
	npm --prefix web run build

package-mlp1: mlp1 web-build
	@$(MAKE) do-package-leaf PLATFORM=mlp1 BIN_SRC=$(BUILD_DIR)/mlp1/$(APP_NAME)

package-local: mac web-build
	@$(MAKE) do-package-leaf PLATFORM=mac BIN_SRC=$(BUILD_DIR)/mac/$(APP_NAME)

package-platform:
	@test -n "$(PLATFORM)" || { echo "usage: make package-platform PLATFORM=<platform>" >&2; exit 1; }
	@case "$(PLATFORM)" in \
		mlp1) $(MAKE) package-mlp1 ;; \
		*) echo "unsupported CentralScrutinizer package platform: $(PLATFORM)" >&2; exit 1 ;; \
	esac

do-package-leaf:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(BIN_SRC)" ]; then \
		echo "Error: do-package-leaf requires PLATFORM and BIN_SRC."; \
		exit 1; \
	fi
	@rm -rf "$(BUILD_DIR)/$(PLATFORM)/package"
	@mkdir -p "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/bin" "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/resources/web"
	@cp "$(BIN_SRC)" "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/bin/$(APP_NAME)"
	@cp launch.sh pak.json "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/"
	@cp -a web/out/. "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/resources/web/"
	@chmod 755 "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/launch.sh" "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/bin/$(APP_NAME)"
	@diff -qr web/out "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/resources/web" >/dev/null || { \
		echo "Error: packaged web files differ from web/out for $(PLATFORM)." >&2; \
		diff -qr web/out "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)/resources/web" >&2 || true; \
		exit 1; \
	}
	@find "$(BUILD_DIR)/$(PLATFORM)/package/$(LEAF_PAK_DIR_NAME)" -maxdepth 3 -type f | sort

package: package-mlp1

PREVIEW_PORT ?= 8877
# Dev-only fixed preview PIN. Production pairing codes rotate on startup.
PREVIEW_PIN ?= 7391
PREVIEW_SDCARD ?= fixtures/mock_sdcard

preview-clear-port:
	@PIDS="$$(lsof -tiTCP:$(PREVIEW_PORT) -sTCP:LISTEN 2>/dev/null | sort -u)"; \
	if [ -n "$$PIDS" ]; then \
		echo "Stopping existing listener(s) on preview port $(PREVIEW_PORT)..."; \
		for PID in $$PIDS; do \
			CMD=$$(ps -p "$$PID" -o command= 2>/dev/null); \
			if [ -n "$$CMD" ]; then \
				echo "  $$PID $$CMD"; \
			else \
				echo "  $$PID"; \
			fi; \
		done; \
		kill $$PIDS; \
		CLEARED=0; \
		for _ in $$(seq 1 50); do \
			if ! lsof -tiTCP:$(PREVIEW_PORT) -sTCP:LISTEN >/dev/null 2>&1; then \
				CLEARED=1; \
				break; \
			fi; \
			sleep 0.1; \
		done; \
		if [ "$$CLEARED" -ne 1 ]; then \
			echo "Error: preview port $(PREVIEW_PORT) is still in use after terminating existing listener(s)." >&2; \
			lsof -nP -iTCP:$(PREVIEW_PORT) -sTCP:LISTEN >&2 || true; \
			exit 1; \
		fi; \
	fi

preview: mac web-build
	@$(MAKE) --no-print-directory preview-clear-port PREVIEW_PORT=$(PREVIEW_PORT)
	@echo "Pairing code: $(PREVIEW_PIN)"
	@echo "Open http://127.0.0.1:$(PREVIEW_PORT)"
	CS_PAIRING_CODE=$(PREVIEW_PIN) ./$(BUILD_DIR)/mac/$(APP_NAME) \
		--headless --port $(PREVIEW_PORT) --web-root web/out --sdcard $(PREVIEW_SDCARD)

clean:
	rm -rf $(BUILD_DIR)
