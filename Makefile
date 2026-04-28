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
	rm -f smash*.sqar
	rm -f smash*.fsdl

deploy: smash
	cp smash BUILD/linux-x64/smash
	ftr pack . -C smash
	ftr up smash*.sqar JFtR/smash
	rm smash*.sqar
	ftr pack . -U smash
	ftr up smash*.fsdl JFtR/smash
	rm smash*.fsdl
	ftr query JFtR/smash
