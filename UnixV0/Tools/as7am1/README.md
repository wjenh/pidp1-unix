# as7am1, the translator

`as7am1` translates a program from1970 PDP-7 Unix in PDP-7 assembler into
am1 source for the PDP-1D to run in a process bank of its Unix V0.

```
as7am1 [-o out.am1] [-r report] [-t title] [-b bank] [-F] [-N flags] file.s ...
as7am1 -L file.s ...
```

| Option | What it does |
|---|---|
| `file.s ...` | the program's source files, in the order the restorers' `build/Makefile` gives them to `tools/as7`, assembled together, as as7 does |
| `-o` | the am1 output, `a.am1` when left out |
| `-r` | the report, one line a construct, `file:line: what`, standard error when left out |
| `-t` | the am1 title line, `as7am1: ` and the first file's name when left out |
| `-b` | the bank, in octal, for a `bank` directive and `start 0N0000`, neither when left out |
| `-F` | a fragment for an `#include`, with no title line and no `start` |
| `-N` | the naive rules, in octal (`translate.h`'s `NAIVE_` flags), for testing the translator only |
| `-L` | no translation, but the memory and the global labels as `tools/as7` assembles them, in its a7out format then its list format's labels, for a comparison with as7 itself |

The exit status is 0, or 1 when the source has an as7 error or a file cannot be read, or 2
when something was reported. as7 itself exits 1 on such an error, sometimes with no message;
a forward label in a difference fails in its first pass. The am1 is written either way.

Build with `make`.
It needs only a C compiler.

## What comes out

The image of a process bank is laid out as follows.

| Words | What |
|---|---|
| 0 | `jmp` to the program's first word |
| 010-017 | the program's auto-index registers |
| 020-023 | the translator's words, `x_lnk` (the link, in its sign), `x_ac` (AC parked), `x_io` (IO parked) and `x_t` (a temporary) |
| 024-077 | free for a hand fix's words |
| 0100 | `SYSFRAME()`, the system call frame |
| 0110 | the routines' return words and the program's literals, then the program |

Each PDP-7 word becomes one am1 word or a short sequence, labeled with its as7 global label
(`u_` and the name, `.` as `_`) or its PDP-7 address (`w_` and five octal digits), so am1 lays
the program out again and every reference stays right however the words between grow.

The return words and the literals (`constants`) are written at the end of the file but placed
at 0110, ahead of the program, so that nothing lies between the program's last stored word and
the reserved words after it. A program that names a buffer by the word before it (a `law` of
the buffer's name less 1, as `init` and the shell have) still names that buffer's neighbor, as
on the PDP-7. The program's origin counts those words (`headWords`), so a hand fix adds no
literal, which would land past the count; it names a word of its own at 024-077.

A routine's return word is `r_` and its name. The second path of a rebuilt skip is `s_` and the
skip's address. The end of the program is `x_end`, and words past it (an array a program keeps
there, as `sh`'s `args`) are `x_end+n`. A name defined as a `jms` of a routine, as `fop.s`
names its routines' calls, is that routine's call. The name followed by an argument becomes a
`jda` of the routine with that argument, and the routine's first word is named `u_` and the
name (the first in alphabetical order where several name one routine).

The first word of each source line carries the line as a comment, `// file:line: text`.

## How it works

`reader.c` assembles the source as `tools/as7` does, statement for statement, keeping each
word's syllables, file and line. `translate.c` then does the following.
1. It takes the words by PDP-7 address and classifies them as code (the first syllable an
   opcode the program has not redefined, or an IOT), inline arguments (the rest of a `jms` or
   `sys` line; the word after `mul`, `idiv`, `div`), data, and reserved words (`.=.+n`).
2. It finds the routines (the targets of `jms`), their callers, and how many `isz` of the
   return word each has, so that a return goes back past the arguments and, for each such
   `isz`, one word further.
3. It follows the flow from the first word and every code word. Data reached is executed,
   which only a negative literal may be.
4. It works out, over the flow, where AC, MQ and the link are live, so that AC is parked around
   `idx` only where it is still needed, IO only where the link's word passes through it, and
   `tad` reported only where its carry is read.
5. It translates each word by its rules, and rebuilds each skip whose next word grew, from the
   last word to the first.

## The report

Each line is a construct to fix by hand or to check. These are code written or read as data,
a `jms` through a pointer or to a word that is not a return word, a routine's return word used
as a value, `xct` of a word that grew, `jmp` or `isz` through an auto-index register, `tad`'s
carry used, `hlt`, an IOT, `sysloc` and call numbers out of range, a reference to low memory,
a numeric address inside the program, a number in 010000-017777 (an address or a count?), a
difference of two places that spans code, data executed, and the flow reaching an argument.

Three more are translated wrong with nothing else to show it, and are reported by
`reportUnseen`.
- A small absolute `law` followed by `tad` (`nm.s:74`; for example `law 3; tad base`) is
  translated as the word's own bits, which break once added to a pointer. Fix it by hand where
  the sum is an address; a sum used as a character or a mask is right as it is.
- An instruction on a `jms`'s line is kept as an argument in its PDP-7 bits, though the routine
  returns to it (`roff.s:669`; for example `jms fetch; jmp i vec`, where `jmp i` is the PDP-1's
  `jsp`). A direct `jmp` there is not reported, since its bits are the PDP-1's `jmp` too (as in
  `jms fetch; jmp again`).
- A routine that returns through its first word, is named as an address by a `law` or a data
  word, and is never the target of a direct `jms` (`db`'s `proct` and `prasc`, called by
  `jms i`) is left a PDP-7 routine, with no prologue and no `r_` return. A routine no word
  names is never called, and is not reported (`fop.s`'s `sin` in `st`).
