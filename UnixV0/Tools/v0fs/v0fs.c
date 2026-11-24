/*
 * v0fs - make, check and list a Type 23 drum file holding the 1970 PDP-7 Unix file system.
 *
 * Usage: v0fs make [-k kernel.dmp] proto image
 *        v0fs check image
 *        v0fs ls [-b] image [path]
 *        v0fs cat [-o] image path
 * where:
 * make    - build image from the prototype proto, replacing any file of that name
 * -k      - also write the kernel's am1 -T dump, bank 0 only, into field 31
 * check   - walk the directories, the i-nodes and the free list; exit 0 only for a sound image
 * ls      - list the directory path, or the one entry path names; dd when path is left out
 * -b      - with ls, each entry's blocks, with the field and drum address of each
 * cat     - copy the file path to standard output, two characters a word, high half first
 * -o      - with cat, the file's words instead, one octal word a line
 * A path is a list of names separated by '/', looked up from dd, i-number 4.
 *
 * The formats are those of the 1970 kernel listings (s4.s, s5.s, s8.s), the 12-word i-node,
 * the 8-word directory entry, the free chain and the system block, in 64-word blocks.
 * Block b is slot b mod 64 of drum field b / 64, the file system taking blocks 0 to 1983 in
 * fields 0 to 30; field 31 holds the kernel's image, outside the file system.
 * Slots are interleaved.
 * Slot s lies 64 x ((s x INTERLEAVE) mod 64) words into its field, so consecutive blocks are INTERLEAVE - 1 slots apart on the drum and a reader has time to
 * start the next transfer before its block comes around.
 * The kernel reads the drum with the same mapping, so INTERLEAVE must match it.
 *
 * Negative words are ones' complement, the PDP-1's.
 * A link count of n is stored as -n, 0777777 - n, and a uid of -1 as 0777776.
 *
 * The drum file is the emulator's, 32 fields of 4096 words, each 18-bit word in a
 * little-endian 32-bit word; a shorter file reads as zeros past its end.
 * make writes the whole drum to a temporary file in the image's directory, then renames it,
 * so a failure leaves an existing image as it was.
 *
 * The prototype is in the format of the pdp7-unix restoration's build/proto, one entry a
 * line.
 *     name f[r-][w-][r-][w-] uid source [inumber]     a file
 *     name d[r-][w-][r-][w-] uid [inumber]            a directory, its entries ending at "$"
 *     name i[r-][w-][r-][w-] uid inumber              a special file
 *     name l---- inumber                              a link, -1 for the entry before it
 * A "#" starts a comment, uids are in octal (-1 allowed), i-numbers in decimal; the end of
 * the file ends any directories still open.
 * The first entry is the top directory, dd, whose name is not stored.
 * Every directory begins with "dd", naming the top directory, and "..", naming itself, the
 * two entries the 1970 commands rely on and the restorers' mkfs7 writes.
 * A file's source is read by its name's ending.
 * A .dmp is am1 -T output, taken from address 0 of its bank to its last stored word; a .oct
 * is octal words separated by white space; anything else is text packed two characters a
 * word, high half first.
 * A relative source is taken from the prototype's directory.
 * I-numbers 1 to 32 are kept for the fixed files and the terminals, so i-numbers not given are
 * assigned from 33 upward in prototype order.
 *
 * The whole drum is held in one struct image and the parsed prototype in one struct proto,
 * both passed by pointer; blockAddress is the only place a block number becomes a drum address.
 *
 * Exit status is 0 on success and 1 on any error; check also exits 1 for an unsound image.
 *
 * Revision history:
 *
 * 20-Nov-2026 Claude initial version
 * 23-Nov-2026 Claude comments cleaned for the release
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>

// The interleave, K, odd so that it permutes a field's 64 slots.
#ifndef INTERLEAVE
#define INTERLEAVE      27          // the fewest the kernel keeps up with on 64-word reads
#endif
#if ((INTERLEAVE % 2) == 0) || (INTERLEAVE < 1)
#error INTERLEAVE must be odd and positive
#endif

#define MASK            0777777
#define FIELDS          32
#define FIELDWORDS      010000
#define DRUMWORDS       (FIELDS * FIELDWORDS)
#define BLOCKWORDS      64
#define SLOTS           (FIELDWORDS / BLOCKWORDS)
#define KERNELFIELD     31
#define FSBLOCKS        (KERNELFIELD * SLOTS)       // blocks 0 to 1983
#define SYSBLOCK        0
#define INODEBLOCK      2
#define INODEBLOCKS     50
#define INODESPERBLOCK  5
#define NINODES         (INODEBLOCKS * INODESPERBLOCK)
#define FIRSTDATA       (INODEBLOCK + INODEBLOCKS)  // 52
#define FIRSTAUTOINUM   33
#define DDINUM          4

// The system block, sysdata (s8.s).
#define S_NXFBLK        0
#define S_NFBLKS        1
#define S_FBLKS         2
#define NFBLKS          10
#define S_UNIQ          12
#define S_TIM           13

// The i-node (s8.s).
#define INODEWORDS      12
#define I_FLAGS         0
#define I_DSKPS         1
#define NDSKPS          7
#define I_UID           8
#define I_NLKS          9
#define I_SIZE          10
#define I_UNIQ          11
#define F_USED          0400000
#define F_LARGE         0200000
#define F_SPECIAL       0000040
#define F_DIRECTORY     0000020
#define F_OWNERREAD     0000010
#define F_OWNERWRITE    0000004
#define F_WORLDREAD     0000002
#define F_WORLDWRITE    0000001
#define MAXDATABLOCKS   (NDSKPS * BLOCKWORDS)       // a large file's 448 blocks, 28672 words

// The directory entry (s8.s).
#define DIRWORDS        8
#define D_I             0
#define D_NAME          1
#define NAMEWORDS       4
#define NAMECHARS       (NAMEWORDS * 2)
#define D_UNIQ          5

#define CHAINWORDS      (NFBLKS - 1)                // free block numbers in a chain block

// The kinds of prototype entry.
#define KFILE           0
#define KDIR            1
#define KSPECIAL        2
#define KLINK           3

#define LINELEN         1024

struct image
{
    uint32_t words[DRUMWORDS];
};

struct inode
{
    uint32_t flags;
    uint32_t dskps[NDSKPS];
    uint32_t uid;
    uint32_t nlks;
    uint32_t size;
    uint32_t uniq;
};

// One prototype line; the entries are kept in prototype order, which lists every directory
// before its contents.
struct entry
{
    char name[NAMECHARS + 1];
    int kind;
    uint32_t perms;
    uint32_t uid;
    char *sourceP;
    int inum;           // as given, 0 when not given; for a link, -1 is the entry before
    int parent;         // index of the directory holding it, -1 for the top
    int line;
    uint32_t *dataP;    // the file's or the directory's words, once read or built
    int size;
};

struct proto
{
    struct entry *entriesP;
    int count;
    int capacity;
    char *dirP;         // the prototype's directory, for relative sources
    char *nameP;
};

// The drum address of the first word of file system block `block`.
static uint32_t blockAddress(uint32_t block)
{
    uint32_t field;
    uint32_t slot;

    field = (block / SLOTS);
    slot = (block % SLOTS);
    return ((field * FIELDWORDS) + (BLOCKWORDS * ((slot * INTERLEAVE) % SLOTS)));
}

// A pointer to the first word of file system block `block`.
static uint32_t *blockP(struct image *imgP, uint32_t block)
{
    return (&imgP->words[blockAddress(block)]);
}

// -n in ones' complement for n of 0 or more, +0 for 0.
static uint32_t onesNeg(uint32_t n)
{
    if( n == 0 )
    {
        return (0);
    }
    return ((MASK ^ n) & MASK);
}

// Two characters packed into a word, the first in the high half.
static uint32_t packChars(int c1, int c2)
{
    return ((((uint32_t)c1 & 0777) << 9) | ((uint32_t)c2 & 0777));
}

// Pack a name of up to eight characters into four words, padded with spaces as the kernel's
// names are.
static void packName(const char *nameP, uint32_t *wordsP)
{
    char padded[NAMECHARS];
    int i;
    int len;

    len = (int)strlen(nameP);
    for( i = 0; i < NAMECHARS; i++ )
    {
        padded[i] = ((i < len) ? nameP[i] : ' ');
    }
    for( i = 0; i < NAMEWORDS; i++ )
    {
        wordsP[i] = packChars((unsigned char)padded[2 * i], (unsigned char)padded[(2 * i) + 1]);
    }
}

// Unpack a four-word name into a string, trailing spaces and NULs dropped and any other
// unprintable character shown as '?'.
static void unpackName(const uint32_t *wordsP, char *nameP)
{
    int i;
    int c;
    int end;

    for( i = 0; i < NAMECHARS; i++ )
    {
        c = (int)((i % 2) == 0 ? ((wordsP[i / 2] >> 9) & 0777) : (wordsP[i / 2] & 0777));
        nameP[i] = (((c >= 040) && (c < 0177)) ? (char)c : ((c == 0) ? ' ' : '?'));
    }
    end = NAMECHARS;
    while( (end > 0) && (nameP[end - 1] == ' ') )
    {
        end--;
    }
    nameP[end] = '\0';
}

// Read i-node `inum` from the image.
static void getInode(struct image *imgP, int inum, struct inode *inodeP)
{
    uint32_t *wP;
    int i;

    wP = (blockP(imgP, INODEBLOCK + (inum / INODESPERBLOCK)) + (INODEWORDS * (inum % INODESPERBLOCK)));
    inodeP->flags = wP[I_FLAGS];
    for( i = 0; i < NDSKPS; i++ )
    {
        inodeP->dskps[i] = wP[I_DSKPS + i];
    }
    inodeP->uid = wP[I_UID];
    inodeP->nlks = wP[I_NLKS];
    inodeP->size = wP[I_SIZE];
    inodeP->uniq = wP[I_UNIQ];
}

// Write i-node `inum` into the image.
static void putInode(struct image *imgP, int inum, const struct inode *inodeP)
{
    uint32_t *wP;
    int i;

    wP = (blockP(imgP, INODEBLOCK + (inum / INODESPERBLOCK)) + (INODEWORDS * (inum % INODESPERBLOCK)));
    wP[I_FLAGS] = inodeP->flags;
    for( i = 0; i < NDSKPS; i++ )
    {
        wP[I_DSKPS + i] = inodeP->dskps[i];
    }
    wP[I_UID] = inodeP->uid;
    wP[I_NLKS] = inodeP->nlks;
    wP[I_SIZE] = inodeP->size;
    wP[I_UNIQ] = inodeP->uniq;
}

// Read the drum file into the image, zeros past a short file's end.
// Returns false, having said why, when it cannot be read or holds more than a drum.
static bool readImage(const char *pathP, struct image *imgP)
{
    FILE *fP;
    unsigned char bytes[4];
    size_t got;
    uint32_t addr;
    uint32_t word;

    memset(imgP, 0, sizeof(*imgP));
    if( !(fP = fopen(pathP, "rb")) )
    {
        fprintf(stderr, "v0fs: %s: %s\n", pathP, strerror(errno));
        return (false);
    }
    for( addr = 0; addr < DRUMWORDS; addr++ )
    {
        got = fread(bytes, 1, 4, fP);
        if( got == 0 )
        {
            break;
        }
        if( got != 4 )
        {
            fprintf(stderr, "v0fs: %s: ends inside a word\n", pathP);
            fclose(fP);
            return (false);
        }
        word = ((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
                ((uint32_t)bytes[3] << 24));
        if( word > MASK )
        {
            fprintf(stderr, "v0fs: %s: word %06o is %o, more than 18 bits\n", pathP, addr, word);
            fclose(fP);
            return (false);
        }
        imgP->words[addr] = word;
    }
    if( (addr == DRUMWORDS) && (fread(bytes, 1, 1, fP) == 1) )
    {
        fprintf(stderr, "v0fs: %s: longer than a drum of %d words\n", pathP, DRUMWORDS);
        fclose(fP);
        return (false);
    }
    fclose(fP);
    return (true);
}

// Write the whole drum to a temporary file beside pathP, then rename it over pathP.
// Returns false, having said why, on any failure, leaving pathP as it was.
static bool writeImage(const char *pathP, const struct image *imgP)
{
    char *tmpP;
    char *copyP;
    int fd;
    FILE *fP;
    uint32_t addr;
    unsigned char bytes[4];
    bool ok;

    copyP = strdup(pathP);
    tmpP = malloc(strlen(pathP) + 32);
    if( !copyP || !tmpP )
    {
        fprintf(stderr, "v0fs: out of memory\n");
        free(copyP);
        free(tmpP);
        return (false);
    }
    sprintf(tmpP, "%s/.v0fs.XXXXXX", dirname(copyP));
    free(copyP);
    if( (fd = mkstemp(tmpP)) < 0 )
    {
        fprintf(stderr, "v0fs: %s: %s\n", tmpP, strerror(errno));
        free(tmpP);
        return (false);
    }
    if( !(fP = fdopen(fd, "wb")) )
    {
        fprintf(stderr, "v0fs: %s: %s\n", tmpP, strerror(errno));
        close(fd);
        unlink(tmpP);
        free(tmpP);
        return (false);
    }
    ok = true;
    for( addr = 0; (addr < DRUMWORDS) && ok; addr++ )
    {
        bytes[0] = (unsigned char)(imgP->words[addr] & 0377);
        bytes[1] = (unsigned char)((imgP->words[addr] >> 8) & 0377);
        bytes[2] = (unsigned char)((imgP->words[addr] >> 16) & 0377);
        bytes[3] = 0;
        ok = (fwrite(bytes, 1, 4, fP) == 4);
    }
    if( (fclose(fP) != 0) || !ok )
    {
        fprintf(stderr, "v0fs: %s: write failed\n", tmpP);
        unlink(tmpP);
        free(tmpP);
        return (false);
    }
    if( rename(tmpP, pathP) != 0 )
    {
        fprintf(stderr, "v0fs: %s: %s\n", pathP, strerror(errno));
        unlink(tmpP);
        free(tmpP);
        return (false);
    }
    free(tmpP);
    return (true);
}

// Append a word to a growing array, doubling it as needed.
// Returns false when memory runs out.
static bool pushWord(uint32_t **wordsPP, int *countP, int *capP, uint32_t word)
{
    uint32_t *newP;

    if( *countP == *capP )
    {
        *capP = ((*capP == 0) ? 256 : (*capP * 2));
        if( !(newP = realloc(*wordsPP, (size_t)*capP * sizeof(uint32_t))) )
        {
            fprintf(stderr, "v0fs: out of memory\n");
            return (false);
        }
        *wordsPP = newP;
    }
    (*wordsPP)[(*countP)++] = word;
    return (true);
}

// Read am1 -T output, "address value" lines, the last one the start address, not a word.
// The words must all lie in one bank, or in bank 0 alone when bank0Only; *wordsPP gets the
// bank's words from its address 0 to its last stored word.
// Returns false, having said why, on a malformed file or a word outside the allowed bank.
static bool readDump(const char *pathP, bool bank0Only, uint32_t **wordsPP, int *countP)
{
    FILE *fP;
    char line[LINELEN];
    char extra[LINELEN];
    unsigned long addr;
    unsigned long value;
    unsigned long *addrsP;
    unsigned long *valuesP;
    int n;
    int cap;
    int i;
    int fields;
    long bank;
    int top;
    unsigned long *growP;

    if( !(fP = fopen(pathP, "r")) )
    {
        fprintf(stderr, "v0fs: %s: %s\n", pathP, strerror(errno));
        return (false);
    }
    addrsP = NULL;
    valuesP = NULL;
    n = 0;
    cap = 0;
    while( fgets(line, sizeof(line), fP) )
    {
        fields = sscanf(line, "%lo %lo %s", &addr, &value, extra);
        if( fields < 1 )
        {
            continue;
        }
        if( fields != 2 )
        {
            fprintf(stderr, "v0fs: %s: not an am1 -T line, or an overlay's word: %s", pathP, line);
            fclose(fP);
            free(addrsP);
            free(valuesP);
            return (false);
        }
        if( n == cap )
        {
            cap = ((cap == 0) ? 1024 : (cap * 2));
            if( !(growP = realloc(addrsP, (size_t)cap * sizeof(unsigned long))) )
            {
                fprintf(stderr, "v0fs: out of memory\n");
                fclose(fP);
                free(addrsP);
                free(valuesP);
                return (false);
            }
            addrsP = growP;
            if( !(growP = realloc(valuesP, (size_t)cap * sizeof(unsigned long))) )
            {
                fprintf(stderr, "v0fs: out of memory\n");
                fclose(fP);
                free(addrsP);
                free(valuesP);
                return (false);
            }
            valuesP = growP;
        }
        addrsP[n] = addr;
        valuesP[n] = value;
        n++;
    }
    fclose(fP);

    // The last line is the start address.
    n--;
    if( n < 1 )
    {
        fprintf(stderr, "v0fs: %s: no words before the start line\n", pathP);
        free(addrsP);
        free(valuesP);
        return (false);
    }
    bank = -1;
    top = -1;
    for( i = 0; i < n; i++ )
    {
        if( (addrsP[i] > 0177777) || (valuesP[i] > MASK) )
        {
            fprintf(stderr, "v0fs: %s: %06lo %06lo is out of range\n", pathP, addrsP[i], valuesP[i]);
            free(addrsP);
            free(valuesP);
            return (false);
        }
        if( bank0Only && ((addrsP[i] / FIELDWORDS) != 0) )
        {
            fprintf(stderr, "v0fs: %s: word %06lo is outside bank 0; the kernel's image is bank 0 alone\n",
                    pathP, addrsP[i]);
            free(addrsP);
            free(valuesP);
            return (false);
        }
        if( bank < 0 )
        {
            bank = (long)(addrsP[i] / FIELDWORDS);
        }
        else if( bank != (long)(addrsP[i] / FIELDWORDS) )
        {
            fprintf(stderr, "v0fs: %s: words in banks %lo and %lo; a program is one bank\n",
                    pathP, (unsigned long)bank, addrsP[i] / FIELDWORDS);
            free(addrsP);
            free(valuesP);
            return (false);
        }
        if( (int)(addrsP[i] % FIELDWORDS) > top )
        {
            top = (int)(addrsP[i] % FIELDWORDS);
        }
    }
    if( !(*wordsPP = calloc((size_t)top + 1, sizeof(uint32_t))) )
    {
        fprintf(stderr, "v0fs: out of memory\n");
        free(addrsP);
        free(valuesP);
        return (false);
    }
    for( i = 0; i < n; i++ )
    {
        (*wordsPP)[addrsP[i] % FIELDWORDS] = (uint32_t)valuesP[i];
    }
    *countP = top + 1;
    free(addrsP);
    free(valuesP);
    return (true);
}

// Read a file of octal words separated by white space.
// Returns false, having said why, on anything that is not an 18-bit octal word.
static bool readOctal(const char *pathP, uint32_t **wordsPP, int *countP)
{
    FILE *fP;
    char token[64];
    char *endP;
    unsigned long value;
    int cap;

    if( !(fP = fopen(pathP, "r")) )
    {
        fprintf(stderr, "v0fs: %s: %s\n", pathP, strerror(errno));
        return (false);
    }
    *wordsPP = NULL;
    *countP = 0;
    cap = 0;
    while( fscanf(fP, "%63s", token) == 1 )
    {
        errno = 0;
        value = strtoul(token, &endP, 8);
        if( (*endP != '\0') || (errno != 0) || (value > MASK) || (token[0] == '-') )
        {
            fprintf(stderr, "v0fs: %s: %s is not an 18-bit octal word\n", pathP, token);
            fclose(fP);
            return (false);
        }
        if( !pushWord(wordsPP, countP, &cap, (uint32_t)value) )
        {
            fclose(fP);
            return (false);
        }
    }
    fclose(fP);
    return (true);
}

// Read a host text file, two bytes a word, the first in the high half; an odd last byte
// leaves the low half 0.
// Returns false, having said why, when it cannot be read.
static bool readText(const char *pathP, uint32_t **wordsPP, int *countP)
{
    FILE *fP;
    int c1;
    int c2;
    int cap;

    if( !(fP = fopen(pathP, "rb")) )
    {
        fprintf(stderr, "v0fs: %s: %s\n", pathP, strerror(errno));
        return (false);
    }
    *wordsPP = NULL;
    *countP = 0;
    cap = 0;
    while( (c1 = getc(fP)) != EOF )
    {
        if( (c2 = getc(fP)) == EOF )
        {
            c2 = 0;
        }
        if( !pushWord(wordsPP, countP, &cap, packChars(c1, c2)) )
        {
            fclose(fP);
            return (false);
        }
    }
    fclose(fP);
    return (true);
}

// True when nameP ends with suffixP.
static bool endsWith(const char *nameP, const char *suffixP)
{
    size_t n;
    size_t s;

    n = strlen(nameP);
    s = strlen(suffixP);
    return ((n >= s) && (strcmp(nameP + (n - s), suffixP) == 0));
}

// Read a file entry's source, by its name's ending, relative to the prototype's directory.
// Returns false, having said why, when it cannot be read.
static bool readSource(struct proto *protoP, struct entry *eP)
{
    char *pathP;
    bool ok;

    if( eP->sourceP[0] == '/' )
    {
        pathP = strdup(eP->sourceP);
    }
    else
    {
        pathP = malloc(strlen(protoP->dirP) + strlen(eP->sourceP) + 2);
        if( pathP )
        {
            sprintf(pathP, "%s/%s", protoP->dirP, eP->sourceP);
        }
    }
    if( !pathP )
    {
        fprintf(stderr, "v0fs: out of memory\n");
        return (false);
    }
    if( endsWith(pathP, ".dmp") )
    {
        ok = readDump(pathP, false, &eP->dataP, &eP->size);
    }
    else if( endsWith(pathP, ".oct") )
    {
        ok = readOctal(pathP, &eP->dataP, &eP->size);
    }
    else
    {
        ok = readText(pathP, &eP->dataP, &eP->size);
    }
    free(pathP);
    return (ok);
}

// Parse a permission word such as drwr- into its kind and permission bits.
// Returns false when it is not five characters of that form.
static bool parsePerms(const char *wordP, int *kindP, uint32_t *permsP)
{
    if( strlen(wordP) != 5 )
    {
        return (false);
    }
    switch( wordP[0] )
    {
    case 'f':
        *kindP = KFILE;
        break;
    case 'd':
        *kindP = KDIR;
        break;
    case 'i':
        *kindP = KSPECIAL;
        break;
    case 'l':
        *kindP = KLINK;
        break;
    default:
        return (false);
    }
    if( ((wordP[1] != 'r') && (wordP[1] != '-')) || ((wordP[2] != 'w') && (wordP[2] != '-')) ||
        ((wordP[3] != 'r') && (wordP[3] != '-')) || ((wordP[4] != 'w') && (wordP[4] != '-')) )
    {
        return (false);
    }
    *permsP = (((wordP[1] == 'r') ? F_OWNERREAD : 0) | ((wordP[2] == 'w') ? F_OWNERWRITE : 0) |
               ((wordP[3] == 'r') ? F_WORLDREAD : 0) | ((wordP[4] == 'w') ? F_WORLDWRITE : 0));
    return (true);
}

// Parse a uid, -1 or an octal word 0 to 0777777; -1 is stored as ones' complement
// 0777776.
// The password file's super-user, "777777", is the word 0777777 when login reads it, and a
// prototype writes it so to give its files that uid.
// Returns false when it is neither.
static bool parseUid(const char *wordP, uint32_t *uidP)
{
    char *endP;
    unsigned long value;

    if( strcmp(wordP, "-1") == 0 )
    {
        *uidP = onesNeg(1);
        return (true);
    }
    errno = 0;
    value = strtoul(wordP, &endP, 8);
    if( (*endP != '\0') || (errno != 0) || (value > 0777777) || (wordP[0] == '-') )
    {
        return (false);
    }
    *uidP = (uint32_t)value;
    return (true);
}

// Parse a decimal i-number; -1 is allowed only when allowPrevious.
// Returns false when it is not one.
static bool parseInum(const char *wordP, bool allowPrevious, int *inumP)
{
    char *endP;
    long value;

    errno = 0;
    value = strtol(wordP, &endP, 10);
    if( (*endP != '\0') || (errno != 0) )
    {
        return (false);
    }
    if( (value == -1) && allowPrevious )
    {
        *inumP = -1;
        return (true);
    }
    if( (value < 1) || (value >= NINODES) )
    {
        return (false);
    }
    *inumP = (int)value;
    return (true);
}

// Parse the prototype file into protoP, in order.
// Returns false, having said why with the line number, on any malformed line.
static bool parseProto(const char *pathP, struct proto *protoP)
{
    FILE *fP;
    char line[LINELEN];
    char *words[8];
    char *tokP;
    char *hashP;
    char *copyP;
    int nWords;
    int lineNo;
    int stack[NINODES];
    int depth;
    bool topDone;
    struct entry *eP;
    struct entry *growP;
    int kind;
    uint32_t perms;

    memset(protoP, 0, sizeof(*protoP));
    protoP->nameP = strdup(pathP);
    copyP = strdup(pathP);
    if( !protoP->nameP || !copyP )
    {
        fprintf(stderr, "v0fs: out of memory\n");
        free(copyP);
        return (false);
    }
    protoP->dirP = strdup(dirname(copyP));
    free(copyP);
    if( !(fP = fopen(pathP, "r")) )
    {
        fprintf(stderr, "v0fs: %s: %s\n", pathP, strerror(errno));
        return (false);
    }
    depth = 0;
    topDone = false;
    lineNo = 0;
    while( fgets(line, sizeof(line), fP) )
    {
        lineNo++;
        if( (hashP = strchr(line, '#')) )
        {
            *hashP = '\0';
        }
        nWords = 0;
        for( tokP = strtok(line, " \t\r\n"); tokP && (nWords < 8); tokP = strtok(NULL, " \t\r\n") )
        {
            words[nWords++] = tokP;
        }
        if( nWords == 0 )
        {
            continue;
        }
        if( strcmp(words[0], "$") == 0 )
        {
            if( depth == 0 )
            {
                fprintf(stderr, "v0fs: %s:%d: \"$\" with no directory open\n", pathP, lineNo);
                fclose(fP);
                return (false);
            }
            depth--;
            continue;
        }
        if( topDone && (depth == 0) )
        {
            fprintf(stderr, "v0fs: %s:%d: an entry after the top directory's \"$\"\n", pathP, lineNo);
            fclose(fP);
            return (false);
        }
        if( (nWords < 2) || !parsePerms(words[1], &kind, &perms) )
        {
            fprintf(stderr, "v0fs: %s:%d: expected a name and a permission word such as frwr-\n",
                    pathP, lineNo);
            fclose(fP);
            return (false);
        }
        if( strlen(words[0]) > NAMECHARS )
        {
            fprintf(stderr, "v0fs: %s:%d: %s is longer than %d characters\n", pathP, lineNo,
                    words[0], NAMECHARS);
            fclose(fP);
            return (false);
        }
        if( protoP->count == protoP->capacity )
        {
            protoP->capacity = ((protoP->capacity == 0) ? 64 : (protoP->capacity * 2));
            if( !(growP = realloc(protoP->entriesP, (size_t)protoP->capacity * sizeof(struct entry))) )
            {
                fprintf(stderr, "v0fs: out of memory\n");
                fclose(fP);
                return (false);
            }
            protoP->entriesP = growP;
        }
        eP = &protoP->entriesP[protoP->count];
        memset(eP, 0, sizeof(*eP));
        strcpy(eP->name, words[0]);
        eP->kind = kind;
        eP->perms = perms;
        eP->line = lineNo;
        eP->parent = ((depth == 0) ? -1 : stack[depth - 1]);
        if( (depth == 0) && (kind != KDIR) )
        {
            fprintf(stderr, "v0fs: %s:%d: the first entry must be the top directory\n", pathP, lineNo);
            fclose(fP);
            return (false);
        }

        // The words after the permissions, by kind.
        if( ((kind == KFILE) && ((nWords < 4) || (nWords > 5))) ||
            ((kind == KDIR) && ((nWords < 3) || (nWords > 4))) ||
            ((kind == KSPECIAL) && (nWords != 4)) || ((kind == KLINK) && (nWords != 3)) )
        {
            fprintf(stderr, "v0fs: %s:%d: wrong number of words for this kind of entry\n", pathP, lineNo);
            fclose(fP);
            return (false);
        }
        if( (kind != KLINK) && !parseUid(words[2], &eP->uid) )
        {
            fprintf(stderr, "v0fs: %s:%d: %s is not a uid, -1 or octal\n", pathP, lineNo, words[2]);
            fclose(fP);
            return (false);
        }
        if( ((kind == KFILE) && (nWords == 5) && !parseInum(words[4], false, &eP->inum)) ||
            ((kind == KDIR) && (nWords == 4) && !parseInum(words[3], false, &eP->inum)) ||
            ((kind == KSPECIAL) && !parseInum(words[3], false, &eP->inum)) ||
            ((kind == KLINK) && !parseInum(words[2], true, &eP->inum)) )
        {
            fprintf(stderr, "v0fs: %s:%d: bad i-number, 1 to %d\n", pathP, lineNo, NINODES - 1);
            fclose(fP);
            return (false);
        }
        if( kind == KFILE )
        {
            if( !(eP->sourceP = strdup(words[3])) )
            {
                fprintf(stderr, "v0fs: out of memory\n");
                fclose(fP);
                return (false);
            }
        }
        if( kind == KDIR )
        {
            if( depth == NINODES )
            {
                fprintf(stderr, "v0fs: %s:%d: directories nested too deep\n", pathP, lineNo);
                fclose(fP);
                return (false);
            }
            stack[depth++] = protoP->count;
            topDone = true;
        }
        protoP->count++;
    }
    fclose(fP);
    if( protoP->count == 0 )
    {
        fprintf(stderr, "v0fs: %s: no entries\n", pathP);
        return (false);
    }
    // The end of the file ends any directories still open, as the restorers' prototype
    // relies on for its top directory.
    return (true);
}

// Give every entry its i-number, the top directory 4, those given as given, the rest from 33
// upward in prototype order, then each link the i-number it names.
// Returns false, having said why, on a clash, a link to nothing, or no i-numbers left.
static bool assignInodes(struct proto *protoP)
{
    bool taken[NINODES];
    int i;
    int next;
    struct entry *eP;

    memset(taken, 0, sizeof(taken));
    eP = &protoP->entriesP[0];
    if( (eP->inum != 0) && (eP->inum != DDINUM) )
    {
        fprintf(stderr, "v0fs: %s:%d: the top directory is dd, i-number %d\n", protoP->nameP,
                eP->line, DDINUM);
        return (false);
    }
    eP->inum = DDINUM;
    for( i = 0; i < protoP->count; i++ )
    {
        eP = &protoP->entriesP[i];
        if( (eP->kind != KLINK) && (eP->inum != 0) )
        {
            if( taken[eP->inum] )
            {
                fprintf(stderr, "v0fs: %s:%d: i-number %d is given twice\n", protoP->nameP, eP->line,
                        eP->inum);
                return (false);
            }
            taken[eP->inum] = true;
        }
    }
    next = FIRSTAUTOINUM;
    for( i = 0; i < protoP->count; i++ )
    {
        eP = &protoP->entriesP[i];
        if( (eP->kind != KLINK) && (eP->inum == 0) )
        {
            while( (next < NINODES) && taken[next] )
            {
                next++;
            }
            if( next == NINODES )
            {
                fprintf(stderr, "v0fs: %s:%d: no i-numbers left, %d in all\n", protoP->nameP, eP->line,
                        NINODES);
                return (false);
            }
            eP->inum = next;
            taken[next] = true;
        }
    }
    for( i = 0; i < protoP->count; i++ )
    {
        eP = &protoP->entriesP[i];
        if( eP->kind != KLINK )
        {
            continue;
        }
        if( eP->inum == -1 )
        {
            eP->inum = protoP->entriesP[i - 1].inum;
        }
        else if( !taken[eP->inum] )
        {
            fprintf(stderr, "v0fs: %s:%d: link to i-number %d, which no entry has\n", protoP->nameP,
                    eP->line, eP->inum);
            return (false);
        }
    }
    return (true);
}

// Append one directory entry, name naming inum, its unique number inum's.
// Returns false when memory runs out.
static bool pushEntry(struct entry *dirP, int *capP, const char *nameP, int inum)
{
    uint32_t name[NAMEWORDS];

    packName(nameP, name);
    return (pushWord(&dirP->dataP, &dirP->size, capP, (uint32_t)inum) &&
            pushWord(&dirP->dataP, &dirP->size, capP, name[0]) &&
            pushWord(&dirP->dataP, &dirP->size, capP, name[1]) &&
            pushWord(&dirP->dataP, &dirP->size, capP, name[2]) &&
            pushWord(&dirP->dataP, &dirP->size, capP, name[3]) &&
            pushWord(&dirP->dataP, &dirP->size, capP, (uint32_t)inum) &&
            pushWord(&dirP->dataP, &dirP->size, capP, 0) &&
            pushWord(&dirP->dataP, &dirP->size, capP, 0));
}

// Build each directory's words, "dd" naming the top directory, ".." naming the directory
// itself, then its entries in prototype order.
// The 1970 commands find the current directory by that "..".
// ls with no name lists "..", stat asks the status of a name in "..", dsw and pd open it;
// no kernel treats the name specially.
// The restorers' mkfs7 writes the same two entries first.
// Returns false when memory runs out.
static bool buildDirectories(struct proto *protoP)
{
    int d;
    int i;
    int cap;
    struct entry *dirP;
    struct entry *eP;

    for( d = 0; d < protoP->count; d++ )
    {
        dirP = &protoP->entriesP[d];
        if( dirP->kind != KDIR )
        {
            continue;
        }
        dirP->dataP = NULL;
        dirP->size = 0;
        cap = 0;
        if( !pushEntry(dirP, &cap, "dd", DDINUM) || !pushEntry(dirP, &cap, "..", dirP->inum) )
        {
            return (false);
        }
        for( i = d + 1; i < protoP->count; i++ )
        {
            eP = &protoP->entriesP[i];
            if( (eP->parent == d) && !pushEntry(dirP, &cap, eP->name, eP->inum) )
            {
                return (false);
            }
        }
    }
    return (true);
}

// Lay the file system into the image, blocks taken in order from 52 for each file and
// directory in prototype order, data blocks first and then any indirect blocks; the i-nodes,
// with every link counted; the free blocks left over; and the system block.
// Returns false, having said why, when a file is too large or the drum is full.
static bool layout(struct proto *protoP, struct image *imgP)
{
    int links[NINODES];
    int highest;
    uint32_t next;
    uint32_t first;
    uint32_t b;
    uint32_t chain;
    uint32_t *sysP;
    uint32_t *wP;
    int i;
    int k;
    int nData;
    int nInd;
    int remainder;
    struct entry *eP;
    struct inode inode;

    memset(links, 0, sizeof(links));
    highest = DDINUM;
    for( i = 0; i < protoP->count; i++ )
    {
        eP = &protoP->entriesP[i];
        if( eP->parent >= 0 )
        {
            links[eP->inum]++;
        }
        if( eP->kind == KDIR )
        {
            // Its "dd" entry and its "..".
            links[DDINUM]++;
            links[eP->inum]++;
        }
        if( eP->inum > highest )
        {
            highest = eP->inum;
        }
    }

    next = FIRSTDATA;
    for( i = 0; i < protoP->count; i++ )
    {
        eP = &protoP->entriesP[i];
        if( eP->kind == KLINK )
        {
            continue;
        }
        memset(&inode, 0, sizeof(inode));
        nData = ((eP->size + BLOCKWORDS - 1) / BLOCKWORDS);
        if( nData > MAXDATABLOCKS )
        {
            fprintf(stderr, "v0fs: %s:%d: %s is %d words, more than the %d a file can hold\n",
                    protoP->nameP, eP->line, eP->name, eP->size, MAXDATABLOCKS * BLOCKWORDS);
            return (false);
        }
        nInd = ((nData > NDSKPS) ? ((nData + BLOCKWORDS - 1) / BLOCKWORDS) : 0);
        if( (next + (uint32_t)nData + (uint32_t)nInd) > FSBLOCKS )
        {
            fprintf(stderr, "v0fs: %s:%d: the drum is full at %s\n", protoP->nameP, eP->line, eP->name);
            return (false);
        }

        // The data, block by block.
        first = next;
        for( k = 0; k < nData; k++ )
        {
            wP = blockP(imgP, next);
            memcpy(wP, eP->dataP + (k * BLOCKWORDS),
                   (size_t)(((eP->size - (k * BLOCKWORDS)) < BLOCKWORDS) ? (eP->size - (k * BLOCKWORDS)) :
                            BLOCKWORDS) * sizeof(uint32_t));
            next++;
        }

        // A large file's pointers name indirect blocks, each holding 64 data block numbers.
        if( nInd > 0 )
        {
            for( k = 0; k < nInd; k++ )
            {
                inode.dskps[k] = (next + (uint32_t)k);
            }
            for( k = 0; k < nData; k++ )
            {
                blockP(imgP, next + (uint32_t)(k / BLOCKWORDS))[k % BLOCKWORDS] = (first + (uint32_t)k);
            }
            next += (uint32_t)nInd;
            inode.flags |= F_LARGE;
        }
        else
        {
            for( k = 0; k < nData; k++ )
            {
                inode.dskps[k] = (first + (uint32_t)k);
            }
        }
        inode.flags |= (F_USED | eP->perms | ((eP->kind == KDIR) ? F_DIRECTORY : 0) |
                        ((eP->kind == KSPECIAL) ? F_SPECIAL : 0));
        inode.uid = eP->uid;
        inode.nlks = onesNeg((uint32_t)links[eP->inum]);
        inode.size = (uint32_t)eP->size;
        inode.uniq = (uint32_t)eP->inum;
        putInode(imgP, eP->inum, &inode);
    }

    // The free blocks, so that the kernel's alloc hands them out in ascending order.
    // alloc takes s.fblks from the top down, then reads a chain block and takes its nine
    // numbers from word 9 down to word 1 and the chain block itself last (s4.s).
    // So the first (count mod 10) free blocks go in s.fblks, and each following group of ten has its
    // highest block as the chain block, holding the other nine in descending order.
    sysP = blockP(imgP, SYSBLOCK);
    remainder = (int)((FSBLOCKS - next) % NFBLKS);
    for( k = 0; k < remainder; k++ )
    {
        sysP[S_FBLKS + (remainder - 1 - k)] = next++;
    }
    sysP[S_NFBLKS] = (uint32_t)remainder;
    chain = 0;
    for( b = (FSBLOCKS - NFBLKS); (b >= next) && (b < FSBLOCKS); b -= NFBLKS )
    {
        wP = blockP(imgP, b + CHAINWORDS);
        wP[0] = chain;
        for( k = 1; k <= CHAINWORDS; k++ )
        {
            wP[k] = (b + (uint32_t)(CHAINWORDS - k));
        }
        chain = (b + CHAINWORDS);
    }
    sysP[S_NXFBLK] = chain;
    sysP[S_UNIQ] = (uint32_t)highest;
    sysP[S_TIM] = 0;
    sysP[S_TIM + 1] = 0;
    return (true);
}

// The data block holding word `k` * 64 of a file, 0 for a hole.
// Returns false, having said why, when a pointer on the way lies outside the file system.
static bool dataBlock(struct image *imgP, const struct inode *inodeP, int k, uint32_t *blockOutP)
{
    uint32_t ind;

    if( inodeP->flags & F_LARGE )
    {
        if( (k / BLOCKWORDS) >= NDSKPS )
        {
            *blockOutP = 0;
            return (true);
        }
        ind = inodeP->dskps[k / BLOCKWORDS];
        if( ind == 0 )
        {
            *blockOutP = 0;
            return (true);
        }
        if( ind >= FSBLOCKS )
        {
            fprintf(stderr, "v0fs: indirect block %u is outside the file system\n", ind);
            return (false);
        }
        *blockOutP = blockP(imgP, ind)[k % BLOCKWORDS];
    }
    else
    {
        *blockOutP = ((k < NDSKPS) ? inodeP->dskps[k] : 0);
    }
    if( *blockOutP >= FSBLOCKS )
    {
        fprintf(stderr, "v0fs: block %u is outside the file system\n", *blockOutP);
        return (false);
    }
    return (true);
}

// Read a file's words, holes as zeros; *wordsPP is allocated and must be freed.
// Returns false, having said why, on a block outside the file system.
static bool readFile(struct image *imgP, const struct inode *inodeP, uint32_t **wordsPP)
{
    int k;
    int nData;
    int n;
    uint32_t b;

    if( !(*wordsPP = calloc((size_t)inodeP->size + 1, sizeof(uint32_t))) )
    {
        fprintf(stderr, "v0fs: out of memory\n");
        return (false);
    }
    nData = (int)((inodeP->size + BLOCKWORDS - 1) / BLOCKWORDS);
    for( k = 0; k < nData; k++ )
    {
        if( !dataBlock(imgP, inodeP, k, &b) )
        {
            free(*wordsPP);
            return (false);
        }
        if( b != 0 )
        {
            n = (int)(((inodeP->size - ((uint32_t)k * BLOCKWORDS)) < BLOCKWORDS) ?
                      (inodeP->size - ((uint32_t)k * BLOCKWORDS)) : BLOCKWORDS);
            memcpy(*wordsPP + (k * BLOCKWORDS), blockP(imgP, b), (size_t)n * sizeof(uint32_t));
        }
    }
    return (true);
}

// Look a path up from dd; *inumP gets its i-number and nameP its last name, "dd" for none.
// Returns false, having said why, when a name is missing or a directory is not one.
static bool lookup(struct image *imgP, const char *pathP, int *inumP, char *nameP)
{
    char *copyP;
    char *partP;
    char *saveP;
    char entryName[NAMECHARS + 1];
    uint32_t *wordsP;
    struct inode inode;
    int inum;
    int e;
    bool found;

    inum = DDINUM;
    strcpy(nameP, "dd");
    if( !(copyP = strdup(pathP ? pathP : "")) )
    {
        fprintf(stderr, "v0fs: out of memory\n");
        return (false);
    }
    for( partP = strtok_r(copyP, "/", &saveP); partP; partP = strtok_r(NULL, "/", &saveP) )
    {
        getInode(imgP, inum, &inode);
        if( !(inode.flags & F_USED) || !(inode.flags & F_DIRECTORY) )
        {
            fprintf(stderr, "v0fs: %s: i-node %d is not a directory\n", pathP, inum);
            free(copyP);
            return (false);
        }
        if( !readFile(imgP, &inode, &wordsP) )
        {
            free(copyP);
            return (false);
        }
        found = false;
        for( e = 0; ((e + DIRWORDS) <= (int)inode.size) && !found; e += DIRWORDS )
        {
            unpackName(wordsP + e + D_NAME, entryName);
            if( (wordsP[e + D_I] != 0) && (strcmp(entryName, partP) == 0) )
            {
                inum = (int)wordsP[e + D_I];
                found = true;
            }
        }
        free(wordsP);
        if( !found || (inum >= NINODES) )
        {
            fprintf(stderr, "v0fs: %s: no entry %s\n", pathP, partP);
            free(copyP);
            return (false);
        }
        snprintf(nameP, NAMECHARS + 1, "%s", partP);
    }
    free(copyP);
    *inumP = inum;
    return (true);
}

// Print one listing line, with the i-number, kind and permissions, links, uid, size in words
// and name; with showBlocks, a line for each block after it.
static void listEntry(struct image *imgP, int inum, const char *nameP, bool showBlocks)
{
    struct inode inode;
    char kind;
    char uid[16];
    uint32_t links;
    uint32_t b;
    int k;
    int nData;

    getInode(imgP, inum, &inode);
    if( !(inode.flags & F_USED) )
    {
        printf("%4d free i-node   %s\n", inum, nameP);
        return;
    }
    kind = ((inode.flags & F_DIRECTORY) ? 'd' : ((inode.flags & F_SPECIAL) ? 'i' :
            ((inode.flags & F_LARGE) ? 'l' : 's')));
    links = ((inode.nlks == 0) ? 0 : ((inode.nlks & 0400000) ? (MASK - inode.nlks) : inode.nlks));
    if( inode.uid == onesNeg(1) )
    {
        strcpy(uid, "-1");
    }
    else
    {
        snprintf(uid, sizeof(uid), "%o", inode.uid);
    }
    printf("%4d %c%c%c%c%c %3u %4s %6u %s\n", inum, kind, ((inode.flags & F_OWNERREAD) ? 'r' : '-'),
           ((inode.flags & F_OWNERWRITE) ? 'w' : '-'), ((inode.flags & F_WORLDREAD) ? 'r' : '-'),
           ((inode.flags & F_WORLDWRITE) ? 'w' : '-'), links, uid, inode.size, nameP);
    if( !showBlocks )
    {
        return;
    }
    nData = (int)((inode.size + BLOCKWORDS - 1) / BLOCKWORDS);
    for( k = 0; k < nData; k++ )
    {
        if( !dataBlock(imgP, &inode, k, &b) )
        {
            return;
        }
        printf("     block %4u field %2u address %04o\n", b, (blockAddress(b) / FIELDWORDS),
               (blockAddress(b) % FIELDWORDS));
    }
    if( inode.flags & F_LARGE )
    {
        for( k = 0; k < NDSKPS; k++ )
        {
            if( (b = inode.dskps[k]) != 0 )
            {
                printf("  indirect %4u field %2u address %04o\n", b, (blockAddress(b) / FIELDWORDS),
                       (blockAddress(b) % FIELDWORDS));
            }
        }
    }
}

// ls, a directory's entries or the one entry the path names.
// Returns the exit status.
static int commandLs(struct image *imgP, const char *pathP, bool showBlocks)
{
    struct inode inode;
    uint32_t *wordsP;
    char name[NAMECHARS + 1];
    int inum;
    int e;

    if( !lookup(imgP, pathP, &inum, name) )
    {
        return (1);
    }
    getInode(imgP, inum, &inode);
    if( !(inode.flags & F_USED) || !(inode.flags & F_DIRECTORY) )
    {
        listEntry(imgP, inum, name, showBlocks);
        return (0);
    }
    if( !readFile(imgP, &inode, &wordsP) )
    {
        return (1);
    }
    for( e = 0; (e + DIRWORDS) <= (int)inode.size; e += DIRWORDS )
    {
        if( wordsP[e + D_I] == 0 )
        {
            continue;
        }
        unpackName(wordsP + e + D_NAME, name);
        if( wordsP[e + D_I] >= NINODES )
        {
            printf("%4u bad i-number %s\n", wordsP[e + D_I], name);
            continue;
        }
        listEntry(imgP, (int)wordsP[e + D_I], name, showBlocks);
    }
    free(wordsP);
    return (0);
}

// cat, a file's words to standard output, as characters or as octal words.
// Returns the exit status.
static int commandCat(struct image *imgP, const char *pathP, bool octal)
{
    struct inode inode;
    uint32_t *wordsP;
    char name[NAMECHARS + 1];
    int inum;
    uint32_t i;

    if( !lookup(imgP, pathP, &inum, name) )
    {
        return (1);
    }
    getInode(imgP, inum, &inode);
    if( !(inode.flags & F_USED) || (inode.flags & F_SPECIAL) )
    {
        fprintf(stderr, "v0fs: %s: i-node %d is %s\n", pathP, inum,
                ((inode.flags & F_USED) ? "a special file" : "free"));
        return (1);
    }
    if( !readFile(imgP, &inode, &wordsP) )
    {
        return (1);
    }
    for( i = 0; i < inode.size; i++ )
    {
        if( octal )
        {
            printf("%06o\n", wordsP[i]);
        }
        else
        {
            // A final NUL is the padding of an odd-length text.
            putchar((int)((wordsP[i] >> 9) & 0377));
            if( ((i + 1) < inode.size) || ((wordsP[i] & 0777) != 0) )
            {
                putchar((int)(wordsP[i] & 0377));
            }
        }
    }
    free(wordsP);
    return (0);
}

// Record one fault for check.
static void fault(int *faultsP, const char *formatP, ...) __attribute__((format(printf, 2, 3)));
static void fault(int *faultsP, const char *formatP, ...)
{
    va_list args;

    va_start(args, formatP);
    printf("fault: ");
    vprintf(formatP, args);
    printf("\n");
    va_end(args);
    (*faultsP)++;
}

// Mark block b used by i-node inum during check, reporting one outside the data area or
// already used.
static void markUsed(int *usedBy, uint32_t b, int inum, int *faultsP)
{
    if( (b < FIRSTDATA) || (b >= FSBLOCKS) )
    {
        fault(faultsP, "block %u of i-node %d is outside the data blocks, %d to %d", b, inum, FIRSTDATA,
              FSBLOCKS - 1);
        return;
    }
    if( usedBy[b] != 0 )
    {
        fault(faultsP, "block %u is in two files: i-nodes %d and %d", b, usedBy[b], inum);
        return;
    }
    usedBy[b] = inum;
}

// Mark block b free during check, reporting one outside the data area or already free.
static void markFree(int *freeCount, uint32_t b, int *faultsP)
{
    if( (b < FIRSTDATA) || (b >= FSBLOCKS) )
    {
        fault(faultsP, "the free list holds block %u, outside the data blocks, %d to %d", b, FIRSTDATA,
              FSBLOCKS - 1);
        return;
    }
    if( freeCount[b]++ != 0 )
    {
        fault(faultsP, "block %u is in the free list twice", b);
    }
}

// check, the i-nodes' blocks, the free list, the directories from dd and the link counts.
// Returns the exit status, 0 for a sound image, 1 when any fault is found.
static int commandCheck(struct image *imgP)
{
    int usedBy[FSBLOCKS];
    int freeCount[FSBLOCKS];
    int refs[NINODES];
    bool visited[NINODES];
    int queue[NINODES];
    int head;
    int tail;
    int faults;
    int inum;
    int target;
    int k;
    int e;
    int steps;
    uint32_t b;
    uint32_t *sysP;
    uint32_t *wP;
    uint32_t *wordsP;
    uint32_t expected;
    char name[NAMECHARS + 1];
    struct inode inode;
    struct inode targetInode;

    memset(usedBy, 0, sizeof(usedBy));
    memset(freeCount, 0, sizeof(freeCount));
    memset(refs, 0, sizeof(refs));
    memset(visited, 0, sizeof(visited));
    faults = 0;

    // Every block of every i-node in use.
    for( inum = 1; inum < NINODES; inum++ )
    {
        getInode(imgP, inum, &inode);
        if( !(inode.flags & F_USED) )
        {
            continue;
        }
        for( k = 0; k < NDSKPS; k++ )
        {
            if( (b = inode.dskps[k]) == 0 )
            {
                continue;
            }
            markUsed(usedBy, b, inum, &faults);
            if( (inode.flags & F_LARGE) && (b >= FIRSTDATA) && (b < FSBLOCKS) )
            {
                for( e = 0; e < BLOCKWORDS; e++ )
                {
                    if( blockP(imgP, b)[e] != 0 )
                    {
                        markUsed(usedBy, blockP(imgP, b)[e], inum, &faults);
                    }
                }
            }
        }
    }

    // The free list, the cached numbers in the system block, then the chain.
    sysP = blockP(imgP, SYSBLOCK);
    if( sysP[S_NFBLKS] > NFBLKS )
    {
        fault(&faults, "the system block's count of free blocks, %u, is more than %d", sysP[S_NFBLKS], NFBLKS);
    }
    else
    {
        for( k = 0; k < (int)sysP[S_NFBLKS]; k++ )
        {
            markFree(freeCount, sysP[S_FBLKS + k], &faults);
        }
    }
    steps = 0;
    for( b = sysP[S_NXFBLK]; b != 0; b = wP[0] )
    {
        if( (b < FIRSTDATA) || (b >= FSBLOCKS) || (freeCount[b] != 0) || (++steps > FSBLOCKS) )
        {
            fault(&faults, "the free chain is broken at block %u", b);
            break;
        }
        markFree(freeCount, b, &faults);
        wP = blockP(imgP, b);
        for( k = 1; k <= CHAINWORDS; k++ )
        {
            markFree(freeCount, wP[k], &faults);
        }
    }

    // Each data block is used once or free once, never both, never neither.
    for( b = FIRSTDATA; b < FSBLOCKS; b++ )
    {
        if( (usedBy[b] != 0) && (freeCount[b] != 0) )
        {
            fault(&faults, "block %u is both used, by i-node %d, and free", b, usedBy[b]);
        }
        else if( (usedBy[b] == 0) && (freeCount[b] == 0) )
        {
            fault(&faults, "block %u is lost: neither used nor free", b);
        }
    }

    // The directories, from dd.
    getInode(imgP, DDINUM, &inode);
    if( !(inode.flags & F_USED) || !(inode.flags & F_DIRECTORY) )
    {
        fault(&faults, "dd, i-node %d, is not a directory in use", DDINUM);
    }
    else
    {
        head = 0;
        tail = 0;
        queue[tail++] = DDINUM;
        visited[DDINUM] = true;
        while( head < tail )
        {
            inum = queue[head++];
            getInode(imgP, inum, &inode);
            if( !readFile(imgP, &inode, &wordsP) )
            {
                fault(&faults, "directory i-node %d cannot be read", inum);
                continue;
            }
            for( e = 0; (e + DIRWORDS) <= (int)inode.size; e += DIRWORDS )
            {
                if( (target = (int)wordsP[e + D_I]) == 0 )
                {
                    continue;
                }
                unpackName(wordsP + e + D_NAME, name);
                if( target >= NINODES )
                {
                    fault(&faults, "directory i-node %d: entry %s names i-number %d, past %d", inum, name,
                          target, NINODES - 1);
                    continue;
                }
                getInode(imgP, target, &targetInode);
                if( !(targetInode.flags & F_USED) )
                {
                    fault(&faults, "directory i-node %d: entry %s names free i-node %d", inum, name, target);
                    continue;
                }
                refs[target]++;
                if( (targetInode.flags & F_DIRECTORY) && !visited[target] )
                {
                    visited[target] = true;
                    queue[tail++] = target;
                }
            }
            free(wordsP);
        }
    }

    // Each i-node in use is named, as often as its link count says.
    for( inum = 1; inum < NINODES; inum++ )
    {
        getInode(imgP, inum, &inode);
        if( !(inode.flags & F_USED) )
        {
            continue;
        }
        if( refs[inum] == 0 )
        {
            fault(&faults, "i-node %d is in use but no directory names it (orphan)", inum);
            continue;
        }
        expected = onesNeg((uint32_t)refs[inum]);
        if( inode.nlks != expected )
        {
            fault(&faults, "i-node %d has link count %06o, but %d entries name it (%06o)", inum, inode.nlks,
                  refs[inum], expected);
        }
    }

    if( faults == 0 )
    {
        printf("sound\n");
        return (0);
    }
    printf("%d fault%s\n", faults, ((faults == 1) ? "" : "s"));
    return (1);
}

// make, which parses the prototype, reads the sources, lays out the file system and the
// kernel's field, and writes the image.
// Returns the exit status.
static int commandMake(struct image *imgP, const char *protoNameP, const char *imageNameP,
                       const char *kernelNameP)
{
    struct proto proto;
    uint32_t *kernelP;
    int kernelSize;
    int i;

    memset(imgP, 0, sizeof(*imgP));
    if( !parseProto(protoNameP, &proto) || !assignInodes(&proto) )
    {
        return (1);
    }
    for( i = 0; i < proto.count; i++ )
    {
        if( (proto.entriesP[i].kind == KFILE) && !readSource(&proto, &proto.entriesP[i]) )
        {
            return (1);
        }
    }
    if( !buildDirectories(&proto) || !layout(&proto, imgP) )
    {
        return (1);
    }
    if( kernelNameP )
    {
        if( !readDump(kernelNameP, true, &kernelP, &kernelSize) )
        {
            return (1);
        }
        memcpy(&imgP->words[KERNELFIELD * FIELDWORDS], kernelP, (size_t)kernelSize * sizeof(uint32_t));
        free(kernelP);
    }
    return (writeImage(imageNameP, imgP) ? 0 : 1);
}

// Print how to use the program.
static void usage(void)
{
    fprintf(stderr,
            "usage: v0fs make [-k kernel.dmp] proto image\n"
            "       v0fs check image\n"
            "       v0fs ls [-b] image [path]\n"
            "       v0fs cat [-o] image path\n"
            "  make: build the drum file image from the prototype proto\n"
            "    -k: write the kernel's am1 -T dump, bank 0 only, into field 31\n"
            "  check: check the file system; exit 0 only when it is sound\n"
            "  ls: list the directory path, or the entry it names; dd when left out\n"
            "    -b: each entry's blocks and their drum addresses\n"
            "  cat: copy the file path to standard output as characters\n"
            "    -o: as octal words, one a line\n"
            "  The interleave is %d.\n", INTERLEAVE);
}

// Parse the command and its options, and run it.
int main(int argc, char **argv)
{
    static struct image image;
    const char *commandP;
    const char *kernelNameP;
    bool showBlocks;
    bool octal;
    int opt;
    int nArgs;
    char **argsP;

    if( argc < 2 )
    {
        usage();
        return (1);
    }
    commandP = argv[1];
    kernelNameP = NULL;
    showBlocks = false;
    octal = false;

    // getopt sees the command as the program's name.
    optind = 1;
    while( (opt = getopt(argc - 1, argv + 1, "k:bo")) != -1 )
    {
        if( (opt == 'k') && (strcmp(commandP, "make") == 0) )
        {
            kernelNameP = optarg;
        }
        else if( (opt == 'b') && (strcmp(commandP, "ls") == 0) )
        {
            showBlocks = true;
        }
        else if( (opt == 'o') && (strcmp(commandP, "cat") == 0) )
        {
            octal = true;
        }
        else
        {
            usage();
            return (1);
        }
    }
    argsP = (argv + 1 + optind);
    nArgs = ((argc - 1) - optind);

    if( (strcmp(commandP, "make") == 0) && (nArgs == 2) )
    {
        return (commandMake(&image, argsP[0], argsP[1], kernelNameP));
    }
    if( (strcmp(commandP, "check") == 0) && (nArgs == 1) )
    {
        return (readImage(argsP[0], &image) ? commandCheck(&image) : 1);
    }
    if( (strcmp(commandP, "ls") == 0) && ((nArgs == 1) || (nArgs == 2)) )
    {
        return (readImage(argsP[0], &image) ? commandLs(&image, ((nArgs == 2) ? argsP[1] : NULL), showBlocks) : 1);
    }
    if( (strcmp(commandP, "cat") == 0) && (nArgs == 2) )
    {
        return (readImage(argsP[0], &image) ? commandCat(&image, argsP[1], octal) : 1);
    }
    usage();
    return (1);
}
