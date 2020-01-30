#include "primes.h"

//
// Globals
//
int gNPrimeThreads;
tSum *gSumTable;
const tSum *gSumEnd;
uint32_t gNSums;

//
// Optimized for odd, positive values of a and b.
//
static inline bool gcdisone(register uint32_t a, register uint32_t b)
{
    while (a != b)
        if (a > b) {
            a -= b;
            a >>= 1;
            a >>= trailing_zeros(a);
        }
        else {
            b -= a;
            b >>= 1;
            b >>= trailing_zeros(b);
        }

    return a == 1;
}

//
// Rewrite p-1 as (d * 2^s) with d odd.
//
inline void decompose(const uint32_t p, uint32_t &d, int &s)
{
    d = p - 1;
    s = trailing_zeros(d);
    d >>= s;
}

static bool MillerRabinBase(const uint32_t a, const uint32_t n,
    const uint32_t d, int s)
{
    const uint32_t n1 = n - 1;
    register uint32_t apow = ipowmod(a, d, n);
    if (apow == 1 || apow == n1) return true;
    while (--s) {
        apow = static_cast<uint32_t>((static_cast<uint64_t>(apow) * apow) % n);
        if (apow == n1) return true;
    }
    return false;
}

//
// Check every number for primality, from start up to and including end.
// Returns number of primes found. Sets total to their sum.
//
uint32_t buildPrimeTable(tSum *const table,
    uint32_t start, const uint32_t end, uint64_t &total)
{
    register tSum *a = table;
    register uint32_t n;
    register uint64_t sum = 0;
    uint32_t d;
    int s;

    //
    // Handle 2 as a special case
    //
    if (start <= 2)
        *a++ = (sum += 2);
    start |= 1;                         // Make odd

    //
    // Handle small numbers here to tighten up general loop below.
    //
    for (n = start; n < 62 && n <= end; n += 2) {
        decompose(n, d, s);
        if (MillerRabinBase(2, n, d, s))
            *a++ = (sum += n);
    }

    //
    // The bases (2, 7, 61) are known to be correct up to 4,759,123,141
    //
    // References:
    // Pomerance, Selfridge, & Wagstaff "The pseudoprimes to 25e+9"
    // Jaeschke Gerhard. "On strong pseudoprimes to several bases"
    //
    for (; n <= end; n += 2)
        if (gcdisone(111546435U, n)) {  // Eliminate composites of small primes
            decompose(n, d, s);         // Calculate d and s for below
            if (MillerRabinBase( 2, n, d, s) &&
                MillerRabinBase( 7, n, d, s) &&
                MillerRabinBase(61, n, d, s))
                *a++ = (sum += n);      // Add prefix sum to table
        }

    total = sum;
    return a - table;
}

//
// Copy n sums from intable to outtable, adding offset to each.
//
void offsetPrimeTable(register uint32_t n, tSum const *const intable,
    tSum *const outtable, register const uint64_t offset)
{
    register const tSum *s = intable;
    register tSum *d = outtable;

    while (n--)
        *d++= *s++ + offset;
}
