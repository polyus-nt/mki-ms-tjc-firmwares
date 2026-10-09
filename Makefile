# Единая точка сборки рукописных прошивок, без GraphML и без прошивки устройств.
PYTHON ?= python3
TOOLCHAIN_BIN ?=

.DEFAULT_GOAL := build
.PHONY: build build-hub build-gateways help

ifneq ($(strip $(TOOLCHAIN_BIN)),)
TOOLCHAIN_ARGS := --toolchain-bin "$(TOOLCHAIN_BIN)"
endif

build:
	$(PYTHON) scripts/build.py $(TOOLCHAIN_ARGS)

build-hub:
	$(PYTHON) scripts/build.py --target hub $(TOOLCHAIN_ARGS)

build-gateways:
	$(PYTHON) scripts/build.py --target gateways $(TOOLCHAIN_ARGS)

help:
	@printf '%s\n' \
		'make build           — хаб и оба Arduino-шлюза (также цель по умолчанию)' \
		'make build-hub       — только центральный хаб' \
		'make build-gateways  — оба Arduino-шлюза' \
		'TOOLCHAIN_BIN=/path/to/bin (необязательно; ARM GCC по умолчанию из PATH)' \
		'PYTHON=python3 (необязательно)'
