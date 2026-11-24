#!/usr/bin/env python3
"""Census of the PDP-7 Unix sources: what each file uses that the PDP-1 lacks.

For each source file: its lines, its assembled words, and the count of each
PDP-7 construct with no direct PDP-1 counterpart (jms and its inline
arguments, sys, law, tad, the link, auto-index indirection, the EAE,
references across the 4K halves, dzm, sad, isz, combined operates, each
IOT), and from those a rough size in am1 if translated word for word.

The parser follows the restorers' assembler, tools/as7, statement for
statement (labels, assignments, `;`-separated words, `<a`, `a>` and `>a`
characters, `Nf`/`Nb` local labels, 8-character label names, `..`
relocation), so that each emitted word is seen with the symbols it was
built from. Files are assembled in the groups the repository's
build/Makefile assembles them in, since a symbol may be defined in one file
of a group and used in another.

    census.py [--root DIR] [--md]   the census, as text or as Markdown
    census.py --test                census-test.s against its "expect lines
    census.py --test --control      the same with one expected count changed:
                                    must fail
    census.py --check-as7 [--control]
                                    compare each program's size with what
                                    tools/as7 punches (needs perl; run in WSL);
                                    the control must fail
"""

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT = os.path.join(HERE, "..", "Documents", "pdp7-unix")

RELATIVE = 0o1000000
MASK = 0o777777

# as7's predefined symbols (tools/as7, %Var), the system call numbers and
# opcodes.
# IOTs are not predefined; the kernel takes them from sop.s.
SYSCALLS = {
    "save": 1, "getuid": 2, "open": 3, "read": 4, "write": 5, "creat": 6,
    "seek": 7, "tell": 8, "close": 9, "link": 10, "unlink": 11,
    "setuid": 12, "rename": 13, "exit": 14, "time": 15, "intrp": 16,
    "chdir": 17, "chmod": 18, "chown": 19, "sysloc": 21, "capt": 23,
    "rele": 24, "status": 25, "smes": 27, "rmes": 28, "fork": 29,
}
MEMREF = {
    "dac": 0o040000, "jms": 0o100000, "dzm": 0o140000, "lac": 0o200000,
    "xor": 0o240000, "add": 0o300000, "tad": 0o340000, "xct": 0o400000,
    "isz": 0o440000, "and": 0o500000, "sad": 0o540000, "jmp": 0o600000,
}
EAE = {
    "eae": 0o640000, "osc": 0o640001, "omq": 0o640002, "cmq": 0o640004,
    "div": 0o640323, "norm": 0o640444, "lls": 0o640600, "clls": 0o641600,
    "als": 0o640700, "lrs": 0o640500, "ecla": 0o641000, "lacs": 0o641001,
    "lacq": 0o641002, "abs": 0o644000, "divs": 0o644323, "clq": 0o650000,
    "frdiv": 0o650323, "lmq": 0o652000, "mul": 0o653122, "idiv": 0o653323,
    "idivs": 0o657323, "frdivs": 0o654323, "muls": 0o657122,
    "norms": 0o660444, "gsm": 0o664000, "lrss": 0o660500, "llss": 0o660600,
    "alss": 0o660700,
}
OPR = {
    "opr": 0o740000, "nop": 0o740000, "cma": 0o740001, "cml": 0o740002,
    "oas": 0o740004, "ral": 0o740010, "rar": 0o740020, "hlt": 0o740040,
    "xx": 0o740040, "sma": 0o740100, "sza": 0o740200, "snl": 0o740400,
    "skp": 0o741000, "spa": 0o741100, "sna": 0o741200, "szl": 0o741400,
    "rtl": 0o742010, "rtr": 0o742020, "cll": 0o744000, "stl": 0o744002,
    "rcl": 0o744010, "rcr": 0o744020, "cla": 0o750000, "clc": 0o750001,
    "las": 0o750004, "glk": 0o750010,
}
LINK_OPS = {"cll", "cml", "stl", "snl", "szl", "ral", "rar", "rtl", "rtr",
            "rcl", "rcr", "glk"}
PREDEF = dict(SYSCALLS)
PREDEF.update(MEMREF)
PREDEF.update(EAE)
PREDEF.update(OPR)
PREDEF.update({"sys": 0o020000, "i": 0o020000, "law": 0o760000,
               ".": RELATIVE, "..": 4096})

# The counted constructs, in report order.
COUNTS = ["jms", "jms_args", "sys", "sys_args", "law", "minus", "tad",
          "link", "autoidx", "indirect", "eae", "xhalf", "dzm", "sad", "isz",
          "xct", "combopr", "iot"]

# Rough extra am1 words per occurrence when a construct is translated word
# for word onto the PDP-1D.
# Each is an estimate for sizing only, not a design.
EXTRA = {
    "jms": 1,       # jsp leaves the return in AC, so the callee's `dac` (an
                    # upper bound, counted per call, paid per subroutine)
    "tad": 2,       # two's complement add on a ones' complement machine
    "link": 2,      # the link kept in a word of memory
    "autoidx": 1,   # an idx of the pointer word before the indirect use
    "eae": 1,       # MQ in IO; shifts of more than 9 places take two
    "xhalf": 1,     # a reference into the other 4K bank, through a pointer
    "combopr": 1,   # a skip and an operate in one word, two words on the
                    # PDP-1, whose skip and operate groups are separate
}
# Not weighted: sys (cal is one word), law (law i), dzm, sad, xct (the
# PDP-1 has them), isz (isp, or idx where it never skips).

# The groups build/Makefile assembles (output name, files under src/).
# sop.s comes first wherever the Makefile lists it, as there.
PROGRAMS = [
    ("kernel", ["sys/sop.s", "sys/s1.s", "sys/s2.s", "sys/s3.s", "sys/s4.s",
                "sys/s5.s", "sys/s6.s", "sys/s7.s", "sys/s8.s"]),
    ("coldboot", ["sys/sop.s", "sys/s1.s", "sys/s2.s", "sys/s3.s",
                  "sys/s4.s", "sys/s5.s", "sys/s6.s", "sys/s7.s",
                  "sys/s8.s", "sys/s9.s"]),
    ("maksys", ["sys/sop.s", "cmd/op.s", "sys/maksys.s"]),
    ("trysys", ["sys/sop.s", "cmd/op.s", "sys/trysys.s"]),
    ("adm", ["cmd/adm.s"]), ("ald", ["cmd/ald.s"]), ("apr", ["cmd/apr.s"]),
    ("as", ["cmd/as.s"]), ("bc", ["cmd/bc.s"]),
    ("b (bl, bi; b.s is generated)", ["cmd/bl.s", "cmd/bi.s"]),
    ("cas", ["cmd/cas.s"]), ("cat", ["cmd/cat.s"]),
    ("check", ["cmd/check.s"]), ("chmod", ["cmd/chmod.s"]),
    ("chown", ["cmd/chown.s"]), ("chrm", ["cmd/chrm.s"]),
    ("cp", ["cmd/cp.s"]), ("db", ["cmd/db.s"]), ("dmabs", ["cmd/dmabs.s"]),
    ("ds", ["cmd/ds.s"]),
    ("dskres", ["cmd/dskres.s", "cmd/dskio.s", "sys/sop.s"]),
    ("dsksav", ["cmd/dsksav.s", "cmd/dskio.s", "sys/sop.s"]),
    ("dsw", ["cmd/dsw.s"]), ("ed", ["cmd/ed1.s", "cmd/ed2.s"]),
    ("init", ["cmd/init.s"]), ("ln", ["cmd/ln.s"]), ("ls", ["cmd/ls.s"]),
    ("moo", ["cmd/moo.s"]), ("nm", ["cmd/nm.s"]),
    ("p", ["cmd/p1.s", "cmd/p2.s", "cmd/p3.s", "cmd/p4.s", "cmd/p5.s"]),
    ("pd", ["cmd/pd.s"]), ("psych", ["cmd/psych.s", "cmd/fop.s"]),
    ("rm", ["cmd/rm.s"]), ("rn", ["cmd/rn.s"]), ("roff", ["cmd/roff.s"]),
    ("salv", ["cmd/salv.s"]), ("sh", ["cmd/sh.s"]),
    ("st (Space Travel)", ["cmd/st1.s", "cmd/st2.s", "cmd/st3.s",
                           "cmd/st4.s", "cmd/fop.s", "cmd/st5.s",
                           "cmd/st6.s", "cmd/st7.s"]),
    ("stat", ["cmd/stat.s"]), ("tm", ["cmd/tm.s"]),
    ("t (t1-t8, not in the build)", ["cmd/t1.s", "cmd/t2.s", "cmd/t3.s",
                                     "cmd/t4.s", "cmd/t5.s", "cmd/t6.s",
                                     "cmd/t7.s", "cmd/t8.s"]),
    ("ttt", ["cmd/ttt1.s", "cmd/ttt2.s"]), ("un", ["cmd/un.s"]),
    ("date (restorers')", ["other/wktdate.s"]),
    ("mv (restorers')", ["other/wktmv.s"]),
    ("od (restorers')", ["other/wktod.s"]),
]


class Stats:
    def __init__(self):
        self.lines = 0
        self.words = 0        # words emitted
        self.reserved = 0     # words skipped by `. = . + n`
        self.instr = 0        # words whose first syllable is an opcode
        self.c = collections.Counter()
        self.iots = collections.Counter()
        self.calls = collections.Counter()

    def add(self, o):
        self.lines += o.lines
        self.words += o.words
        self.reserved += o.reserved
        self.instr += o.instr
        self.c.update(o.c)
        self.iots.update(o.iots)
        self.calls.update(o.calls)

    def am1_estimate(self):
        return self.words + self.reserved + sum(
            EXTRA.get(k, 0) * n for k, n in self.c.items())


class Assembler:
    """Two passes over a group of files, after tools/as7."""

    def __init__(self, files, root):
        self.files = files
        self.root = root
        self.stats = {}
        self.undefined = set()
        self.top = 0

    def run(self):
        self.var = dict(PREDEF)
        self.glabel = {}
        self.llabel = collections.defaultdict(dict)
        self.rlabel = collections.defaultdict(list)
        # As in as7, only `.` is reset for pass two; every other symbol,
        # `..` among them, keeps its value from the end of pass one.
        for stage in (1, 2):
            self.stage = stage
            self.var["."] = RELATIVE
            for f in self.files:
                self.parse_file(f)
        return self

    # The input.

    def parse_file(self, name):
        self.file = name
        path = os.path.join(self.root, "src", name) \
            if not os.path.isabs(name) else name
        with open(path, encoding="latin-1") as fh:
            text = fh.read().split("\n")
        if text and text[-1] == "":
            text.pop()
        if self.stage == 2:
            self.st = self.stats.setdefault(name, Stats())
            self.st.lines = len(text)
        for line in text:
            self.line = line
            self.words_on_line = []
            self.parse_line()
            if self.stage == 2:
                self.after_line()

    def eol(self):
        return self.line == "" or self.line.startswith('"')

    def parse_line(self):
        while True:
            if self.eol():
                return
            self.line = self.line.lstrip()
            if self.line.startswith("@"):
                return
            while True:
                m = re.match(r"([A-Za-z0-9_.]+):\s*", self.line)
                if not m:
                    break
                self.line = self.line[m.end():]
                self.process_label(m.group(1))
            if self.eol():
                return
            m = re.match(r"([^;= \t]+)\s*=", self.line)
            if m:
                lhs = m.group(1)
                self.line = self.line[m.end():]
                value, syl = self.parse_expression()
                if value is None:
                    value = 0
                if lhs == "." and self.stage == 2:
                    old = self.var["."]
                    step = (value & MASK) - (old & MASK)
                    if 0 < step < 0o20000:
                        self.st.reserved += step
                self.var[lhs] = value
            else:
                value, syl = self.parse_expression()
                if self.stage == 2:
                    self.emit(value, syl)
                self.var["."] = self.var["."] + 1
            self.line = re.sub(r"^\s*;?", "", self.line, count=1)

    def process_label(self, label):
        loc = self.var["."]
        if re.fullmatch(r"\d+", label):
            if self.stage == 1:
                self.rlabel[label].append(loc)
            return
        label = label[:8]
        if label.startswith("L"):
            self.llabel[self.file][label] = loc
        else:
            self.glabel[label] = loc

    def get_label(self, sym):
        sym = sym[:8]
        if sym in self.llabel[self.file]:
            return self.llabel[self.file][sym]
        return self.glabel.get(sym)

    def relative_label(self, n, d):
        cur = self.var["."]
        locs = self.rlabel.get(n, [])
        if d == "f":
            for r in locs:
                if r > cur:
                    return r
        else:
            for r in sorted(locs, reverse=True):
                if r < cur:
                    return r
        return cur

    def parse_expression(self):
        """Returns (word, syllables); syllables are (op, kind, text, value)."""
        word = 0
        flags = 0
        syl = []
        while True:
            self.line = self.line.lstrip()
            if self.line == "" or self.line[0] in '";':
                return (word | flags), syl
            op = "|"
            if self.line.startswith("-"):
                op, self.line = "-", self.line[1:]
            elif self.line.startswith("+"):
                op, self.line = "+", self.line[1:]
            kind, text = None, None
            if len(self.line) >= 2 and self.line[0] == "<":
                kind, text = "char", self.line[1]
                value = ord(text) << 9
                self.line = self.line[2:]
            elif len(self.line) >= 2 and self.line[1] == ">":
                kind, text = "char", self.line[0]
                value = ord(text)
                self.line = self.line[2:]
            elif len(self.line) >= 2 and self.line[0] == ">":
                kind, text = "char", self.line[1]
                value = ord(text)
                self.line = self.line[2:]
            else:
                m = re.match(r"[A-Za-z_.][A-Za-z0-9_.]*", self.line)
                if m:
                    text = m.group(0)
                    self.line = self.line[m.end():]
                    kind = "sym"
                    if text in self.var:
                        value = self.var[text]
                    elif self.get_label(text) is not None:
                        value = self.get_label(text)
                    else:
                        value = 0
                        if self.stage == 2:
                            self.undefined.add(text)
                else:
                    m = re.match(r"(\d+)([fb])", self.line)
                    if m:
                        kind, text = "local", m.group(0)
                        self.line = self.line[m.end():]
                        value = self.relative_label(m.group(1), m.group(2))
                    else:
                        m = re.match(r"\d+", self.line)
                        if m:
                            kind, text = "num", m.group(0)
                            self.line = self.line[m.end():]
                            if text.startswith("0"):
                                value = 0
                                for d in text:
                                    value = value * 8 + ord(d) - ord("0")
                            else:
                                value = int(text)
                            value &= MASK
                        else:
                            self.line = ""
                            return None, syl
            syl.append((op, kind, text, value))
            sflags = value & RELATIVE
            value &= MASK
            if op == "+":
                word += value
                flags |= sflags
            elif op == "-":
                word -= value
                if flags & RELATIVE and sflags & RELATIVE:
                    flags &= ~RELATIVE
            else:
                word |= value
                flags |= sflags
            word &= MASK

    # The census.

    def reloc(self, v):
        if v & RELATIVE:
            return ((v & MASK) + (self.var[".."] & MASK)) & MASK
        return v & MASK

    def emit(self, word, syl):
        st = self.st
        st.words += 1
        self.words_on_line.append(syl)
        here = self.reloc(self.var["."])
        self.top = max(self.top, here + 1)
        if not syl or syl[0][1] != "sym":
            if syl and syl[0][0] == "-" and syl[0][1] == "num":
                st.c["minus"] += 1
            return
        first = syl[0][2]
        names = [s[2] for s in syl if s[1] == "sym"]
        val = self.var.get(first)
        if first in MEMREF and first in self.var and \
                self.var[first] == MEMREF[first]:
            st.instr += 1
            if first in ("jms", "tad", "dzm", "sad", "isz", "xct"):
                st.c[first] += 1
            indirect = "i" in names[1:]
            if indirect:
                st.c["indirect"] += 1
            # The address field, relocated as the loader would.
            addr_syl = [s for s in syl[1:] if not (s[1] == "sym" and
                                                    s[2] == "i")]
            a = 0
            rel = False
            for op, kind, text, v in addr_syl:
                rel = rel or bool(v & RELATIVE)
                v &= MASK
                a = (a + v) if op == "+" else (a - v) if op == "-" else (a | v)
            a &= 0o17777
            if rel:
                a = (a + (self.var[".."] & MASK)) & 0o17777
            if indirect and 0o10 <= a <= 0o17 and not rel:
                st.c["autoidx"] += 1
            if a >= 0o20 and (a >> 12) != (here >> 12) & 1:
                st.c["xhalf"] += 1
        elif first == "sys":
            st.instr += 1
            st.c["sys"] += 1
            call = names[1] if len(names) > 1 else "?"
            if len(syl) > 1 and syl[1][1] == "num":
                call = syl[1][2]
            st.calls[call] += 1
        elif first == "law":
            st.instr += 1
            st.c["law"] += 1
        elif first in EAE and val == EAE[first]:
            st.instr += 1
            st.c["eae"] += 1
        elif first in OPR and val == OPR[first]:
            st.instr += 1
            ops = [n for n in names if n in OPR]
            if len(ops) > 1:
                st.c["combopr"] += 1
            st.c["link"] += sum(1 for n in ops if n in LINK_OPS)
        elif val is not None and (val & MASK) >> 12 == 0o70 and \
                not val & RELATIVE:
            st.instr += 1
            st.c["iot"] += 1
            st.iots[first] += 1

    def after_line(self):
        # Inline arguments, the words after a jms or sys on the same line.
        w = self.words_on_line
        for k, syl in enumerate(w):
            if syl and syl[0][1] == "sym" and syl[0][2] in ("jms", "sys"):
                if k + 1 < len(w):
                    self.st.c[syl[0][2] + "_args"] += len(w) - k - 1
                break


# The reports.

def census(root):
    per_file = {}
    per_prog = []
    undefined = {}
    for name, files in PROGRAMS:
        a = Assembler(files, root).run()
        total = Stats()
        for f in files:
            total.add(a.stats[f])
            per_file.setdefault(f, a.stats[f])
        per_prog.append((name, files, total, a.top))
        if a.undefined:
            undefined[name] = sorted(a.undefined)
    return per_file, per_prog, undefined


HEAD = ["lines", "words", "rsv", "jms", "args", "sys", "args", "law", "-n",
        "tad", "link", "aidx", "ind", "eae", "xhalf", "dzm", "sad", "isz",
        "xct", "copr", "iot", "am1~", "4K?"]


def row(name, s, fits=True):
    est = s.am1_estimate()
    vals = [s.lines, s.words, s.reserved] + [s.c[k] for k in COUNTS] + \
        [est, ("yes" if est <= 4096 else "NO") if fits else ""]
    return [name] + [str(v) for v in vals]


def print_table(rows, md):
    if md:
        print("| " + " | ".join(rows[0]) + " |")
        print("|" + "---|" * len(rows[0]))
        for r in rows[1:]:
            print("| " + " | ".join(r) + " |")
        return
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    for r in rows:
        print("  ".join(c.rjust(w) if i else c.ljust(w)
                        for i, (c, w) in enumerate(zip(r, widths))))


def report(root, md):
    per_file, per_prog, undefined = census(root)
    print("Per program (files assembled together, as build/Makefile does):\n")
    rows = [["program"] + HEAD]
    grand = Stats()
    for name, files, total, top in per_prog:
        rows.append(row(name, total))
    seen = set()
    for name, files, total, top in per_prog:
        for f in files:
            if f not in seen:
                seen.add(f)
                grand.add(per_file[f])
    rows.append(row("ALL FILES ONCE", grand, fits=False))
    print_table(rows, md)
    print("\nPer file (each once):\n")
    rows = [["file"] + HEAD]
    for f in sorted(per_file):
        rows.append(row(f, per_file[f], fits=False))
    print_table(rows, md)
    print("\nIOTs, by mnemonic, all files once:")
    for k, n in sorted(grand.iots.items(), key=lambda x: (-x[1], x[0])):
        print("  %-6s %d" % (k, n))
    print("\nSystem calls, by name, all files once:")
    for k, n in sorted(grand.calls.items(), key=lambda x: (-x[1], x[0])):
        print("  %-8s %d" % (k, n))
    if undefined:
        print("\nUndefined symbols (a group the census could not resolve):")
        for k, v in undefined.items():
            print("  %s: %s" % (k, " ".join(v)))
    return 0


# The test.

def run_test(control):
    path = os.path.join(HERE, "census-test.s")
    expect = {}
    with open(path, encoding="latin-1") as fh:
        for line in fh:
            m = re.match(r'"\s*expect\s+(\w+)\s+(\d+)', line)
            if m:
                expect[m.group(1)] = int(m.group(2))
    if not expect:
        print("FAIL: no expect lines in", path)
        return 1
    if control:
        k = "jms" if "jms" in expect else sorted(expect)[0]
        expect[k] += 1
        print("control: expecting %s = %d, one more than the file says"
              % (k, expect[k]))
    a = Assembler([path], HERE).run()
    s = a.stats[path]
    got = dict(s.c)
    got.update({"lines": s.lines, "words": s.words, "reserved": s.reserved,
                "instr": s.instr})
    for k, n in s.iots.items():
        got["iot_" + k] = n
    for k, n in s.calls.items():
        got["call_" + k] = n
    bad = 0
    for k in sorted(expect):
        if got.get(k, 0) != expect[k]:
            print("  %s: expected %d, counted %d" % (k, expect[k], got.get(k, 0)))
            bad += 1
    # Every nonzero count must have been foreseen in an expect line.
    for k in sorted(got):
        if got[k] and k not in expect:
            print("  %s: counted %d, no expect line" % (k, got[k]))
            bad += 1
    if a.undefined:
        print("  undefined:", " ".join(sorted(a.undefined)))
        bad += 1
    print("test:", "PASS" if not bad else "FAIL (%d)" % bad)
    return 1 if bad else 0


def check_as7(root, control):
    """Each program's span against the words tools/as7 punches for it.
    The control leg adds one word to the census's first figure: must fail."""
    as7 = os.path.join(root, "tools", "as7")
    bad = 0
    for n, (name, files, total, top) in enumerate(census(root)[1]):
        if control and n == 0:
            top += 1
            print("control: one word added to %s's census figure" % name)
        paths = [os.path.join(root, "src", f) for f in files]
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "a.out")
            r = subprocess.run(["perl", as7, "-f", "ptr", "-o", out] + paths,
                               cwd=d, capture_output=True, text=True)
            size = os.path.getsize(out) // 3 if os.path.exists(out) else -1
        # ptr punches from .. to the highest word stored.
        mine = top - (Assembler(files, root).run().var[".."] & MASK)
        ok = size == mine
        bad += not ok
        print("%-34s as7 %6d  census %6d  %s%s" % (
            name, size, mine, "ok" if ok else "DIFFERS",
            "" if r.returncode == 0 else "  (as7 exit %d)" % r.returncode))
    print("check-as7:", "PASS" if not bad else "FAIL (%d)" % bad)
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--root", default=DEFAULT_ROOT)
    p.add_argument("--md", action="store_true")
    p.add_argument("--test", action="store_true")
    p.add_argument("--control", action="store_true")
    p.add_argument("--check-as7", action="store_true")
    a = p.parse_args()
    if a.test:
        return run_test(a.control)
    if a.check_as7:
        return check_as7(os.path.abspath(a.root), a.control)
    return report(os.path.abspath(a.root), a.md)


if __name__ == "__main__":
    sys.exit(main())
