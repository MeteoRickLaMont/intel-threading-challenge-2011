#include <stdio.h>
#include <math.h>
#include <memory.h>
#include <assert.h>
#include "primes.h"             // For floor_log2
#include "powers.h"

//
// Globals
//
uint32_t gMaxPower;             // Command-line inputs
PerfectPower *gPowerTable;
tPower gFirstPower;
volatile tPower gLastPower;
const volatile PerfectPower *gPowerEnd;

PerfectPower::PerfectPower(uint32_t x, uint32_t k) : base(x), exp(k)
{
    n = ipow(base, exp);
}

void PerfectPower::incrBase()
{
    tPower oldn = n;
    n = ipow(++base, exp);
    if (n < oldn)
        n = (tPower)-1;         // Handle 64-bit overflow
}

//
// Inline class for generating perfect powers in sorted order.
//
class PowerGenerator {
public:
    PowerGenerator(tPower first) {
        //
        // Only consider perfect powers with prime exponents up to and
        // including gMaxPower. The others just produce duplicates.
        //
        // The higher primes (43, 47, 53 and 59) may be eliminated from
        // consideration because no number raised to that power can be
        // expressed as the sum of consecutive 32-bit primes.
        //
        // These exponents may all be expressed as k-pair sums:
        // sum(5:13) = 36 = 6**2
        // sum(3:5) = 8 = 2**3
        // sum(257:313) = 3125 = 5**5
        // sum(61:67) = 128 = 2**7
        // sum(858993367:858993607) = 8589934592 = 8**11
        // sum(4093:4099) = 8192 = 2**13
        // sum(3109:3449) = 131072 = 2**17
        // sum(4592009:4595893) = 1162261467 = 3**19
        // sum(182797249:182807263) = 94143178827 = 3**23
        // sum(3354269:3356741) = 536870912 = 2**29
        // sum(991812461:1004634857) = 617673396283947 = 3**31
        // sum(654469037:654473381) = 137438953472 = 2**37
        // sum(2189273:8555623) = 2199023255552 = 2**41
        //
        static uint32_t viable[] = {2, 3, 5, 7, 11, 13, 17,
                                    19, 23, 29, 31, 37, 41};
        static const int nviable = sizeof(viable) / sizeof(viable[0]);

        uint32_t *e;
        uint32_t b;
        fHeap = new PerfectPower[nviable];          // Worst case size
        fSize = 0;
        double lnfirst = log(first);
        for (e = viable; e < viable + nviable && *e <= gMaxPower; ++e) {
            //
            // Let b = ceiling(nth root of first)
            //
            b = static_cast<uint32_t>(exp(lnfirst / *e));
            if (ipow(b, *e) < first) ++b;   // safe way to do ceiling
            assert(ipow(b, *e) >= first);
            assert(ipow(b-1, *e) < first);  // don't miss first one
            fHeap[fSize++] = PerfectPower(b, *e);
        }

        //
        // Heapify in O(fSize) time
        //
        for (int i = fSize / 2 - 1; i >= 0; --i)
            pushdown(i);
    }

    ~PowerGenerator()
    {
        delete [] fHeap;
    }

    const PerfectPower *top() { return fHeap; }
    void pop() {
        tPower last = fHeap->n;
        do {
            fHeap->incrBase();
            pushdown(0);
        } while (fHeap->n == last);
    }

private:
    int fSize;                  // Number of items in heap
    PerfectPower *fHeap;

    void swap(PerfectPower &a, PerfectPower &b)
    {
        PerfectPower t = b;
        b = a;
        a = t;
    }

    void pushdown(int i)
    {
        int halfn = fSize >> 1;
        while (i < halfn) {
            int right = i + 1 << 1;
            int left = right - 1;
            if (right < fSize &&
                fHeap[right] < fHeap[i] &&
                fHeap[right] < fHeap[left]) {
                swap(fHeap[i], fHeap[right]);
                i = right;
            }
            else if (fHeap[left] < fHeap[i]) {
                swap(fHeap[i], fHeap[left]);
                i = left;
            }
            else
                break;
        }
    }
};

//
// Generate the perfect powers, in order and without duplicates, from
// gFirstPower to gLastPower.
//
// Note that fewer than half of these numbers are even expressible as the 
// sum of consecutive primes. It takes too long to sort these out, so
// just generate them all.
//
uint32_t buildPowerTable(PerfectPower *&powertable)
{
    //
    // Estimate the number of perfect powers [gFirstPower, gLastPower]
    // The number of perfect squares in this range is given by:
    //     nsquares = sqrt(gLastPower) - sqrt(gFirstPower) + 1;
    // Use that as an upper bound on the number of perfect cubes also.
    //
    // The number of perfect quads is:
    //     nquads = nsquares / 32 + 1
    // Use that as an upper bound on the number of perfect powers of
    // order 5 and 7.
    //
    // There aren't many perfect powers of higher order. Only:
    //     nhigher = 86
    // covers exponents 11 through 41 over the range [5,POWER_MAX].
    //
    uint32_t nsquares =
        static_cast<uint32_t>(sqrt(gLastPower) - sqrt(gFirstPower)) + 1;
    uint32_t nquads = (nsquares >> 3) + 1;
    uint32_t npowers = (nsquares + nquads << 1) + 86;
    if (npowers > POWERTABLE_MAX)
        npowers = POWERTABLE_MAX;
    powertable = new PerfectPower[npowers];

    register PerfectPower *p; 
    PerfectPower *powerend = powertable + npowers;
    PowerGenerator pgen(gFirstPower);
    const PerfectPower *top = pgen.top();
    for (p = powertable; p < powerend && top->n <= gLastPower; ++p) {
        *p = *top;
        pgen.pop();
    }
    assert(top->n >= gLastPower);
    p->n = ~0ul;        // Sentinel value for strategystride
    ++p;
    return p - powertable;
}

//
// Returns pointer to number p in array a of size n.
// If p is not in array, return the element before (largest a[i] < p).
// To instead return the element after (smallest a[i] > p) set bias to 1.
//
// Watch out for case where p < a[0]. It will return a - 1 + bias, so this
// pointer will be out of range unless bias = 1.
//
const PerfectPower *findpower(const tPower p, const PerfectPower *a,
    const uint32_t n, const int32_t bias)
{
    const int log2n = floor_log2(n);
    register const PerfectPower *alast = a + n;
    register uint32_t b;
    for (b = 1U << log2n; b; b >>= 1)
        if (a + b < alast && a[b].n <= p) {
            a += b;
            if (a->n == p)
                return a;
        }
    if (a->n == p)
        return a;
    return a + (a->n < p ? bias : bias - 1);
}
