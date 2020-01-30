#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>             // For uint64_t

//
// Types
//
typedef uint64_t tSum;          // the sum of a series of consecutive primes
typedef uint64_t tPower;        // a perfect power x^k (for x and k >= 2)

//
// Return the base 2 logarithm of v (rounded down to nearest integer).
//
inline int floor_log2(const int v)
{
    int r;
    asm volatile(" bsrl %1, %0": "=r"(r): "rm"(v));
    return r;
}

inline int floor_log2(const uint32_t v)
{
    int r;
    asm volatile(" bsrl %1, %0": "=r"(r): "rm"(v));
    return r;
}

inline long floor_log2(const uint64_t v)
{
    long r;
    asm volatile(" bsrq %1, %0": "=r"(r): "rm"(v));
    return r;
}

inline int ceil_log2(const int v)
{
    int r;
    asm volatile(" bsrl %1, %0": "=r"(r): "rm"(v));
    return v & (v - 1) ? r + 1 : r;
}

inline long ceil_log2(const uint64_t v)
{
    long r;
    asm volatile(" bsrq %1, %0": "=r"(r): "rm"(v));
    return v & (v - 1) ? r + 1 : r;
}

//
// Return the number of LSBs that are zero (following the lowest set bit).
//
inline int trailing_zeros(const uint32_t v)
{
    int r;
    asm volatile(" bsfl %1, %0": "=r"(r): "rm"(v));
    return r;
}

//
// Return base^exp
//
inline uint64_t ipow(uint64_t base, uint64_t exp)
{
    register uint64_t n = base;
    register uint64_t mask = 1UL << floor_log2(exp);
    while (mask >>= 1) {
        n *= n;
        if (exp & mask)
            n *= base;
    }
    return n;
}

//
// Return base^exp % modulus
//
inline uint32_t ipowmod(uint32_t base, uint32_t exp, uint32_t modulus)
{
    register uint64_t n = base;
    register uint32_t mask = 1U << floor_log2(exp);
    while (mask >>= 1) {
        n = (n * n) % modulus;
        if (exp & mask)
            n = (n * base) % modulus;
    }
    return (uint32_t)n;
}

#endif
