/*
 * translate.h - translate.c's entry, the translation of an assembled as7 program to am1, and
 * its options.
 */

#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "as7am1.h"

// The naive rules, for testing the translator only.
// Each replaces one rule of the translation with the obvious one that the PDP-1 gets wrong,
// so that a test can show the rule is needed.
#define NAIVE_ISZ   01      // every isz an isp
#define NAIVE_CMA   02      // cma never followed by sub [1]
#define NAIVE_JDA   04      // a routine's return kept as jda leaves it, 16 bits, used by jmp i
#define NAIVE_SKIP  010     // a skip over a word as if that word were one PDP-1 word
#define NAIVE_AIDX  020     // auto-index registers used without the increment
#define NAIVE_LINK  040     // rotates of AC alone, the link left out
#define NAIVE_FILL  0100    // the PDP-7's long shifts as the PDP-1's arithmetic shifts

struct tOptions
{
    const char *outNameP;
    const char *reportNameP;           // NULL for standard error
    const char *titleP;                // NULL for the default
    int bank;                          // -1 for no bank directive
    int fragment;                      // no title and no start, for an #include
    unsigned naive;
};

int translate(struct as7 *aP, const struct tOptions *oP);

#endif
