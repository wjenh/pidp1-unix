/*
 * translate.c - the translation of an assembled PDP-7 program into am1 for a process bank of
 * the PDP-1D.
 *
 * The program's words are taken by PDP-7 address.
 * Each is classified as code (its first syllable an opcode), an inline argument (after jms
 * or sys on its line, or the operand word after mul, idiv or div), data, or reserved
 * (skipped by `. = . + n`); data reached by the flow from code is executed, and only a
 * negative literal may be (it loads itself, as law).
 * The liveness of AC, MQ and the link is worked out over the flow, routines' returns going
 * back to their callers, so that AC is parked only where the PDP-1's idx and isp would
 * destroy a value still needed, IO (the PDP-1's MQ) only where the link's word passes
 * through it, and a tad's carry is reported only where the link is read after it.
 *
 * Each word becomes one am1 word or a short sequence, labeled by its PDP-7 address (or its
 * as7 name), so that am1 lays the program out again.
 * A skip whose next word became more than one PDP-1 word, or whose sequence must finish on
 * both paths, is rebuilt with jumps to the labels of the words it skipped between.
 * The machine facts the sequences rest on are the PDP-7 handbook's (F-75, the operate and
 * EAE groups) and the PDP-1 handbook's (F15D, the skip and shift groups), and measurements
 * of the two machines.
 *
 * translate() returns 0, 1 when the output cannot be written, or 2 when anything was
 * reported for a hand fix.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "translate.h"

#define NADDR       020000L        // PDP-7 user programs live in 010000-017777
#define USERBASE    010000L
#define MAXLINES    48
#define MAXNAME     48
#define MAXTEXT     96
#define MAXREF      64             // a name and an offset, as nameRef() writes it
#define MAXSUCC     4096           // the successors of one word, every return of a routine

#define LIVE_AC     1u
#define LIVE_MQ     2u
#define LIVE_L      4u

// PDP-7 memory reference opcodes, bits 0-3.
#define OP_DAC      0040000L
#define OP_JMS      0100000L
#define OP_DZM      0140000L
#define OP_LAC      0200000L
#define OP_XOR      0240000L
#define OP_ADD      0300000L
#define OP_TAD      0340000L
#define OP_XCT      0400000L
#define OP_ISZ      0440000L
#define OP_AND      0500000L
#define OP_SAD      0540000L
#define OP_JMP      0600000L

// PDP-7 operate group bits (F-75, the operate instructions).
#define OPR_CMA     01L
#define OPR_CML     02L
#define OPR_OAS     04L
#define OPR_RAL     010L
#define OPR_RAR     020L
#define OPR_HLT     040L
#define OPR_SMA     0100L
#define OPR_SZA     0200L
#define OPR_SNL     0400L
#define OPR_SKP     01000L
#define OPR_TWO     02000L
#define OPR_CLL     04000L
#define OPR_CLA     010000L

// PDP-7 EAE bits (F-75, EAE bit assignments) and the three instructions with an operand.
#define EAE_SIGN    020000L        // AC sign to the link, event 1
#define EAE_CLRMQ   010000L        // clear MQ, event 1
#define EAE_ABS     004000L
#define EAE_MQORAC  002000L        // MQ |= AC, event 2
#define EAE_CLRAC   001000L        // clear AC, event 2
#define EAE_CLASS   0700L
#define EAE_STEP    077L
#define EAE_MUL     0653122L
#define EAE_IDIV    0653323L
#define EAE_DIV     0640323L
#define SYS_EXIT    14
#define SYS_SYSLOC  21
#define SYS_SMES    27

enum wordKind
{
    WK_NONE,
    WK_RESERVED,                   // in the program's span, never stored
    WK_DATA,
    WK_ARG,                        // an inline argument or an EAE operand
    WK_CODE
};

enum instClass
{
    IC_NONE,
    IC_MEMREF,
    IC_LAW,
    IC_OPR,
    IC_EAE,
    IC_SYS,
    IC_IOT,
    IC_LITERAL                     // a negative literal that is executed
};

struct strList
{
    int n;
    char *lineP[MAXLINES];
};

struct tWord
{
    struct srcWord *sP;            // NULL for a reserved word
    int kind;
    int cls;
    int isEntry;                   // the target of a jms, its return word
    int isPointer;                 // used through an indirect reference
    int isStored;                  // the target of a direct dac or dzm
    int isCount;                   // an isz whose skip is kept
    int nArgs;
    int executed;
    int nCont;                     // an entry's call sites
    long *contP;
    int nIsz;                      // an entry's isz of its return, its skip returns
    unsigned liveIn;
    unsigned liveOut;
    char name[MAXNAME];
    int runStart;                  // a reserved word that starts a run
    int hasSkip;
    struct strList pre;            // for a skip, what comes before the PDP-1 skip,
    char skip[MAXTEXT + 16];       // the skip itself and its inverse ("" when none),
    char skipInv[MAXTEXT + 16];
    struct strList post;           // and what both paths must still do
    struct strList body;           // with no skip, the whole sequence
    struct strList out;            // the composed lines
};

struct tr
{
    struct as7 *aP;
    const struct tOptions *oP;
    struct tWord *wP;
    long lo;                       // the program, [lo, hi)
    long hi;
    long lastStored;               // the last stored word; what follows is not in the image
    FILE *repP;
    int nReports;
};

static const struct
{
    const char *nameP;
    long value;
    int cls;
} opNames[] =
{
    { "dac", OP_DAC, IC_MEMREF }, { "jms", OP_JMS, IC_MEMREF }, { "dzm", OP_DZM, IC_MEMREF },
    { "lac", OP_LAC, IC_MEMREF }, { "xor", OP_XOR, IC_MEMREF }, { "add", OP_ADD, IC_MEMREF },
    { "tad", OP_TAD, IC_MEMREF }, { "xct", OP_XCT, IC_MEMREF }, { "isz", OP_ISZ, IC_MEMREF },
    { "and", OP_AND, IC_MEMREF }, { "sad", OP_SAD, IC_MEMREF }, { "jmp", OP_JMP, IC_MEMREF },
    { "sys", 0020000, IC_SYS }, { "law", 0760000, IC_LAW },
    { "eae", 0640000, IC_EAE }, { "osc", 0640001, IC_EAE }, { "omq", 0640002, IC_EAE },
    { "cmq", 0640004, IC_EAE }, { "div", 0640323, IC_EAE }, { "norm", 0640444, IC_EAE },
    { "lls", 0640600, IC_EAE }, { "clls", 0641600, IC_EAE }, { "als", 0640700, IC_EAE },
    { "lrs", 0640500, IC_EAE }, { "ecla", 0641000, IC_EAE }, { "lacs", 0641001, IC_EAE },
    { "lacq", 0641002, IC_EAE }, { "abs", 0644000, IC_EAE }, { "divs", 0644323, IC_EAE },
    { "clq", 0650000, IC_EAE }, { "frdiv", 0650323, IC_EAE }, { "lmq", 0652000, IC_EAE },
    { "mul", 0653122, IC_EAE }, { "idiv", 0653323, IC_EAE }, { "idivs", 0657323, IC_EAE },
    { "frdivs", 0654323, IC_EAE }, { "muls", 0657122, IC_EAE }, { "norms", 0660444, IC_EAE },
    { "gsm", 0664000, IC_EAE }, { "lrss", 0660500, IC_EAE }, { "llss", 0660600, IC_EAE },
    { "alss", 0660700, IC_EAE },
    { "opr", 0740000, IC_OPR }, { "nop", 0740000, IC_OPR }, { "cma", 0740001, IC_OPR },
    { "cml", 0740002, IC_OPR }, { "oas", 0740004, IC_OPR }, { "ral", 0740010, IC_OPR },
    { "rar", 0740020, IC_OPR }, { "hlt", 0740040, IC_OPR }, { "xx", 0740040, IC_OPR },
    { "sma", 0740100, IC_OPR }, { "sza", 0740200, IC_OPR }, { "snl", 0740400, IC_OPR },
    { "skp", 0741000, IC_OPR }, { "spa", 0741100, IC_OPR }, { "sna", 0741200, IC_OPR },
    { "szl", 0741400, IC_OPR }, { "rtl", 0742010, IC_OPR }, { "rtr", 0742020, IC_OPR },
    { "cll", 0744000, IC_OPR }, { "stl", 0744002, IC_OPR }, { "rcl", 0744010, IC_OPR },
    { "rcr", 0744020, IC_OPR }, { "cla", 0750000, IC_OPR }, { "clc", 0750001, IC_OPR },
    { "las", 0750004, IC_OPR }, { "glk", 0750010, IC_OPR },
};

static void report(struct tr *tP, long a, const char *fmtP, ...);
static void addLine(struct strList *lP, const char *fmtP, ...);
static void addList(struct strList *dP, const struct strList *sP);
static int inProg(struct tr *tP, long a);
static long wordValue(struct tr *tP, long a);
static int instClass(struct tr *tP, struct srcWord *sP);
static int isMemref(struct tr *tP, long a, long op);
static int isOneToOne(struct tr *tP, long a);
static const char *nameRef(struct tr *tP, long a, char *bufP);
static void dataRef(struct tr *tP, long from, long a, char *bufP);
static void checkSpan(struct tr *tP, long a);
static void buildImage(struct tr *tP);
static void classify(struct tr *tP);
static void nameWords(struct tr *tP);
static int successors(struct tr *tP, long a, int forFlow, long *succP, int *unknownP);
static void flow(struct tr *tP);
static unsigned transfer(struct tr *tP, long a, unsigned live);
static void liveness(struct tr *tP);
static int numericCma(struct tr *tP, long a);
static void fillLines(struct strList *lP, long mask);
static void shiftLines(struct strList *lP, const char *opP, long n);
static void translateMemref(struct tr *tP, long a);
static void translateOpr(struct tr *tP, long a);
static void translateEae(struct tr *tP, long a);
static void translateWord(struct tr *tP, long a);
static void dataText(struct tr *tP, long a, char *bufP);
static void compose(struct tr *tP);
static void sourceComment(struct tr *tP, long a, char *bufP, size_t size);
static void writeOutput(struct tr *tP, FILE *fP);
static void reportUnseen(struct tr *tP);

// A construct that cannot be translated, or must be checked by hand, by file and line.
static void report(struct tr *tP, long a, const char *fmtP, ...)
{
    va_list ap;
    struct srcWord *sP;
    const char *fileP;
    const char *slashP;

    sP = (inProg(tP, a) ? tP->wP[a].sP : NULL);
    fileP = "?";
    if(sP)
    {
        fileP = tP->aP->fileP[sP->fileNo];
        if((slashP = strrchr(fileP, '/')))
        {
            fileP = slashP + 1;
        }
        fprintf(tP->repP, "%s:%d: ", fileP, sP->lineNo);
    }
    else
    {
        fprintf(tP->repP, "%06lo: ", a);
    }
    va_start(ap, fmtP);
    vfprintf(tP->repP, fmtP, ap);
    va_end(ap);
    fprintf(tP->repP, "\n");
    tP->nReports++;
}

static void addLine(struct strList *lP, const char *fmtP, ...)
{
    va_list ap;
    char buf[MAXTEXT];

    if(lP->n >= MAXLINES)
    {
        fprintf(stderr, "as7am1: a word's translation is too long\n");
        exit(1);
    }
    va_start(ap, fmtP);
    vsnprintf(buf, sizeof(buf), fmtP, ap);
    va_end(ap);
    lP->lineP[lP->n++] = strdup(buf);
}

static void addList(struct strList *dP, const struct strList *sP)
{
    int k;

    for(k = 0; k < sP->n; k++)
    {
        addLine(dP, "%s", sP->lineP[k]);
    }
}

static int inProg(struct tr *tP, long a)
{
    return ((a >= tP->lo) && (a < tP->hi));
}

static long wordValue(struct tr *tP, long a)
{
    return (tP->wP[a].sP ? (tP->wP[a].sP->value & WORDMASK) : 0);
}

// The class of a word by its first syllable, an opcode as7 predefines and the program has
// not redefined; an IOT is a name whose value is an absolute 70xxxx (sop.s's definitions).
static int instClass(struct tr *tP, struct srcWord *sP)
{
    struct symEntry *eP;
    struct syllable *s0P;
    unsigned k;

    if(!sP->defined || (sP->nSyl == 0))
    {
        return (IC_NONE);
    }
    s0P = &sP->sylP[0];
    if((s0P->kind != SYL_SYM) || (s0P->op != '|'))
    {
        return (IC_NONE);
    }
    for(k = 0; k < (sizeof(opNames) / sizeof(opNames[0])); k++)
    {
        if(strcmp(opNames[k].nameP, s0P->text) == 0)
        {
            eP = symFind(&tP->aP->var, s0P->text);
            if(eP && eP->isDefined && (eP->value == opNames[k].value))
            {
                return (opNames[k].cls);
            }
            return (IC_NONE);
        }
    }
    if(!(s0P->value & RELATIVE) && (((s0P->value & WORDMASK) >> 12) == 070))
    {
        return (IC_IOT);
    }
    // A program may name a routine's call by assigning a jms of the routine to a name (as
    // fop.s does), and that name followed by an argument is the call.
    // It is a name alone whose relocatable value is a jms.
    // A label's value never has opcode bits.
    if((sP->nSyl == 1) && (s0P->value & RELATIVE) && ((s0P->value & 0740000L) == OP_JMS))
    {
        return (IC_MEMREF);
    }
    return (IC_NONE);
}

// 1 when the word at a is code, a memory reference with opcode op.
static int isMemref(struct tr *tP, long a, long op)
{
    return (inProg(tP, a) && (tP->wP[a].kind == WK_CODE) && (tP->wP[a].cls == IC_MEMREF) &&
            ((wordValue(tP, a) & 0740000L) == op));
}

// 1 when the word at a becomes exactly one PDP-1 word in the same place among its
// neighbors, so that an address computed from it stays right.
static int isOneToOne(struct tr *tP, long a)
{
    int k;

    if(!inProg(tP, a))
    {
        return (0);
    }
    k = tP->wP[a].kind;
    return ((k == WK_RESERVED) || (k == WK_ARG) || ((k == WK_DATA) && !tP->wP[a].isEntry));
}

// The am1 name of address a, its label or a reserved run's label plus the offset.
static const char *nameRef(struct tr *tP, long a, char *bufP)
{
    long r;

    if((a >= tP->hi) && (a < NADDR))
    {
        // Past the program, the free words a program keeps an array in (sh's args).
        snprintf(bufP, MAXREF, (a == tP->hi) ? "x_end" : "x_end+0%lo", (a - tP->hi));
        return (bufP);
    }
    if(!inProg(tP, a))
    {
        snprintf(bufP, MAXREF, "0%lo", (a & 07777));
        return (bufP);
    }
    if((tP->wP[a].kind == WK_RESERVED) && !tP->wP[a].runStart)
    {
        for(r = a; !tP->wP[r].runStart; r--)
        {
        }
        snprintf(bufP, MAXREF, "%s+0%lo", tP->wP[r].name, (a - r));
        return (bufP);
    }
    snprintf(bufP, MAXREF, "%s", tP->wP[a].name);
    return (bufP);
}

// The am1 text for a data reference to address a made by the word at from.
// It is a's name when a keeps its place, else the expression's one relocatable base plus the
// offset, when that base keeps its place (law xbuf-1 next to code that grew), else a's name
// (a pointer to code).
static void dataRef(struct tr *tP, long from, long a, char *bufP)
{
    struct srcWord *sP;
    long base;
    long k;
    int n;
    int nRel;
    char name[MAXREF];

    if(isOneToOne(tP, a) || !tP->wP[from].sP)
    {
        nameRef(tP, a, bufP);
        return;
    }
    sP = tP->wP[from].sP;
    nRel = 0;
    base = 0;
    for(n = 0; n < sP->nSyl; n++)
    {
        if(sP->sylP[n].value & RELATIVE)
        {
            nRel++;
            if(sP->sylP[n].op != '-')
            {
                base = (relocate(tP->aP, sP->sylP[n].value) & 017777L);
            }
        }
    }
    if((nRel == 1) && (base != a) && isOneToOne(tP, base))
    {
        k = a - base;
        nameRef(tP, base, name);
        snprintf(bufP, MAXTEXT, "%s%c0%lo", name, ((k < 0) ? '-' : '+'), ((k < 0) ? -k : k));
        return;
    }
    nameRef(tP, a, bufP);
}

// A value that counts the words between two places is wrong where code between them grew.
static void checkSpan(struct tr *tP, long a)
{
    struct srcWord *sP;
    long x;

    sP = tP->wP[a].sP;
    if(!sP || !sP->hasSpan)
    {
        return;
    }
    for(x = sP->spanLo; x < sP->spanHi; x++)
    {
        if(inProg(tP, x) && (tP->wP[x].kind == WK_CODE))
        {
            report(tP, a, "a difference of two places spans code, whose length changes");
            return;
        }
    }
}

// The words by address, the last store winning as in as7's memory, and the program's span.
static void buildImage(struct tr *tP)
{
    struct as7 *aP;
    struct srcWord *sP;
    long k;
    long end;

    aP = tP->aP;
    tP->wP = calloc(NADDR + 1, sizeof(struct tWord));
    tP->lo = (symFind(&aP->var, "..")->value & WORDMASK);
    end = relocate(aP, symFind(&aP->var, ".")->value);
    tP->lastStored = tP->lo - 1;
    for(k = 0; k < aP->nWords; k++)
    {
        sP = &aP->wordP[k];
        if((sP->loc < tP->lo) || (sP->loc >= NADDR))
        {
            fprintf(tP->repP, "%06lo: a word stored outside the program's addresses\n",
                    sP->loc);
            tP->nReports++;
            continue;
        }
        if(tP->wP[sP->loc].sP)
        {
            tP->hi = NADDR;
            report(tP, sP->loc, "a second word stored at this address");
        }
        tP->wP[sP->loc].sP = sP;
        if(sP->loc > tP->lastStored)
        {
            tP->lastStored = sP->loc;
        }
    }
    tP->hi = ((end > tP->lastStored) ? end : (tP->lastStored + 1));
    if(tP->hi > NADDR)
    {
        tP->hi = NADDR;
    }
}

// Code, arguments, data and reserved words; the routines' entries and their callers; the
// words used as pointers.
static void classify(struct tr *tP)
{
    struct tWord *wP;
    struct srcWord *sP;
    long a;
    long k;
    long v;
    long target;
    struct tWord *eP;

    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if(!wP->sP)
        {
            wP->kind = WK_RESERVED;
            continue;
        }
        wP->cls = instClass(tP, wP->sP);
        wP->kind = ((wP->cls != IC_NONE) ? WK_CODE : WK_DATA);
    }

    // Inline arguments, the rest of a jms's or a sys's line and the operand after mul or div.
    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if(wP->kind != WK_CODE)
        {
            continue;
        }
        v = wordValue(tP, a);
        if(((wP->cls == IC_MEMREF) && ((v & 0740000L) == OP_JMS)) || (wP->cls == IC_SYS))
        {
            sP = wP->sP;
            for(k = a + 1; inProg(tP, k) && tP->wP[k].sP; k++)
            {
                if((tP->wP[k].sP->fileNo != sP->fileNo) || (tP->wP[k].sP->lineNo != sP->lineNo))
                {
                    break;
                }
                tP->wP[k].kind = WK_ARG;
                wP->nArgs++;
            }
        }
        else if((wP->cls == IC_EAE) && ((((v & EAE_CLASS) == 0100) || ((v & EAE_CLASS) == 0300))))
        {
            if(inProg(tP, a + 1) && tP->wP[a + 1].sP)
            {
                tP->wP[a + 1].kind = WK_ARG;
                wP->nArgs = 1;
            }
            else
            {
                report(tP, a, "mul or div without its operand word");
            }
        }
    }

    // Entries and pointers.
    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if((wP->kind != WK_CODE) || (wP->cls != IC_MEMREF))
        {
            continue;
        }
        v = wordValue(tP, a);
        target = (v & 017777L);
        if(!wP->sP->rel || !inProg(tP, target))
        {
            continue;
        }
        if(v & 020000L)
        {
            tP->wP[target].isPointer = 1;
            continue;
        }
        if(((v & 0740000L) == OP_DAC) || ((v & 0740000L) == OP_DZM))
        {
            tP->wP[target].isStored = 1;
        }
        if((v & 0740000L) == OP_JMS)
        {
            eP = &tP->wP[target];
            if(eP->kind != WK_DATA)
            {
                report(tP, a, "jms to a word that is not a routine's return word");
                continue;
            }
            eP->isEntry = 1;
            eP->contP = realloc(eP->contP, (eP->nCont + 1) * sizeof(long));
            eP->contP[eP->nCont++] = a;
        }
    }

    // An isz is a count, whose skip is kept, unless an indirect jmp (a return) follows it,
    // the word after next is not code for the skip to reach, or its word is an auto-index
    // register, an increment that never reaches zero.
    // Not only a loop's back jump follows a count.
    // In sh, an isz followed by a skp and a jmp ends chdir's loop, and two isz in a row count
    // with the first.
    // A word used as a pointer may be a count too (stat's octal counts digits in c, then
    // walks the buffer with it).
    // Kept where the word never reaches zero, the skip costs a sza and changes nothing.
    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if(!isMemref(tP, a, OP_ISZ))
        {
            continue;
        }
        v = wordValue(tP, a);
        target = (v & 017777L);
        if(wP->sP->rel && inProg(tP, target) && tP->wP[target].isEntry && !(v & 020000L))
        {
            tP->wP[target].nIsz++;
            continue;
        }
        wP->isCount = (inProg(tP, a + 1) && (tP->wP[a + 1].kind == WK_CODE) &&
                       !(isMemref(tP, a + 1, OP_JMP) && (wordValue(tP, a + 1) & 020000L)) &&
                       inProg(tP, a + 2) && ((tP->wP[a + 2].kind == WK_CODE) ||
                       ((tP->wP[a + 2].kind == WK_DATA) && !tP->wP[a + 2].isEntry &&
                       (wordValue(tP, a + 2) >= 0760000L))) &&
                       !(!wP->sP->rel && (target >= 010) && (target <= 017)));
        if(tP->oP->naive & NAIVE_ISZ)
        {
            wP->isCount = 1;
        }
    }
}

// A routine called through a name for its call (in fop.s, each such name is a jms of an entry
// named q, defined again for every routine) is named for that call, u_ and the name, so that
// a hand-written routine can take its place under the same name.
// The first name in alphabetical order wins.
static void nameAliases(struct tr *tP, struct symTab *usedP)
{
    struct symEntry *eP;
    struct symEntry *bestP;
    long v;
    int n;
    int k;
    int ok;
    char name[MAXNAME];

    for(;;)
    {
        bestP = NULL;
        for(n = 0; n < HASHSIZE; n++)
        {
            for(eP = tP->aP->var.bucketP[n]; eP; eP = eP->nextP)
            {
                if(!eP->isDefined || !(eP->value & RELATIVE) ||
                   ((eP->value & 0740000L) != OP_JMS))
                {
                    continue;
                }
                v = (relocate(tP->aP, eP->value) & 017777L);
                if(!inProg(tP, v) || tP->wP[v].name[0])
                {
                    continue;
                }
                snprintf(name, sizeof(name), "u_%s", eP->nameP);
                ok = 1;
                for(k = 2; name[k]; k++)
                {
                    if(!isalnum((unsigned char)name[k]) && (name[k] != '_'))
                    {
                        ok = 0;
                    }
                }
                if(ok && !symFind(usedP, name) &&
                   (!bestP || (strcmp(eP->nameP, bestP->nameP) < 0)))
                {
                    bestP = eP;
                }
            }
        }
        if(!bestP)
        {
            return;
        }
        v = (relocate(tP->aP, bestP->value) & 017777L);
        snprintf(name, sizeof(name), "u_%s", bestP->nameP);
        symSet(usedP, name, v);
        snprintf(tP->wP[v].name, MAXNAME, "%s", name);
    }
}

// Every word's am1 name, its as7 global label with a u_ prefix where that is a usable am1
// name, else w_ and its PDP-7 address.
// The prefixes keep every name clear of am1's names and of each other; r_ names the routines' return words, s_ the jumps inside a skip, x_ the
// translator's own words.
static void nameWords(struct tr *tP)
{
    struct symTab used;
    struct symEntry *eP;
    struct symEntry **listP;
    long a;
    long v;
    int n;
    int count;
    int k;
    int ok;
    char name[MAXNAME];

    symInit(&used);
    nameAliases(tP, &used);
    count = 0;
    for(n = 0; n < HASHSIZE; n++)
    {
        for(eP = tP->aP->glabel.bucketP[n]; eP; eP = eP->nextP)
        {
            count++;
        }
    }
    listP = malloc((count + 1) * sizeof(*listP));
    count = 0;
    for(n = 0; n < HASHSIZE; n++)
    {
        for(eP = tP->aP->glabel.bucketP[n]; eP; eP = eP->nextP)
        {
            listP[count++] = eP;
        }
    }

    // The alphabetically first usable name of an address wins, so the output is stable.
    for(n = 0; n < count; n++)
    {
        for(k = n + 1; k < count; k++)
        {
            if(strcmp(listP[k]->nameP, listP[n]->nameP) < 0)
            {
                eP = listP[n];
                listP[n] = listP[k];
                listP[k] = eP;
            }
        }
    }
    for(n = 0; n < count; n++)
    {
        v = relocate(tP->aP, listP[n]->value);
        if(!(listP[n]->value & RELATIVE) || !inProg(tP, v) || tP->wP[v].name[0])
        {
            continue;
        }
        ok = 1;
        snprintf(name, sizeof(name), "u_%s", listP[n]->nameP);
        for(k = 2; name[k]; k++)
        {
            if(name[k] == '.')
            {
                name[k] = '_';
            }
            else if(!isalnum((unsigned char)name[k]) && (name[k] != '_'))
            {
                ok = 0;
            }
        }
        if(ok && !symFind(&used, name))
        {
            symSet(&used, name, v);
            snprintf(tP->wP[v].name, MAXNAME, "%s", name);
        }
    }
    free(listP);

    for(a = tP->lo; a < tP->hi; a++)
    {
        if(!tP->wP[a].name[0])
        {
            snprintf(tP->wP[a].name, MAXNAME, "w_%05lo", a);
        }
        else if(tP->wP[a].kind == WK_RESERVED)
        {
            tP->wP[a].runStart = 1;
        }
        if((tP->wP[a].kind == WK_RESERVED) &&
           ((a == tP->lo) || (tP->wP[a - 1].kind != WK_RESERVED) || (a == tP->lastStored + 1)))
        {
            tP->wP[a].runStart = 1;
        }
    }
}

// The PDP-7 successors of the executed word at a, into succP; *unknownP is set where a jump
// goes through a pointer the translator cannot follow.
// For the flow, jms also continues after its arguments; for liveness it goes only into the
// routine, whose returns go back to every caller's continuation and the word after it (a
// skip return).
// Returns the count.
static int successors(struct tr *tP, long a, int forFlow, long *succP, int *unknownP)
{
    struct tWord *wP;
    struct tWord *eP;
    long v;
    long op;
    long target;
    long c;
    long b;
    int n;
    int k;
    int j;

    wP = &tP->wP[a];
    v = wordValue(tP, a);
    n = 0;
    *unknownP = 0;
    switch(wP->cls)
    {
    case IC_MEMREF:
        op = (v & 0740000L);
        target = (v & 017777L);
        if(!wP->sP->rel && (target == USERBASE))
        {
            target = tP->lo;
        }
        if(op == OP_JMP)
        {
            if(!(v & 020000L))
            {
                if(inProg(tP, target))
                {
                    succP[n++] = target;
                }
            }
            else if(wP->sP->rel && inProg(tP, target) && tP->wP[target].isEntry)
            {
                eP = &tP->wP[target];
                for(k = 0; k < eP->nCont; k++)
                {
                    // A routine returns past its call's arguments, and one word further
                    // for each isz of its return word.
                    c = eP->contP[k];
                    for(j = tP->wP[c].nArgs; (j <= tP->wP[c].nArgs) || (j <= eP->nIsz); j++)
                    {
                        if(n >= MAXSUCC)
                        {
                            fprintf(stderr, "as7am1: too many returns from one routine\n");
                            exit(1);
                        }
                        // A skip return is taken only to an instruction.
                        // A routine's two isz of its return may be on two paths, each
                        // returning one past.
                        b = c + 1 + j;
                        if((j == tP->wP[c].nArgs) || (inProg(tP, b) &&
                           ((tP->wP[b].kind == WK_CODE) || ((tP->wP[b].kind == WK_DATA) &&
                           !tP->wP[b].isEntry && (wordValue(tP, b) >= 0760000L)))))
                        {
                            succP[n++] = b;
                        }
                    }
                }
                if(eP->nCont == 0)
                {
                    *unknownP = 1;
                }
            }
            else
            {
                *unknownP = 1;
            }
        }
        else if(op == OP_JMS)
        {
            if(!(v & 020000L) && inProg(tP, target) && tP->wP[target].isEntry)
            {
                succP[n++] = target + 1;
            }
            else
            {
                *unknownP = 1;
            }
            if(forFlow)
            {
                succP[n++] = a + 1 + wP->nArgs;
            }
        }
        else if(((op == OP_ISZ) && wP->isCount) || (op == OP_SAD) || (op == OP_XCT))
        {
            succP[n++] = a + 1;
            succP[n++] = a + 2;
        }
        else
        {
            succP[n++] = a + 1;
        }
        break;
    case IC_OPR:
        if(v & OPR_HLT)
        {
            break;
        }
        succP[n++] = a + 1;
        if(v & (OPR_SMA | OPR_SZA | OPR_SNL | OPR_SKP))
        {
            succP[n++] = a + 2;
        }
        break;
    case IC_SYS:
        if((v - 0020000L) != SYS_EXIT)
        {
            succP[n++] = a + 1 + wP->nArgs;
        }
        break;
    case IC_EAE:
        succP[n++] = a + 1 + wP->nArgs;
        break;
    case IC_IOT:
        succP[n++] = a + 1;
        succP[n++] = a + 2;
        break;
    default:
        succP[n++] = a + 1;
        break;
    }
    return (n);
}

// Marks what is executed, from the program's first word and from every code word; a data
// word reached is executed, which only a negative literal may be.
static void flow(struct tr *tP)
{
    long *stackP;
    long succ[MAXSUCC];
    long a;
    long b;
    long v;
    int sp;
    int n;
    int k;
    int unknown;
    struct tWord *wP;
    long *predP;
    char *queuedP;

    // Each address is queued once, with the word the flow came from, for the reports.
    stackP = malloc(NADDR * sizeof(long));
    predP = malloc(NADDR * sizeof(long));
    queuedP = calloc(NADDR, 1);
    sp = 0;
    for(a = tP->hi - 1; a >= tP->lo; a--)
    {
        if((a == tP->lo) || (tP->wP[a].kind == WK_CODE))
        {
            stackP[sp++] = a;
            predP[a] = -1;
            queuedP[a] = 1;
        }
    }
    while(sp > 0)
    {
        a = stackP[--sp];
        wP = &tP->wP[a];
        wP->executed = 1;
        if(wP->kind == WK_DATA)
        {
            v = wordValue(tP, a);
            if(!wP->isEntry && (v >= 0760000L))
            {
                wP->kind = WK_CODE;
                wP->cls = IC_LITERAL;
            }
            else
            {
                report(tP, a, "a data word is executed (%06lo), reached from %06lo", v, predP[a]);
                continue;
            }
        }
        else if(wP->kind != WK_CODE)
        {
            report(tP, a, "the flow reaches an argument or a reserved word, from %06lo",
                   predP[a]);
            continue;
        }
        n = successors(tP, a, 1, succ, &unknown);
        for(k = 0; k < n; k++)
        {
            b = succ[k];
            if(inProg(tP, b) && !queuedP[b])
            {
                queuedP[b] = 1;
                predP[b] = a;
                stackP[sp++] = b;
            }
        }
    }
    free(stackP);
    free(predP);
    free(queuedP);
}

// Live registers before the word at a, from those after it, by the word's events in reverse
// order.
// The operate group senses, then clears, complements and rotates; the EAE's event 1 copies
// AC's sign to the link and clears MQ, event 2 loads MQ and clears AC (F-75).
static unsigned transfer(struct tr *tP, long a, unsigned live)
{
    struct tWord *wP;
    long v;
    long op;
    long cls;
    int sign;
    unsigned need;

    wP = &tP->wP[a];
    v = wordValue(tP, a);
    switch(wP->cls)
    {
    case IC_MEMREF:
        op = (v & 0740000L);
        if(op == OP_LAC)
        {
            live &= ~LIVE_AC;
        }
        else if(op == OP_TAD)
        {
            live = ((live & ~LIVE_L) | LIVE_AC);
        }
        else if((op == OP_DAC) || (op == OP_ADD) || (op == OP_XOR) || (op == OP_AND) ||
                (op == OP_SAD))
        {
            live |= LIVE_AC;
        }
        else if(op == OP_XCT)
        {
            live |= (LIVE_AC | LIVE_MQ | LIVE_L);
        }
        break;
    case IC_LAW:
    case IC_LITERAL:
        live &= ~LIVE_AC;
        break;
    case IC_SYS:
        live = (LIVE_AC | (((v - 0020000L) == SYS_SMES) ? LIVE_MQ : 0));
        break;
    case IC_IOT:
        live |= (LIVE_AC | LIVE_MQ);
        break;
    case IC_OPR:
        if(v & (OPR_RAL | OPR_RAR))
        {
            // The rotate sets the link from AC; the old link matters only in AC.
            need = (((live & (LIVE_AC | LIVE_L)) ? LIVE_AC : 0) | ((live & LIVE_AC) ? LIVE_L : 0));
            live = ((live & ~(LIVE_AC | LIVE_L)) | need);
        }
        if(v & OPR_OAS)
        {
            live |= LIVE_AC;
        }
        if(v & OPR_CML)
        {
            live |= LIVE_L;
        }
        if(v & OPR_CMA)
        {
            live |= LIVE_AC;
        }
        if(v & OPR_CLL)
        {
            live &= ~LIVE_L;
        }
        if(v & OPR_CLA)
        {
            live &= ~LIVE_AC;
        }
        if(v & (OPR_SMA | OPR_SZA))
        {
            live |= LIVE_AC;
        }
        if(v & OPR_SNL)
        {
            live |= LIVE_L;
        }
        break;
    case IC_EAE:
        sign = ((v & EAE_SIGN) != 0);
        cls = (v & EAE_CLASS);
        if((v == EAE_MUL) || (v == EAE_IDIV))
        {
            return (LIVE_AC);
        }
        if(v == EAE_DIV)
        {
            return (LIVE_AC | LIVE_MQ);
        }
        if(cls == 0500)
        {
            need = 0;
            need |= ((live & (LIVE_AC | LIVE_MQ)) ? LIVE_AC : 0);
            need |= ((live & LIVE_MQ) ? LIVE_MQ : 0);
            need |= ((!sign && (live & LIVE_AC)) ? LIVE_L : 0);
            live = ((live & ~(LIVE_AC | LIVE_MQ)) | need);
        }
        else if(cls == 0600)
        {
            need = 0;
            need |= ((live & LIVE_AC) ? LIVE_AC : 0);
            need |= ((live & (LIVE_AC | LIVE_MQ)) ? LIVE_MQ : 0);
            need |= ((!sign && (live & (LIVE_AC | LIVE_MQ))) ? LIVE_L : 0);
            live = ((live & ~(LIVE_AC | LIVE_MQ)) | need);
        }
        else if(cls == 0700)
        {
            need = (((live & LIVE_AC) && !sign) ? LIVE_L : 0);
            live |= need;
        }
        else if(cls == 0)
        {
            if(v & 04)
            {
                live |= LIVE_MQ;
            }
            if(v & 02)
            {
                live |= (LIVE_MQ | ((v & EAE_CLRAC) ? 0 : LIVE_AC));
                live &= ((v & EAE_CLRAC) ? ~LIVE_AC : ~0u);
                live |= LIVE_MQ;
            }
        }
        else
        {
            live |= (LIVE_AC | LIVE_MQ | LIVE_L);
        }
        if(v & EAE_CLRAC)
        {
            live &= ~LIVE_AC;
        }
        if(v & EAE_MQORAC)
        {
            live = ((live & ~LIVE_MQ) | LIVE_AC | ((v & EAE_CLRMQ) ? 0 : LIVE_MQ));
        }
        if(v & EAE_CLRMQ)
        {
            live &= ~LIVE_MQ;
        }
        if(sign)
        {
            live = ((live & ~LIVE_L) | LIVE_AC);
        }
        break;
    default:
        break;
    }
    return (live);
}

// Live registers after every executed word, to a fixed point.
// A jump the translator cannot follow keeps AC and MQ live, not the link.
// A carry out of tad is not carried that far.
static void liveness(struct tr *tP)
{
    long succ[MAXSUCC];
    long a;
    unsigned out;
    unsigned in;
    int changed;
    int unknown;
    int n;
    int k;

    do
    {
        changed = 0;
        for(a = tP->hi - 1; a >= tP->lo; a--)
        {
            if((tP->wP[a].kind != WK_CODE) || !tP->wP[a].executed)
            {
                continue;
            }
            out = 0;
            n = successors(tP, a, 0, succ, &unknown);
            for(k = 0; k < n; k++)
            {
                if(inProg(tP, succ[k]))
                {
                    out |= tP->wP[succ[k]].liveIn;
                }
            }
            if(unknown)
            {
                out |= (LIVE_AC | LIVE_MQ);
            }
            in = transfer(tP, a, out);
            if((out != tP->wP[a].liveOut) || (in != tP->wP[a].liveIn))
            {
                tP->wP[a].liveOut = out;
                tP->wP[a].liveIn = in;
                changed = 1;
            }
        }
    } while(changed);
}

// 1 when cma's result is used as a number (PDP-7's ~x is -x-1, the PDP-1's -x), 0 when it
// is used as bits (and, xor) or not used.
// The first word after it that uses AC decides.
static int numericCma(struct tr *tP, long a)
{
    long b;
    long v;
    long op;
    int k;

    for(k = 0, b = a + 1; (k < 8) && inProg(tP, b); k++, b++)
    {
        if(tP->wP[b].kind != WK_CODE)
        {
            return (1);
        }
        v = wordValue(tP, b);
        switch(tP->wP[b].cls)
        {
        case IC_MEMREF:
            op = (v & 0740000L);
            if((op == OP_AND) || (op == OP_XOR) || (op == OP_LAC))
            {
                return (0);
            }
            if((op == OP_DZM) || (op == OP_ISZ))
            {
                continue;
            }
            return (1);
        case IC_LAW:
        case IC_LITERAL:
            return (0);
        case IC_OPR:
            if((v & OPR_CLA) && !(v & (OPR_SMA | OPR_SZA)))
            {
                return (0);
            }
            if(v & (OPR_SMA | OPR_SZA | OPR_CMA | OPR_RAL | OPR_RAR | OPR_OAS))
            {
                return (1);
            }
            continue;
        default:
            return (1);
        }
    }
    return (1);
}

// AC |= (link ? mask : 0), the link's sign spread over AC by two arithmetic shifts, AC kept
// in x_t meanwhile.
static void fillLines(struct strList *lP, long mask)
{
    addLine(lP, "dac x_t");
    addLine(lP, "lac x_lnk");
    addLine(lP, "sar 9s");
    addLine(lP, "sar 8s");
    addLine(lP, "and [0%06lo]", (mask & WORDMASK));
    addLine(lP, "ior x_t");
}

// A shift or rotate of n places, nine at most an instruction (F15D, the shift group).
static void shiftLines(struct strList *lP, const char *opP, long n)
{
    while(n > 0)
    {
        addLine(lP, "%s %lds", opP, ((n > 9) ? 9 : n));
        n -= ((n > 9) ? 9 : n);
    }
}

static void translateMemref(struct tr *tP, long a)
{
    struct tWord *wP;
    struct tWord *xP;
    long v;
    long op;
    long target;
    int ind;
    int rel;
    int autoIdx;
    int entry;
    int liveAc;
    const char *mnP;
    char opnd[MAXTEXT];
    char ret[MAXTEXT];

    wP = &tP->wP[a];
    v = wordValue(tP, a);
    op = (v & 0740000L);
    ind = ((v & 020000L) != 0);
    target = (v & 017777L);
    rel = wP->sP->rel;
    autoIdx = (!rel && ind && (target >= 010) && (target <= 017));
    entry = (rel && inProg(tP, target) && tP->wP[target].isEntry);
    liveAc = ((wP->liveOut & LIVE_AC) != 0);
    snprintf(ret, sizeof(ret), "r_%s", entry ? tP->wP[target].name + 2 : "");

    // The operand, a label, a bank-relative address, or an auto-index register.
    if(rel)
    {
        if(target < tP->lo)
        {
            report(tP, a, "an address outside the program");
        }
        if((op == OP_JMP) || (op == OP_JMS) || (op == OP_XCT))
        {
            nameRef(tP, target, opnd);
        }
        else
        {
            dataRef(tP, a, target, opnd);
        }
        xP = (inProg(tP, target) ? &tP->wP[target] : NULL);
        if(xP && !ind && (xP->kind == WK_CODE) && (op != OP_JMP) && (op != OP_JMS) &&
           (op != OP_XCT))
        {
            report(tP, a, "code read or written as data (self-modifying code)");
        }
    }
    else if(target >= USERBASE)
    {
        snprintf(opnd, sizeof(opnd), "0%lo", (target - USERBASE));
        if((target > tP->lo) && (target < tP->hi))
        {
            report(tP, a, "a numeric address inside the program");
        }
        if(wP->sP->hasSpan)
        {
            report(tP, a, "an address computed from a difference of labels");
        }
    }
    else
    {
        snprintf(opnd, sizeof(opnd), "0%lo", target);
        if((target < 010) || (target > 017))
        {
            report(tP, a, "a reference to low memory, the PDP-7 kernel's");
        }
    }
    checkSpan(tP, a);
    if(ind && entry)
    {
        // An argument read or stepped through the return, whose address is in r_S.
        snprintf(opnd, sizeof(opnd), "%s", ret);
    }

    if(autoIdx && (tP->oP->naive & NAIVE_AIDX))
    {
        autoIdx = 0;
    }

    switch(op)
    {
    case OP_JMS:
        if(ind)
        {
            report(tP, a, "jms through a pointer");
        }
        addLine(&wP->body, "jda %s", opnd);
        return;
    case OP_JMP:
        if(ind && entry)
        {
            addLine(&wP->body, (tP->oP->naive & NAIVE_JDA) ? "jmp i %s" : "jmp %s", ret);
        }
        else if(autoIdx)
        {
            report(tP, a, "jmp through an auto-index register");
            addLine(&wP->body, "jmp i %s", opnd);
        }
        else
        {
            addLine(&wP->body, ind ? "jmp i %s" : "jmp %s", opnd);
        }
        return;
    case OP_ISZ:
        if(entry && !ind)
        {
            if(liveAc)
            {
                addLine(&wP->body, "dac x_ac");
            }
            addLine(&wP->body, "idx %s", ret);
            if(liveAc)
            {
                addLine(&wP->body, "lac x_ac");
            }
            return;
        }
        if(autoIdx)
        {
            report(tP, a, "isz through an auto-index register");
        }
        if(wP->isCount)
        {
            // idx and a test of AC for +0, the PDP-7's skip at zero, whether the word counts
            // up from a negative or is an increment never meant to reach zero, where isp would
            // skip at every positive value.
            // idx takes -1 to +0.
            wP->hasSkip = 1;
            if(liveAc)
            {
                addLine(&wP->pre, "dac x_ac");
                addLine(&wP->post, "lac x_ac");
            }
            if(tP->oP->naive & NAIVE_ISZ)
            {
                snprintf(wP->skip, sizeof(wP->skip), ind ? "isp i %s" : "isp %s", opnd);
                return;
            }
            addLine(&wP->pre, ind ? "idx i %s" : "idx %s", opnd);
            snprintf(wP->skip, sizeof(wP->skip), "sza");
            snprintf(wP->skipInv, sizeof(wP->skipInv), "sza i");
            return;
        }
        if(liveAc)
        {
            addLine(&wP->body, "dac x_ac");
        }
        addLine(&wP->body, ind ? "idx i %s" : "idx %s", opnd);
        if(liveAc)
        {
            addLine(&wP->body, "lac x_ac");
        }
        return;
    case OP_SAD:
        wP->hasSkip = 1;
        if(autoIdx)
        {
            addLine(&wP->pre, "dac x_ac");
            addLine(&wP->pre, "idx %s", opnd);
            addLine(&wP->pre, "lac x_ac");
            snprintf(wP->skip, sizeof(wP->skip), "sad i %s", opnd);
            snprintf(wP->skipInv, sizeof(wP->skipInv), "sas i %s", opnd);
        }
        else if(ind && entry)
        {
            snprintf(wP->skip, sizeof(wP->skip), "sad i %s", ret);
            snprintf(wP->skipInv, sizeof(wP->skipInv), "sas i %s", ret);
        }
        else
        {
            snprintf(wP->skip, sizeof(wP->skip), ind ? "sad i %s" : "sad %s", opnd);
            snprintf(wP->skipInv, sizeof(wP->skipInv), ind ? "sas i %s" : "sas %s", opnd);
        }
        return;
    default:
        break;
    }

    switch(op)
    {
    case OP_DAC:
        mnP = "dac";
        break;
    case OP_DZM:
        mnP = "dzm";
        break;
    case OP_LAC:
        mnP = "lac";
        break;
    case OP_XOR:
        mnP = "xor";
        break;
    case OP_ADD:
    case OP_TAD:
        mnP = "add";
        break;
    case OP_XCT:
        mnP = "xct";
        break;
    default:
        mnP = "and";
        break;
    }
    if((op == OP_TAD) && (wP->liveOut & LIVE_L))
    {
        report(tP, a, "tad's carry into the link is used");
    }
    if(entry && !ind)
    {
        report(tP, a, "a routine's return word used as a value");
    }
    if(autoIdx)
    {
        if(op == OP_XCT)
        {
            report(tP, a, "xct through an auto-index register");
        }
        if(op == OP_LAC)
        {
            addLine(&wP->body, "idx %s", opnd);
        }
        else if((op == OP_DZM) && !liveAc)
        {
            addLine(&wP->body, "idx %s", opnd);
        }
        else
        {
            addLine(&wP->body, "dac x_ac");
            addLine(&wP->body, "idx %s", opnd);
            addLine(&wP->body, "lac x_ac");
        }
        addLine(&wP->body, "%s i %s", mnP, opnd);
        return;
    }
    if(ind && entry)
    {
        addLine(&wP->body, "%s i %s", mnP, ret);
        return;
    }
    addLine(&wP->body, ind ? "%s i %s" : "%s %s", mnP, opnd);
}

// The operate group, the skip senses first, then cla and cll, then cma and cml, then the
// rotate (F-75, the operate instructions).
// The link is the sign of x_lnk; a rotate through it goes through IO (F15D, where rcl and rcr
// rotate AC and IO together), parked when MQ is live.
static void translateOpr(struct tr *tP, long a)
{
    struct tWord *wP;
    struct strList ops;
    long v;
    int park;
    int steps;
    int k;
    const char *condP;

    wP = &tP->wP[a];
    v = wordValue(tP, a);
    memset(&ops, 0, sizeof(ops));
    park = ((wP->liveOut & LIVE_MQ) != 0);

    if(v & OPR_CLA)
    {
        addLine(&ops, "cla");
    }
    if(v & OPR_CLL)
    {
        addLine(&ops, "dzm x_lnk");
    }
    if(v & OPR_CMA)
    {
        addLine(&ops, "cma");
        if(!(tP->oP->naive & NAIVE_CMA) && numericCma(tP, a))
        {
            addLine(&ops, "sub [1]");
        }
    }
    if(v & OPR_CML)
    {
        addLine(&ops, "dac x_t");
        addLine(&ops, "lac x_lnk");
        addLine(&ops, "cma");
        addLine(&ops, "dac x_lnk");
        addLine(&ops, "lac x_t");
    }
    if(v & OPR_OAS)
    {
        addLine(&ops, "dac x_t");
        addLine(&ops, "lat");
        addLine(&ops, "ior x_t");
    }
    if(v & (OPR_RAL | OPR_RAR))
    {
        steps = ((v & OPR_TWO) ? 2 : 1);
        if(tP->oP->naive & NAIVE_LINK)
        {
            addLine(&ops, "%s %ds", (v & OPR_RAL) ? "ral" : "rar", steps);
        }
        else
        {
            if(park)
            {
                addLine(&ops, "dio x_io");
            }
            addLine(&ops, "lio x_lnk");
            for(k = 0; k < steps; k++)
            {
                if(v & OPR_RAL)
                {
                    addLine(&ops, "rcl 1s");
                    addLine(&ops, "rir 1s");
                }
                else
                {
                    addLine(&ops, "ril 1s");
                    addLine(&ops, "rcr 1s");
                }
            }
            addLine(&ops, "dio x_lnk");
            if(park)
            {
                addLine(&ops, "lio x_io");
            }
        }
    }
    if(v & OPR_HLT)
    {
        report(tP, a, "hlt would stop the whole PDP-1");
        addLine(&ops, "hlt");
    }

    if(!(v & (OPR_SMA | OPR_SZA | OPR_SNL | OPR_SKP)))
    {
        addList(&wP->body, &ops);
        return;
    }

    wP->hasSkip = 1;
    addList(&wP->post, &ops);
    if(v & OPR_SNL)
    {
        if(v & (OPR_SMA | OPR_SZA))
        {
            report(tP, a, "a skip on the link and on AC in one word");
        }
        if(park)
        {
            addLine(&wP->pre, "dio x_io");
            // The restore must come before the operations, which may use IO themselves.
            memmove(&wP->post.lineP[1], &wP->post.lineP[0], wP->post.n * sizeof(char *));
            wP->post.lineP[0] = strdup("lio x_io");
            wP->post.n++;
        }
        addLine(&wP->pre, "lio x_lnk");
        snprintf(wP->skip, sizeof(wP->skip), (v & OPR_SKP) ? "spi" : "spi i");
        snprintf(wP->skipInv, sizeof(wP->skipInv), (v & OPR_SKP) ? "spi i" : "spi");
        return;
    }
    if(!(v & (OPR_SMA | OPR_SZA)))
    {
        // skp alone always skips, since AC's sign is either plus or minus.
        snprintf(wP->skip, sizeof(wP->skip), "sma spa");
        snprintf(wP->skipInv, sizeof(wP->skipInv), "sma spa i");
        return;
    }
    condP = (((v & OPR_SMA) && (v & OPR_SZA)) ? "sma sza" : ((v & OPR_SMA) ? "sma" : "sza"));
    snprintf(wP->skip, sizeof(wP->skip), (v & OPR_SKP) ? "%s i" : "%s", condP);
    snprintf(wP->skipInv, sizeof(wP->skipInv), (v & OPR_SKP) ? "%s" : "%s i", condP);
}

// The EAE, MQ being IO.
// mul, idiv and div take the word after them as the operand and jump over it; the shifts
// fill from the link as the PDP-7's do, the signed ones having first copied AC's sign into it
// (F-75, EAE bit assignments and the shift descriptions).
static void translateEae(struct tr *tP, long a)
{
    struct tWord *wP;
    struct strList *lP;
    long v;
    long n;
    long cls;
    long low;
    int naive;
    char opnd[MAXTEXT];
    char next[MAXTEXT];

    wP = &tP->wP[a];
    lP = &wP->body;
    v = wordValue(tP, a);
    naive = ((tP->oP->naive & NAIVE_FILL) != 0);
    cls = (v & EAE_CLASS);
    n = (v & EAE_STEP);

    if((cls == 0100) || (cls == 0300))
    {
        nameRef(tP, a + 1, opnd);
        nameRef(tP, a + 2, next);
        // The PDP-7's EAE takes the operand as 18 unsigned bits, the PDP-1's mul and div
        // as a signed word.
        // An operand of 0400000 or more is negative here (date's divisor, for one).
        if(inProg(tP, a + 1) && (wordValue(tP, a + 1) >= 0400000L))
        {
            report(tP, a, "an EAE operand of 0400000 or more, negative to the PDP-1's mul and div");
        }
        if(v == EAE_MUL)
        {
            // The product's sign is in AC and IO bit 17 too, so AC:IO is twice it (F15D).
            addLine(lP, "mul %s", opnd);
            addLine(lP, "scr 1s");
        }
        else if((v == EAE_IDIV) || (v == EAE_DIV))
        {
            // div wants the dividend doubled over AC:IO and leaves the quotient in AC and the
            // remainder in IO, where the PDP-7 left them in MQ and AC.
            // No skip is an overflow, which leaves the dividend.
            if(v == EAE_IDIV)
            {
                addLine(lP, "lia");
                addLine(lP, "cla");
            }
            addLine(lP, "scl 1s");
            addLine(lP, "div %s", opnd);
            addLine(lP, "jmp .+2");
            addLine(lP, "swp");
        }
        else
        {
            report(tP, a, "an EAE multiply or divide other than mul, idiv and div");
        }
        if(wP->liveOut & LIVE_L)
        {
            addLine(lP, "dzm x_lnk");
        }
        addLine(lP, "jmp %s", next);
        return;
    }
    if(v & EAE_ABS)
    {
        report(tP, a, "an EAE absolute value or sign operation");
    }
    if(v & EAE_SIGN)
    {
        addLine(lP, "dac x_lnk");
    }
    if((v & EAE_CLRMQ) && !(v & EAE_MQORAC))
    {
        addLine(lP, "cli");
    }
    if(v & EAE_MQORAC)
    {
        if(v & EAE_CLRMQ)
        {
            addLine(lP, "lia");
        }
        else
        {
            addLine(lP, "dac x_t");
            addLine(lP, "lai");
            addLine(lP, "ior x_t");
            addLine(lP, "lia");
            addLine(lP, "lac x_t");
        }
    }
    if((v & EAE_CLRAC) && !((cls == 0) && (n & 02)))
    {
        // lacq's clear is the load's own.
        addLine(lP, "cla");
    }
    switch(cls)
    {
    case 0:
        if((n & 06) == 06)
        {
            report(tP, a, "complement MQ and OR MQ into AC in one word");
        }
        if(n & 01)
        {
            report(tP, a, "the EAE step counter read into AC");
        }
        if(n & 070)
        {
            report(tP, a, "unknown EAE setup bits");
        }
        if(n & 02)
        {
            if(v & EAE_CLRAC)
            {
                addLine(lP, "lai");
            }
            else
            {
                addLine(lP, "dac x_t");
                addLine(lP, "lai");
                addLine(lP, "ior x_t");
            }
        }
        if(n & 04)
        {
            addLine(lP, "swp");
            addLine(lP, "cma");
            addLine(lP, "swp");
        }
        return;
    case 0500:
        // Right, scr fills with AC's sign, which for the signed shift is what the link holds.
        shiftLines(lP, "scr", n);
        if(!(v & EAE_SIGN) && !naive && (n > 0))
        {
            if(n >= 18)
            {
                report(tP, a, "an unsigned long right shift of 18 places or more");
                return;
            }
            addLine(lP, "and [0%06lo]", (WORDMASK >> n));
            fillLines(lP, (WORDMASK & ~(WORDMASK >> n)));
        }
        return;
    case 0600:
        if(naive)
        {
            shiftLines(lP, "scl", n);
            return;
        }
        // Left, rotate, then the low n bits of the 36 are the link's.
        shiftLines(lP, "rcl", n);
        if(n == 0)
        {
            return;
        }
        if(n <= 18)
        {
            low = ((1L << n) - 1);
            addLine(lP, "swp");
            addLine(lP, "and [0%06lo]", (WORDMASK & ~low));
            fillLines(lP, low);
            addLine(lP, "swp");
        }
        else
        {
            low = ((1L << (n - 18)) - 1);
            addLine(lP, "and [0%06lo]", (WORDMASK & ~low));
            fillLines(lP, low);
            addLine(lP, "swp");
            addLine(lP, "cla");
            fillLines(lP, WORDMASK);
            addLine(lP, "swp");
        }
        return;
    case 0700:
        if(naive)
        {
            shiftLines(lP, "sal", n);
            return;
        }
        shiftLines(lP, "ral", n);
        if(n == 0)
        {
            return;
        }
        low = ((n >= 18) ? WORDMASK : ((1L << n) - 1));
        addLine(lP, "and [0%06lo]", (WORDMASK & ~low));
        fillLines(lP, low);
        return;
    default:
        report(tP, a, "the EAE normalize");
        return;
    }
}

static void translateWord(struct tr *tP, long a)
{
    struct tWord *wP;
    long v;
    long k;
    long n;

    wP = &tP->wP[a];
    v = wordValue(tP, a);
    switch(wP->cls)
    {
    case IC_MEMREF:
        translateMemref(tP, a);
        break;
    case IC_OPR:
        translateOpr(tP, a);
        break;
    case IC_EAE:
        translateEae(tP, a);
        break;
    case IC_LAW:
        n = (v & 017777L);
        if(wP->sP->rel)
        {
            char ref[MAXTEXT];

            dataRef(tP, a, n, ref);
            addLine(&wP->body, "law %s", ref);
        }
        else if(n >= USERBASE)
        {
            addLine(&wP->body, "law 0%lo", (n - USERBASE));
        }
        else
        {
            // A small law loads its own bits, which programs use as characters.
            addLine(&wP->body, "lac [0%06lo]", v);
        }
        checkSpan(tP, a);
        break;
    case IC_LITERAL:
        // The PDP-7 loads -k in two's complement; the PDP-1's law i k is -k in ones'.
        k = (01000000L - v);
        if(k <= 07777)
        {
            addLine(&wP->body, "law i 0%lo", k);
        }
        else
        {
            addLine(&wP->body, "lac [0%06lo]", (WORDMASK - k));
        }
        break;
    case IC_SYS:
        n = (v - 0020000L);
        if((n < 1) || (n > 29))
        {
            report(tP, a, "sys with a call number out of range");
        }
        if(n == SYS_SYSLOC)
        {
            report(tP, a, "sysloc: the kernel addresses it returns are the PDP-1 kernel's");
        }
        addLine(&wP->body, "cal 0%lo", (n & 0777));
        break;
    default:
        report(tP, a, "an IOT in a user program");
        addLine(&wP->body, "0%06lo", v);
        break;
    }
    if(!wP->hasSkip && (wP->body.n == 0))
    {
        // nop, and operations that leave every register as it was, stay one word.
        addLine(&wP->body, "nop");
    }
}

// A data word's am1 text, an address made from a label, or a number, a negative literal
// rewritten in ones' complement.
static void dataText(struct tr *tP, long a, char *bufP)
{
    struct srcWord *sP;
    long v;
    long high;
    int n;
    int minus;
    char ref[MAXTEXT];

    sP = tP->wP[a].sP;
    if(!sP || !sP->defined)
    {
        strcpy(bufP, "0");
        return;
    }
    v = (sP->value & WORDMASK);
    checkSpan(tP, a);
    if(sP->rel)
    {
        high = (v & 0760000L);
        if((v & 017777L) < tP->lo)
        {
            report(tP, a, "a pointer outside the program");
        }
        dataRef(tP, a, (v & 017777L), ref);
        if(high)
        {
            snprintf(bufP, MAXTEXT + 16, "0%06lo|%s", high, ref);
        }
        else
        {
            snprintf(bufP, MAXTEXT + 16, "%s", ref);
        }
        return;
    }
    minus = 0;
    for(n = 0; n < sP->nSyl; n++)
    {
        minus |= (sP->sylP[n].op == '-');
    }
    if(minus && (v >= 0400000L))
    {
        v -= 1;
    }
    else if((v >= USERBASE) && (v <= 017777L) && !tP->wP[a].isStored)
    {
        report(tP, a, "a number in the user address range (%06lo): an address or a count?", v);
    }
    snprintf(bufP, MAXTEXT + 16, "0%06lo", v);
}

// Each word's final lines; a skip is built around the length of the word it skips, so the
// words are composed from the last to the first.
static void compose(struct tr *tP)
{
    struct tWord *wP;
    long a;
    int lenX;
    int naive;
    char text[MAXTEXT + 16];
    char xName[MAXTEXT];
    char yName[MAXTEXT];

    naive = ((tP->oP->naive & NAIVE_SKIP) != 0);
    for(a = tP->hi - 1; a >= tP->lo; a--)
    {
        wP = &tP->wP[a];
        if((wP->kind == WK_DATA) || (wP->kind == WK_ARG))
        {
            dataText(tP, a, text);
            addLine(&wP->out, "%s", text);
            if(wP->isEntry)
            {
                // jda left the return address in AC and AC in this word (F15D, jda).
                addLine(&wP->out, (tP->oP->naive & NAIVE_JDA) ? "dac r_%s" : "dap r_%s",
                        wP->name + 2);
                addLine(&wP->out, "lac %s", wP->name);
            }
            continue;
        }
        if(wP->kind == WK_RESERVED)
        {
            addLine(&wP->out, "0");
            continue;
        }
        if(!wP->hasSkip)
        {
            addList(&wP->out, &wP->body);
            continue;
        }

        lenX = ((inProg(tP, a + 1)) ? tP->wP[a + 1].out.n : 1);
        nameRef(tP, a + 1, xName);
        nameRef(tP, a + 2, yName);
        addList(&wP->out, &wP->pre);
        if(naive)
        {
            addLine(&wP->out, "%s", wP->skip);
            addList(&wP->out, &wP->post);
            continue;
        }
        if(wP->post.n > 0)
        {
            // Both paths finish the word, and the skip's path jumps over the skipped word.
            addLine(&wP->out, "%s", wP->skip);
            addLine(&wP->out, "jmp s_%05lo", a);
            addList(&wP->out, &wP->post);
            addLine(&wP->out, "jmp %s", yName);
            snprintf(text, sizeof(text), "s_%05lo, %s", a, wP->post.lineP[0]);
            addLine(&wP->out, "%s", text);
            for(lenX = 1; lenX < wP->post.n; lenX++)
            {
                addLine(&wP->out, "%s", wP->post.lineP[lenX]);
            }
            continue;
        }
        if(lenX == 1)
        {
            addLine(&wP->out, "%s", wP->skip);
        }
        else if(wP->skipInv[0])
        {
            addLine(&wP->out, "%s", wP->skipInv);
            addLine(&wP->out, "jmp %s", yName);
        }
        else
        {
            addLine(&wP->out, "%s", wP->skip);
            addLine(&wP->out, "jmp %s", xName);
            addLine(&wP->out, "jmp %s", yName);
        }
    }
}

// The source line of the word at a, as a comment for its first output line; a trailing
// backslash would join the next line in cpp, so it goes.
static void sourceComment(struct tr *tP, long a, char *bufP, size_t size)
{
    struct srcWord *sP;
    const char *fileP;
    const char *slashP;
    char text[200];
    size_t n;
    size_t k;
    char *lineP;

    bufP[0] = '\0';
    sP = tP->wP[a].sP;
    if(!sP || (sP->posOnLine != 0))
    {
        return;
    }
    fileP = tP->aP->fileP[sP->fileNo];
    if((slashP = strrchr(fileP, '/')))
    {
        fileP = slashP + 1;
    }
    lineP = tP->aP->lineTextP[sP->fileNo][sP->lineNo - 1];
    for(n = 0, k = 0; lineP[k] && (n < (sizeof(text) - 1)); k++)
    {
        if((lineP[k] == '\t') || (lineP[k] == ' '))
        {
            if((n > 0) && (text[n - 1] != ' '))
            {
                text[n++] = ' ';
            }
        }
        else if(isprint((unsigned char)lineP[k]))
        {
            text[n++] = lineP[k];
        }
    }
    while((n > 0) && ((text[n - 1] == ' ') || (text[n - 1] == '\\')))
    {
        n--;
    }
    text[n] = '\0';
    snprintf(bufP, size, "// %s:%d: %s", fileP, sP->lineNo, text);
}

// The words the routines' returns and am1's constants take at 0110, ahead of the
// program.
// They cannot follow the program's last stored word.
// A pointer to the word before a reserved buffer (a law of the buffer's name less 1, as in
// init and sh) names the last stored word, and its first store would land on them.
// am1 gives literals written alike one entry, so the distinct texts are an upper
// bound on its constants; a word too many is a gap, one too few an overwrite
// that am1 stops on.
static long headWords(struct tr *tP)
{
    char **seenP;
    long nSeen;
    long room;
    const char *lineP;
    const char *openP;
    const char *closeP;
    size_t len;
    long n;
    long a;
    long j;
    int k;

    seenP = NULL;
    nSeen = 0;
    room = 0;
    n = 0;
    for(a = tP->lo; a < tP->hi; a++)
    {
        if(tP->wP[a].isEntry)
        {
            n++;
        }
        for(k = 0; k < tP->wP[a].out.n; k++)
        {
            for(lineP = tP->wP[a].out.lineP[k]; (openP = strchr(lineP, '[')); lineP = closeP)
            {
                if(!(closeP = strchr(openP, ']')))
                {
                    break;
                }
                len = (size_t)(closeP - openP);
                for(j = 0; j < nSeen; j++)
                {
                    if((strlen(seenP[j]) == len) && (strncmp(seenP[j], openP, len) == 0))
                    {
                        break;
                    }
                }
                if(j < nSeen)
                {
                    continue;
                }
                if(nSeen == room)
                {
                    room = (room ? (room * 2) : 64);
                    if(!(seenP = realloc(seenP, (room * sizeof(*seenP)))))
                    {
                        fprintf(stderr, "as7am1: out of memory\n");
                        exit(1);
                    }
                }
                seenP[nSeen++] = strndup(openP, len);
            }
        }
    }
    n += nSeen;
    for(j = 0; j < nSeen; j++)
    {
        free(seenP[j]);
    }
    free(seenP);
    return n;
}

static void writeOutput(struct tr *tP, FILE *fP)
{
    const struct tOptions *oP;
    struct tWord *wP;
    long a;
    long r;
    int k;
    int len;
    char comment[240];
    char label[MAXNAME + 2];
    char start[MAXTEXT];
    char *lineP;
    char *commaP;

    oP = tP->oP;
    if(!oP->fragment)
    {
        if(oP->titleP)
        {
            fprintf(fP, "%s\n", oP->titleP);
        }
        else
        {
            fprintf(fP, "as7am1: %s\n", tP->aP->fileP[0]);
        }
    }
    fprintf(fP, "// Translated by as7am1 from the PDP-7 source");
    for(k = 0; k < tP->aP->nFiles; k++)
    {
        fprintf(fP, " %s", tP->aP->fileP[k]);
    }
    fprintf(fP, "\n// for a process bank: word 0 the jump to the start, 020-077 the translator's\n"
                "// words, 0100-0107 the system call frame, the routines' returns and the\n"
                "// constants from 0110 (laid down at the end), then the program.\n");
    fprintf(fP, "#include \"unixsys.ah\"\n        octal\n");
    if(oP->bank >= 0)
    {
        fprintf(fP, "        bank %o\n", oP->bank);
    }
    fprintf(fP, "0/\n        jmp %s\n", nameRef(tP, tP->lo, start));
    for(k = 1; k < 020; k++)
    {
        fprintf(fP, "        0\n");
    }
    fprintf(fP, "020/\nx_lnk,  0       // the link, in its sign\nx_ac,   0\nx_io,   0\n"
                "x_t,    0\n0100/\n        SYSFRAME()\n0%lo/\n", (0110 + headWords(tP)));

    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if(a == tP->lastStored + 1)
        {
            // What follows is not in the PDP-7 image, only reserved words.
            break;
        }
        if(wP->kind == WK_RESERVED)
        {
            if(wP->runStart)
            {
                for(r = a + 1; (r <= tP->lastStored) && (tP->wP[r].kind == WK_RESERVED) &&
                    !tP->wP[r].runStart; r++)
                {
                }
                fprintf(fP, "%s,\n        table 0%lo, 0\n", wP->name, (r - a));
            }
            continue;
        }
        sourceComment(tP, a, comment, sizeof(comment));
        for(k = 0; k < wP->out.n; k++)
        {
            lineP = wP->out.lineP[k];
            snprintf(label, sizeof(label), "%s,", wP->name);
            if((k > 0) && (strncmp(lineP, "s_", 2) == 0) && (commaP = strchr(lineP, ',')))
            {
                // The second path's label, made in compose() as "s_nnnnn, text".
                snprintf(label, sizeof(label), "%.*s", (int)(commaP - lineP + 1), lineP);
                lineP = commaP + 2;
            }
            len = fprintf(fP, "%-8s%s", ((k == 0) || (lineP != wP->out.lineP[k])) ? label : "",
                          lineP);
            if((k == 0) && comment[0])
            {
                fprintf(fP, "%*s%s", (len < 40) ? (40 - len) : 1, "", comment);
            }
            fprintf(fP, "\n");
        }
    }
    for(a = tP->lastStored + 1; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if(wP->runStart)
        {
            for(r = a + 1; (r < tP->hi) && !tP->wP[r].runStart; r++)
            {
            }
            fprintf(fP, "%s,\n        .+0%lo/\n", wP->name, (r - a));
        }
    }
    fprintf(fP, "x_end,\n");

    // The routines' returns and the constants, in the room left at 0110.
    // The constants directive takes every literal before it, so it comes last in the file.
    fprintf(fP, "0110/\n");
    for(a = tP->lo; a < tP->hi; a++)
    {
        if(tP->wP[a].isEntry)
        {
            snprintf(label, sizeof(label), "r_%s,", tP->wP[a].name + 2);
            fprintf(fP, "%-8s%s\n", label, (oP->naive & NAIVE_JDA) ? "0" : "jmp 0");
        }
    }
    fprintf(fP, "        constants\n");
    if(!oP->fragment)
    {
        fprintf(fP, "start 0%o\n", ((oP->bank >= 0) ? (oP->bank << 12) : 0));
    }
}

// Translates the program read into aP; returns 0, 1 when the output cannot be written, or 2
// when constructs were reported for a hand fix.
// Reports three constructs the rules translate wrong, which no other report names.
// A small law added to by tad is translated as its own bits, which the PDP-1's end-around
// carry and bank bits break once they reach a pointer (nm.s:74).
// An instruction on a jms's line is kept as an argument in its PDP-7 bits though a return
// reaches it (roff.s:669, where jmp i is the PDP-1's jsp), but for a direct jmp, whose bits
// are the PDP-1's jmp too (as in jms fetch; jmp again).
// A routine never the target of a direct jms that returns through its first word and is
// named as an address by a law or a data word is left with no prologue and no r_ return
// (db's proct and prasc, called through jms i).
// A routine no word names is never called (fop.s's sin in st), and is not reported.
static void reportUnseen(struct tr *tP)
{
    struct tWord *wP;
    char *markP;
    long a;
    long k;
    long v;
    long target;

    // markP is 1 for an address a law or a data word names, 2 once reported.
    markP = calloc(NADDR, 1);
    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        target = (wordValue(tP, a) & 017777L);
        if(wP->sP && wP->sP->rel && inProg(tP, target) &&
           ((wP->kind == WK_DATA) || ((wP->kind == WK_CODE) && (wP->cls == IC_LAW))))
        {
            markP[target] = 1;
        }
    }
    for(a = tP->lo; a < tP->hi; a++)
    {
        wP = &tP->wP[a];
        if(wP->kind != WK_CODE)
        {
            continue;
        }
        v = wordValue(tP, a);
        if((wP->cls == IC_LAW) && !wP->sP->rel && ((v & 017777L) < USERBASE) &&
           isMemref(tP, a + 1, OP_TAD))
        {
            report(tP, a, "a small law added to by tad: kept as its own bits, which the "
                   "PDP-1's add carries end-around");
        }
        if(isMemref(tP, a, OP_JMS))
        {
            for(k = (a + 1); k <= (a + wP->nArgs); k++)
            {
                if((tP->wP[k].cls != IC_NONE) && !((tP->wP[k].cls == IC_MEMREF) &&
                   ((wordValue(tP, k) & 0760000L) == OP_JMP) && tP->wP[k].sP->rel))
                {
                    report(tP, k, "an instruction on a jms's line: kept as an argument in "
                           "its PDP-7 bits, though a return reaches it");
                }
            }
        }
        target = (v & 017777L);
        if(isMemref(tP, a, OP_JMP) && (v & 020000L) && wP->sP->rel && inProg(tP, target) &&
           !tP->wP[target].isEntry && (tP->wP[target].kind == WK_DATA) &&
           inProg(tP, (target + 1)) && (tP->wP[target + 1].kind == WK_CODE) &&
           (markP[target] == 1))
        {
            markP[target] = 2;
            report(tP, target, "a routine returning through its first word, never called by "
                   "a direct jms: left with no prologue and no r_ return");
        }
    }
    free(markP);
}

int translate(struct as7 *aP, const struct tOptions *oP)
{
    struct tr t;
    FILE *outP;
    long a;

    memset(&t, 0, sizeof(t));
    t.aP = aP;
    t.oP = oP;
    t.repP = stderr;
    if(oP->reportNameP && !(t.repP = fopen(oP->reportNameP, "w")))
    {
        fprintf(stderr, "as7am1: cannot write %s\n", oP->reportNameP);
        return (1);
    }
    buildImage(&t);
    classify(&t);
    nameWords(&t);
    flow(&t);
    reportUnseen(&t);
    liveness(&t);
    for(a = t.lo; a < t.hi; a++)
    {
        if(t.wP[a].kind == WK_CODE)
        {
            translateWord(&t, a);
        }
    }
    compose(&t);
    for(a = t.lo; a < t.hi; a++)
    {
        if((t.wP[a].kind == WK_CODE) && t.wP[a].cls == IC_MEMREF &&
           ((wordValue(&t, a) & 0740000L) == OP_XCT) && t.wP[a].sP->rel &&
           inProg(&t, (wordValue(&t, a) & 017777L)) &&
           (t.wP[wordValue(&t, a) & 017777L].out.n != 1))
        {
            report(&t, a, "xct of a word that is not one PDP-1 word");
        }
    }
    if(!(outP = fopen(oP->outNameP, "w")))
    {
        fprintf(stderr, "as7am1: cannot write %s\n", oP->outNameP);
        return (1);
    }
    writeOutput(&t, outP);
    fclose(outP);
    fprintf(t.repP, "%d reported\n", t.nReports);
    if(t.repP != stderr)
    {
        fclose(t.repP);
    }
    return ((t.nReports > 0) ? 2 : 0);
}
