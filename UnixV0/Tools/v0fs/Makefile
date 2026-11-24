# v0fs, the drum image tool.
# K is the interleave, decimal here; it must match the kernel's INTERLEAVE in
# Src/kernel/unix.ah, which am1 reads as octal (27 is 033 there).
CC=gcc
CFLAGS=-O2 -Wall -Wextra -std=gnu11
K=27

all: v0fs

v0fs: v0fs.c
	$(CC) $(CFLAGS) -DINTERLEAVE=$(K) v0fs.c -o v0fs

clean:
	@rm -f v0fs 2>/dev/null || true
