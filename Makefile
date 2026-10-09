# Единая точка сборки рукописных прошивок, без GraphML и без прошивки устройств.
PYTHON ?= python3
COMPILER_ROOT ?= ../lapki-compiler
TOOLCHAIN_BIN ?=

.DEFAULT_GOAL := build
.PHONY: build build-hub build-gateways check-compiler help

ifneq ($(strip $(TOOLCHAIN_BIN)),)
TOOLCHAIN_ARGS := --toolchain-bin "$(TOOLCHAIN_BIN)"
endif

build: build-hub build-gateways

check-compiler:
	@test -d "$(COMPILER_ROOT)/compiler/library" || { \
		printf '%s\n' 'Не найден lapki-compiler. Укажите COMPILER_ROOT=/path/to/lapki-compiler (ревизия указана в README).'; \
		exit 1; \
	}

build-hub: check-compiler
	$(PYTHON) scripts/build_hub.py --compiler-root "$(COMPILER_ROOT)" $(TOOLCHAIN_ARGS)

build-gateways: check-compiler
	$(PYTHON) scripts/build_gateways.py --compiler-root "$(COMPILER_ROOT)" $(TOOLCHAIN_ARGS)

help:
	@printf '%s\n' \
		'make build           — хаб и оба Arduino-шлюза (также цель по умолчанию)' \
		'make build-hub       — только центральный хаб' \
		'make build-gateways  — оба Arduino-шлюза' \
		'COMPILER_ROOT=/path/to/lapki-compiler (по умолчанию ../lapki-compiler)' \
		'TOOLCHAIN_BIN=/path/to/bin (необязательно; ARM GCC по умолчанию из PATH)' \
		'PYTHON=python3 (необязательно)'
