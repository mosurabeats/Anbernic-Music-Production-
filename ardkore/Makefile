# Desktop build:  make
# Handheld build: make CC=aarch64-linux-gnu-gcc PKG_CONFIG=aarch64-linux-gnu-pkg-config
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -Wall -Wextra -Wno-format-truncation -Isrc

CORE := src/engine.c src/sample.c src/wav.c src/machine.c src/demo.c src/ps1.c \
        src/params.c src/project.c src/ui.c src/font.c

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

all: ardkore

ardkore: $(CORE) src/main.c src/*.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE) src/main.c $(SDL_LIBS) -lm

tests/test_engine: $(CORE) tests/test_engine.c src/*.h
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/test_engine.c -lm

test: tests/test_engine
	./tests/test_engine

clean:
	rm -f ardkore tests/test_engine

.PHONY: all test clean
