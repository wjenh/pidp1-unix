/*
 * as7am1 - translates a program of the 1970 PDP-7 Unix, in the source of its assembler as,
 * into am1 source for the PDP-1D, to run in a process bank of the Unix recreated there.
 *
 * Usage: as7am1 [-o out.am1] [-r report] [-t title] [-b bank] [-F] [-N flags] file.s ...
 *        as7am1 -L file.s ...
 * where:
 * file.s  - the program's source files, in the order the restorers' build/Makefile gives
 *           them to tools/as7; they are assembled together, as as7 does
 * -o      - the am1 output; a.am1 when left out
 * -r      - the report of what could not be translated, by file and line; standard error
 *           when left out
 * -t      - the am1 title line; "as7am1: " and the first file's name when left out
 * -b      - the bank, in octal, for a bank directive and the start address; none when left
 *           out
 * -F      - a fragment, for an #include: no title line and no start
 * -N      - the naive rules, in octal (translate.h's NAIVE_ flags), for testing the
 *           translator only
 * -L      - no translation: the memory and the global labels as tools/as7 assembles them,
 *           in its a7out format (address and word) then its list format's labels, for a
 *           comparison with as7 itself
 *
 * reader.c assembles the source as tools/as7 does; translate.c finds the code and the data
 * and writes the am1, one PDP-7 word to one am1 word or a short sequence, every word that
 * is referred to labeled by its PDP-7 address, so that am1 lays the program out again.
 *
 * Exit status is 0 on success, 1 when the source has an as7 error or a file cannot be read,
 * and 2 when the translation reported a construct it could not translate.
 *
 * Revision history:
 *
 * 21-Nov-2026 Claude initial version
 * 23-Nov-2026 Claude comments cleaned for the release
 *
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "as7am1.h"
#include "translate.h"

static int compareNames(const void *aP, const void *bP);
static void listing(struct as7 *aP);
static void usage(void);

// Orders label entries as Perl's sort does, byte by byte.
static int compareNames(const void *aP, const void *bP)
{
    return (strcmp((*(struct symEntry *const *)aP)->nameP,
                   (*(struct symEntry *const *)bP)->nameP));
}

// The words as as7's a7out format prints @Mem, each address's last store winning, then the
// global labels as its list format's dump_labels prints them.
static void listing(struct as7 *aP)
{
    long maxLoc;
    long *memP;
    char *setP;
    long k;
    long addr;
    int n;
    int count;
    struct symEntry *eP;
    struct symEntry **listP;

    maxLoc = -1;
    for(k = 0; k < aP->nWords; k++)
    {
        if(aP->wordP[k].loc > maxLoc)
        {
            maxLoc = aP->wordP[k].loc;
        }
    }
    memP = calloc(maxLoc + 2, sizeof(long));
    setP = calloc(maxLoc + 2, 1);
    for(k = 0; k < aP->nWords; k++)
    {
        memP[aP->wordP[k].loc] = aP->wordP[k].value;
        setP[aP->wordP[k].loc] = (char)aP->wordP[k].defined;
    }
    for(k = 0; k <= maxLoc; k++)
    {
        if(setP[k])
        {
            printf("%06lo: %06lo\n", k, memP[k]);
        }
    }

    count = 0;
    for(n = 0; n < HASHSIZE; n++)
    {
        for(eP = aP->glabel.bucketP[n]; eP; eP = eP->nextP)
        {
            count++;
        }
    }
    listP = malloc((count + 1) * sizeof(*listP));
    count = 0;
    for(n = 0; n < HASHSIZE; n++)
    {
        for(eP = aP->glabel.bucketP[n]; eP; eP = eP->nextP)
        {
            listP[count++] = eP;
        }
    }
    qsort(listP, count, sizeof(*listP), compareNames);
    printf("\nLabels:\n");
    for(n = 0; n < count; n++)
    {
        addr = listP[n]->value;
        if(addr & RELATIVE)
        {
            addr = ((addr & WORDMASK) + symFind(&aP->var, "..")->value);
        }
        printf("%-8.8s %#06lo %s\n", listP[n]->nameP, (addr & WORDMASK),
               (listP[n]->value & RELATIVE) ? "r" : "");
    }
    free(listP);
    free(memP);
    free(setP);
}

// Prints the usage to standard error and exits with status 1.
static void usage(void)
{
    fprintf(stderr, "usage: as7am1 [-o out.am1] [-r report] [-t title] [-b bank] [-F] "
                    "[-N flags] file.s ...\n"
                    "       as7am1 -L file.s ...\n");
    exit(1);
}

// Reads the options and the files, then lists or translates; returns the exit status in the
// header above.
int main(int argc, char **argv)
{
    struct as7 *aP;
    struct tOptions opts;
    int listOnly;
    int c;
    int status;

    memset(&opts, 0, sizeof(opts));
    opts.outNameP = "a.am1";
    opts.bank = -1;
    listOnly = 0;
    while((c = getopt(argc, argv, "o:r:t:b:FN:L")) != -1)
    {
        switch(c)
        {
        case 'o':
            opts.outNameP = optarg;
            break;
        case 'r':
            opts.reportNameP = optarg;
            break;
        case 't':
            opts.titleP = optarg;
            break;
        case 'b':
            opts.bank = (int)strtol(optarg, NULL, 8);
            if((opts.bank < 0) || (opts.bank > 017))
            {
                usage();
            }
            break;
        case 'F':
            opts.fragment = 1;
            break;
        case 'N':
            opts.naive = (unsigned)strtoul(optarg, NULL, 8);
            break;
        case 'L':
            listOnly = 1;
            break;
        default:
            usage();
        }
    }
    if((optind >= argc) || ((argc - optind) > MAXFILES))
    {
        usage();
    }

    aP = calloc(1, sizeof(*aP));
    for(c = optind; c < argc; c++)
    {
        aP->fileP[aP->nFiles++] = argv[c];
    }
    aP->quiet = listOnly;
    if(readerRun(aP) != 0)
    {
        return (1);
    }
    if(listOnly)
    {
        listing(aP);
        return (aP->errors ? 1 : 0);
    }
    if(aP->errors)
    {
        fprintf(stderr, "as7am1: the source has as7 errors; nothing written\n");
        return (1);
    }
    status = translate(aP, &opts);
    return (status);
}
