#ifndef PRIMES_H
#define PRIMES_H

#include <stdint.h>             // For uint32_t
#include "common.h"

//
// Constants
//
const uint32_t PRIME_MAX = 4294967291u;         // Highest 32-bit prime

//
// Globals
//
extern int gNPrimeThreads;

//
// gSumTable[n] contains the sum of n-1 primes from a chosen origin:
//     sum[i=1,n](prime[origin + i])
//
// The nth prime can be deduced as gSumTable[n] - gSumTable[n-1]
//
// More valuably, the sum over a k-pair can be found with one subtraction:
// sum(i,j) = gSumTable[j+1] - gSumTable[i] (for 0 <= i <= j)
//
const uint64_t SUM_MAX = 425649736193687430UL;  // Sum of all 32-bit primes
const uint32_t SUMTABLE_MAX = 203280222;        // Worst case including 0 and 2
extern tSum *gSumTable;
extern const tSum *gSumEnd;
extern uint32_t gNSums;

//
// Exported functions
//
extern uint32_t buildPrimeTable(tSum *const table,
    uint32_t start, const uint32_t end, uint64_t &total);
extern void offsetPrimeTable(register uint32_t n, tSum const *const intable,
    tSum *const outtable, register const uint64_t offset);

#endif
