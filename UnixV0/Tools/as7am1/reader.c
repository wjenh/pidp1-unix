/*
 * reader.c - as7am1's assembler of the PDP-7 source, statement for statement the
 * restorers' tools/as7 (pdp7-unix, Warren Toomey and Phil Budne), so that every word and
 * every symbol has the value as7 gives it.
 *
 * Two passes over the files in the order given.
 * As in as7, only `.` is reset for the second pass; every other symbol keeps its value from
 * the end of the first, numeric labels are collected in the first pass only, and an
 * undefined name is an error only in the second.
 * Each word emitted in the second pass is kept with the syllables it was built from, its file
 * and line, and its place on the line, for translate.c.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "as7am1.h"

// as7's predefined names, the system call numbers and the opcodes (tools/as7, %Var).
static const struct
{
    const char *nameP;
    long value;
} predef[] =
{
    { "save", 1 }, { "getuid", 2 }, { "open", 3 }, { "read", 4 }, { "write", 5 },
    { "creat", 6 }, { "seek", 7 }, { "tell", 8 }, { "close", 9 }, { "link", 10 },
    { "unlink", 11 }, { "setuid", 12 }, { "rename", 13 }, { "exit", 14 }, { "time", 15 },
    { "intrp", 16 }, { "chdir", 17 }, { "chmod", 18 }, { "chown", 19 }, { "sysloc", 21 },
    { "capt", 23 }, { "rele", 24 }, { "status", 25 }, { "smes", 27 }, { "rmes", 28 },
    { "fork", 29 },
    { "sys", 0020000 }, { "i", 0020000 },
    { "dac", 0040000 }, { "jms", 0100000 }, { "dzm", 0140000 }, { "lac", 0200000 },
    { "xor", 0240000 }, { "add", 0300000 }, { "tad", 0340000 }, { "xct", 0400000 },
    { "isz", 0440000 }, { "and", 0500000 }, { "sad", 0540000 }, { "jmp", 0600000 },
    { "eae", 0640000 }, { "osc", 0640001 }, { "omq", 0640002 }, { "cmq", 0640004 },
    { "div", 0640323 }, { "norm", 0640444 }, { "lls", 0640600 }, { "clls", 0641600 },
    { "als", 0640700 }, { "lrs", 0640500 }, { "ecla", 0641000 }, { "lacs", 0641001 },
    { "lacq", 0641002 }, { "abs", 0644000 }, { "divs", 0644323 }, { "clq", 0650000 },
    { "frdiv", 0650323 }, { "lmq", 0652000 }, { "mul", 0653122 }, { "idiv", 0653323 },
    { "idivs", 0657323 }, { "frdivs", 0654323 }, { "muls", 0657122 }, { "norms", 0660444 },
    { "gsm", 0664000 }, { "lrss", 0660500 }, { "llss", 0660600 }, { "alss", 0660700 },
    { "opr", 0740000 }, { "nop", 0740000 }, { "cma", 0740001 }, { "cml", 0740002 },
    { "oas", 0740004 }, { "ral", 0740010 }, { "rar", 0740020 }, { "hlt", 0740040 },
    { "xx", 0740040 }, { "sma", 0740100 }, { "sza", 0740200 }, { "snl", 0740400 },
    { "skp", 0741000 }, { "spa", 0741100 }, { "sna", 0741200 }, { "szl", 0741400 },
    { "rtl", 0742010 }, { "rtr", 0742020 }, { "cll", 0744000 }, { "stl", 0744002 },
    { "rcl", 0744010 }, { "rcr", 0744020 }, { "cla", 0750000 }, { "clc", 0750001 },
    { "las", 0750004 }, { "glk", 0750010 }, { "law", 0760000 },
};

static unsigned hashName(const char *nameP);
static int isPerlSpace(int c);
static void skipSpace(struct as7 *aP);
static int atEol(struct as7 *aP);
static int err(struct as7 *aP, const char *msgP, const char *argP);
static int fileKey(struct as7 *aP);
static void setLabel(struct as7 *aP, const char *labelP, long loc);
static int getLabel(struct as7 *aP, const char *symP, long *valueP);
static void processLabel(struct as7 *aP, const char *labelP);
static long findRelative(struct as7 *aP, const char *numP, char dir);
static int parseExpression(struct as7 *aP, long *wordP, struct syllable **sylPP, int *nSylP);
static void emitWord(struct as7 *aP, long word, int defined, struct syllable *sylP, int nSyl);
static void parseLine(struct as7 *aP);
static int loadFile(struct as7 *aP, int fileNo);
static void parseFile(struct as7 *aP, int fileNo);
static void truncate8(char *nameP, const char *srcP);

// as7 knows a label by its first eight characters (moo.s defines "standing" and uses
// "standings").
static void truncate8(char *nameP, const char *srcP)
{
    size_t n;

    n = strlen(srcP);
    if(n > 8)
    {
        n = 8;
    }
    memcpy(nameP, srcP, n);
    nameP[n] = '\0';
}

// A string hash for the symbol tables.
static unsigned hashName(const char *nameP)
{
    unsigned h;

    h = 5381;
    while(*nameP)
    {
        h = ((h * 33) ^ (unsigned char)*nameP++);
    }
    return (h % HASHSIZE);
}

void symInit(struct symTab *tP)
{
    memset(tP, 0, sizeof(*tP));
}

// The entry for nameP, or NULL.
struct symEntry *symFind(struct symTab *tP, const char *nameP)
{
    struct symEntry *eP;

    for(eP = tP->bucketP[hashName(nameP)]; eP; eP = eP->nextP)
    {
        if(strcmp(eP->nameP, nameP) == 0)
        {
            return (eP);
        }
    }
    return (NULL);
}

// Sets nameP to value, making the entry if it is new; returns the entry.
struct symEntry *symSet(struct symTab *tP, const char *nameP, long value)
{
    struct symEntry *eP;
    unsigned h;

    if(!(eP = symFind(tP, nameP)))
    {
        eP = calloc(1, sizeof(*eP));
        eP->nameP = strdup(nameP);
        h = hashName(nameP);
        eP->nextP = tP->bucketP[h];
        tP->bucketP[h] = eP;
    }
    eP->value = value;
    eP->isDefined = 1;
    return (eP);
}

// Perl's \s, the characters as7 strips, not only blanks and tabs.
static int isPerlSpace(int c)
{
    return ((c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\f') ||
            (c == '\v'));
}

static void skipSpace(struct as7 *aP)
{
    while(isPerlSpace((unsigned char)*aP->lineP))
    {
        aP->lineP++;
    }
}

// 1 at the end of the line or at a comment.
static int atEol(struct as7 *aP)
{
    return ((*aP->lineP == '\0') || (*aP->lineP == '"'));
}

// as7's err(), which marks the run failed and reports in pass two; returns as7's value, 0.
static int err(struct as7 *aP, const char *msgP, const char *argP)
{
    aP->errors = 1;
    if((aP->stage == 2) && !aP->quiet)
    {
        fprintf(stderr, "%s:%d: %s%s\n", aP->fileP[aP->curFile], aP->lineNo, msgP,
                argP ? argP : "");
    }
    return (0);
}

// as7 keys its local labels by the file's path, so a file given twice shares one table.
static int fileKey(struct as7 *aP)
{
    int k;

    for(k = 0; k < aP->curFile; k++)
    {
        if(strcmp(aP->fileP[k], aP->fileP[aP->curFile]) == 0)
        {
            return (k);
        }
    }
    return (aP->curFile);
}

// as7's set_label, keeping eight characters; a label is local when declared @local or
// starting with L.
// A local keeps its first value, a global its last.
static void setLabel(struct as7 *aP, const char *labelP, long loc)
{
    char name[9];
    struct symEntry *eP;
    int k;

    truncate8(name, labelP);
    k = fileKey(aP);
    if(symFind(&aP->isLocal[k], name) || (name[0] == 'L'))
    {
        eP = symFind(&aP->llabel[k], name);
        if(!eP || (eP->value == loc))
        {
            symSet(&aP->llabel[k], name, loc);
        }
    }
    else
    {
        symSet(&aP->glabel, name, loc);
    }
}

// as7's get_label; returns 1 with *valueP set when the label is known.
static int getLabel(struct as7 *aP, const char *symP, long *valueP)
{
    char name[9];
    struct symEntry *eP;

    truncate8(name, symP);
    if((eP = symFind(&aP->llabel[fileKey(aP)], name)) ||
       (eP = symFind(&aP->glabel, name)))
    {
        *valueP = eP->value;
        return (1);
    }
    return (0);
}

static void processLabel(struct as7 *aP, const char *labelP)
{
    const char *cP;
    struct symEntry *eP;

    for(cP = labelP; isdigit((unsigned char)*cP); cP++)
    {
    }
    if(*cP == '\0')
    {
        if(aP->stage == 1)
        {
            if(!(eP = symFind(&aP->rlabel, labelP)))
            {
                eP = symSet(&aP->rlabel, labelP, 0);
            }
            if(eP->nLocs == eP->maxLocs)
            {
                eP->maxLocs = (eP->maxLocs ? (eP->maxLocs * 2) : 8);
                eP->locsP = realloc(eP->locsP, eP->maxLocs * sizeof(long));
            }
            eP->locsP[eP->nLocs++] = symFind(&aP->var, ".")->value;
        }
        return;
    }
    setLabel(aP, labelP, symFind(&aP->var, ".")->value);
}

// as7's find_relative_label, the first location after `.` for f, the last before it for b.
static long findRelative(struct as7 *aP, const char *numP, char dir)
{
    struct symEntry *eP;
    long cur;
    long best;
    int found;
    int k;

    cur = symFind(&aP->var, ".")->value;
    if(!(eP = symFind(&aP->rlabel, numP)) || (eP->nLocs == 0))
    {
        return (err(aP, "relative label never defined: ", numP));
    }
    found = 0;
    best = 0;
    for(k = 0; k < eP->nLocs; k++)
    {
        if(dir == 'f')
        {
            if(eP->locsP[k] > cur)
            {
                return (eP->locsP[k]);
            }
        }
        else if((eP->locsP[k] < cur) && (!found || (eP->locsP[k] > best)))
        {
            best = eP->locsP[k];
            found = 1;
        }
    }
    if(found)
    {
        return (best);
    }
    return (err(aP, "undefined relative reference ", numP));
}

// as7's parse_expression, syllables ORed, added or subtracted, with the relocatable flag.
// Returns 1 with *wordP set, or 0 where as7 returns undef (the rest of the line is dropped).
static int parseExpression(struct as7 *aP, long *wordP, struct syllable **sylPP, int *nSylP)
{
    long word;
    long flags;
    long value;
    long sylFlags;
    char op;
    char *startP;
    struct syllable s;
    struct symEntry *eP;
    int n;

    word = 0;
    flags = 0;
    aP->curHasSpan = 0;
    aP->curSpanLo = 0;
    aP->curSpanHi = 0;
    *sylPP = NULL;
    *nSylP = 0;
    for(;;)
    {
        skipSpace(aP);
        if((*aP->lineP == '\0') || (*aP->lineP == '"') || (*aP->lineP == ';'))
        {
            *wordP = (word | flags);
            return (1);
        }
        op = '|';
        if(*aP->lineP == '-')
        {
            op = '-';
            aP->lineP++;
        }
        else if(*aP->lineP == '+')
        {
            op = '+';
            aP->lineP++;
        }
        memset(&s, 0, sizeof(s));
        s.op = op;
        startP = aP->lineP;

        // Perl's `.` matches any character but a newline, and the line has none.
        if((startP[0] == '<') && (startP[1] != '\0'))
        {
            s.kind = SYL_CHAR;
            value = ((long)(unsigned char)startP[1] << 9);
            aP->lineP += 2;
        }
        else if((startP[0] != '\0') && (startP[1] == '>'))
        {
            s.kind = SYL_CHAR;
            value = (unsigned char)startP[0];
            aP->lineP += 2;
        }
        else if((startP[0] == '>') && (startP[1] != '\0'))
        {
            s.kind = SYL_CHAR;
            value = (unsigned char)startP[1];
            aP->lineP += 2;
        }
        else if(isalpha((unsigned char)startP[0]) || (startP[0] == '_') || (startP[0] == '.'))
        {
            while(isalnum((unsigned char)*aP->lineP) || (*aP->lineP == '_') ||
                  (*aP->lineP == '.'))
            {
                aP->lineP++;
            }
            s.kind = SYL_SYM;
            n = (int)(aP->lineP - startP);
            snprintf(s.text, sizeof(s.text), "%.*s", n, startP);
            if((eP = symFind(&aP->var, s.text)) && eP->isDefined)
            {
                value = eP->value;
                if(eP->hasSpan && !aP->curHasSpan)
                {
                    aP->curHasSpan = 1;
                    aP->curSpanLo = eP->spanLo;
                    aP->curSpanHi = eP->spanHi;
                }
            }
            else if(!getLabel(aP, s.text, &value))
            {
                value = 0;
                if(aP->stage == 2)
                {
                    err(aP, "not defined: ", s.text);
                }
            }
        }
        else if(isdigit((unsigned char)startP[0]))
        {
            while(isdigit((unsigned char)*aP->lineP))
            {
                aP->lineP++;
            }
            n = (int)(aP->lineP - startP);
            snprintf(s.text, sizeof(s.text), "%.*s", n, startP);
            if((*aP->lineP == 'f') || (*aP->lineP == 'b'))
            {
                s.kind = SYL_LOCAL;
                value = 0;
                if(aP->stage == 2)
                {
                    value = findRelative(aP, s.text, *aP->lineP);
                }
                snprintf(s.text, sizeof(s.text), "%.*s", n + 1, startP);
                aP->lineP++;
            }
            else
            {
                // A leading 0 makes it octal digit by digit, 8 and 9 included, as as.s did.
                s.kind = SYL_NUM;
                value = 0;
                if(startP[0] == '0')
                {
                    for(n = 0; isdigit((unsigned char)startP[n]); n++)
                    {
                        value = (((value * 8) + (startP[n] - '0')) & 077777777777L);
                    }
                }
                else
                {
                    value = strtol(startP, NULL, 10);
                }
                value &= WORDMASK;
            }
        }
        else
        {
            err(aP, "huh? ", aP->lineP);
            aP->lineP = aP->lineP + strlen(aP->lineP);
            return (0);
        }

        s.value = value;
        *sylPP = realloc(*sylPP, (*nSylP + 1) * sizeof(struct syllable));
        (*sylPP)[(*nSylP)++] = s;

        sylFlags = (value & RELATIVE);
        value &= WORDMASK;
        if(op == '+')
        {
            word += value;
            flags |= sylFlags;
        }
        else if(op == '-')
        {
            word -= value;
            if(flags & RELATIVE)
            {
                if(sylFlags & RELATIVE)
                {
                    // A count of words between two places, kept for translate.c.
                    flags &= ~RELATIVE;
                    if(!aP->curHasSpan)
                    {
                        aP->curHasSpan = 1;
                        aP->curSpanLo = relocate(aP, (value | RELATIVE));
                        aP->curSpanHi = relocate(aP, (((word + value) & WORDMASK) | RELATIVE));
                    }
                }
            }
            else if(sylFlags & RELATIVE)
            {
                err(aP, "absolute value minus relative", NULL);
            }
        }
        else
        {
            word |= value;
            flags |= sylFlags;
        }
        word &= WORDMASK;
    }
}

// Relocates a value carrying as7's flag by `..`.
long relocate(struct as7 *aP, long v)
{
    if(v & RELATIVE)
    {
        return ((v & WORDMASK) + (symFind(&aP->var, "..")->value & WORDMASK));
    }
    return (v);
}

// Keeps a word of pass two, relocated as as7 stores it in @Mem.
static void emitWord(struct as7 *aP, long word, int defined, struct syllable *sylP, int nSyl)
{
    struct srcWord *wP;

    if(aP->nWords == aP->maxWords)
    {
        aP->maxWords = (aP->maxWords ? (aP->maxWords * 2) : 1024);
        aP->wordP = realloc(aP->wordP, aP->maxWords * sizeof(struct srcWord));
    }
    wP = &aP->wordP[aP->nWords++];
    memset(wP, 0, sizeof(*wP));
    wP->loc = relocate(aP, symFind(&aP->var, ".")->value);
    wP->defined = defined;
    wP->rel = (defined && (word & RELATIVE)) ? 1 : 0;
    wP->value = defined ? relocate(aP, word) : 0;
    wP->fileNo = aP->curFile;
    wP->lineNo = aP->lineNo;
    wP->posOnLine = aP->wordsOnLine++;
    wP->sylP = sylP;
    wP->nSyl = nSyl;
    wP->hasSpan = aP->curHasSpan;
    wP->spanLo = aP->curSpanLo;
    wP->spanHi = aP->curSpanHi;
}

// as7's parse_line, labels, then an assignment or a word, `;` separating statements.
static void parseLine(struct as7 *aP)
{
    char *p;
    char *nameEndP;
    char lhs[MAXTOK];
    long word;
    int defined;
    int n;
    struct syllable *sylP;
    struct symEntry *eP;

    for(;;)
    {
        if(atEol(aP))
        {
            return;
        }
        skipSpace(aP);
        if(*aP->lineP == '@')
        {
            p = aP->lineP;
            if(strncmp(p, "@local", 6) == 0 && isPerlSpace((unsigned char)p[6]))
            {
                p += 6;
                while(isPerlSpace((unsigned char)*p))
                {
                    p++;
                }
                for(n = 0; p[n] && !isPerlSpace((unsigned char)p[n]); n++)
                {
                }
                if(n > 0)
                {
                    snprintf(lhs, sizeof(lhs), "%.*s", n, p);
                    symSet(&aP->isLocal[fileKey(aP)], lhs, 1);
                }
            }
            return;
        }

        // Labels, each a run of [A-Za-z0-9_.] and a colon.
        for(;;)
        {
            p = aP->lineP;
            while(isalnum((unsigned char)*p) || (*p == '_') || (*p == '.'))
            {
                p++;
            }
            if((p == aP->lineP) || (*p != ':'))
            {
                break;
            }
            snprintf(lhs, sizeof(lhs), "%.*s", (int)(p - aP->lineP), aP->lineP);
            aP->lineP = p + 1;
            skipSpace(aP);
            processLabel(aP, lhs);
        }
        if(atEol(aP))
        {
            return;
        }

        // An assignment, a run of characters other than ; = blank and tab, then =.
        p = aP->lineP;
        while(*p && (*p != ';') && (*p != '=') && (*p != ' ') && (*p != '\t'))
        {
            p++;
        }
        nameEndP = p;
        while(isPerlSpace((unsigned char)*p))
        {
            p++;
        }
        if((nameEndP > aP->lineP) && (*p == '='))
        {
            snprintf(lhs, sizeof(lhs), "%.*s", (int)(nameEndP - aP->lineP), aP->lineP);
            aP->lineP = p + 1;
            defined = parseExpression(aP, &word, &sylP, &n);
            free(sylP);
            eP = symSet(&aP->var, lhs, defined ? word : 0);
            eP->isDefined = defined;
            eP->hasSpan = aP->curHasSpan;
            eP->spanLo = aP->curSpanLo;
            eP->spanHi = aP->curSpanHi;
        }
        else
        {
            defined = parseExpression(aP, &word, &sylP, &n);
            if(aP->stage == 2)
            {
                emitWord(aP, word, defined, sylP, n);
            }
            else
            {
                free(sylP);
            }
            eP = symFind(&aP->var, ".");
            eP->value++;
        }

        skipSpace(aP);
        if(*aP->lineP == ';')
        {
            aP->lineP++;
        }
    }
}

// Reads a file's lines once, keeping them for both passes and for the output's comments.
// Returns 0, or -1 when it cannot be read.
static int loadFile(struct as7 *aP, int fileNo)
{
    FILE *fP;
    char *textP;
    long size;
    long k;
    long start;
    int n;

    if(!(fP = fopen(aP->fileP[fileNo], "rb")))
    {
        fprintf(stderr, "as7am1: cannot open %s\n", aP->fileP[fileNo]);
        return (-1);
    }
    fseek(fP, 0, SEEK_END);
    size = ftell(fP);
    fseek(fP, 0, SEEK_SET);
    textP = malloc(size + 1);
    if(fread(textP, 1, size, fP) != (size_t)size)
    {
        fclose(fP);
        fprintf(stderr, "as7am1: cannot read %s\n", aP->fileP[fileNo]);
        return (-1);
    }
    fclose(fP);
    textP[size] = '\0';

    // As Perl's chomp, lines end at newline, and a last line without one is still a line.
    n = 0;
    for(k = 0; k < size; k++)
    {
        n += (textP[k] == '\n');
    }
    aP->lineTextP[fileNo] = calloc(n + 2, sizeof(char *));
    n = 0;
    start = 0;
    for(k = 0; k <= size; k++)
    {
        if((k == size) || (textP[k] == '\n'))
        {
            if((k == size) && (k == start))
            {
                break;
            }
            textP[k] = '\0';
            aP->lineTextP[fileNo][n++] = &textP[start];
            start = k + 1;
        }
    }
    aP->nLines[fileNo] = n;
    return (0);
}

static void parseFile(struct as7 *aP, int fileNo)
{
    char *copyP;
    int k;
    int first;

    aP->curFile = fileNo;
    for(k = 0; k < aP->nLines[fileNo]; k++)
    {
        aP->lineNo = k + 1;
        copyP = strdup(aP->lineTextP[fileNo][k]);
        aP->lineP = copyP;
        aP->origLineP = aP->lineTextP[fileNo][k];
        aP->wordsOnLine = 0;
        first = aP->nWords;
        parseLine(aP);
        if((aP->stage == 2) && (aP->nWords > first))
        {
            aP->wordP[aP->nWords - 1].lastOnLine = 1;
        }
        free(copyP);
    }
}

// Both passes over the files; returns 0, or -1 when a file cannot be read.
// aP->errors is as7's exit status.
int readerRun(struct as7 *aP)
{
    unsigned k;
    int f;

    symInit(&aP->var);
    symInit(&aP->glabel);
    symInit(&aP->rlabel);
    for(f = 0; f < aP->nFiles; f++)
    {
        symInit(&aP->llabel[f]);
        symInit(&aP->isLocal[f]);
        if(loadFile(aP, f) != 0)
        {
            return (-1);
        }
    }
    for(k = 0; k < (sizeof(predef) / sizeof(predef[0])); k++)
    {
        symSet(&aP->var, predef[k].nameP, predef[k].value);
    }
    symSet(&aP->var, "..", 4096);
    for(aP->stage = 1; aP->stage <= 2; aP->stage++)
    {
        symSet(&aP->var, ".", RELATIVE);
        for(f = 0; f < aP->nFiles; f++)
        {
            parseFile(aP, f);
        }
    }
    aP->stage = 2;
    return (0);
}
