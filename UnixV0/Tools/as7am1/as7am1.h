/*
 * as7am1.h - the shared types of as7am1, the translator from the 1970 PDP-7 Unix
 * assembler's source (as7) to am1 for the PDP-1D.
 *
 * reader.c assembles the source as the restorers' tools/as7 does, keeping every word with
 * the syllables it was built from; translate.c classifies the words and writes am1.
 * All state is in one struct as7, passed by pointer.
 */

#ifndef AS7AM1_H
#define AS7AM1_H

#include <stdio.h>

#define WORDMASK    0777777L
#define RELATIVE    01000000L      // as7's flag on a relocatable value
#define ADDRMASK    017777L        // a PDP-7 memory reference's 13-bit address
#define MAXTOK      64
#define MAXFILES    32
#define HASHSIZE    1024

enum sylKind
{
    SYL_SYM,                       // a name
    SYL_NUM,                       // a number
    SYL_CHAR,                      // <a, a> or >a
    SYL_LOCAL                      // 1f or 1b
};

struct syllable
{
    char op;                       // '|', '+' or '-'
    int kind;
    char text[MAXTOK];             // as written
    long value;                    // as as7 had it, RELATIVE flag included
};

// One emitted word, in pass two.
struct srcWord
{
    long loc;                      // relocated PDP-7 address
    long value;                    // relocated word; as in as7, relocation is not masked
    int defined;                   // 0 when as7 found an error in the expression
    int rel;                       // the expression was relocatable
    int fileNo;
    int lineNo;
    int posOnLine;                 // 0 for the first word of its source line
    int lastOnLine;                // 1 for the last word of its source line
    int nSyl;
    struct syllable *sylP;
    int hasSpan;                   // the value counts the words between two places
    long spanLo;                   // those places, relocated: [spanLo, spanHi)
    long spanHi;
};

struct symEntry
{
    char *nameP;
    long value;
    int isDefined;                 // 0 for an assignment whose expression failed
    int hasSpan;                   // a difference of two places, as in struct srcWord
    long spanLo;
    long spanHi;
    int nLocs;                   // relative labels: every location, in order
    int maxLocs;
    long *locsP;
    struct symEntry *nextP;
};

struct symTab
{
    struct symEntry *bucketP[HASHSIZE];
};

struct as7
{
    int nFiles;
    char *fileP[MAXFILES];         // the paths, as given
    int stage;                     // 1 or 2
    int curFile;
    int lineNo;
    char *lineP;                   // the rest of the line being parsed
    char *origLineP;
    int wordsOnLine;
    int curHasSpan;                // the expression being parsed is a difference of places
    long curSpanLo;
    long curSpanHi;
    int errors;
    int quiet;                     // no error messages, for -L

    struct symTab var;             // `.`, `..`, assignments and the predefined names
    struct symTab glabel;          // global labels
    struct symTab llabel[MAXFILES];
    struct symTab isLocal[MAXFILES];
    struct symTab rlabel;          // numeric labels, every location

    int nWords;
    int maxWords;
    struct srcWord *wordP;
    char **lineTextP[MAXFILES];    // each file's lines, for comments in the output
    int nLines[MAXFILES];
};

// reader.c
void symInit(struct symTab *tP);
struct symEntry *symFind(struct symTab *tP, const char *nameP);
struct symEntry *symSet(struct symTab *tP, const char *nameP, long value);
int readerRun(struct as7 *aP);
long relocate(struct as7 *aP, long v);

#endif
