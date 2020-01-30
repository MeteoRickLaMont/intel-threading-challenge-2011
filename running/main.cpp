//
// Intel Threading Challenge 2011 submission
// P1: A3 - Running Numbers
// Rick LaMont <lamont@dotcsw.com>
// Compile with:
//     source /opt/intel/composerxe/bin/compilervars.sh intel64
//     make
//

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <iostream>
#include <sstream>
#include <algorithm>

using std::string;
using std::stringstream;
using std::ios_base;
using std::min;

//
// Use SSE4.1 instruction set if available. It has the PTEST instruction
// which simplifies comparisons with zero or an arbitrary value.
//
#if defined(__ICC) && !defined(__SSE4_1__)
#define __SSE4_1__
#endif
#ifdef __SSE4_1__
    #include <smmintrin.h>
    static __m128i gAllOnes;                    // all bits one
    #define iszero(a) _mm_testz_si128(a, a)
    #define isequal(a, b) _mm_testc_si128(_mm_cmpeq_epi32(a, b), gAllOnes)
#else
    #define iszero(a) \
        (_mm_movemask_epi8(_mm_cmpeq_epi32(a, _mm_setzero_si128())) == 0xffff)
    #define isequal(a, b) (_mm_movemask_epi8(_mm_cmpeq_epi32(a, b)) == 0xffff)
    #define _mm_blendv_epi8(a, b, m) _mm_or_si128(_mm_andnot_si128((m), (a)), \
        _mm_and_si128((m), (b)))
#endif

#ifndef _mm_extract_epi32
    #define _mm_extract_epi32(x, imm) \
        _mm_cvtsi128_si32(_mm_srli_si128((x), 4 * (imm)))
#endif

//
// For unrolling loops and such
//
#define REPEAT8(x)      x x x x x x x x
#define REPEAT16(x)     REPEAT8(x)   REPEAT8(x)
#define REPEAT32(x)     REPEAT16(x)  REPEAT16(x)

//
// Constants
//
const int CYCLES_PER_BLOCK = 37;                // 1 block = 37 cycles
const int NJOBS = 2 * CYCLES_PER_BLOCK;         // 37 for zeros, 37 for repeats
const int MAXTHREADS = NJOBS;                   // 1 thread per job, max

//
// File globals
//
static __m128i gStart, gByteAdder, gDwordAdder; // command-line inputs
static __m128i gByteSteps[CYCLES_PER_BLOCK];    // multiplication table
static int gZBlock0;                            // min 0 LSB's of block step
static uint32_t gPeriod0, gPeriod1,             // min cycles to roll over...
                gPeriod2, gPeriod3;             // ...each DWORD in accumulator
static __m128i gCarryOut;                       // gDwordAdder & ~lowbitmask
static volatile int gNextJob;                   // hand out job numbers
static volatile uint64_t gIncumbent;            // best solution so far
static pthread_t gThreads[MAXTHREADS];          // thread ID's
static pthread_barrier_t gBarrierDone;          // block when jobs depleted

//
// Forward declarations
//
static __m128i parsedwords(const char *const *const argv, int arg);
static void *threadmain(void *data);
static uint64_t barrelLock(int job);
static void initBarrelLock();

//
// Return the number 0 bits in v after the lowest 1 bit.
// If v == 0 return 32.
//
inline int trailingZeros(const uint32_t v)
{
    if (!v) return 32;                  // bsfl is undefined on all zero input
#ifdef __ICC
    return _bit_scan_forward(v);        // icpc has intrinsic
#else
    int r;                              // use inline assmebly on g++
    asm volatile(" bsfl %1, %0": "=r"(r): "rm"(v));
    return r;
#endif
}

//
// Technically, it's not safe to read a volatile variable while it's
// being updated by another thread. You might catch it "half updated",
// say with 32-bits of the old value and 32-bits of the new.
//
// Use an atomic operation to safely fetch the value of gIncumbent.
//
inline uint64_t getIncumbent()
{
    return __sync_fetch_and_add(&gIncumbent, 0);        // add 0 is no-op
}

//
// Problem Description:
//
// Write code that receives an input in the size of 16 BYTEs and runs until
// all bytes are zero or repeat the original input bytes. Every cycle of
// the program a value is added to each BYTE separately. Every 37th cycle,
// the buffer is converted to four DWORDs and a value is added to each DWORD
// separately. For 38th cycle, the DWORD is converted back to 16 BYTEs and
// the addition process repeats (another 37 cycles each). Since addition
// of fixed size values is cyclic itself, after enough iterations through
// this process, the value of the array will either repeat the initial
// values or each becomes zero. The goal of your code is to compute the
// number of cycles required to reach all zero values simultaneously or
// detect the repetition of the original input values.
//
// Correction: Simulation begins on cycle number 0. There's a DWORD add on
// cycle 0 and again whenever cycle % 37 == 0. Output the number of cycles
// where cycle number 0 counts as the first one.
//
int main(const int argc, const char *const *const argv)
{
    timeval then, now;

    //
    // Parse input
    //
    if (argc != 4) {
        fprintf(stderr, "Usage: %s source byte-adder dword-adder\n", argv[0]);
        return -1;
    }
    gStart = parsedwords(argv, 1);
    gByteAdder = parsedwords(argv, 2);
    gDwordAdder = parsedwords(argv, 3);
    gettimeofday(&then, 0);	        // Input is ready. Start timer.

    //
    // Setup static variables to be used by barrelLock algorithm.
    //
    initBarrelLock();

    //
    // Start all threads.
    //
    int nthreads = sysconf(_SC_NPROCESSORS_CONF);
    if (nthreads > MAXTHREADS) nthreads = MAXTHREADS;
    gNextJob = nthreads;        // First job after the initial round
    pthread_barrier_init(&gBarrierDone, 0, nthreads + 1);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_t *pt, *pend = gThreads + nthreads;
    for (pt = gThreads; pt < pend; ++pt)
        if (pthread_create(pt, &attr, threadmain, static_cast<void *>(pt))) {
            perror("pthread_create");
            return -1;
        }

    //
    // Wait for all jobs to be completed. It's slightly faster to use a
    // barrier than to join all the threads now. Do that after printing
    // output.
    //
    pthread_barrier_wait(&gBarrierDone);
    gettimeofday(&now, 0);		// Output is ready. Measure time.

    //
    // Output Description:
    // The program only need to write the number of cycles it took until
    // the Source buffer reached the value of zero in all bytes or repeats
    // the input digits.
    //
    printf("%lu cycles\n", gIncumbent);

    //
    // Clean up
    //
    pthread_attr_destroy(&attr);
    pthread_barrier_destroy(&gBarrierDone);
    for (pt = gThreads; pt < pend; ++pt)
        pthread_join(*pt, NULL);

    //
    // Print the time measurement for accurate score.
    //
    double msecs = (now.tv_sec - then.tv_sec) * 1000.0 +
                   (now.tv_usec - then.tv_usec) / 1000.0;
    printf("Elapsed time: %g milliseconds\n", msecs);

    return 0;
}

//
// Input Description:
// 
// The input is passed to the program as three command line arguments each
// is a collection of hex digits. The arguments are separated by space. The
// first argument is the Source buffer to which data is added. The second
// argument is the BYTE Addition value which is the value to add to the
// Source buffer as byte every cycle. The third argument is the DWORD
// Addition value which is the value to add to the Source buffer every
// 37th cycle.
//
// Assumptions: All three arguments will be 32 characters in length and
// contain only hexadecimal digits. Print a warning/error if input does
// not conform.
// 
static __m128i parsedwords(const char *const *const argv, int arg)
{
    int i;
    uint32_t d[4];
    const char *const s = argv[arg];
    const char *p1, *p2;
    const char hexdigits[] = "0123456789abcdefABCDEF";

    size_t k, len = strlen(s);
    if ((k = strspn(s, hexdigits)) != len) {
        fprintf(stderr,
            "error: arg %d contains '%c' which is not a hex digit.\n"
            "Aborting program.\n", arg, s[k]);
        exit(-1);
    }
    if (len < 32)
        fprintf(stderr,
            "warning: arg %d contains fewer than 32 hex digits.\n"
            "Padding most significant bits with zeros.\n", arg);
    else if (len > 32)
        fprintf(stderr,
            "warning: arg %d contains more than 32 hex digits.\n"
            "Truncating upper bits.\n", arg);
    p2 = s + len;
    p1 = p2 - 8;
    for (i = 0; i < 4; ++i) {
        if (p2 <= s)
            d[i] = 0;
        else {
            if (p1 < s) p1 = s;
            stringstream sstm(string(p1, p2));
            sstm.flags(ios_base::hex);
            sstm >> d[i];
            p1 -= 8;
            p2 -= 8;
        }
    }

    //
    // Note: The arguments to _mm_set_epi32 are i3, i2, i1, and i0,
    // where i3 will be loaded into the low DWORD. The d[] array is already
    // backwards (because of right to left scan). Flip it back by passing
    // the values in i0,i1,i2,i3 order.
    //
    return _mm_set_epi32(d[0], d[1], d[2], d[3]);
}

//
// Main entry point of all threads. This function serves two purposes:
// 1. Assigning jobs to threads (there are generally more jobs than threads).
// 2. Updating the incumbent solution with the lowest value found so far.
//
// Both of these are accomplished lock-free by using intrinsics for
// atomic operations.
//
void *threadmain(void *data)
{
    //
    // The first job number this thread will take is given by its thread
    // number: thread #0 takes job #0, thread #1 takes job #1, etc.
    //
    // After finishing its first job, each thread will take gNextJob++
    // (which is initialized to the next job number nthreads). Use an
    // atomic increment operation to avoid collisions on job numbers.
    //
    int job = (pthread_t *)data - gThreads;     // First job is thread number
    do {
        //
        // The first CYCLES_PER_BLOCK jobs look for zeros.
        // The remainder look for repetition of the initial values.
        //
        uint64_t cycles, oldval;
        cycles = barrelLock(job);

        //
        // If this solution takes fewer cycles than the incumbent solution,
        // try to install it as the new incumbent. Use compare and swap (CAS)
        // intrinsic in case another thread is also trying to install a new
        // incumbent. It will abort the update and return false if *gIncumbent
        // has changed. When that happens, see if cycles is still better than
        // the incubment and, if it is, try again.
        //
        if (cycles)
            while (cycles < (oldval = getIncumbent()) &&
                !__sync_bool_compare_and_swap(&gIncumbent, oldval, cycles))
                ;
    } while ((job = __sync_fetch_and_add(&gNextJob, 1)) < NJOBS);

    //
    // Signal that this thread is done and then exit.
    //
    pthread_barrier_wait(&gBarrierDone);
    pthread_exit(NULL);
}

//
// Advance accumulator "a" by count blocks.
// Use a modified version of Duff's Device that handles count-1 == 0.
//
// Note: The inner loop takes 256 bytes of instruction space:
//     32 lines * 2 instructions per line * 4 bytes per instruction
// Use care when inlining multiple times not to exceed L1 instruction
// cache size (32K on all Xeons?).
//
inline __m128i skim(int count, __m128i a,
    __m128i front, __m128i back, __m128i b)
{
    //
    // Finish current block and do DWORD step to start next one
    //
    a = _mm_add_epi32(_mm_add_epi8(a, back), gDwordAdder);

    //
    // Quickly skim over count-1 whole blocks. Do not use front/back
    // steps to visit any particular bucket along the way.
    //
    register int n = (--count + 31) / 32;
    switch (count & 31)
        while (--n > 0) {
             a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 31: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 30: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 29: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 28: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 27: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 26: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 25: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 24: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 23: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 22: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 21: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 20: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 19: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 18: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 17: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 16: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 15: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 14: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 13: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 12: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 11: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 10: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  9: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  8: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  7: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  6: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  5: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  4: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  3: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  2: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  1: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  0: ;
        }

    //
    // Half a block back to home bucket.
    //
    return _mm_add_epi8(a, front);
}

//
// Specialized versions of skim for when it's known that:
//     0 < count <= max
// where max is a small power of 2.
//
inline __m128i skim2max(int count, __m128i a,
    __m128i front, __m128i back, __m128i b)
{
    a = _mm_add_epi32(_mm_add_epi8(a, back), gDwordAdder);
    if (count == 2)
        a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    return _mm_add_epi8(a, front);
}

inline __m128i skim4max(int count, __m128i a,
    __m128i front, __m128i back, __m128i b)
{
    a = _mm_add_epi32(_mm_add_epi8(a, back), gDwordAdder);
    switch (count) {
    case  4: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  3: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  2: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    default: ;
    }
    return _mm_add_epi8(a, front);
}

inline __m128i skim8max(int count, __m128i a,
    __m128i front, __m128i back, __m128i b)
{
    a = _mm_add_epi32(_mm_add_epi8(a, back), gDwordAdder);
    switch (count) {
    case  8: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  7: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  6: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  5: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  4: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  3: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  2: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    default: ;
    }
    return _mm_add_epi8(a, front);
}

inline __m128i skim16max(int count, __m128i a,
    __m128i front, __m128i back, __m128i b)
{
    a = _mm_add_epi32(_mm_add_epi8(a, back), gDwordAdder);
    switch (count) {
    case 16: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 15: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 14: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 13: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 12: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 11: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case 10: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  9: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  8: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  7: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  6: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  5: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  4: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  3: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    case  2: a = _mm_add_epi32(_mm_add_epi8(a, b), gDwordAdder);
    default: ;
    }
    return _mm_add_epi8(a, front);
}

//
// The problem description screams "SSE":
//     Q. 128 bit integer registers?  A. SSE2
//     Q. Add four DWORDS?            A. PADDD (_mm_add_epi32 intrinsic)
//     Q. Add sixteen BYTES?          A. PADDB (_mm_add_epi8 intrinsic)
//
// The 37 cycle pattern whispers "You have 40 cores. Multithread by the
// bucket." where a bucket represents the number of cycles since the
// previous DWORD step. Bucket number 0 has just taken its DWORD step
// and has a long way to go before its next one. Bucket number
// CYCLES_PER_BLOCK-1 has taken its last BYTE step and is ready for its
// DWORD step. Each bucket then steps CYCLES_PER_BLOCK at a time.
//
// SSE and multithreading will only go so far when the basic algorithm is
// "grind it out". In order to go *really* fast one needs a better algorithm.
//
// Enter the barrel lock algorithm, so named because it's analogous to
// opening a 4 dial barrel lock when you already know the combination:
//     http://www.northerntool.com/images/product/images/971816_lg.jpg
//
// It works in four phases from right to left. The first phase gets the
// rightmost dial into position. The second phase works on the second dial
// without moving the first dial, etc. Graphically, the 128 bit accumulator
// approaches the solution state in this order:
//
//     33221100 33221100 33221100 33221100
//
// where each number represents the phase in which the corresponding nibble
// (hex digit) arrives at its target value.
//
// This is easier said than done. The dials on a barrel lock are independent.
// Once the first dial is in place you don't have to move it again. However,
// the problem statement more closely resembles an odometer than a barrel
// lock. The second dial doesn't advance until the first dial rolls over.
// The trick is to make an odometer act like a barrel lock.
//
// Working out the modulo arithmetic on paper reveals the "period" of each
// dial - the minimum number of cycles for it to return to its target stage.
// This is essentially how phases 1 and 2 work. They quickly advance by
// period cycles without stopping to check any values. Phases 0 and 3 are
// highly optimized (in different manners) to take advantage of their
// positions at each end of the DWORD. Phase 0 uses "normal math" because
// every block is like any other. There's no surprise carries coming in every
// 37th cycle. Phase 3 benefits from not caring about upward carries.
//
// Define "probe" as any comparison between the accumulator and a full
// or partial goal state. In the worst case this algorithm needs:
//
//     phase 0:   8 probes
//     phase 1: 256 probes
//     phase 2: 256 probes
//     phase 3: 256 probes
//     total:   776
//
// That's per job but most jobs fail to make it beyond phase 0. In a typical
// test, two jobs will finish phase 3 and all the others will fail to launch.
// 
// Return value is number of cycles if a solution is found. Otherwise, zero.
//
static uint64_t barrelLock(int job)
{
    register __m128i a, b, front, back, keymask, carry, target, keys;
    register uint64_t cycles, delta;
    register int n;

    //
    // Let front = the cumulative BYTE adder step from the beginning of a
    // block (after the DWORD step) to this job's bucket. Let back = the
    // BYTE step from this job's bucket to the end of the block. Then to
    // go around the block it would be:
    //
    //     a += back
    //     a += gDwordAdder
    //     a += front
    // 
    // Let b = (CYCLES_PER_BLOCK - 1) * BYTEadder. To go two blocks:
    //
    //     a += back             To the end of this block
    //     a += gDwordAdder      Begin next block
    //     a += b                Step a whole block
    //     a += gDwordAdder      Begin another
    //     a += front            Half a block home to our bucket
    //
    // The skim() function extrapolates this to step n blocks.
    //
    bool isrepeat = (job >= CYCLES_PER_BLOCK);
    int bucket = job % CYCLES_PER_BLOCK;
    cycles = bucket + 1;                        // At least 1 cycle
    front = gByteSteps[bucket];                 // Step from bucket to end
    back = gByteSteps[CYCLES_PER_BLOCK - 1 - bucket];
    b = gByteSteps[CYCLES_PER_BLOCK - 1];       // b = 36 * BYTEadder

    a = _mm_add_epi32(gStart, gDwordAdder);     // cycle 0: seed by adding DWORD
    a = _mm_add_epi8(a, front);                 // Advance to job's bucket
    target = isrepeat ? gStart : _mm_setzero_si128();

    //
    // Phase 0
    //
    // At the end of this phase the accumulator will look like this in hex:
    //
    //     ......tt ......tt ......tt ......tt
    //
    // Where the t's represent target nibbles and '.' are don't care.
    // In other words, the least significant byte of each DWORD in the
    // accumulator will be in its goal state.
    //
    // Dial them in one bit at a time. This will take up to 8 probes and
    // 8 increments. It would be possible to recheck the bits after each
    // increment and exit immediately if they don't match. However, this
    // would optimize jobs which fail to launch. There are plenty of threads
    // standing by to process those. It's more important to optimize successful
    // jobs, so optimistically forge ahead through the probes and check all
    // 8 bits at the end.
    //
    keymask = _mm_set1_epi32(1 << gZBlock0);
    n = 1;
    delta = CYCLES_PER_BLOCK;
    switch (gZBlock0) {
    case 0:
        // Are low bits already zero?
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            // No. Increment by one block.
            a = _mm_add_epi8(_mm_add_epi32(
                _mm_add_epi8(a, back), gDwordAdder), front);
            // Count cycles in increment
            cycles += delta;
        }
        // Shift left to next bit and fallthrough to next case.
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 1:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            // Advance by n blocks, where n depends on gZBlock0
            a = skim2max(n, a, front, back, b);
            cycles += delta;
        }
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 2:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            a = skim4max(n, a, front, back, b);
            cycles += delta;
        }
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 3:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            a = skim8max(n, a, front, back, b);
            cycles += delta;
        }
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 4:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            a = skim16max(n, a, front, back, b);
            cycles += delta;
        }
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 5:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            a = skim(n, a, front, back, b);
            cycles += delta;
        }
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 6:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            a = skim(n, a, front, back, b);
            cycles += delta;
        }
        n <<= 1;
        delta <<= 1;
        keymask = _mm_slli_epi32(keymask, 1);
    case 7:
        if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target)))) {
            a = skim(n, a, front, back, b);
            cycles += delta;
        }
    default:                                    // 8+ trailing zeros in b
        ;                                       // key may already be zero
    }
    keymask = _mm_set1_epi32(0xff);             // low byte mask
    if (!iszero(_mm_and_si128(keymask, _mm_xor_si128(a, target))))
        return 0;                               // can't get there from here

    //
    // Phase 1
    //
    // The goal of this phase is to get the accumulator here:
    //
    //     ....tttt ....tttt ....tttt ....tttt
    //
    // The least significant byte of each DWORD is already there.
    // Take the smallest possible step that preserves this invariant.
    //
    // It may take up to 256 iterations to find the solution or prove
    // that it doesn't exist. The "one bit at a time" approach from
    // phase 0 no longer applies due to carries from the lower byte on
    // DWORD adds. For example, this second byte might have the same
    // value for two consecutive blocks and then start moving again.
    //
    if (isequal(a, target)) return cycles;
    if (cycles >= getIncumbent()) return 0;     // better solution found

    keymask = _mm_slli_si128(keymask, 1);       // next byte
    keys = _mm_and_si128(target, keymask);      // match these keys

    delta = gPeriod0 * CYCLES_PER_BLOCK;        // cycles per period
    if (isrepeat && cycles == delta) return 0;  // job 73 bound for max cycles
    n = (gPeriod1 / gPeriod0) >> 3;             // n * REPEAT8 = period
    do {
        REPEAT8(
            if (isequal(_mm_and_si128(a, keymask), keys))
                goto phase2;                    // next phase
            a = skim(gPeriod0, a, front, back, b);
            cycles += delta;
        )
    } while (--n > 0);                          // n may have started at 0
    return 0;                                   // no match found in phase 1

    //
    // Phase 2
    //
    // The goal state:
    //
    //     ..tttttt ..tttttt ..tttttt ..tttttt
    //
    // Again, take the smallest possible step that doesn't mess up bytes
    // 0 and 1 of each DWORD in the accumulator. Ordinarily the smallest
    // possible step will be 256 * 256 and phase 2 may terminate after
    // 256 iterations. The actual number depends upon the minimum number
    // of trailing zeros of the four DWORDS in gCarryOut. After
    // 2 ^ (16 - zcarry) blocks, that lowest bit in gCarryOut will have
    // been added to itself so many times that it shifts up into byte 2.
    // That's the first time bytes 0 and 1 will roll over to zero again.
    //
    // This is actually the slowest of the four phases because the inner
    // loop inside skim() must "turn the crank" up to 64K times.
    //
phase2:
    if (isequal(a, target)) return cycles;
    if (cycles >= getIncumbent()) return 0;

    keymask = _mm_slli_si128(keymask, 1);
    keys = _mm_and_si128(target, keymask);

    delta = gPeriod1 * CYCLES_PER_BLOCK;
    n = (gPeriod2 / gPeriod1) >> 3;
    do {
        REPEAT8(
            if (isequal(_mm_and_si128(a, keymask), keys))
                goto phase3;
            a = skim(gPeriod1, a, front, back, b);
            cycles += delta;
        )
    } while (--n > 0);
    return 0;

    //
    // Phase 3
    //
    // Arrive at the solution:
    //
    //     tttttttt tttttttt tttttttt tttttttt
    //
    // Take advantage of a shortcut. These are big steps now. In the ordinary
    // case of zcarry = 0, each iteration adds CYCLES_PER_BLOCK * 2^24 cycles
    // to the count. That's enough for each byte to roll over
    // CYCLES_PER_BLOCK * 2^16 times. The lower three bytes of each DWORD will
    // return to the target but the upper byte will be increased by a series
    // of carries. The net size of this increase equals the lowest 8 non-zero
    // bits of gCarryOut. Carries originating from the middle bytes will
    // accumulate 256 at a time, shifting themselves into the bit bucket.
    //
    // Doing this analysis on paper means no more turning the crank to skim
    // over blocks in the loop. This shortcut was not taken in phases 1 and 2
    // because I couldn't predict how many carries would propagate upward to
    // the next higher byte (i.e. when carries would arrive and if the middle
    // byte would equal 0xff at the time, triggering a propagated carry).
    // Perhaps it's possible but I couldn't figure out the math. Here it's not
    // a problem because there is no next higher byte to be concerned with.
    //
phase3:
    if (cycles >= getIncumbent()) return 0;

    uint64_t oldcycles = cycles;                        // Snapshot of state
    __m128i olda = a;

    delta = gPeriod2 * CYCLES_PER_BLOCK;
    n = gPeriod3 >> 3;
    n = 32;
    do {
        REPEAT8(
            if (isequal(a, target))
                return cycles;
            a = _mm_add_epi32(a, gCarryOut);            // Use the shortcut!
            cycles += delta;
        )
    } while (--n > 0);

    //
    // Made it to phase3 and yet still didn't find a solution. That's
    // suspicious. Perhaps gCarryOut is incorrect. Recalculate it the
    // old-fashioned way. If it changes, roll back and repeat phase 3.
    //
    a = skim(gPeriod2, _mm_setzero_si128(), front, back, b);
    if (!isequal(a, gCarryOut)) {
        gCarryOut = a;
        cycles = oldcycles;
        a = olda;
        n = gPeriod3 >> 3;
        do {
            REPEAT8(
                if (isequal(a, target)) return cycles;
                a = _mm_add_epi32(a, gCarryOut);
                cycles += delta;
            )
        } while (--n > 0);
    }
    return 0;
}

//
// Initialize all of the following file globals:
//     __m128i gAllOnes;
//     __m128i gByteSteps[CYCLES_PER_BLOCK];
//     int gZBlock0;
//     uint32_t gPeriod0, gPeriod1, gPeriod2, gPeriod3;
//     __m128i gCarryOut;
//     uint64_t gIncumbent;
//
// Note to reader: It may be clearer to first read about these variables' usage
// above in barrelLock(). Their initialization is more complex than their use.
//
static void initBarrelLock()
{
    int z0, z1, z2, z3;         // trailing 0's temporaries
    int zblock, zcarry;         // min 0 LSB's of block step and carry out
    __m128i zmask;

    //
    // Initialize SSE 4.1 constant.
    //
#ifdef __SSE4_1__
    gAllOnes = _mm_cmpeq_epi64(gAllOnes, gAllOnes);
#endif

    //
    // Calculate the byte adder multiplication table.
    // Modular addition is associative:
    //
    //     (a + b) + b == a + (b + b)   (mod n)
    //
    // Pre-calculate values 2*b, 3*b, 4*b, etc. and use associative property
    // to step up to CYCLES_PER_BLOCK-1 with one addition.
    //
    // warning: CYCLES_PER_BLOCK implicitly hardcoded to 37 here.
    //
    __m128i a, b, *p = gByteSteps;
    b = gByteAdder;
    *p++ = _mm_xor_si128(b, b);                 // 0
    *p++ = a = b;                               // 1 * byteadder
    *p++ = a = _mm_add_epi8(a, b);              // 2 * byteadder
    REPEAT32(*p++ = a = _mm_add_epi8(a, b);)    // 3 through 34
    *p++ = a = _mm_add_epi8(a, b);              // 35 * byteadder
    *p = b =_mm_add_epi8(a, b);                 // b = 36 * byteadder
    a = _mm_add_epi32(b, gDwordAdder);          // a = full block step

    //
    // If all low bytes of accumulator are already at target, how many
    // blocks does it take to for them to get back to target again?
    //
    // The low bytes of the accumulator roll over every 256, 128, 64, etc.
    // blocks depending on the number of trailing zeros in the block step:
    //
    //     blockstep = gDwordAdder + (36 * gByteAdder) % 256
    //
    // If the least significant bit on any DWORD in the block step is set,
    // the low byte in the corresponding accumulator DWORD will take 256
    // blocks to return to the target. The period is 128 if bit 1 is the
    // lowest bit set, 64 for bit 2, and so on.
    //
    // The variable "a" currently contains the block step. Count the number
    // of trailing zeros in each DWORD. Let gZBlock0 be the minimum of these
    // four counts. The period for byte(s) 0 will then given by:
    //
    //     gPeriod0 = 2 ^ (8 - gZBlock0)
    //
    // SSE doesn't have _bit_scan_forward or *independent* shifts of
    // epi32 values. Rough it with scalar code for awhile.
    //
    z0 = min(8, trailingZeros(_mm_extract_epi32(a, 0)));
    z1 = min(8, trailingZeros(_mm_extract_epi32(a, 1)));
    z2 = min(8, trailingZeros(_mm_extract_epi32(a, 2)));
    z3 = min(8, trailingZeros(_mm_extract_epi32(a, 3)));
    gZBlock0 = min(min(z0, z1), min(z2, z3));
    gPeriod0 = 1 << 8 - gZBlock0;

    //
    // Over the course of gPeriod0 block steps, how many carries will there
    // be up to byte 1 of the accumulator?
    //
    // It's usually just the low byte of each DWORD in gDwordAdder. However,
    // set bits in gDwordAdder that are lower than bit z0 (or z1/z2/z3) have
    // already been canceled out by the bits of 36 * gByteAdder, resulting in
    // a 0 bit in the block step. Otherwise z0 would indicate this 1 bit.
    //
    // For example, consider:
    //     gStart =      0x0000
    //     gByteAdder =  0x00EC
    //     gDwordAdder = 0x0090
    //     block step =  0x00C0 (gDwordAdder + (36 * gByteAdder) % 256)
    //     gZBlock0 =         6 (trailing zeros of block step)
    //     gPeriod0 =         4 (2 ^ (8 - gZBlock0))
    // After 4 block steps the accumulator = 0x0200 (not the 0x0900 had
    // gDwordAdder been shifted left by 4 bits). The lower set bit of
    // 0x0090 has no effect on the carry.
    //
    // Mask such bits out of gDwordAdder to get an accurate "carry out"
    // value of byte(s) 0.
    //
    //     gCarryOut = ((gStart & ~zmask) + gDwordAdder) & zmask
    //
    zmask = _mm_set_epi32(0xffffffff << z3,
                          0xffffffff << z2,
                          0xffffffff << z1,
                          0xffffffff << z0);
    gCarryOut = _mm_and_si128(
        _mm_add_epi32(
            _mm_andnot_si128(zmask, gStart),
            gDwordAdder),
        zmask);

    //
    // It's also useful to know the minimum number of trailing zeros
    // in gCarryOut.
    //
    z0 = trailingZeros(_mm_extract_epi32(gCarryOut, 0));
    z1 = trailingZeros(_mm_extract_epi32(gCarryOut, 1));
    z2 = trailingZeros(_mm_extract_epi32(gCarryOut, 2));
    z3 = trailingZeros(_mm_extract_epi32(gCarryOut, 3));
    zcarry = min(min(z0, z1), min(z2, z3));

    //
    // Repeat all of the above for bytes 1 and 2 if necessary
    // (which it rarely is).
    //
    gPeriod3 = 256;
    if (zcarry < 8) {
        //
        // This is the usual case. The number of block steps needed for
        // both a1 and a0 to repeat is given by:
        //
        //     gPeriod1 = 2 ^ (16 - zcarry)
        //
        gPeriod1 = 1 << 16 - zcarry;
        gPeriod2 = 1 << 24 - zcarry;

        //
        // Normalize gCarryOut so that bit #24 contains the least
        // significant set bit.
        //
        gCarryOut = _mm_slli_epi32(gCarryOut, 24 - zcarry);
    }
    else {
        //
        // The odds of getting here are 1 in 2^32
        //
        a = _mm_srli_epi32(_mm_add_epi32(b, gCarryOut), 8);
        z0 = min(8, trailingZeros(_mm_extract_epi32(a, 0)));
        z1 = min(8, trailingZeros(_mm_extract_epi32(a, 1)));
        z2 = min(8, trailingZeros(_mm_extract_epi32(a, 2)));
        z3 = min(8, trailingZeros(_mm_extract_epi32(a, 3)));
        zblock = min(gZBlock0, min(min(z0, z1), min(z2, z3)));
        gPeriod1 = 1 << 8 - zblock;

        zmask = _mm_set_epi32(0xffffffff << z3 + 8,
                              0xffffffff << z2 + 8,
                              0xffffffff << z1 + 8,
                              0xffffffff << z0 + 8);
        gCarryOut = _mm_and_si128(
            _mm_add_epi32(
                _mm_andnot_si128(zmask, gStart),
                gCarryOut),
            zmask);

        z0 = trailingZeros(_mm_extract_epi32(gCarryOut, 0));
        z1 = trailingZeros(_mm_extract_epi32(gCarryOut, 1));
        z2 = trailingZeros(_mm_extract_epi32(gCarryOut, 2));
        z3 = trailingZeros(_mm_extract_epi32(gCarryOut, 3));
        zcarry = min(min(z0, z1), min(z2, z3));

        if (zcarry < 16) {
            gPeriod2 = 1 << 24 - zcarry;
            gCarryOut = _mm_slli_epi32(gCarryOut, 24 - zcarry);
        }
        else {
            //
            // The odds of getting here are 1 in 2^64
            //
            a = _mm_srli_epi32(_mm_add_epi32(b, gCarryOut), 16);
            z0 = min(8, trailingZeros(_mm_extract_epi32(a, 0)));
            z1 = min(8, trailingZeros(_mm_extract_epi32(a, 1)));
            z2 = min(8, trailingZeros(_mm_extract_epi32(a, 2)));
            z3 = min(8, trailingZeros(_mm_extract_epi32(a, 3)));
            zblock = min(zblock, min(min(z0, z1), min(z2, z3)));
            gPeriod2 = 1 << 8 - zblock;

            zmask = _mm_set_epi32(0xffffffff << z3 + 16,
                                  0xffffffff << z2 + 16,
                                  0xffffffff << z1 + 16,
                                  0xffffffff << z0 + 16);
            gCarryOut = _mm_and_si128(
                _mm_add_epi32(
                    _mm_andnot_si128(zmask, gStart),
                    gCarryOut),
                zmask);

            z0 = trailingZeros(_mm_extract_epi32(gCarryOut, 0));
            z1 = trailingZeros(_mm_extract_epi32(gCarryOut, 1));
            z2 = trailingZeros(_mm_extract_epi32(gCarryOut, 2));
            z3 = trailingZeros(_mm_extract_epi32(gCarryOut, 3));
            zcarry = min(min(z0, z1), min(z2, z3));

            if (zcarry < 24)
                gCarryOut = _mm_slli_epi32(gCarryOut, 24 - zcarry);
            else
                gPeriod3 >>= zcarry - 24;
        }
    }

    //
    // All tests inevitably repeat their initial values after
    // CYCLES_PER_BLOCK * 2 ^ (32 - zcarry) cycles. Job #73 would
    // be the one to organically find this limit but it would take
    // the maximum number of probes: 776.
    //
    // The next line provides a safety net for job #73 so it can
    // bail out early. Job #73 could be skipped entirely if it
    // weren't for trivial repeats such as:
    //
    //     gStart =      80000000
    //     gDwordAdder = 000000DC
    //     gByteAdder =  00000001
    //     Repeats after 37 cycles
    //
    gIncumbent = CYCLES_PER_BLOCK * (1ul << 32 - zcarry); // Worst case
}
