CC = cc
CFLAGS = -std=c11 -Wall -Wextra -O2
.PHONY: all clean run
all: sim
sim: main.c systolic.c fifo.c pe.c
	$(CC) $(CFLAGS) -o $@ main.c
run: sim
	./sim
clean:
	rm -f sim
