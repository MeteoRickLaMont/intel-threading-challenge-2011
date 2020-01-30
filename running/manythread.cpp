//
// Intel Threading Challenge 2011 submission
// P1: A3 - Running Numbers
// Rick LaMont <lamont@dotcsw.com>
// Compile with: make
//

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
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
// Use SSE4.1 instruction set if available. It has the PTEST
// instruction which speeds up comparisons with zero and initial.
//
#ifdef __SSE4_1__
    #include <smmintrin.h>
    #define iszero(a) _mm_testz_si128(a, a)
    #define isequal(a, b) _mm_testc_si128(_mm_cmpeq_epi32(a, b), gAllOnes)
#else
    #define iszero(a) (_mm_movemask_epi8(_mm_cmpeq_epi32(a, gZero)) == 0xffff)
    #define isequal(a, b) (_mm_movemask_epi8(_mm_cmpeq_epi32(a, b)) == 0xffff)
#endif

#ifndef _mm_extract_epi8
    #define _mm_extract_epi8(x, imm) \
        ((((imm) & 0x1) == 0) ?      \
        _mm_extract_epi16((x), (imm) >> 1) & 0xff : \
        _mm_extract_epi16(_mm_srli_epi16((x), 8), (imm) >> 1))
#endif

#ifndef _mm_extract_epi32
    #define _mm_extract_epi32(x, imm) \
        _mm_cvtsi128_si32(_mm_srli_si128((x), 4 * (imm)))
#endif

//
// For unrolling loops and such
//
#define REPEAT2(x)      x x
#define REPEAT4(x)      x x x x
#define REPEAT8(x)      x x x x x x x x
#define REPEAT16(x)     REPEAT8(x)   REPEAT8(x)
#define REPEAT32(x)     REPEAT16(x)  REPEAT16(x)

//
// Constants
//
const int CYCLES_PER_BLOCK = 37;
const uint64_t MAX_CYCLES = ((uint64_t)CYCLES_PER_BLOCK << 32);
const int MAXTHREADS = 2 * CYCLES_PER_BLOCK;

//
// Types
//
typedef void *(*ThreadFunc)(void *);

//
// File globals
//
static __m128i gStart, gByteAdder, gDwordAdder; // command-line inputs
static __m128i gByteSteps[CYCLES_PER_BLOCK];    // multiplication table
#ifdef __SSE4_1__
static __m128i gAllOnes;                        // all bits one
#else
static __m128i gZero;                           // all bits zero
#endif
static int gZ0, gZ1, gZ2, gZ3;                  // trailing 0's of block steps
static pthread_t gThreads[MAXTHREADS];
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;
static volatile uint64_t gIncumbent;            // best solution so far

//
// Forward declarations
//
static __m128i parsedwords(const char *const *const argv, int arg);
void *general(void *data);
template<int Z> static void *fourphase(void *data);

//
// Return the number of LSBs that are zero (following the lowest set bit).
// Result is undefined if v == 0.
//
inline int trailing_zeros(const uint32_t v)
{
    if (!v) return 32;          // bsfl is undefined on all zero input
    int r;
    asm volatile(" bsfl %1, %0": "=r"(r): "rm"(v));
    return r;
}

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
    gettimeofday(&then, 0);	// Input is ready

    //
    // Initialize SSE constant.
    // Compiler will load it into a register when used heavily.
    //
#ifdef __SSE4_1__
    gAllOnes = _mm_cmpeq_epi64(gAllOnes, gAllOnes);
#else
    gZero = _mm_xor_si128(gZero, gZero);
#endif

    //
    // Calculate the byte adder multiplication table.
    //
    __m128i a, b, *p = gByteSteps;
    b = gByteAdder;
    *p++ = _mm_xor_si128(b, b);         // 0
    *p++ = a = b;                       // 1 * byteadder
    *p++ = a = _mm_add_epi8(a, b);      // 2 * byteadder
    REPEAT32(*p++ = a = _mm_add_epi8(a, b);)
    *p++ = a = _mm_add_epi8(a, b);      // 35 * byteadder
    *p = a =_mm_add_epi8(a, b);         // 36 * byteadder
    a = _mm_add_epi32(a, gDwordAdder);  // a = full block step

    //
    // If all keys are already at target, how many blocks does it take to
    // for them to get back to target again?
    //
    // accumulator (a) now contains 36 * BYTEadder + DWORDadder
    // The keys equal the delta for a full cycle. If the least significant
    // bit on any key is set, that key will take 256 blocks to return to
    // the target. The period is 128 if bit 1 is the lowest bit set, 64
    // for bit 2, and so on.
    //
    // Examine the four key bytes in (a). Count the number of trailing
    // zeros in each. Let z be the minimum of these 4 counts. Period will
    // then given by:
    //
    //     period = 2 ^ (8 - z)
    //
    gZ0 = trailing_zeros(_mm_extract_epi32(a, 0));
    gZ1 = trailing_zeros(_mm_extract_epi32(a, 1));
    gZ2 = trailing_zeros(_mm_extract_epi32(a, 2));
    gZ3 = trailing_zeros(_mm_extract_epi32(a, 3));
    int z = min(min(gZ0, gZ1), min(gZ2, gZ3));
    gIncumbent = MAX_CYCLES >> z;            // Worst case: no matches

    ThreadFunc threadmain;
    int nthreads = 2 * CYCLES_PER_BLOCK;
    switch (z) {
    case 0: threadmain = fourphase<0>; break;
    case 1: threadmain = fourphase<1>; break;
    case 2: threadmain = fourphase<2>; break;
    case 3: threadmain = fourphase<3>; break;
    case 4: threadmain = fourphase<4>; break;
    case 5: threadmain = fourphase<5>; break;
    case 6: threadmain = fourphase<6>; break;
    case 7: threadmain = fourphase<7>; break;
    default:
        threadmain = general; // TODO specialize fourphase for these cases
        nthreads = CYCLES_PER_BLOCK;
        break;
    }

    //
    // Start all threads.
    //
    pthread_t *pt, *pend = gThreads + nthreads;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    for (pt = gThreads; pt < pend; ++pt)
        if (pthread_create(pt, &attr, threadmain, static_cast<void *>(pt))) {
            perror("pthread_create");
            return -1;
        }

    //
    // Wait for threads to exit.
    //
    pthread_attr_destroy(&attr);
    for (pt = gThreads; pt < pend; ++pt)
        pthread_join(*pt, NULL);

    //
    // Print output.
    //
    gettimeofday(&now, 0);		// Output is ready.
    printf("%lu cycles\n", gIncumbent);

    double msecs = (now.tv_sec - then.tv_sec) * 1000.0 +
                   (now.tv_usec - then.tv_usec) / 1000.0;
    fprintf(stderr, "Elapsed time: %g milliseconds\n", msecs);
    return 0;
}

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

    return _mm_set_epi32(d[0], d[1], d[2], d[3]);
}

//
// Most general SSE solution. This is perfectly adequate in all situations
// and probably what the problem designers had in mind. Now it's only used
// on "spoiler" problems where all the keys are zero (period = 1). The more
// aggressive scheme below handles the specific cases of higher periods.
//
// Each thread takes one bucket in the block which starts at cycle bucket+1:
//
//     gStart + gDwordAdder + tnum * gByteAdder
//
// It advances by steps of CYCLES_PER_BLOCK.
//
// Note: Only CYCLES_PER_BLOCK (37) threads are required for this scheme.
//       Each thread checks for both exit criteria (0 and initial).
//
void *general(void *data)
{
    int bucket = (pthread_t *)data - gThreads;
    register uint64_t cycles = bucket + 1;   // Start at cycle 1
    register __m128i a, front, back;

    a = gStart;
    front = gByteSteps[bucket];
    back = gByteSteps[CYCLES_PER_BLOCK - 1 - bucket];

    //
    // Main loop. Iterate until this thread finds a solution or
    // another thread finds an incumbent solution that uses fewer cycles
    // than the ones being considered here.
    //
    // This loop should be all in-register except for the comparison
    // with gIncumbent which is volatile.
    //
    a = _mm_add_epi32(a, gDwordAdder);  // Cycle 0: seed by adding DWORD
    do {
	a = _mm_add_epi8(a, front);	// Offset to our bucket
        if (iszero(a) || isequal(a, gStart)) goto found0;
	a = _mm_add_epi8(a, back);	// From our bucket to the end
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found1;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found2;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found3;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found4;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found5;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found6;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);

	a = _mm_add_epi8(a, front);
        if (iszero(a) || isequal(a, gStart)) goto found7;
	a = _mm_add_epi8(a, back);
	a = _mm_add_epi32(a, gDwordAdder);
    } while ((cycles += 8 * CYCLES_PER_BLOCK) < gIncumbent);
    pthread_exit(NULL);			// Another thread found a solution

    //
    // This thread found a solution. Use these cascading goto labels
    // to get an accurate cycle count for where the solution was found.
    //
found7: cycles += CYCLES_PER_BLOCK;
found6: cycles += CYCLES_PER_BLOCK;
found5: cycles += CYCLES_PER_BLOCK;
found4: cycles += CYCLES_PER_BLOCK;
found3: cycles += CYCLES_PER_BLOCK;
found2: cycles += CYCLES_PER_BLOCK;
found1: cycles += CYCLES_PER_BLOCK;
found0:
    pthread_mutex_lock(&gMutex);
    if (cycles < gIncumbent)
	gIncumbent = cycles;
    pthread_mutex_unlock(&gMutex);
    pthread_exit(NULL);
}

//
// Skim over COUNT blocks (where COUNT is a compile-time constant).
//
template<int COUNT>
inline __m128i skim(__m128i a, __m128i b, __m128i d)
{
    register int n = (COUNT + 31) / 32;
    a = _mm_add_epi32(a, d);    // Each block begins with a DWORD step
    switch (COUNT & 31)
    case 0: do { a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 31:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 30:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 29:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 28:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 27:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 26:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 25:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 24:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 23:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 22:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 21:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 20:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 19:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 18:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 17:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 16:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 15:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 14:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 13:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 12:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 11:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case 10:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  9:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  8:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  7:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  6:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  5:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  4:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  3:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  2:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
    case  1:     a = _mm_add_epi8(a, b); a = _mm_add_epi32(a, d);
            } while (--n);
    return a;
}

//
// When bytes 0 reach target simultaneously, you can find additional
// targets by skimming PERIOD0 blocks at a time.
//
// When bytes 1 reach target simultaneously, you can find additional
// targets by skimming 256 * PERIOD0  blocks at a time.
//
// When bytes 2 reach target simultaneously, you can find additional
// targets by skimming 65536 * PERIOD0 blocks at a time.
// A quicker alternative goes something like this:
//
// A[3] += DWORDadder[0]
//
// where DWORDadder is masked and/or shifted based on gZ0 - gZ3.
// This trick only works on most significant bytes 3 because carries
// upward are irrelevant.
//
template<int Z>
static void *fourphase(void *data)
{
    const uint32_t PERIOD0 = 1 << 8 - Z;
    const uint32_t PERIOD1 = 256 * PERIOD0;
    const uint32_t PERIOD2 = 256 * PERIOD1;

    register __m128i a, b, front, back, target, keymask, keys;
    register uint64_t cycles;
    int tnum, bucket, n;

    tnum = (pthread_t *)data - gThreads;
    bucket = tnum % CYCLES_PER_BLOCK;
    cycles = bucket + 1;                        // At least 1 cycle

    a = _mm_add_epi32(gStart, gDwordAdder);     // Cycle 0: seed by adding DWORD
    b = gByteSteps[CYCLES_PER_BLOCK - 1];       // b = 36 * BYTEadder
    front = gByteSteps[bucket];
    back = gByteSteps[CYCLES_PER_BLOCK - 1 - bucket];

    if (tnum >= CYCLES_PER_BLOCK)
        target = gStart;                        // target is initial values
    else {
        if (Z > 0) {
            keymask = _mm_set1_epi32((1 << Z) - 1);
            if (!iszero(_mm_and_si128(_mm_add_epi8(a, front), keymask)))
                goto nosolution;
        }
        target = _mm_xor_si128(a, a);           // target is all zeros
    }

    //
    // Phase 0: Get all least significant bytes (0, 4, 8 and 12) of
    // accumulator into target state. Must be able to do so within PERIOD0
    // iterations or else it cannot be done.
    //
    keymask = _mm_set1_epi32(0xff);             // low byte mask
    keys = _mm_and_si128(target, keymask);      // keys are low bytes of target
    switch (PERIOD0) {
    case 8:
        REPEAT4(
            a = _mm_add_epi8(a, front);
            if (isequal(_mm_and_si128(a, keymask), keys)) goto phase1;
            a = _mm_add_epi8(a, back);
            a = _mm_add_epi32(a, gDwordAdder);
            cycles += CYCLES_PER_BLOCK;
        )
    case 4:
        REPEAT2(
            a = _mm_add_epi8(a, front);
            if (isequal(_mm_and_si128(a, keymask), keys)) goto phase1;
            a = _mm_add_epi8(a, back);
            a = _mm_add_epi32(a, gDwordAdder);
            cycles += CYCLES_PER_BLOCK;
        )
    case 2:
        REPEAT2(
            a = _mm_add_epi8(a, front);
            if (isequal(_mm_and_si128(a, keymask), keys)) goto phase1;
            a = _mm_add_epi8(a, back);
            a = _mm_add_epi32(a, gDwordAdder);
            cycles += CYCLES_PER_BLOCK;
        )
        break;
    default:                    // For PERIOD0 > 8, set up loop
        n = PERIOD0 / 8;
        do {
            REPEAT8(
                a = _mm_add_epi8(a, front);
                if (isequal(_mm_and_si128(a, keymask), keys)) goto phase1;
                a = _mm_add_epi8(a, back);
                a = _mm_add_epi32(a, gDwordAdder);
                cycles += CYCLES_PER_BLOCK;
            )
        } while (--n);
        break;
    }
    goto nosolution;			// Can't get there from here

    //
    // Phase 1: Get bytes 1, 5, 9 and 13 of accumulator into target state.
    // Bytes 0, 4, 8 and 12 are already there. Take smallest step that
    // preserves this invariant. This may take up to 256 iterations.
    //
phase1:
    if (isequal(a, target)) goto solution;           // That was easy
    if (cycles >= gIncumbent) goto nosolution;       // Better solution found

    keymask = _mm_set1_epi32(0xffff);                // low word mask
    keys = _mm_and_si128(target, keymask);
    n = 32;
    do {
        REPEAT8(
            if (isequal(_mm_and_si128(a, keymask), keys)) goto phase2;
            a = _mm_add_epi8(a, back);               // Half block to end
            a = skim<PERIOD0-1>(a, b, gDwordAdder);  // Skim n-1 full blocks
            a = _mm_add_epi8(a, front);              // Half block home
            cycles += PERIOD0 * CYCLES_PER_BLOCK;
        )
    } while (--n);
    goto nosolution;

    //
    // Phase 2: Get bytes 2, 6, 10 and 14 of accumulator into target state.
    // Lower bytes are already there. Take smallest step that preserves this
    // invariant. This may take up to 256 iterations.
    //
phase2:
    if (isequal(a, target)) goto solution;
    if (cycles >= gIncumbent) goto nosolution;

    keymask = _mm_set1_epi32(0xffffff);
    keys = _mm_and_si128(target, keymask);
    n = 32;
    do {
        REPEAT8(
            if (isequal(_mm_and_si128(a, keymask), keys)) goto phase3;
            a = _mm_add_epi8(a, back);
            a = skim<PERIOD1-1>(a, b, gDwordAdder);
            a = _mm_add_epi8(a, front);
            cycles += PERIOD1 * CYCLES_PER_BLOCK;
        )
    } while (--n);
    goto nosolution;

    //
    // Phase 3: Get bytes 3, 7, 11 and 15 of accumulator into target state.
    // Lower bytes are already there. Take smallest step that preserves this
    // invariant. This may take up to 256 iterations.
    //
phase3:
    if (isequal(a, target)) goto solution;
    if (cycles >= gIncumbent) goto nosolution;

    // TODO Test with different values of Z and Z0-Z3.
    b = _mm_slli_epi32(_mm_and_si128(
        _mm_set_epi32(0xffffffff << gZ3 - Z,
                      0xffffffff << gZ2 - Z,
                      0xffffffff << gZ1 - Z,
                      0xffffffff << gZ0 - Z), gDwordAdder), 24 - Z);
    n = 32;
    do {
        REPEAT8(
            if (isequal(a, target)) goto solution;
#if 0
            a = _mm_add_epi8(a, back);
            a = skim<PERIOD2-1>(a, b, gDwordAdder); // don't set b above
            a = _mm_add_epi8(a, front);
#else
            a = _mm_add_epi32(a, b);            // Use the shortcut
#endif
            cycles += PERIOD2 * CYCLES_PER_BLOCK;
        )
    } while (--n);
    goto nosolution;

    //
    // Jump here when a solution is found. If this solution takes fewer
    // cycles than the incumbent solution, replace it. Use double-checked
    // lock pattern to prevent two threads from updating the incumbent
    // at the same time.
    //
solution:
    if (cycles < gIncumbent) {
        pthread_mutex_lock(&gMutex);
        if (cycles < gIncumbent)
            gIncumbent = cycles;
        pthread_mutex_unlock(&gMutex);
    }

nosolution:
    pthread_exit(NULL);
}
