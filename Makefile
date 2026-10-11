# Единая точка сборки рукописных прошивок, без GraphML и без прошивки устройств.
PYTHON ?= python3
TOOLCHAIN_BIN ?=

.DEFAULT_GOAL := build
.PHONY: build build-hub build-hub-test build-gateways build-gateway-actuator-2 build-buttons build-lmp build-lmp8 help

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

build-gateway-actuator-2:
	$(PYTHON) scripts/build.py --target gateway-actuator-2 $(TOOLCHAIN_ARGS)

build-buttons:
	$(PYTHON) scripts/build.py --target buttons $(TOOLCHAIN_ARGS)

build-lmp:
	$(PYTHON) scripts/build.py --target lmp $(TOOLCHAIN_ARGS)

# Старое имя не собирает восьмилампочницу: это alias новой цели lmp.
build-lmp8: build-lmp

help:
	@printf '%s\n' \
		'make build           — хаб, три Arduino-шлюза, кнопочница и обычная lmp' \
		'make build-hub       — только центральный хаб' \
		'make build-hub-test  — тестовый хаб: ответ 0xAA на байт SimpleBus' \
		'make build-gateways  — три Arduino-шлюза' \
		'make build-gateway-actuator-2 — второй actuator-шлюз, адрес 17' \
		'make build-buttons   — кнопочница: концевики на пинах 1 и 2' \
		'make build-lmp       — обычная лампочница фаблаб: два замка и пылесос' \
		'make build-lmp8      — устаревший alias build-lmp' \
		'TOOLCHAIN_BIN=/path/to/bin (необязательно; ARM GCC по умолчанию из PATH)' \
		'PYTHON=python3 (необязательно)'
