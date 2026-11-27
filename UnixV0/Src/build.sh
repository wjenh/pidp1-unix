#!/bin/bash
# build.sh -- builds the system from these sources: the commands, the kernel, the boot
# program and the drum.
#
#     bash build.sh OUT
#
# OUT is created if not present.
# Needs gcc, perl and the PiDP-1 am1 assembler.
# AM1 names # the assembler and AM1INC its include directory; they default to the PiDP-1 install's
# /opt/pidp1-mods/bin/am1 and /opt/pidp1-mods/Am1Includes.
# 1. Builds v0fs from ../Tools/v0fs and bn, the host's B compiler, from b/bn.c.
# 2. Assembles each command in cmd into OUT/cmd (the .dmp is what v0fs reads), the kernel
#    into OUT/kernel (unix.rim to load, unix.dmp for the drum's field 31) and the boot
#    program into OUT/boot (boot.rim).
# 3. Assembles as1 and makes as1x.oct, the file its overlays are read from.
# 4. Compiles B's two passes with bn, assembles them with the restorers' as7
#    (../Tools/as7), and makes b.dmp and bx.oct.
# 5. Makes OUT/unix.drum from proto, with the kernel in field 31, and checks it with
#    v0fs check.
# Exit status 0 only when every step passed.
# 23-Nov-2026 Claude/wje initial version.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$(cd "$HERE/../Tools" && pwd)
AM1=${AM1:-/opt/pidp1-mods/bin/am1}
AM1INC=${AM1INC:-/opt/pidp1-mods/Am1Includes}
PROGS="init sh cat cp chmod chown chrm ln ls rm rn stat dsw pd date mv od ed roff nm un db as check salv st psych ttt p ibmread ibmls"

[ $# -eq 1 ] || { echo "usage: build.sh OUT"; exit 2; }
[ -x "$AM1" ] || { echo "no am1 at $AM1: set AM1"; exit 2; }
[ -d "$AM1INC" ] || { echo "no am1 include directory at $AM1INC: set AM1INC"; exit 2; }
mkdir -p "$1" || exit 1
OUT=$(cd "$1" && pwd)

fails=0
fail() {
    echo "FAIL $*"
    fails=$((fails + 1))
}

# am1 in directory $1 on source $2, then any defines; stops on an error or warning.
assemble() {
    local d=$1 src=$2 out
    shift 2
    for flags in "-s -l -S" "-T"; do
        out=$(cd "$d" && "$AM1" -i "$AM1INC" -I "$HERE/include" "$@" $flags "$src" 2>&1)
        if [ $? -ne 0 ] || echo "$out" | grep -qi "error\|warning"; then
            fail "am1 $flags $src: $out"
            return 1
        fi
    done
    return 0
}

# 1. The tools.
mkdir -p "$OUT/tools" "$OUT/cmd" "$OUT/kernel" "$OUT/boot" "$OUT/fs" "$OUT/b"
gcc -O2 -Wall -Wextra -std=gnu11 -o "$OUT/tools/v0fs" "$TOOLS/v0fs/v0fs.c" || exit 1
gcc -std=gnu89 -w -o "$OUT/tools/bn" "$HERE/b/bn.c" || exit 1

# 2. The commands, the kernel and the boot program.
for p in $PROGS; do
    cp "$HERE/cmd/$p.am1" "$OUT/cmd/$p.am1"
    assemble "$OUT/cmd" "$p.am1" && echo "built $p"
done
assemble "$OUT/kernel" "$HERE/kernel/unix.am1" && echo "built the kernel"
assemble "$OUT/boot" "$HERE/boot/boot.am1" && echo "built boot"

# 3. as1.dmp is the core part with the first overlay.  as1x.oct holds as1m's texts, MSGSZ
# words, then each overlay's words from OV, OVSZ each, in as1c.ac's OVK order, then one
# block whose first word is "cal 016", so that a load past the last overlay ends the run.
# A stub of as1c.ac alone gives the core part's end, which must be below OV.  Every overlay
# must hold the same words below OV as as1.dmp, and none past OV+OVSZ-1.
AS1OVS="as1k as1f as1l as1g as1d as1h as1e as1p as1o"
as1def() {
    sed -n "s/^#define $1  *\([0-7]*\).*/\1/p" "$HERE/cmd/as1c.ac"
}
AS1X='
function oct(s,  i, n) {
    n = 0
    for (i = 1; i <= length(s); i++) {
        n = n * 8 + substr(s, i, 1)
    }
    return n
}
function take(  f, a) {
    split(prev, f, " ")
    a = oct(f[1]) % 4096
    w[nf, a] = oct(f[2])
    has[nf, a] = 1
    if (!(nf in top) || a > top[nf]) {
        top[nf] = a
    }
}
NF == 0 { next }
FNR == 1 { prev = ""; nf++; name[nf] = FILENAME }
{ if (prev != "") take(); prev = $0 }
END {
    # prev, the last file s start address, is not a word
    ov = oct(ov); ovsz = oct(ovsz); msgsz = oct(msgsz)
    bad = 0
    if (top[1] >= ov) { printf "the core part ends at %o, past OV %o\n", top[1], ov; bad = 1 }
    if (top[2] >= ov + ovsz) { printf "%s ends at %o, past %o\n", name[2], top[2], ov + ovsz - 1; bad = 1 }
    if (top[3] >= msgsz) { printf "%s ends at %o, past %o\n", name[3], top[3], msgsz - 1; bad = 1 }
    printf "the core part ends %05o, OV %05o, OVSZ %05o; %s %d words\n", top[1], ov, ovsz, name[2], top[2] - ov + 1
    for (a = 0; a < msgsz; a++) print sprintf("%06o", w[3, a] + 0) > out
    for (k = 4; k <= nf; k++) {
        for (a = 0; a < ov; a++) {
            if (has[k, a] != has[2, a] || w[k, a] + 0 != w[2, a] + 0) {
                printf "%s: the core part differs from %s at %o\n", name[k], name[2], a; bad = 1
                break
            }
        }
        if (top[k] >= ov + ovsz) { printf "%s ends at %o, past %o\n", name[k], top[k], ov + ovsz - 1; bad = 1 }
        printf "%s %d words\n", name[k], top[k] - ov + 1
        for (a = ov; a < ov + ovsz; a++) print sprintf("%06o", w[k, a] + 0) > out
    }
    print "160016" > out
    for (a = 1; a < ovsz; a++) print "000000" > out
    exit bad
}'
printf 'as1stub\n#include "unixsys.ah"\n        octal\n        bank 2\n#include "as1c.ac"\nstart 020000\n' \
    > "$OUT/cmd/as1stub.am1"
as1ok=1
for p in as1stub as1 as1m $AS1OVS; do
    [ $p = as1stub ] || cp "$HERE/cmd/$p.am1" "$OUT/cmd/$p.am1"
    assemble "$OUT/cmd" "$p.am1" -I "$HERE/cmd" || as1ok=0
done
if [ $as1ok -eq 1 ]; then
    out=$(cd "$OUT/cmd" && awk -v ov="$(as1def OV)" -v ovsz="$(as1def OVSZ)" -v msgsz="$(as1def MSGSZ)" \
        -v out=as1x.oct "$AS1X" as1stub.dmp as1.dmp as1m.dmp $(for p in $AS1OVS; do echo $p.dmp; done) 2>&1)
    if [ $? -ne 0 ]; then
        fail "as1x: $out"
    else
        echo "$out"
        echo "built as1 and as1x"
    fi
fi

# 4. b's two passes, compiled by bn and assembled by as7 after bop.s, brt.s and bx.s.
# cmd/b.dmp is pass 1, the command, from word 0.  cmd/bx.oct is pass 2's words from bgo
# on, which pass 1 reads over itself.  The two images must hold the same words below bgo
# but word 020 (bsp, each pass's end).  Each pass must leave its stack room below brt.s's
# buffers at 07570, pass 1 BSTK1 words and pass 2 BSTK2.
BSTK1=200
BSTK2=96
BX='
function oct(s,  i, n) {
    n = 0
    for (i = 1; i <= length(s); i++) {
        n = n * 8 + substr(s, i, 1)
    }
    return n
}
FNR == 1 { nf++ }
/^[0-7]+: [0-7]+/ {
    a = oct(substr($1, 1, length($1) - 1))
    w[nf, a] = oct($2)
    if (a > top[nf]) top[nf] = a
    if ($3 == "bgo:") bgo[nf] = a
    if ($3 == "bsp:") bend[nf] = oct($2)
}
END {
    bad = 0
    if (!(1 in bgo) || !(2 in bgo) || bgo[1] != bgo[2]) {
        print "bgo not found in both passes, or not at one address"
        exit 1
    }
    for (a = 0; a < bgo[1]; a++) {
        if (a != 16 && w[1, a] + 0 != w[2, a] + 0) {
            printf "the passes differ below bgo, at %o\n", a
            bad = 1
            break
        }
    }
    need[1] = stk1; need[2] = stk2
    for (k = 1; k <= 2; k++) {
        printf "pass %d ends %05o, %d words of stack room\n", k, bend[k], 3960 - bend[k]
        if (3960 - bend[k] < need[k]) {
            printf "pass %d leaves less than %d\n", k, need[k]
            bad = 1
        }
    }
    printf "bgo %05o; bx %d words\n", bgo[2], top[2] - bgo[2] + 1
    for (a = 0; a <= top[1]; a++) printf "%06o %06o\n", a, w[1, a] + 0 > dmp
    print "000000 000000" > dmp
    for (a = bgo[2]; a <= top[2]; a++) printf "%06o\n", w[2, a] + 0 > bx
    exit bad
}'
bok=1
for p in b1 b2; do
    out=$("$OUT/tools/bn" "$HERE/b/$p.b" "$OUT/b/$p.s" 2>&1)
    if [ $? -ne 0 ] || [ -n "$out" ]; then
        fail "bn $p.b: $out"
        bok=0
        break
    fi
    out=$(perl "$TOOLS/as7" -o "$OUT/b/$p.a7" "$HERE/b/bop.s" "$HERE/b/brt.s" "$HERE/b/bx.s" \
        "$OUT/b/$p.s" 2>&1)
    st=$?
    out=$(echo "$out" | grep -v '^I*$' | grep -v '\.s$')
    if [ $st -ne 0 ] || [ -n "$out" ]; then
        fail "as7 $p: $out"
        bok=0
    fi
done
if [ $bok -eq 1 ]; then
    out=$(awk -v stk1=$BSTK1 -v stk2=$BSTK2 -v dmp="$OUT/cmd/b.dmp" -v bx="$OUT/cmd/bx.oct" "$BX" \
        "$OUT/b/b1.a7" "$OUT/b/b2.a7" 2>&1)
    if [ $? -ne 0 ]; then
        fail "b: $out"
    else
        echo "$out"
        echo "built b and bx"
    fi
fi

# 5. The drum, from the prototype beside the programs and the text files it names.
cp "$HERE"/fs/* "$OUT/fs/"
cp "$HERE/b/bop.s" "$HERE/b/brt.s" "$OUT/fs/"
cp "$HERE/proto" "$OUT/proto"
out=$(cd "$OUT" && tools/v0fs make -k kernel/unix.dmp proto unix.drum 2>&1)
if [ $? -ne 0 ]; then
    fail "v0fs make: $out"
else
    out=$("$OUT/tools/v0fs" check "$OUT/unix.drum" 2>&1)
    if [ $? -ne 0 ]; then
        fail "v0fs check: $out"
    else
        echo "PASS the drum: $OUT/unix.drum, v0fs check clean"
    fi
fi

if [ $fails -eq 0 ]; then
    echo "PASS: all built"
else
    echo "FAIL: $fails failed"
fi
[ $fails -eq 0 ]
