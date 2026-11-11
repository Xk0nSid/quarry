.PHONY: all clean run test example

CFLAGS := -fsanitize=address -g -std=c99 -Wall -Werror -pedantic

SOURCES :=

HEADERS := src/quarry.h

all: build build/app

clean:
	@rm -rf build

example: build/example
	@./build/example

build/example: src/example.c
	@clang -o $@ $< $(CFLAGS)

build:
	@mkdir -p build

build/app: src/main.c $(SOURCES) $(HEADERS)
	@clang -o $@ $< $(SOURCES) $(CFLAGS)

run: build build/app
	@./build/app
