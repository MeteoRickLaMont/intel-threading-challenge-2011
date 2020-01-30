//
// Intel Threading Challenge 2011 submission
// P1: A2 - Consecutive Primes
// Rick LaMont <lamont@dotcsw.com>
// Compile with: make
//

//
// A pair of consecutive primes would be two prime numbers that have no other
// prime between them. The idea of consecutive primes can be extended to any
// number "k", in which there are only k primes between and including the
// smallest and largest primes of the ordered set. We would refer to this
// set as a "k-pair". Two examples of 3-consecutive prime sets would be
// [29, 31, 37] and [11083, 11087, 11093].
//
// Problem Description:
// Write a threaded code to find the sum of all possible "k-pair" consecutive
// prime number sets and determine if their sum is a perfect power. For
// example, given p1, p2, and p3 are consecutive primes, the sum of these
// primes is N (= p1 + p2 + p3). A number N is a perfect power if N = n^m,
// with n, m >= 2 (i.e., N is a perfect square, perfect cube, etc.). Input
// to the application will be from the command line, which will include
// the range of numbers from which primes will be recruited and the
// highest exponent (m) the application will need to consider. Output of
// the application will be the sets of consecutive primes that are added
// together resulting in a perfect power value.
//

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <memory.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "primes.h"
#include "powers.h"
#include "searches.h"
#include "output.h"

//
// Constants
//
const int MAXTHREADS = 100;              // More than we'll need
const char screate[] = "pthread_create"; // Part of an error message

//
// Types
//
struct ThreadIO {
    uint32_t a32;                        // General purpose registers...
    uint64_t b64;                        // ...for inter-thread communication
};

//
// Foward declarations.
//
static void *primethread(void *d);
static void *powerthread(void *d);

//
// File globals.
//
static uint32_t gStart, gEnd;                   // Command-line inputs
static ThreadIO gThreads[MAXTHREADS];           // Inter-thread communication
static pthread_barrier_t gBarrierPrime;

int main(const int argc, const char *const *const argv)
{
    timeval then, now;
    gettimeofday(&then, 0);

    //
    // Input Description:
    // The input to the program will be from the command line as three
    // integers. The first and second integers will be the start and the end,
    // inclusive, of the range from which prime numbers must be used. The
    // third command line parameter is the maximum power m, which the
    // application must use to compute perfect powers. The input values will
    // be represented with 32-bit unsigned integers. The fourth parameter on
    // the command-line would be the name of the text file to hold the output.
    //
    // Example command line: ./primesums 1 29 6 sumsout.txt
    //
    if (argc != 5) {
        fprintf(stderr, "Usage: %s start end maxpower outfile\n", argv[0]);
        return -1;
    }
    gStart = strtoul(argv[1], 0, 10);
    gEnd = strtoul(argv[2], 0, 10);
    gMaxPower = strtoul(argv[3], 0, 10);
    OutputFile::fp = fopen(argv[4], "w");
    if (!OutputFile::fp) {
        perror(argv[4]);
        return -1;
    }
    if (gStart > gEnd) {
        fprintf(stderr, "Range start may not be greater than end\n");
        return -1;
    }

    //
    // The highest value of gStart that can produce output is 4294951477:
    // sum(4294951477:4294962533) = 2108823848041 = 1452179**2
    // 
    if (gMaxPower < 2 || gEnd < 5 || gStart > 4294951477U)
        goto nosolution;

    //
    // Trim input
    //
    if (gStart < 2)
        gStart = 2;
    if (gEnd > PRIME_MAX)
        gEnd = PRIME_MAX;

    //
    // Decide how many threads to create and how many will be prime threads
    // or power threads.
    //
    int nthreads;
    nthreads = sysconf(_SC_NPROCESSORS_CONF);
    if (nthreads < 2) nthreads = 2;     // minimum = 1 prime + 1 power thread
    gNPrimeThreads = nthreads - 1;      // Always 1 power thread
    gNSearchThreads = nthreads;         // All threads will join in search

    //
    // Start prime threads and power threads.
    //
    int t;
#ifndef NDEBUG
    gLastPower = POWER_MAX;                     // Prevent false negative assert
#endif
    pthread_t pthreads[MAXTHREADS];             // Thread ID's
    pthread_barrier_init(&gBarrierPrime, 0, gNPrimeThreads);
    pthread_barrier_init(&gBarrierSearch, 0, gNSearchThreads);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    for (t = 0; t < gNPrimeThreads; ++t)
        if (pthread_create(pthreads + t, &attr,
            primethread, static_cast<void *>(gThreads + t))) {
            perror(screate);
            return -1;
        }
    for (; t < nthreads; ++t)
        if (pthread_create(pthreads + t, &attr,
            powerthread, static_cast<void *>(gThreads + t))) {
            perror(screate);
            return -1;
        }

    //
    // Wait for all threads to exit.
    // If there were no results, print a statement to that fact.
    //
    pthread_attr_destroy(&attr);
    for (t = 0; t < nthreads; ++t)
        pthread_join(pthreads[t], NULL);
nosolution:
    if (OutputFile::noresults)
        fputs("No consecutive prime sums equal a perfect power.\n",
            OutputFile::fp);

    //
    // Exiting the program will take care of all this:
    //
    // delete [] gSumTable;
    // delete [] gPowerTable;
    // pthread_barrier_destroy(&gBarrierPrime);
    // pthread_barrier_destroy(&gBarrierSearch);
    // fclose(OutputFile::fp);

    //
    // Timing:
    // The total execution time of the application will be used for
    // scoring. For most accurate timing results, submission codes would
    // include timing code to measure and print total execution time
    // to stdout
    //
    gettimeofday(&now, 0);
    double secs = (now.tv_sec - then.tv_sec) +
                  (now.tv_usec - then.tv_usec) / 1000000.0;
    printf("Elapsed time: %g seconds\n", secs);
    return 0;
}

//
// Main entry point for prime threads.
// STEP 1: Create local table of prefix sums of primes
// STEP 2: Allocate global prefix sum table
// STEP 3: Coalesce local prefix sum table into global table
// STEP 4: Invoke search strategy
//
// There are barriers before steps 2, 3 and 4.
//
static void *primethread(void *d)
{
    ThreadIO *io = (ThreadIO *)d;
    int tnum = io - gThreads;

    //
    // Split range [gStart, gEnd] into gNPrimeThreads equal portions
    //
    uint32_t range = gEnd - gStart + 1;
    uint32_t start = gStart +
        static_cast<uint64_t>(tnum) * range / gNPrimeThreads;
    uint32_t end = gStart +
        static_cast<uint64_t>(tnum + 1) * range / gNPrimeThreads - 1;

    //
    // STEP 1: Create a partial prefix sum table of all primes in range.
    // Input: none
    // Output: a32 = number of primes in this partial table
    //         b64 = the sum of all primes in our table
    //
    uint32_t nsums;
    tSum *localtable;
    if (start > end) {                  // More threads than work
        io->a32 = 0;                    // Next thread will get this one
        io->b64 = 0;
    }
    else {
        //
        // Quick bounds on the number of sums:
        // (x/log x)(1 + 0.992/log x) < pi(x) <(x/log x)(1 + 1.2762/log x)
        // Pierre Dusart 1999
        // Lower bound holds for x > 598 (subtract 7 for lower numbers)
        // Upper bound holds for x > 1
        //
        double logstart, logend;
        uint32_t istart, iend;
        logstart = log(start);
        logend = log(end);
        istart = static_cast<uint32_t>(start / logstart *
            (1 + 0.992 / logstart)) - 7;
        iend = static_cast<uint32_t>(end / logend * (1 + 1.2762 / logend));
        if (istart < 0)
            istart = 0;
        if (iend > SUMTABLE_MAX)
            iend = SUMTABLE_MAX;

        //
        // Allocate sum table for this thread and fill it
        //
        nsums = iend - istart + 1;
        localtable = new tSum[nsums];
        uint32_t sum0 = 0;
        if (start == gStart) {
            localtable[0] = 0;
            sum0 = 1;
        }
        nsums = buildPrimeTable(localtable + sum0, start, end, io->b64) + sum0;
        io->a32 = nsums;
    }

    //
    // STEP 2 (single-threaded): Allocate global prefix sum table.
    //                           Prepare prime threads to coalesce.
    // Input:  a32 = number of primes in each partial table
    //         b64 = the sum of all primes in each table
    // Output: a32 = offset into gSumTable for each thread to fill
    //         b64 = sum of all primes lower than the thread's first
    //
    pthread_barrier_wait(&gBarrierPrime);
    if (tnum == 0) {                    // Only first thread works
        gNSums = 0;
        tSum offset = 0;
        for (int t = 0; t < gNPrimeThreads; ++t) {
            uint32_t nprimes = gThreads[t].a32;
            tSum sigma = gThreads[t].b64;
            gThreads[t].a32 = gNSums;
            gThreads[t].b64 = offset;
            gNSums += nprimes;
            offset += sigma;
        }
        gSumTable = new tSum[gNSums];
        --gNSums;                       // Don't count the 0
        assert(offset <= gLastPower);
        gLastPower = offset;            // Help power thread to finish sooner

        //
        // gSumEnd will point *one beyond* highest prime to consider.
        // It's okay to dereference gSumEnd.
        // The value of the last prime is gSumEnd[0] - gSumEnd[-1] 
        //
        gSumEnd = gSumTable + gNSums;
    }

    //
    // STEP 3: Coalesce local table into global prefix sum table.
    //         Add the sum of all lower primes while doing the copy.
    // Input:  a32 = offset into gSumTable for this thread to fill
    //         b64 = sum of all primes lower than ours
    // Output: none
    //
    // There are parallel algorithms for doing prefix sums but they require
    // a single table as input. Since we're coalescing gNPrimeThreads tables
    // into one, it's faster to do the adds during the copy.
    //
    pthread_barrier_wait(&gBarrierPrime);
    if (start <= end) {
        offsetPrimeTable(nsums, localtable, gSumTable + io->a32, io->b64);
        delete [] localtable;
    }

    //
    // STEP 4: Kick off search strategy.
    //
    pthread_barrier_wait(&gBarrierSearch);
    if (gNSums > 1)
        strategystride(tnum);

    pthread_exit(NULL);
}

//
// Main entry point for power thread.
// STEP 1: Estimate range of perfect powers to generate.
// STEP 2: Create global power tables.
// STEP 3: Trim power table and kick off search
//
static void *powerthread(void *d)
{
    ThreadIO *io = (ThreadIO *)d;
    int tnum = io - gThreads;

    //
    // STEP 1: Estimate size of power table.
    //
    // Quick bounds on the number of sums:
    // (x/log x)(1 + 0.992/log x) < pi(x) <(x/log x)(1 + 1.2762/log x)
    // Pierre Dusart 1999
    // Lower bound holds for x > 598 (subtract 7 for lower numbers)
    // Upper bound holds for x > 1
    //
    double logstart, logend;
    uint32_t istart, iend;
    logstart = log(gStart);
    logend = log(gEnd);
    istart = static_cast<uint32_t>(gStart / logstart *
        (1 + 0.992 / logstart)) - 7;
    iend = static_cast<uint32_t>(gEnd / logend * (1 + 1.2762 / logend));
    if (istart < 0)
        istart = 0;
    if (iend > SUMTABLE_MAX)
        iend = SUMTABLE_MAX;

    //
    // Estimate the sum of primes [gStart, gEnd]
    // The sum of the first n primes is approximately n^2 * ln(n) / 2
    // Experimentally found that:
    // 0.52 * n^2 * ln(n) < sigma < 0.53845 * n^2 * ln(n) + 7
    //
    gFirstPower = static_cast<tPower>(gStart + 1) << 1; // Tight lower bound
    double upper = 0.53845 * iend * iend * log(iend) + 7;
    double lower = 0.52 * istart * istart * log(istart+1);
    if (upper > lower) {
        gLastPower = static_cast<tPower>(upper - lower);
        if (gLastPower < gFirstPower)
            gLastPower = gFirstPower;
        else if (gLastPower > POWER_MAX)
            gLastPower = POWER_MAX;
    }
    else
        gLastPower = gFirstPower;

    //
    // STEP 2: Create table of perfect powers from gFirstPower to gLastPower.
    //
    uint32_t npowers = buildPowerTable(gPowerTable);
    gPowerEnd = gPowerTable + npowers - 1;      // Less one for sentinel

    //
    // STEP 3: Trim power table
    //
    // Point gPowerEnd to last perfect power to consider in powertable. It
    // is safe to dereference gPowerEnd. Prior to trimming, gPowerEnd[0]
    // contains a sentinel value that some search strategies rely upon.
    //
    // This requires both the power table and the sum table to be ready.
    // Use gBarrierSearch here and in primethread. The search will have
    // already started but trimming is an optional step that optimizes
    // searches near the end. Technically, it may not be safe to update
    // a pointer here (non-atomic write) and read it from the search.
    // Live on the edge!
    //
    pthread_barrier_wait(&gBarrierSearch);
    gPowerEnd = findpower(*gSumEnd, gPowerTable, npowers, 0) + 1;

    //
    // Kick off search strategy.
    //
    if (gNSums > 1)
        strategystride(tnum);

    pthread_exit(NULL);
}
