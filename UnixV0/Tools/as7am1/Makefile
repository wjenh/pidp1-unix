# as7am1, the translator from the 1970 PDP-7 Unix assembler's source to am1.
CC=gcc
CFLAGS=-O2 -Wall -Wextra -std=gnu11
OBJS=as7am1.o reader.o translate.o

all: as7am1

as7am1: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o as7am1

%.o: %.c as7am1.h translate.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f as7am1 $(OBJS) 2>/dev/null || true
