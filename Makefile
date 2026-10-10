# Единая точка сборки рукописных прошивок, без GraphML и без прошивки устройств.
PYTHON ?= python3
TOOLCHAIN_BIN ?=

.DEFAULT_GOAL := build
.PHONY: build build-hub build-hub-test build-gateways build-buttons build-lmp8 help

ifneq ($(strip $(TOOLCHAIN_BIN)),)
TOOLCHAIN_ARGS := --toolchain-bin "$(TOOLCHAIN_BIN)"
endif

build:
	$(PYTHON) scripts/build.py $(TOOLCHAIN_ARGS)

build-hub:
	$(PYTHON) scripts/build.py --target hub $(TOOLCHAIN_ARGS)

build-hub-test:
	$(PYTHON) scripts/build.py --target hub-test $(TOOLCHAIN_ARGS)

build-gateways:
	$(PYTHON) scripts/build.py --target gateways $(TOOLCHAIN_ARGS)

build-buttons:
	$(PYTHON) scripts/build.py --target buttons $(TOOLCHAIN_ARGS)

build-lmp8:
	$(PYTHON) scripts/build.py --target lmp8 $(TOOLCHAIN_ARGS)

help:
	@printf '%s\n' \
		'make build           — хаб, оба Arduino-шлюза, кнопочница и lmp8' \
		'make build-hub       — только центральный хаб' \
		'make build-hub-test  — тестовый хаб: ответ 0xAA на байт SimpleBus' \
		'make build-gateways  — оба Arduino-шлюза' \
		'make build-buttons   — кнопочница: концевики на пинах 1 и 2' \
		'make build-lmp8      — восьмилампочница: замки и релейная матрица' \
		'TOOLCHAIN_BIN=/path/to/bin (необязательно; ARM GCC по умолчанию из PATH)' \
		'PYTHON=python3 (необязательно)'
