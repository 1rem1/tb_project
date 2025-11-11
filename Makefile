# Makefile cho dự án ESP-IDF
# Sử dụng: make [build|flash|monitor|erase|clean] PORT=/dev/ttyUSB0 BAUD=115200

IDF_PY ?= idf.py
PORT   ?= /dev/ttyUSB0
BAUD   ?= 115200

.PHONY: all build flash monitor erase clean help

all: build

help:
	@echo "Targets:"
	@echo "  make build          # build project"
	@echo "  make flash          # flash lên thiết bị (PORT=$(PORT), BAUD=$(BAUD))"
	@echo "  make monitor        # mở serial monitor (PORT=$(PORT))"
	@echo "  make erase          # xóa flash (erase-flash)"
	@echo "  make clean          # xoá thư mục build"
	@echo "  make bfm            # build, flash và mở monitor"
	@echo "  make efm            # erase flash và mở monitor"

build:
	$(IDF_PY) build

flash:
	$(IDF_PY) -p $(PORT) -b $(BAUD) flash

monitor:
	$(IDF_PY) -p $(PORT) monitor

erase:
	$(IDF_PY) -p $(PORT) erase-flash

clean:
	@echo "Cleaning build artifacts..."
	-$(IDF_PY) fullclean 2>/dev/null || rm -rf build

bfm:
	@echo "Building and flashing..."
	$(IDF_PY) build && $(IDF_PY) -p $(PORT) -b $(BAUD) flash monitor

efm:
	@echo "Erasing flash and monitoring..."
	$(IDF_PY) -p $(PORT) erase-flash && $(IDF_PY) -p $(PORT) monitor