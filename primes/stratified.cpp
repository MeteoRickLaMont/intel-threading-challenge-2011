#include <sys/time.h>
#include <sys/stat.h>
#include <math.h>                       // For sqrt
#include <assert.h>
#include "searches.h"
#include "primes.h"                     // For gSumTable
#include "powers.h"                     // For gPowerTable
#include "output.h"                     // For OutputFile

//
// Globals
//
int gNSearchThreads;
pthread_barrier_t gBarrierSearch;

//
// Given a thread number and a work chunk size (a portion of gNKPairs)
// return the first stride (0 based) that this thread should work on.
//
// This is for load balancing. Early strides have many k-pairs whereas
// the last stride has only one. Low numbered threads get a few long
// strides and high numbered threads get many short ones.
//
inline uint32_t firststride(const int i, const int n, const uint64_t chunk)
{
    const uint64_t c = (n - i) * chunk;
    return gNSums - ((static_cast<uint32_t>(sqrt(1 + (c << 3))) + 1) >> 1);
}

inline uint32_t firststride(const int tnum, const uint32_t iblock,
    const uint32_t jblock, const uint64_t chunk)
{
    const uint64_t c = (gNSearchThreads - tnum) * chunk;
    return gNSums - iblock - ((1 + static_cast<uint32_t>(
        sqrt(1 + 4 * (jblock * (jblock - 1) + 2 * c)))) >> 1) + 1;
}

//
// Another strategy no longer used.
//
void strategystratified(int tnum)
{
    OutputFile file;

    //
    // Split gNKPairs into n blocks of BLOCKSIZE.
    // BLOCKSIZE should be tuned to keep portions of gPowerTable in cache
    // without blocking on the barrier too frequently.
    //
    // const uint64_t BLOCKSIZE = (1ul << 38);  // Tunable
    const uint64_t BLOCKSIZE = (1ul << 28);     // Tunable (greater than SUMTABLE_MAX)
    uint32_t nblocks = (gNKPairs + BLOCKSIZE - 1) / BLOCKSIZE;
    uint32_t block, iblock, jblock;
    for (block = 0, iblock = 0; block < nblocks; ++block, iblock = jblock) {
        //
        // A block represents a band of strides [iblock, jblock).
        // Remember jblock from last iteration. It becomes iblock.
        //
        // The number of k-pairs in a block equals BLOCKSIZE except
        // for the final iteration when it's whatever is leftover.
        //
        jblock = firststride(block + 1, nblocks, BLOCKSIZE);
        if (jblock > gNSums - 1)
            jblock = gNSums - 1;
        if (tnum == 0)
            printf("Block %u extends from stride %u upto %u (gNSums = %u)\n", block, iblock + 1, jblock + 1, gNSums);
        uint64_t nkpairs;
        if (block < nblocks - 1)
            nkpairs = BLOCKSIZE;
        else
            nkpairs = gNKPairs - block * BLOCKSIZE;

        //
        // Split nkpairs into gNSearchThreads equal portions.
        // The last portion may be slightly smaller due to rounding.
        //
        uint64_t chunk = (nkpairs + gNSearchThreads - 1) / gNSearchThreads;
        printf("stride = firststride(%d, %u, %u, %lu)\n", tnum, iblock, jblock, chunk);
        printf("nextstridestride = firststride(%d, %u, %u, %lu)\n", tnum + 1, iblock, jblock, chunk);
        uint32_t stride = firststride(tnum, iblock, jblock, chunk);
        uint32_t nextstride = firststride(tnum + 1, iblock, jblock, chunk);
        if (nextstride > jblock)
            nextstride = jblock;
        printf("Thread %d takes strides %u upto %u\n", tnum, stride, nextstride);

        //
        // Outer loop iterates over strides.
        //
        const PerfectPower *pbase = gPowerTable;
        register const PerfectPower *p;
        register const tSum *left, *right;
        register tSum sum;
        for (; stride < nextstride; ++stride) {
            //
            // Set up sum and power variables for a new stride
            //
            left = gSumTable;
            right = left + stride + 1;
            sum = *right;
            p = pbase;

            //
            // Search for power at beginning of new stride starting from pbase
            // (the final estimate of the last stride).
            //
            // Most of the time the next power is within BINARY_THRESHOLD steps
            // of pbase. Don't bother setting up a binary search in this case.
            // Proceed to linear search below.
            //
            const uint32_t BINARY_THRESHOLD = 180;      // Tunable parameters
            const uint32_t BINARY_MINSTEP = 22;
            if (p + BINARY_THRESHOLD <= gPowerEnd &&
                p[BINARY_THRESHOLD].n < sum) {
                //
                // Establish upper bound on next p. Use several iterations of
                // binary search between pbase and this upper bound, leaving
                // p->n <= sum. Then fall into linear search to finish the job.
                //
                uint64_t step, halfstep;
                const PerfectPower *pmid;
                step = sum - p->n;
                step /= floor_log2(step);  // delta / log(delta) is upper bound
                if (p + step > gPowerEnd)
                    step = gPowerEnd - p;
                assert(p[step].n >= sum);
                while (step > BINARY_MINSTEP) {
                    pmid = p + (halfstep = (step >> 1));
                    if (sum < pmid->n)
                        step = halfstep - 1;
                    else if (sum > pmid->n) {
                        p = pmid + 1;
                        step -= halfstep + 1;
                    }
                    else
                        break;
                }
            }
            while (p->n < sum)
                ++p;
            pbase = p;                      // Bookmark for next iteration

            //
            // Original inner loop (assumes p[-1].n < sum <= p->n):
            // for (;;) {
            //     if (p->n == sum) file.printresult(left, right, p);
            //     if (++right > gSumEnd) break;
            //     sum = *right - *++left;
            //     while (p->n < sum) ++p;
            // }
            //
            // Rearrange as a do-while:
            // do {
            //     while (p->n < sum) ++p;
            //     if (p->n == sum) file.printresult(left, right, p);
            //     sum = *++right - *++left;
            // } while (right <= gSumEnd);
            //
            // Now unroll using Duff's Device:
            // http://en.wikipedia.org/wiki/Duff's_device
            //
            uint32_t count = gSumEnd - right + 1;
            uint32_t n = (count + 7) >> 3;
            switch (count & 7) {
            case 0: do { while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 7:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 6:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 5:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 4:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 3:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 2:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
            case 1:      while (p->n < sum) ++p;
                         if (p->n == sum) file.printresult(left, right, p);
                         sum = *++right - *++left;
                    } while (--n);
            }
        }

        //
        // Synchronize search threads to barrier before starting another
        // block. This prevents one thread getting too far ahead and spoiling
        // the cache.
        //
        if (block < nblocks - 1)
            pthread_barrier_wait(&gBarrierSearch);
    }
}
