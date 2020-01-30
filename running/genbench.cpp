#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

union sse {
    int32_t dwords[4];
    uint8_t bytes[16];
};

int main(int argc, char **argv)
{
    int i, j;
    int clearbits = 0;

    if (argc > 1)
        clearbits = atoi(argv[1]);
    if (argc > 2)
        srand(atoi(argv[2]));
    else
        srand(1234);

    sse a, b, d;
    uint64_t cycles = (uint64_t)rand();
    for (i = 0; i < 4; ++i) {
        b.dwords[i] = rand();
        d.dwords[i] = rand();
    }

    if (clearbits > 0) {
        uint32_t mask = (1 << clearbits) - 1;
        a = d;
        for (j = 0; j < 36; ++j)
            for (i = 0; i < 16; ++i)
                a.bytes[i] += b.bytes[i];
        for (i = 0; i < 4; ++i)
            d.dwords[i] -= a.dwords[i] & mask;
    }

    for (i = 0; i < 4; ++i)
        a.dwords[i] = 0;

    printf("# Anticipated cycles = %lu (could be fewer)\n", cycles);
    while (cycles--) {
        if (cycles % 37)
            for (i = 0; i < 16; ++i)
                a.bytes[i] -= b.bytes[i];
        else
            for (i = 0; i < 4; ++i)
                a.dwords[i] -= d.dwords[i];
    }

    printf("./running ");
    for (i = 0; i < 4; ++i)
        printf("%08x", a.dwords[i]);
    putchar(' ');
    for (i = 0; i < 4; ++i)
        printf("%08x", b.dwords[i]);
    putchar(' ');
    for (i = 0; i < 4; ++i)
        printf("%08x", d.dwords[i]);
    putchar('\n');

    return 0;
}
