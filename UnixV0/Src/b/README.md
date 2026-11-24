# B on the PDP-1

B, compiled to PDP-1 instructions; 1970's `b` wrote threaded code.
The language and the library are the 1970 verson.
Where the system differs from 1970's B is documented in the release `README.md` under "Departures from 1970".

| File | What |
|---|---|
| `bn.c` | the restorers' `b.c` with its generator replaced by the native one |
| `bop.s` | `..=0`, then the PDP-1's names for `as`, the instructions, `i`, `sys` and the system calls |
| `brt.s` | the run time (the start, `bent` and `bret`, `bmul`, `bdiv` and `bmod`) and 1970's library (`read`, `write`, `flush`, `array`, `fin`, `fout`) |
| `b1.b` | the PDP-1's `b`, pass 1, the restorers' lexer and parser, writing the generator's calls to `btmp` |
| `b2.b` | pass 2, `bx` in `system`, `bn.c`'s generator in B, reading `btmp` and writing `x.s` |
| `bx.s` | the link between the passes, assembled after `brt.s` in both, which makes `btmp`, reads `bx` over pass 1 and removes `btmp` |

## Using B

On the PDP-1 a program is compiled, assembled and run by

```
b x.b x.s
as bop.s brt.s x.s
a.out
```

`b`, `bx`, `bop.s` and `brt.s` are in `system`; `bop.s` and `brt.s` are linked into the user's
directory via `ln system bop.s`, etc.
A program in more than one source is compiled a file at a time and assembled together,
as in `as bop.s brt.s pr.s x.s`.
`b` prints nothing on a valid source, each error as a code and a line (`ex 3`), at most 20 for each pass.
`as1` is not run on the result.
It rejects it, printing `a.out is not PDP-7 code`.

On the host, these commands do the same with `bn`, which writes what `b` writes, byte for byte,
and `as7`, the restorers' assembler in the release's `Tools/`.

```
gcc -std=gnu89 -w -o bn bn.c
./bn x.b x.s
perl as7 -o x.a7 bop.s brt.s x.s
```

## Building

`b` and `bx` are built by the release's `Src/build.sh`.
`bn` compiles `b1.b` and `b2.b`, and `as7` assembles each after `bop.s`, `brt.s` and `bx.s`.
`b` is pass 1's image from word 0, and `bx` pass 2's words from `bgo` on,
which pass 1 reads over itself.
The two images hold the same words below `bgo` but word 020, `bsp`, each pass's end.
Each pass's stack runs from its end up to `brt.s`'s buffers at 07570, and `build.sh`
requires room for 200 words in pass 1 and 96 in pass 2.

## The code

`brt.s` has no comment past its first line, since `as` on the PDP-1 takes about 5.9 ms a character.
This section is its documentation.

- **The image.**
  `brt.s` puts `jmp bstart` at word 0, its words from 020, and the call frame at 0100.
  `bstart` is at 0110, then the run time and the library, then the program.
  `b` ends each output with `bend=.`, where the stack starts.
- **The start.**
  `bstart` opens the files as 1970's `initio` does.
  With no argument the program reads and writes the terminal; `x` opens `x` for reading,
  `x y` also creates `y` for writing.
  It takes two 64-word buffers below `lastv`, then calls `main`.
  `bstop` flushes and exits.
- **A function** is `.f: .+1`, `jda bent`, `4f`.
  Its descriptor, `4:`, holds N, F, and then the N fixed words, the parameters and autos from `4f+2`,
  then the temporaries.
  F is the number of frame words its auto vectors take.
- **A call** pushes each argument (`dac bsp i; idx bsp`), then does `jsp .f i` (or through any
  word), followed by the count.
  `bent` saves the N fixed words on the stack, pushes the frame pointer `bfp`,
  the descriptor's word and the return, sets `bfp`, reserves F words, and copies
  the arguments, at most N, into the first fixed words.
  `jmp bret`, with the value in AC, restores all of it, pops the arguments, and returns past the count.
  `return` with no value returns AC as it is.
- **Autos.**
  A scalar auto is its fixed word, saved and restored by each call, and `&x` gives that word.
  An auto vector's words are in the activation's frame and its fixed word points to them, set at entry.
- **Labels.**
  A label is `l<n>=.`.
  A backward jump is `jmp l<n>`, a forward one `jmp 1f+n i` through the function's label table `1:`, written at its end.
- **Constants** are written as octal ones' complement bits (`-1` as 0777776), never with a `-`
  for `as` to evaluate, since `as` adds in two's complement.
  Each is `law n` or `law i n` where it fits, or else a word of the function's literal pool `3:`,
  at most 100, error `lt` if exhausted.
- **Strings** are `law .+2; jmp 2f`, the words, `2:`, two characters a word, high half first, ended by `*e` (04).
- **Conditions.**
  For `if` or `while`, a comparison is a skip and a jump.
  As a value, each comparison gives 0 or 1.
- **Arithmetic** is ones' complement.
  Unary `-` never gives -0.
  `*`, `/` and `%` are `jda bmul`, `bdiv` or `bmod`, followed by the operand's word.
  A zero divisor gives 0.
- **The library.**
  `read` gives the next character and 04 at eofnd.
   `write` writes a word's two characters, high half first, leaving out a 0 half.
   `flush` writes the buffer.
   `array(n)` takes n words below `lastv` and gives their address.
   `fin` and `fout` are words.
- **The two passes.**
  Pass 1 writes a record to the scratch file `btmp` in the current directory where `b.b` called the generator.
  A record is byte 001, a letter, and the arguments in signed decimal, each followed by a space.
  At its end, `bx.s`'s `bchain` reopens `btmp` as the input, reads
  `bx` over the bank from `bgo` up from the current directory or else through a link to
  `system`'s, and calls pass 2 which writes `x.s`.
  At the return it removes `btmp`.
  If `btmp` cannot be made or `bx` cannot be read, `b` stops with no message.

## License

`bn.c`, `b1.b` and `b2.b`, like the restorers' `b.c` and `b.b` they come from, are GPL3,
(C) 2016 Robert Swierczek, from the pdp7-unix repository (github.com/DoctorWkt/pdp7-unix).
