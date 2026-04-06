CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -Iinclude

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o) main.o

.PHONY: all clean

all: smash

smash: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f smash main.o src/*.o
