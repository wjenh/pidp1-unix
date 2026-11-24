# The system's commands

The commands of the 1970 system, translated from its PDP-7 assemble to am1 by
`../../Tools/as7am1` with changes needed for the PDP-1, and `as1`, written in am1.

| File | What |
|---|---|
| `<p>.am1` | a command as it is assembled, the translation with its changes |
| `as1.am1`, `as1c.ac` and the other `as1*` files | `as1`, the second phase of the assembler, written in am1.  `as1.am1` and `as1c.ac` are the program, `as1m.am1` the texts of its reports, and its nine overlays are read from the file `as1x`, `as1c.ac` lists them. |

The translated commands are `init`, `sh`, `cat`, `cp`, `chmod`, `chown`, `chrm`, `ln`, `ls`,
`rm`, `rn`, `stat`, `dsw`, `pd`, `roff`, `nm`, `un`, `db`, `as`, `check`, `salv` (from
`src/cmd/` in the restorers' pdp7-unix repository), `ed` (`ed1.s` and `ed2.s`), `st`, Space
Travel (`st1.s` to `st7.s` with `fop.s`, in the restorers' Makefile's order), `psych`
(`psych.s` with `fop.s`), `ttt` (`ttt1.s` and `ttt2.s`), `p` (`p1.s` to `p5.s`), and `date`,
`mv` and `od`, from the restorers' `src/other/wktdate.s`, `wktmv.s` and `wktod.s`, which the
restored system's `system` directory holds under those names.
`dttt` is a link to `ttt` which shows its board on the display when its name begins `dt`.

The translations keep the 1970 program's labels and order, one am1 word for each PDP-7 word
where the PDP-1 allows it.
A comment naming a 1970 source line is not kept here.

## Building

`../build.sh OUT` assembles every command here into `OUT/cmd` (`<p>.dmp`, which `v0fs` reads),
with the kernel, `as1`, B and the drum.

`as1` is assembled into `as1.dmp`, the program with its first overlay at `OV`, and
`as1x.oct`, the file its other overlays come from.
`as1x.oct` holds `as1m`'s texts (`MSGSZ` words), then each overlay's words from `OV`,
`OVSZ` each, in `as1c.ac`'s `OVK` order, then a block that starts with `cal 016`.
A stub of `as1c.ac` alone gives the core part's end which must be below `OV`.
Each overlay must hold `as1.dmp`'s words below `OV` and none past `OV+OVSZ-1`.
`as1` refuses an `a.rel` whose head word 2, `as`'s final `..`, is not 4096.
A B program's, after `bop.s` sets `..` to 0.
It prints `a.out is not PDP-7 code` and writes nothing.

## The changes

- **`init`** starts a login on each of `NTTY` terminals, 4 by default, which must be the
  kernel's `NTERM`, where 1970 had two.
  Each login is a child process that opens its terminal's `ttyin`-`ttyout` pair.
  The parent keeps the children's process ids, and the message a dead login sends finds its terminal there.
  The names `tty2in` to `tty5out` follow `password`'s.
  The loader of the shell reads the shell to the process bank's 0.
  The prompt is `PDP-1 login: `.
- **`sh`** tests for a line's start against `h_lb1` which holds the buffer's address.
  The PDP-7's `law` loaded an instruction word and 1970 compared with that word, while the PDP-1's
  `law` loads only the address.
  The loader reads to 0, as `init`'s does, and a mask is 07771 for the bank.
- **`pd`** has no `hlt` in its error routine, since that would stop every user's process.
  The caller's address goes into AC and `sys save` writes the core image.
- **`date`** divides by the sixtieths in an hour, a divisor past 0377777.
  The PDP-7's EAE took it unsigned and the PDP-1's `div` takes it signed.
  The time is divided by 54000, then by 4, and the remainder made up again from a table of 54000's multiples.
- **`ed`** compiled a regular expression to PDP-7 instructions that its matcher executed.
  These are rewritten in PDP-1 code, Thompson's method kept.
  A group is a `jda` to `h_mch`, `h_mbol`, `h_meol` or `h_fnd`, a list entry a `jsp` to the group.
  `*` is left out, 1970's compile never reaches it.
- **`roff`** steps the auto-index word itself before the jump through its request table
  since the PDP-1 has no auto-index (`h_jt`).
  After the call of `getword` is a word that is executed at a line's end.
  It is the routine's return, `jmp r_text`.
- **`nm`** steps its sort to the next entry by adding 6 (nm.s:74).
  The PDP-7 added a negative word whose address bits wrap and the PDP-1's end-around carry made each step 7 words.
  The fix adds +6.
- **`db`** reads the name list to 07777, the bank's top (`h_top`).
  Its indirect call of the type routine goes through `h_ty`, the PDP-1's `jda` having no indirect form.
  `prsym` is a `jda` routine and is called so, and `proct` and `prasc` return as PDP-7 routines did through
  their first word, which `h_ty` sets.
  `db` also prints and takes PDP-1 instructions.
  Eight one-word hooks, each marked `by hand`, go to a block after `nlbufp` (02715 to 03633), and
  the name list keeps 2,147 words.
- **The mode** (`h_md`).
  The file is read as PDP-7 code, as 1970's `db` read it, but only when it is `a.out` and `a.rel`'s head
  word 2 is 010000.
  Otherwise it is PDP-1 code, the masks are 07777, and `relocval` and `mrelocv` are 0.
- **Printing** (`h_p1`).
  An instruction's name comes from the table `h_tab`, 56 entries of
  a two-word name and a value, never from `n.out`.
  A `skp` or `opr` word with no single name prints as its bits' names, any bits left in octal.
  An opcode not in the table prints in octal.
  `n.out` names only an address and a system call.
- **A typed name** (`h_gs`) is looked up in the table first, then in `n.out`.
- **`as`** keeps its return in `tlookup`'s first word, which is the routine's flag.
  Its output stays PDP-7 code, `..` at 4096.
  It also writes `a.rel` for `as1`, a record for each word
  `process` stores, and a head; the format is in the comment before `h_tad`.
  Its sums of addresses and numbers go through `h_tad`, two's complement in nine-bit halves, so that
  `a.out` and `n.out` are 1970's word for word.
- **`check` and `salv`** read the drum with `sys rblk`, and `salv` writes it with `sys wblk`.
  In 1970 they called the kernel's routines through `sys sysloc`, which a program in its own
  bank cannot do.
  The i-node is read by `h_iget`, and an indirect or chain block straight into `ldskbuf`.
  A block the kernel refuses is passed over (an indirect block) or ends the walk (a chain block).
  `h_betw` is 1970's range test, and the bit for a block is read from `h_bits`.
  The bounds are the drum's, 250 i-numbers, `d709` 51 and `d6400` 1984.
  `salv` builds the new free list in the system block's order, writes each chain block with `sys wblk`,
  and writes block 0 last.
  It answers `?` to any user but the super-user, who alone may write blocks.
- **`st`** includes `fop.ac`, the floating point package rewritten in am1, in place of
  `fop.s`'s translation, with `FOP_NOSIN`, `FOP_NOSFAD` and `FOP_NOFNO` defined.
  A flag that the PDP-7's `law` made negative is set explicitly.
  The display code is rewritten for the Type 340.
  From `dispcl` to the bank's top are the texts, the routines that build a list
  (`h_put`, `h_room`, `h_text`, `h_lst`, `h_lend`), the buttons' reader `h_rdpb`, list A, the
  static part and list B.
  `HLA`, list A's room, is half of what the program leaves, so that
  `h_gate` + 1 + `HLA` + 1 is 010000 or a word short of it.
  For speed, with what it computes unchanged, `h_far` returns early for a body far away
  and the loads and stores of `absx`, `absy`, and `ftmp1` are `jsp`s to fast entries.
  `ST_K`, 3 unless defined, is how many of 1970's one-second steps a frame takes.
  Each place it changes is an `#if ST_K == 1` with 1970's words and an `#else` with k = 3's.
  Only 1 and 3 have words.
- **`psych`** includes `fop.ac` with `FOP_NOSFAD`, `FOP_NOFNO` and `FOP_TSIN` (its `sin` from
  a table, for speed), and keeps the five constants `psych.s` names after it.
  The push buttons are `h_pbs` and `h_pbi`, made by `h_rdpb`.
  The display is rewritten for the 340.
  Each segment is a `jda h_seg`, and `capture` gives `capt` the static part, `h_lst`.
  The count in `done` names its word `u_9f`, where 1970's label named a later word and the
  PDP-1 halted. `doauto` still reads the bank's word 0, where 1970's location 0 held the last
  break's PC, and `mul`, signed on the PDP-1, gives other random numbers.
- **`ttt`** takes the PDP-1's `add` in the tables that `xct` runs to add a cell, where the
  translation kept the PDP-7's `tad`, which is the PDP-1's `dzm`.
  The words are negative, so `unwind` finds the end mark with `sad`, and `mark` and
  `done` mask the cell's number before shifting.
  `try` keeps its return word in `r_try` at its saves and restores, and `messg`
  steps past its string by its return word.
  A pen hit on a cell is `h_lp`, and the buttons `h_pbs`, both made by `h_rdpb`.
  The display is rewritten for the 340.
  Each cell keeps 1970's 16 words, with the strokes in 340 vectors of at most 127, and each cell's first word sets
  scale 0.
  The board and the MOVE and EXIT buttons follow the cells in one list (`h_stat`).
  1970's blink is the cell's first word changed between bright and dim each 14 sixtieths.
- **`p`** computed with the EAE throughout in 1970, so each step of its arithmetic is written
  again to give the PDP-7's result, word for word, from values kept as the PDP-1's integers.
  The signed products and their shifts are `h_t12`, `h_t15` and `h_t16`, and `ballball` and
  `rotate` are rewritten on them.
  The friction is `h_frc` on the magnitude from `h_mag`.
  The PDP-7's right shift, a floor, is `h_fl1`, `h_fl6` and `h_fl8`.
  A masked negative sum adds 2^16 first (`h_b16`).
  The test of two balls near is `h_near`.
  The push buttons are `h_pbs`, made by `h_rdpb`.
  The display is rewritten for the 340 as one list captured once, with a
  block for each ball whose circle is shown or hidden by its vectors' visible bit, and the nine buttons.
  For speed, the pair loop tests the second ball before its copy (`h_pr`), and
  `getball` and `putball` multiply by 5 with a shift (`h_g5`).
  `P_K` (2) is how many of 1970's steps a frame takes, and `-DP_K=1` gives 1970's frame.

`dsw` writes a one-word `jmp` over its one-word `lac` and is correct as translated.
The other commands needed no fix.
