#include <stdio.h>      // For FILE
#include <stdint.h>     // For uint64_t
#include <pthread.h>    // For pthread_mutex_t
#include "common.h"     // For tSum

class PerfectPower;

//
// Buffer output before flushing to the file.
// The order of lines in the file is insignificant.
// Whichever thread can acquire the mutex is free to write whole lines.
//
// Writing to output file goes through this class.
// It buffers characters and flushes them to the file only when the mutex
// is available.
//
class OutputFile {
public:
    OutputFile();
    ~OutputFile();

    void printresult(const tSum *firstprime, const tSum *lastprime,
        const PerfectPower *power);
    static FILE *fp;                    // Shared output file
    static bool noresults;              // True if no solutions found

private:
    static const size_t BUFSIZE = (8*1024 - 3*sizeof(void *));
    struct OutputBuffer {
        OutputBuffer *next;
        char buf[BUFSIZE];
    };

    OutputBuffer *head, *tail, *freelist;
    char *s;            // Add characters to buffer here
    int avail;          // Number of bytes remaining in buffer at s
    static pthread_mutex_t fpMutex;

    OutputBuffer *bufalloc();
    void bufflush();
};
