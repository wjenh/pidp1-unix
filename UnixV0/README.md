# Unix on the PDP-1

This is the first Unix, the system Thompson and Ritchie built on the PDP-7 at Bell Labs in 1970,
recreated on pidp-1 PDP-1.
It is frequently referred to as *Unix v0*.

23-Nov-2026\
Initial release

## What's here

The kernel is rewritten in am1 keeping the 1970
kernel's structure, system calls, file system and messages.
The commands are translated from the 1970 sources as restored by the pdp7-unix project,
github.com/DoctorWkt/pdp7-unix, and updated for changes needed for the PDP-1.
What a user types and sees is 1970 Unix with the departures listed below.

The system has the kernel with room for fourteen processes, each in its own memory bank, four user
terminals, the 1970 file system on the drum, and these commands:
- `init` and `sh`, the login process and the shell.
- The file commands `cat`, `cp`, `chmod`, `chown`, `chrm`, `ln`, `ls`, `mv`, `rm`, `rn`, `stat`,
  `od`, `pd`, `date` and `dsw`.
- `ed`, the editor, and `roff`, the formatter.
- `as`, the assembler, with `as1`, which together assemble PDP-7 assembler into PDP-1 executables
- `nm`, `un` and `db`, the debugger.
- `check` and `salv`, which check and repair the file system.
- B, the language, compiled to actual PDP-1 instructions by `b`.
- The display programs for the Type 340 and its light pen.
  `st` is Space Travel, `psych` draws figures, `ttt` and `dttt` are 4x4x4 tic-tac-toe, and `p` is pool.

The files and directories here are:
| File | What |
|---|---|
| `unix.drum` | the drum with the file system, the commands in `system`, the users `ken`, `dmr` and `doug`, and the kernel on track 31 |
| `boot.rim` | the drum boot, which reads the kernel from track 31 and starts it |
| `unix.rim` | the kernel alone, does not read or update track 31 but expects the drum filesystem |
| `Src/` | the sources and the build scripts |
| `Tools/` | the host tools the build uses |
| `LICENSE`, `DISCLAIMER.md` | the license and the provenance |

## What it needs

The current pidp1-mods environment.
It uses the Type 23 drum, the DCS2 terminal multiplexer, the BBN timesharing
clock and the Chrono-Log clock.
The display programs need the Type 340 display with its lightpen, and the sense switches.
The PiDP-1's `Docs/UsingType23Drum.md`, `Docs/UsingDCS2.md`,
`Docs/UsingTimesharingClock.md`, `Docs/UsingTheChronologClock.md` and
`Docs/UsingType340Display.md` describe each device.

## Starting it

1. Unix uses the entire drum.
Keep a copy of your current drum file, /opt/pidp1-mods/pdp23drum, if you want,
then make this one the active drum either by
copying to /opt/pidp1-mods/pdp23drum or managing it with *pdp1central*.
```
cp unix.drum /opt/pidp1-mods/pdp23drum
```
2. Load `boot.rim` your favorite way.
The Soroban typewriter prints `PDP-1 unix`, but does not have to be open.
To start without the boot, load `unix.rim`.
You can also restart Unix via address 0100 as long as memory is intact.
3. Connect a terminal with `telnet <host> 2030`.
Logins get terminals 0 to 3 in turn.
At `PDP-1 login:` type a user's name, then the password at `password:`.
The prompt is `@`.

There are 3 users, `ken`/`ken`, `dmr`/`dmr`, and `doug`/`doug`.\
The super-user is `system`/`system`.

Type in lower case.\
ESC and ctrl-C interrupt a program and a ctrl-D or disconnecting logs out.\
\# deletes the last character typed and @ erases the entire line.
There is no backspace, del, or ctrl-U for erasing.

The typewriter is the operator's console where the kernel prints any of its messages.

## Using it

The commands are as the 1970 manual describes them.
A few things that are new on the PDP-1 follow.

**Assembling a program.**
Write it with `ed` and assemble it with `as` which generates PDP-7 code as in 1970 and also new file `a.rel`
which contains metadata needed for the next phase.

Run `as1`, which creates a PDP-1 executable overwriting `a.out` and its addresses in `n.out`.
'op.s' contains the opcode definitions needed.
```
ln system op.s
as op.s yourprog.s
as1
a.out
```
`as1` translates the PDP-7 instructions , up to about 1,760 PDP-7 words and about 357 symbols.
It gives an error and line if it finds something it can't convert, such as a jump through an
auto-index register or an IOT, and generates nothing.

**Using the B compiler.**
Write the program with `ed`, compile it with `b`, assemble it, putting it after `bop.s` and `brt.s`,
and run `a.out`.
The library is 1970's `read`, `write`, `flush`, `fin`, `fout` and `array`.
```
ln system bop.s
ln system brt.s
b x.b x.s
as bop.s brt.s x.s
a.out
```
`b` creates and removes a scratch file `btmp` in the current directory.

**db.**
`db` is the debugger.
It reads a PDP-1 executable and lists the PDP-1's instructions.
On `as`'s `a.out` with `a.rel`,it reads PDP-7 code as 1970's `db` did.

**The display programs.**
Each draws on the Type 340 and uses the light pen.
Command words are drawn on the display in place of 1970's push buttons.
The sense switches are also used.
An ESC or ctrl-C on the terminal ends the program.
- `st`, Space Travel. The buttons are `EXIT RESET FWD BACK LEFT RIGHT UP DOWN`.
  Thrust and turning act while the pen stays down on their button and the others once per touch.
  The sense switches are SS1 forward, SS2 back, SS3 left, SS4 right, SS5 scale up and SS6 scale down,
  and SS5 and SS6 together is a reset.
- `psych`.
  The buttons are `FACT- FACT+ STEP- STEP+ BIG SMALL TYPE EXIT`, and SS1 to SS6 are
  `FACT-` to `SMALL`.
  After `TYPE` the program waits to read from the terminal, as in 1970.
  Type `a` for auto, which runs until the pen touches `FACT-`, or `1` or `2` with octal digits, or Return.
- `ttt` plays at the terminal. Type a cell as three digits from 0 to 3.
  `dttt` plays on the display.
  The pen on a cell selects it, and `MOVE` or a sense switch plays it.
- `p`.
  The buttons are `LEFT RIGHT FAST LEFT FAST RIGHT HIT TAP DUMP RESTORE EXIT`, and SS1 to
  SS6 are 1970's buttons 0 to 5.
  `DUMP` saves the game in `pdump` and `RESTORE` brings it back.

## Departures from 1970

The PDP-1 is not a PDP-7, although they are similar.
The PDP-7 was a cheaper version with a reduced instruction set but faster and with 2's complement support, some
index pseudo-registers, and critically, an extended arithmetic element.
The PDP-7 was about 3 times faster than the PDP-1.

Where the system had to change, the change is as small as the hardware allows:
- **Terminals.**
  DCS2 lines, four by default, where 1970 had the console teletype and the GRAPHIC-2 display.
  Terminals 2 and up are `ttykin` and `ttykout`.
  DCS2 echoes, ESC and ctrl-C interrupt, and a dropped connection is end of file.
  The login prompt is `PDP-1 login:`.
  `init` allows login on every terminal and restarts one whose login ended.
  It waits for a connection to be established from your favorite telnet client.
- **Processes.**
  Fourteen, all in memory, each in its own bank, with no swapping.
  The clock preempts a process whose timeslice expires.
  This is a significant difference, 1970 did not have preemptive scheduling and a user process
  could tie up the machine.
  `fork` copies an entire bank, about 250 ms.
- **The drum.**
  1,984 file system blocks and 250 i-numbers, where 1970 had 6,400 blocks and 3,540 i-numbers.
  The original had a disk drive with significantly more capacity than the Type 23 drum used here.
  There is no backup surface, so there is no `dskres` or `dsksav`.
  New files are numbered from 33.
  `creat` with no free i-node returns -1.
- **The paper tape** is read and punched a character a call, by polling.
  The reader's end of tape is 2 seconds without a character.
- **Time.**
  The time of day is set from the Chrono-Log at each boot.
  `date` prints the year as 1970, as the restored program does.
  It also duplicates the bug where days can be totally incorrect, it is a true replication.
- **The operator's console.**
  The kernel prints its messages on the typewriter where 1970 halted and the boot prints `PDP-1 unix`.
- **`check` and `salv`**
  access the drum through two new system calls, `rblk` (20) and `wblk`
  (22), where 1970 called the kernel's routines.
  `wblk` needs super-user privilige, so `salv` answers `?` for any other user.
  `check` passes over a block the kernel cannnot read where 1970 halted.
- **`sysloc`** gives the kernel's addresses as 16-bit words and `betwen` is not included.
- **`pd`** does not halt on an error.
  AC holds the caller's address and the core image is saved since a halt would stop every user.
- **`as` and `as1`.**
  `as` still writes PDP-7 code, which the PDP-1 cannot run, and now also `a.rel`.
  `as1` generates the PDP-1 executable from them.
  `as1` and `as1x`, the file of its overlays, are new in `system`, and so is `op.s`.
- **`db`** reads every file but `as`'s `a.out` as PDP-1 code and maps the PDP-1's
  instruction names.
  A `skp` or `opr` word with no single name prints as its bits' names (`sma spa i`).
  On a core file it shows the PDP-1's AC and PC.
- **B** compiles to PDP-1 instructions where 1970 `b` wrote threaded code so a program is
  assembled using `bop.s` and `brt.s`.
  The program's generated assebmly code is given last after these.
  The arithmetic is ones' complement. `-1`'s bits are 0777776, which `&` and `|` show,
  an overflow carries end-around, and `/` and `%` are signed (`-7/2` is -3).
  A -0, which only `&` or `|` creates, is unequal to 0 for `==`.
  A scalar automatic's address `&x` generates one fixed word, so a deeper call of the same function sees its
  own `x`.
  `b` is two passes, with `bx` its second, in `system`, and each pass prints at most 20 messages.
  A decimal constant greater than 131071 or an octal one of more than six digits gives
  different bits than the original did.
- **Floating point.**
  `fdv` and `sqrt` are within a few units of the 35th bit, where the original
  `fdv` was off by up to about 2^-15, and `sin` follows the same.
  `fix` past -131071 gives -131071.
- **The display.**
  The Type 340 is used in place of the GRAPHIC-2.
  The two were almost identical.
  `capt` takes a 340 list in the caller's bank and the light pen and sense switches replace the push buttons.
  The list is redrawn at most 31 times a second and the display belongs to the last process to call `capt`.
- **Space Travel** draws planets as diamonds and circles in 40-unit arcs, without the blink.
  It draws 1.55 frames a second, against the original's 4.9, each frame three of the original one-second steps,
  so the game keeps 95% of the 1970 pace in motion three times as coarse.
- **`psych`** draws a figure in 2.0 s against the original 3.7, with its `sin` looked up from a table.
  Its figures are exact where 1970 version drifted after some 70 segments of 100.
- **`ttt`** blinks its cells itself and chooses at random among equal moves, as in the original.
- **`p`** computes the original's physics to the bit, but each frame is two 1970 steps with the
  edges and collisions tested once.
  A pocketed ball is hidden in place.
- **Speed.**
  The drum's revolution, the PDP-1's 5 us cycle, and translated instruction sequences make most
  commands slower than they were on the PDP-7.
  `as` takes 16 to 42 s and `as1` 15 to 34 s for a command the size of `rm` to `ls`,
  and `b` takes 214 s on the B compiler's source.

## Known issues

- `as` has room for about 357 symbols and `as1` for about 1,760 PDP-7 words.
  `st` is too large for them, so it is built on the Linux side.
- Three PDP-7 constructs are not translated, and `as1` rejects them.
  They are a small absolute `law n` added to a pointer, an instruction on a call's line that the routine
  returns to, and a routine reached only through an indirect `jms`.
  Such a program needs a change to its source.

## Building

The images can be built again on a Linux host with gcc and perl, and the PiDP-1's am1 assembler.
```
bash Src/build.sh OUT
```
`AM1` names the assembler and `AM1INC` its include directory.
They default to the PiDP-1 install's `/opt/pidp1-mods/bin/am1` and `/opt/pidp1-mods/Am1Includes`.
The images are `OUT/unix.drum`, `OUT/kernel/unix.rim` and `OUT/boot/boot.rim`.

`Src/cmd/README.md` describes the commands and their hand fixes, `Src/b/README.md` B, and
`Tools/v0fs/README.md` and `Tools/as7am1/README.md` the host tools.
`Tools/as7` is the restorers' PDP-7 cross-assembler, which the B build uses.

## Reading an IBM 1301 record

This build adds a read-only IBM 1301 driver and the `ibmread` command. It
requires the 1301 controller from pidp1-mods and a six-bit image assigned to
that controller. Use a dedicated image with the default record layout. UNIX
still boots from the Type 23 drum and uses it for its filesystem.

At the UNIX prompt, logged in as the super-user:

```text
@ ibmread 0 123 4
```

The arguments are decimal module (0–9), track (0–9999), and record (1–5).
The command prints 176 octal words, one per line. To save the listing
safely, give the destination as a fourth argument:

```text
@ ibmread 0 123 4 testfile
```

This creates a new drum file in the current directory. The kernel checks
for an unused inode and enough free blocks before creating the file. It
allows eleven data blocks, one indirect block, and up to two directory
blocks. An existing name or insufficient capacity returns an error with
the directory unchanged. Ordinary shell `>` redirection opens the
destination before the program starts, so use the fourth argument for a
checked save.

For a disposable test image, `dsk1301 make` creates the default record layout
and `dsk1301 put` seeds a record with one byte per six-bit character. Stop
using an image in the host tool before letting the guest read it.

The kernel entry is `cal SYSIBMREAD` (octal 036), followed by the caller's
buffer address. Pass the track in AC and `module * 8 + record` in IO. The
buffer must hold 176 words in the caller's bank above the system call frame.
The call returns AC zero on success or minus one on failure. On failure it
leaves the caller's buffer intact. It requires a record of exactly 176
words. The driver uses a one-second timeout across the seek and read. It
issues no disk write or format orders.

## Listing a UNIX V0 volume on the IBM disk

`ibmls` lists the IBM volume's root directory (`dd`). Give it a directory
name to list a directory directly under the root. Ordinary users can list
directories they have read permission for. A readable directory ends in
`/`, and an unreadable directory ends in `!/`:

```text
@ ibmls
@ ibmls system
@ ibmls ken
```

Use a V0 volume built for module zero. The host-side image builder checks an
existing Type 23 UNIX image and copies its filesystem to a fresh IBM image
with the default six-bit format:

```sh
python3 Tools/ibmvolume/ibmvolume.py unix.drum volume0 \
    --v0fs /path/to/v0fs --dsk1301 /path/to/dsk1301
```

Set `ibm1301file` to the full path to `volume%d` to make that image available
as module zero. Prepare the image offline, and use a separate image for
the UNIX boot drum. Each 64-word filesystem block occupies the first 64
words of a 176-word record. Block `b` is on decimal track `b // 5`, record
`b % 5 + 1`. The image builder translates the drum's interleave as it copies
the filesystem. `ibmls` reads direct and indirect directory blocks through
the IBM driver. Regular `ls` continues to list files on the UNIX drum.

### Copying a file from the IBM volume

Use `ibmread path newfile` to copy a regular file to a new file in the
current UNIX drum directory. The copy preserves the file's 18-bit words.
The V0 shell allows eight characters per argument. For a longer path with
one directory component, give the directory and filename separately:

```text
@ ibmread ken sys.rc testfile
```

The kernel checks read permission on the IBM directory and file and write
permission on the destination directory. It checks for a free inode and
enough data, indirect, and directory blocks before creating the file. An
existing destination is preserved. If an IBM read fails during copying,
UNIX removes the incomplete file. A directory such as `ken/dd` is listed
with `ibmls` and cannot be copied as a regular file. The numeric record
form of `ibmread` is available to the super-user.

## License

The PDP-1 code is under the GNU General Public License, version 3, in `LICENSE`.
B's compiler, from the restorers' `b.b` and `b.c`, and the restorers' files on the drum (`password`,
`sys.rc`, `moostat`, `b_readme`, `hello.b`) and `Tools/as7` are theirs, under the same license.
`DISCLAIMER.md` states the provenance.
