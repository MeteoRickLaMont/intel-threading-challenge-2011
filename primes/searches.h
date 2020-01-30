#include <stdint.h>             // For uint64_t
#include <pthread.h>            // For pthread_barrier_t
#include "common.h"

//
// Exported globals
//
extern int gNSearchThreads;
extern pthread_barrier_t gBarrierSearch;

//
// Exported functions
//
// At one point there were 5 search strategies here. A function pointer
// was used to select the one anticpiated to be the fastest based on the
// command-line inputs. Eventually "stride" became the fastest strategy
// for all inputs. I keep "power" around for testing and because it's
// a unique approach.
//
extern void strategystride(int tnum);
extern void strategypower(int tnum);    // unused
