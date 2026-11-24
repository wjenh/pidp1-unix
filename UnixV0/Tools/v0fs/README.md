# v0fs, the drum image tool

`v0fs` makes, checks and lists a Type 23 drum file that holds the file system of the 1970
PDP-7 Unix, word for word as its kernel kept it, for the PDP-1 recreation.

```
v0fs make [-k kernel.dmp] proto image
v0fs check image
v0fs ls [-b] image [path]
v0fs cat [-o] image path
```

| Command | What it does |
|---|---|
| `make` | builds `image` from the prototype `proto`, replacing any file of that name |
| `-k kernel.dmp` | also writes the kernel's am1 `-T` dump into field 31; every word must be in bank 0 |
| `check` | walks the directories from `dd`, every i-node and the free list; prints `sound` and exits 0, or prints each fault and exits 1 |
| `ls` | lists the directory `path`, or the one entry `path` names; `dd` when `path` is left out |
| `-b` | with `ls`, after each entry, its blocks, with each block's field and drum address (octal) |
| `cat` | copies the file `path` to standard output, two characters a word, high half first; a final NUL is dropped |
| `-o` | with `cat`, the file's words instead, one octal word a line |

A path is names separated by `/`, looked up from `dd`, i-number 4, as in `system/password` or
`ken/sys.rc`. The exit status is 0 on success and 1 on any error or, for `check`, any fault.

Build it with `make`; `make K=n` sets the interleave (below). It needs only a C compiler.

## The listing

```
   4 drwr-   6   -1     56 dd
  33 srw--   1   -1     38 password
```

Each line gives the i-number; the kind (`d` directory, `i` special file, `s` small file, `l`
large file) and the four permission bits (owner read and write, others read and write); the
links; the uid in octal, `-1` for the superuser; the size in words; and the name.

## The prototype

The prototype is in the format of the pdp7-unix restoration's `build/proto`, one entry a line.

```
name f[r-][w-][r-][w-] uid source [inumber]     a file
name d[r-][w-][r-][w-] uid [inumber]            a directory; its entries end at a "$" line
name i[r-][w-][r-][w-] uid inumber              a special file
name l---- inumber                              a link; -1 is the entry on the line before
```

- `#` starts a comment. Uids are octal words to 777777, or `-1` (stored as 0777776). I-numbers
  are decimal, 1 to 249.
- The first entry is the top directory, `dd`, i-number 4; its name is not stored. The end of
  the file ends any directory still open, so the restorers' file, which never closes its top
  directory, is read as it is.
- Names are 1 to 8 characters.
- Every directory begins with two entries, `dd`, naming the top directory, and `..`, naming the
  directory itself. The 1970 commands find the current directory through that `..` (`ls` with
  no name lists `..`; `stat` asks for a name's status in `..`; `dsw` and `pd` open it), and no
  kernel treats the name specially; the restorers' `mkfs7` writes the same two entries first.
  Each counts as a link.
- I-numbers 1 to 32 are kept for the fixed files (1 `core`, 3 `system`, 6 `ttyin`, 7
  `keyboard`, 8 `pptin`, 11 `ttyout`, 12 `display`, 13 `pptout`) and the terminals, which the
  prototype names with their numbers. Entries without one get 33 upward, in prototype order.

**A file's source** is read by its name's ending, relative to the prototype's directory unless
absolute.
- `.dmp` is am1's `-T` output, its test mode. The last line, the start address, is not a word.
  Every word must be in one bank. The file is the bank's words from its address 0 to its last
  stored word, gaps as zeros, so a program assembled in any bank is the same file.
- `.oct` is octal words separated by white space.
- Anything else is text, two 8-bit characters a word, high half first; an odd last character
  leaves the low half 0. Line ends are kept as they are (Unix's line end is NL, 012).

## The layout

Block b is slot b mod 64 of drum field b / 64; a block and a slot are 64 words.

| Blocks | What |
|---|---|
| 0 | the system block, `s.nxfblk`, `s.nfblks`, `s.fblks` (10), `s.uniq`, `s.tim` (2), then zeros |
| 1 | unused |
| 2-51 | i-nodes, five a block, 12 words each, i-numbers 0 to 249 (0 is never used) |
| 52-1983 | files, directories, indirect blocks and the free list |
| field 31 | the kernel, bank 0's words at the same addresses, not part of the file system |

- **The i-node** holds the flags (0400000 used, 0200000 large, 040 special, 020 directory, 010
  owner read, 04 owner write, 02 others read, 01 others write), seven block numbers, the uid,
  the link count, the size in words and the unique number. A file of more than seven blocks is
  large. Its seven words name indirect blocks of 64 block numbers each, so a file holds 28,672
  words at most.
- **Negative words are ones' complement**, the PDP-1's. A link count of n is -n, `0777777 - n`
  (one link is `0777776`), and uid -1 is `0777776`.
- **The directory entry** holds the i-number, four name words (space padded), the unique
  number, and two zeros.
- **Unique numbers.** Each i-node's is its i-number, copied into every entry naming it, and
  `s.uniq` is the highest one, so the kernel's next is new.
- **Blocks** are taken in order from 52, for each directory and file in prototype order, its
  data blocks, then any indirect blocks.
- **The free list** is ordered so that the kernel's `alloc` hands the free blocks out in
  ascending order. `alloc` takes `s.fblks` from the top down, then reads the chain block
  `s.nxfblk` names, takes its words 9 down to 1 and the chain block itself last. So the first
  (free blocks mod 10) go in `s.fblks`, lowest at the top, and each later group of ten has its
  highest block as the chain block, its word 0 naming the next chain block (0 at the end) and
  words 1 to 9 the group's other nine, highest first.

## The interleave

Consecutive blocks are not adjacent on the drum. Slot s of a field lies at word
`64 x ((s x K) mod 64)` of the field, K odd. A reader finishing one block then has K - 1 slots,
about 0.54 ms each, to start the next transfer before that block comes around, rather than
waiting a whole 35 ms revolution. K is the define `INTERLEAVE`, 27 unless `make K=n` says
otherwise. The kernel's drum driver must use the same value (`INTERLEAVE` in
`Src/kernel/unix.ah`, in octal, 033). 27 is the fewest, as measured with the kernel, that lets
it start the next transfer of a run of 64-word reads before its block comes around, with the
kernel's sequence breaks running. `v0fs` prints it with its usage.
