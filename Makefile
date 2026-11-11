# Madí - Model, Align & Diagram Interconnects
# Homage to Arte Madí (Buenos Aires, 1946) — Broken Canvas, Linear Grids & Articulated Spatial Structures
# Copyright (C) 2026 Madí Contributors
# SPDX-License-Identifier: GPL-3.0-or-later

.DEFAULT_GOAL := help
.PHONY: help configure all lib libs core routing io gui cli run-gui run \
        madi madi-gui \
        test smoke-test test-core test-routing test-io test-gui test-themes \
        clean distclean install format format-check tidy lint check \
        tidy-core tidy-routing tidy-io tidy-gui \
        package deb rpm appimage docker podman

BUILD_DIR     ?= build
BUILD_TYPE    ?= Release
CMAKE         ?= cmake
CTEST         ?= ctest
CLANG_FORMAT  ?= clang-format
CLANG_TIDY    ?= clang-tidy
JOBS          ?= $(shell nproc 2>/dev/null || echo 4)
PREFIX        ?= /usr/local
CMAKE_FLAGS   ?=

help: ## Display this list of available targets (default)
	@echo "=========================================================================="
	@echo "  Madí: Model, Align & Diagram Interconnects (Homage to Arte Madí 1946)"
	@echo "=========================================================================="
	@echo ""
	@echo "Usage: make [target] [BUILD_DIR=path] [BUILD_TYPE=Release|Debug]"
	@echo ""
	@echo "Available Targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Configuration Variables:"
	@echo "  BUILD_DIR      Directory for CMake build output (current: $(BUILD_DIR))"
	@echo "  BUILD_TYPE     Build configuration (Release, Debug, RelWithDebInfo; current: $(BUILD_TYPE))"
	@echo "  JOBS           Number of parallel build threads (current: $(JOBS))"
	@echo "  PREFIX         Installation prefix path (current: $(PREFIX))"
	@echo "  CMAKE_FLAGS    Extra flags passed to CMake configure step"
	@echo "  CLANG_FORMAT   Path to clang-format executable (current: $(CLANG_FORMAT))"
	@echo "  CLANG_TIDY     Path to clang-tidy executable (current: $(CLANG_TIDY))"
	@echo "=========================================================================="

configure: ## Configure the CMake project in $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR) 2>/dev/null || true
	$(CMAKE) -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_FLAGS)

all: configure ## Build all targets (libraries, GUI application, and test suite)
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

lib: configure ## Build all domain libraries (madi-core, madi-routing, madi-io)
	$(CMAKE) --build $(BUILD_DIR) --target madi-core madi-routing madi-io -j$(JOBS)

libs: lib ## Alias for lib

core: configure ## Build Phase 1 domain library (madi-core)
	$(CMAKE) --build $(BUILD_DIR) --target madi-core -j$(JOBS)

routing: configure ## Build Phase 2 wire physics & snapping library (madi-routing)
	$(CMAKE) --build $(BUILD_DIR) --target madi-routing -j$(JOBS)

io: configure ## Build Phase 3 persistence & Draw.io interop library (madi-io)
	$(CMAKE) --build $(BUILD_DIR) --target madi-io -j$(JOBS)

gui: configure ## Build Phase 4 madi-gui Qt6 desktop application
	@if ! $(CMAKE) --build $(BUILD_DIR) --target madi-gui -j$(JOBS); then \
		echo ""; \
		echo "ERROR: Target 'madi-gui' failed to build."; \
		echo "Ensure Qt 6 development libraries are installed:"; \
		echo "  sudo apt-get install qt6-base-dev qt6-svg-dev libgl-dev"; \
		exit 1; \
	fi

madi-gui: gui ## Alias for gui

run-gui: gui ## Build and launch the madi-gui application
	@"$(BUILD_DIR)/madi-gui"

run: run-gui ## Alias for run-gui

cli: configure ## Build madi command-line executable
	$(CMAKE) --build $(BUILD_DIR) --target madi -j$(JOBS)

madi: cli ## Alias for cli

test: configure ## Build and execute test suite via CTest
	$(CMAKE) --build $(BUILD_DIR) --target CoreSmokeTest RoutingSmokeTest IoSmokeTest ThemeSmokeTest -j$(JOBS)
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

test-core: configure ## Build and execute Phase 1 CoreSmokeTest
	$(CMAKE) --build $(BUILD_DIR) --target CoreSmokeTest -j$(JOBS)
	$(CTEST) --test-dir $(BUILD_DIR) -R "^CoreSmokeTest$$" --output-on-failure

test-routing: configure ## Build and execute Phase 2 RoutingSmokeTest
	$(CMAKE) --build $(BUILD_DIR) --target RoutingSmokeTest -j$(JOBS)
	$(CTEST) --test-dir $(BUILD_DIR) -R "^RoutingSmokeTest$$" --output-on-failure

test-io: configure ## Build and execute Phase 3 IoSmokeTest
	$(CMAKE) --build $(BUILD_DIR) --target IoSmokeTest -j$(JOBS)
	$(CTEST) --test-dir $(BUILD_DIR) -R "^IoSmokeTest$$" --output-on-failure

test-gui: configure ## Run Qt6 GUI self-test in offscreen mode
	$(CMAKE) --build $(BUILD_DIR) --target madi-gui -j$(JOBS)
	@if [ -x "$(BUILD_DIR)/madi-gui" ]; then \
		QT_QPA_PLATFORM=offscreen "$(BUILD_DIR)/madi-gui" --self-test; \
	elif [ -x "$(BUILD_DIR)/bin/madi-gui" ]; then \
		QT_QPA_PLATFORM=offscreen "$(BUILD_DIR)/bin/madi-gui" --self-test; \
	else \
		find $(BUILD_DIR) -name "madi-gui" -type f -executable -exec env QT_QPA_PLATFORM=offscreen {} --self-test \; ; \
	fi

test-themes: configure ## Build and execute ThemeSmokeTest
	$(CMAKE) --build $(BUILD_DIR) --target ThemeSmokeTest -j$(JOBS)
	$(CTEST) --test-dir $(BUILD_DIR) -R "^ThemeSmokeTest$$" --output-on-failure

smoke-test: configure ## Build and execute standalone smoke tests directly
	@$(CMAKE) --build $(BUILD_DIR) --target CoreSmokeTest RoutingSmokeTest IoSmokeTest ThemeSmokeTest -j$(JOBS)
	@echo "===> Running CoreSmokeTest..."
	@if [ -x "$(BUILD_DIR)/CoreSmokeTest" ]; then "$(BUILD_DIR)/CoreSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/tests/core/CoreSmokeTest" ]; then "$(BUILD_DIR)/tests/core/CoreSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/bin/CoreSmokeTest" ]; then "$(BUILD_DIR)/bin/CoreSmokeTest"; \
	else find $(BUILD_DIR) -name "CoreSmokeTest" -type f -executable -exec {} \; ; fi
	@echo "===> Running RoutingSmokeTest..."
	@if [ -x "$(BUILD_DIR)/RoutingSmokeTest" ]; then "$(BUILD_DIR)/RoutingSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/tests/routing/RoutingSmokeTest" ]; then "$(BUILD_DIR)/tests/routing/RoutingSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/bin/RoutingSmokeTest" ]; then "$(BUILD_DIR)/bin/RoutingSmokeTest"; \
	else find $(BUILD_DIR) -name "RoutingSmokeTest" -type f -executable -exec {} \; ; fi
	@echo "===> Running IoSmokeTest..."
	@if [ -x "$(BUILD_DIR)/IoSmokeTest" ]; then "$(BUILD_DIR)/IoSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/tests/io/IoSmokeTest" ]; then "$(BUILD_DIR)/tests/io/IoSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/bin/IoSmokeTest" ]; then "$(BUILD_DIR)/bin/IoSmokeTest"; \
	else find $(BUILD_DIR) -name "IoSmokeTest" -type f -executable -exec {} \; ; fi
	@echo "===> Running ThemeSmokeTest..."
	@if [ -x "$(BUILD_DIR)/ThemeSmokeTest" ]; then "$(BUILD_DIR)/ThemeSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/tests/gui/ThemeSmokeTest" ]; then "$(BUILD_DIR)/tests/gui/ThemeSmokeTest"; \
	elif [ -x "$(BUILD_DIR)/bin/ThemeSmokeTest" ]; then "$(BUILD_DIR)/bin/ThemeSmokeTest"; \
	else find $(BUILD_DIR) -name "ThemeSmokeTest" -type f -executable -exec {} \; ; fi
	@echo "===> Smoke tests passed successfully."

format: ## Format all C++ source files and headers using clang-format
	@which $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "$(CLANG_FORMAT) not found in PATH"; exit 1; }
	@find include src tests -type f \( -name "*.hpp" -o -name "*.cpp" -o -name "*.h" -o -name "*.c" \) -exec $(CLANG_FORMAT) -i {} +
	@echo "Formatting complete."

format-check: ## Verify source code formatting without making changes
	@which $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "$(CLANG_FORMAT) not found in PATH"; exit 1; }
	@find include src tests -type f \( -name "*.hpp" -o -name "*.cpp" -o -name "*.h" -o -name "*.c" \) -exec $(CLANG_FORMAT) --dry-run --Werror {} +
	@echo "Format check passed."

tidy: configure ## Run clang-tidy static analysis across project source files
	@which $(CLANG_TIDY) >/dev/null 2>&1 || { echo "$(CLANG_TIDY) not found in PATH"; exit 1; }
	@files=$$(find src/core src/routing src/io src/gui -type f -name "*.cpp" ! -name "*mocs_compilation*" 2>/dev/null); \
	if [ -n "$$files" ]; then \
		$(CLANG_TIDY) -p $(BUILD_DIR) $$files; \
	fi

tidy-core: configure ## Run clang-tidy on Phase 1 core sources
	@which $(CLANG_TIDY) >/dev/null 2>&1 || { echo "$(CLANG_TIDY) not found in PATH"; exit 1; }
	@files=$$(find src/core -type f -name "*.cpp" ! -name "*mocs_compilation*" 2>/dev/null); \
	if [ -n "$$files" ]; then $(CLANG_TIDY) -p $(BUILD_DIR) $$files; fi

tidy-routing: configure ## Run clang-tidy on Phase 2 routing sources
	@which $(CLANG_TIDY) >/dev/null 2>&1 || { echo "$(CLANG_TIDY) not found in PATH"; exit 1; }
	@files=$$(find src/routing -type f -name "*.cpp" ! -name "*mocs_compilation*" 2>/dev/null); \
	if [ -n "$$files" ]; then $(CLANG_TIDY) -p $(BUILD_DIR) $$files; fi

tidy-io: configure ## Run clang-tidy on Phase 3 io sources
	@which $(CLANG_TIDY) >/dev/null 2>&1 || { echo "$(CLANG_TIDY) not found in PATH"; exit 1; }
	@files=$$(find src/io -type f -name "*.cpp" ! -name "*mocs_compilation*" 2>/dev/null); \
	if [ -n "$$files" ]; then $(CLANG_TIDY) -p $(BUILD_DIR) $$files; fi

tidy-gui: configure ## Run clang-tidy on Phase 4 gui sources
	@which $(CLANG_TIDY) >/dev/null 2>&1 || { echo "$(CLANG_TIDY) not found in PATH"; exit 1; }
	@files=$$(find src/gui -type f -name "*.cpp" ! -name "*mocs_compilation*" 2>/dev/null); \
	if [ -n "$$files" ]; then $(CLANG_TIDY) -p $(BUILD_DIR) $$files; fi

lint: tidy ## Alias for tidy

check: format-check test ## Run format check and full test suite

clean: ## Clean compiled object files and binaries in $(BUILD_DIR)
	@if [ -d "$(BUILD_DIR)" ]; then $(CMAKE) --build $(BUILD_DIR) --target clean; fi

distclean: ## Remove entire $(BUILD_DIR) directory and artifacts
	rm -rf $(BUILD_DIR)

install: configure ## Install binaries and headers to $(PREFIX)
	$(CMAKE) --install $(BUILD_DIR) --prefix $(PREFIX)

package: all ## Generate CPack binary packages (DEB, RPM, TGZ)
	@cd $(BUILD_DIR) && cpack

deb: all ## Generate Debian / Ubuntu (.deb) binary package
	@cd $(BUILD_DIR) && cpack -G DEB

rpm: all ## Generate Red Hat / Fedora (.rpm) binary package
	@cd $(BUILD_DIR) && cpack -G RPM

appimage: ## Build standalone Linux AppImage bundle
	@chmod +x packaging/appimage/build-appimage.sh
	./packaging/appimage/build-appimage.sh

docker: ## Build multi-stage OCI container image with Docker
	docker build -t madi:latest -f packaging/docker/Dockerfile .

podman: ## Build multi-stage OCI container image with Podman
	podman build -t madi:latest -f packaging/docker/Dockerfile .
