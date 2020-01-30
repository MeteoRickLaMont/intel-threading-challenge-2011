#include <sys/time.h>
#include <sys/stat.h>
#include <math.h>                       // For sqrt
#include <assert.h>
#include "searches.h"
#include "primes.h"                     // For gSumTable
#include "powers.h"                     // For gPowerTable
#include "output.h"                     // For OutputFile

//
// Tunable parameters to optimize search strategy
//
const uint64_t BARRIER_THRESHOLD = 1ul << 27; // How many k-pairs before sync
const uint32_t BINARY_THRESHOLD = 180;  // Less than this? Linear search
const uint32_t BINARY_MINSTEP = 22;     // Abandon binary search early

//
// Globals
//
int gNSearchThreads;
pthread_barrier_t gBarrierSearch;

//
// The stride strategy: Given a table of n prime prefix sums s[0..n-1] and
// a sorted table of m perfect powers p[0..m-1], iterate over all k-pairs.
// For each k-pair, search for the sum in p. Output all matches.
//
// Define "stride" as the set of all k-pairs with the same value of k.
// Stride n contains all k-pairs where k = n + 2. For example, stride 0
// has all the 2-pairs (the sum of two consecutive primes). There are
// gNSums-1 strides in all, numbered 0 through gNSums-2.
//
// Each thread considers an entire stride at time. At each iteration of the
// inner loop we're effectively adding one prime at the leading edge and 
// subtracting one prime at the trailing edge. Thus the sum grows gradually,
// at a pace similar to that of the power table. The linear search of the
// power table in the inner loop usually iterates 0 or 1 times.
//
// The running time for this algorithm is O(n^2 + m).
// It is appropriate for all inputs.
//
void strategystride(int tnum)
{
    OutputFile file;

    //
    // Divide up the work into blocks of nthreads strides each.
    // There's a barrier inside the outer loop, so make sure every
    // thread iterates nblocks times (even if it has no more work).
    //
    uint32_t nblocks = (gNSums + gNSearchThreads - 2) / gNSearchThreads;

    //
    // Keep a running total of the number of k-pairs processed so far by
    // this thread. Early strides have nearly gNSums k-pairs whereas strides
    // at the end only have a few.
    //
    // This value must be consistent across all threads so they reach the
    // barrier on the same iteration. It must not depend on tnum! Use the
    // average stride size for the block. Add it to the running total
    // at the bottom of the outer loop.
    //
    uint64_t sumkpairs = 0;             // Running total of k-pairs processed
    uint32_t nkpairs = gNSums - (gNSearchThreads >> 1); // Average for block 0

    //
    // Outer loop iterates over blocks. Each thread is assigned one stride
    // in the block.
    //
    // Stride i+1 is one k-pair smaller than stride i. If each thread took
    // every nth stride, the first thread would get the most work. Instead,
    // alternate the order on odd-numbered blocks. This ensures that each
    // thread gets an equal number of k-pairs.
    //
    // For example, 8 search threads would be assigned strides as follows.
    //
    //    tnum = 0  1  2  3  4  5  6  7
    //         ------------------------
    // Block 0|  0  1  2  3  4  5  6  7
    // Block 1| 15 14 13 12 11 10  9  8
    // Block 2| 16 17 18 19 20 21 22 23
    // Block 3| 31 30 29 28 27 26 25 24
    //
    uint32_t stride;
    const PerfectPower *pbase = gPowerTable;
    register const PerfectPower *p;
    register const tSum *left, *right;
    register tSum sum;
    const PerfectPower *pmid, *upper;
    for (stride = tnum;
         nblocks--;
         stride += (gNSearchThreads - stride % gNSearchThreads << 1) - 1)
    {
        //
        // Every so often, resynchronize the threads to keep their data
        // access pattern coherent. Barriers are expensive, so don't
        // do this too often. It's useful for large tests to prevent
        // individual threads from running ahead or falling behind, spoiling
        // the cache. This could happen because of an inbalance in the
        // number of outputs, for example.
        // 
        // sumkpairs provides the mechanism to hit the barrier consistently
        // at fixed time intervals (independent of gNSearchThreads). Use
        // BARRIER_THRESHOLD to tune the frequency.
        //
        if (sumkpairs > BARRIER_THRESHOLD) {    // Tunable threshold
            sumkpairs = 0;                      // Reset total
            pthread_barrier_wait(&gBarrierSearch);
        }
        if (stride >= gNSums - 1)               // No more work, last block
            break;                              // Past barrier, safe to break

        //
        // Set up sum and power variables for a new stride.
        //
        // Note the inter-thread coherency of the three main pointers.
        // For thread number t:
        //     - left[t] points to gSumTable (all threads have same value)
        //     - right[t] points to gSumTable + t (consecutive values)
        //     - p[t] will point to nearly consecutive values in power table
        //
        // These three pointers move upward together in all threads.
        //
        left = gSumTable;                       // *left == 0 initially
        right = left + stride + 2;              // Minimum k-pair: k = 2
        sum = *right;                           // sum == *right - *left
        p = pbase;                              // Improve p below

        //
        // Search for power at beginning of new stride starting from pbase
        // (the beginning of this thread's previous stride). Find p that
        // satisfies: p[-1].n < sum <= p->n
        //
        // Half the time the next power is within BINARY_THRESHOLD steps
        // of pbase. Don't bother setting up a binary search in this case.
        // Proceed directly to linear search below.
        //
        pmid = p + BINARY_THRESHOLD;
        if (pmid < gPowerEnd && pmid->n < sum) {
            //
            // Binary search:
            //
            // Establish lower and upper bounds on next p using linear
            // interpolation from p and pmid. Run several iterations of
            // binary search between p and upper, then fall into linear
            // search to finish the job.
            //
            upper = pmid + 2 * BINARY_THRESHOLD *
                (sum - pmid->n) / (pmid->n - p->n);
            if (upper >= gPowerEnd)
                upper = const_cast<const PerfectPower *>(gPowerEnd) - 1;
            assert(upper <= pmid || upper[1].n >= sum);
            p = pmid + 1;
            while (upper - p > BINARY_MINSTEP) {
                pmid = p + (upper - p >> 1);
                if (sum < pmid->n)
                    upper = pmid - 1;
                else if (sum > pmid->n)
                    p = pmid + 1;
                else
                    break;
            }
        }

        //
        // Linear search:
        //
        // sum is nearby. Find first p->n >= sum
        // Let pbase = p for next time.
        //
        while (p->n < sum)
            ++p;
        pbase = p;                      // Bookmark for next iteration

        //
        // Original inner loop:
        // for (;;) {
        //     if (p->n == sum)
        //         file.printresult(left, right, p);
        //     if (++right > gSumEnd)
        //         break;
        //     sum = *right - *++left;
        //     while (p->n < sum)
        //         ++p;
        // }
        //
        // Rearrange as a do-while (linear search is only redundant on
        // first iteration):
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
        switch (count & 7)
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

        //
        // Advance running total of k-pairs.
        // Just finished processing (approximately) nkpairs.
        // Next iteration will have gNSearchThreads fewer k-pairs.
        //
        sumkpairs += nkpairs;           // Running total
        nkpairs -= gNSearchThreads;
    }
}

//
// The power strategy turns the stride strategy inside out. It iterates
// over perfect powers. For each one, it finds all k-pairs that sum to
// the power by using a guided search through the sum space.
//
// This one is O(n * m). It's only a viable strategy when m is small compared
// to n^2, which can happen for low values of gStart.
//
// NOTE: Not used anymore. I keep it around for testing the correctness of
// strategystride()
//
void strategypower(int tnum)
{
    OutputFile file;

    const tSum *baseRight = gSumTable + 2;  // Let this drift upward with n
    const tSum *sum8 = gSumEnd - 8;         // Unrolled loop target
    const PerfectPower *p;
    register const tSum *left, *right;
    register tSum sum;
    register tPower n;
    for (p = gPowerTable + tnum; p < gPowerEnd; p += gNSearchThreads) {
        n = p->n;
        left = gSumTable;
        while ((sum = *baseRight) < n)
            ++baseRight;
        right = baseRight;

        //
        // Unrolled inner loop
        //
        while (left < right - 8 - 1 && right <= sum8) {
            if (sum < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            if ((sum = *right - *++left) < n) ++right;
            else if (sum == n) file.printresult(left, right, p);
            sum = *right - *++left;
        }

        //
        // Finish up with rolled version of inner loop.
        // Note the two distinct exit points.
        //
        for (;;) {
            if (sum >= n) {
                if (sum == n)
                    file.printresult(left, right, p);
                if (++left >= right - 1)
                    break;
            }
            else if (++left, ++right > gSumEnd)
                break;
            sum = *right - *left;
        }
    }
}
