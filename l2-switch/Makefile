CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -Iinclude
SRC      = src/ethernet.c src/mactable.c src/stp.c src/switch.c src/tap.c
OBJ      = $(SRC:.c=.o)

all: l2sw

l2sw: $(OBJ) src/main.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(OBJ) tests/test_l2.o
	$(CC) $(CFLAGS) -o run_tests $(OBJ) tests/test_l2.o
	./run_tests

clean:
	rm -f $(OBJ) src/main.o tests/test_l2.o l2sw run_tests

.PHONY: all test clean
