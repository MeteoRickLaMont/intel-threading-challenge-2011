//
// Intel Threading Challenge 2011
// Maze of Life
// Rick LaMont <lamont@dotcsw.com>
// Compile with make
//

//
// Maze of Life is a puzzle/game devised by Andrea Gilbert
// (http://www.clickmazes.com/). Starting with a Game of Life initial
// setup and the standard rules to compute successive generations,
// one cell in the initial setup is designated the intelligent cell and
// one point on the grid is designated as the goal. Between generations
// of the Game of Life, the intelligent cell is allowed to move to an
// empty grid point surrounding the current location of the cell or the
// intelligent cell can remain at the current grid point. Computation
// of the next generation of dead and alive cells is done with the
// inclusion of the intelligent cell as any other cell. The purpose of
// the game is to keep the intelligent cell alive and move it to reach
// the goal grid point. If the intelligent cell dies without reaching
// the goal grid point, the game is over.
// 
// Problem Description:
// Write a threaded code to find a solution to an input instance of the Maze
// of Life. Input to the application will be from a text file listed first on
// the command line. The file will contain the initial configuration of
// the grid for the start of the game. Output of the application will be
// the path of the intelligent cell from the initial grid point to the
// goal grid point and will be stored in the second file listed on the
// command line. The path must ensure that the intelligent cell remains
// alive from start to reaching the goal as the cell interacts with the
// other alive cells from one generation to the next.
//

const int MAXTHREADS = 80;

#include <bits/stdtr1c++.h>
#include <sys/time.h>           // For gettimeofday()
#include <unistd.h>
#include <tbb/tbb.h>
#include "stopwatch.h"
#include "board.h"
#include "parser.h"

//
// File globals. These parameters are common to all positions.
//
#ifdef STATS
static Stopwatch tGlobal("Global Time");
static Stopwatch tSeed("Seed queue");
static Stopwatch tThread("Start threads");
static Stopwatch tPart1("main");
#endif

struct ThreadLocal {
    alignas(hardware_destructive_interference_size) BoardStats stats;
};
static std::array<ThreadLocal, MAXTHREADS> gThreads;

static std::atomic_bool gFound{false}; // true when solution found
#ifdef STATS
static std::atomic_int gCulled{0};
#endif
static timeval begin;
static int nthreads = -1;
static FILE *gFout;             // Output file

static tbb::concurrent_vector<Move> gCurrBeam;
static std::vector<Move> gPrevBeam;
static int maxbeams = 2000;

inline double elapsed(timeval &then)
{
    timeval now;
    gettimeofday(&now, 0);
    return now.tv_sec - then.tv_sec + (now.tv_usec - then.tv_usec)/1000000.;
}

static void print_stats()
{
    //
    // Report running time
    //
    double msecs = elapsed(begin) * 1000;
    fprintf(stderr, "Elapsed time: %g milliseconds\n", msecs);

    //
    // Additional statistics if requested
    //
#ifdef STATS
    fprintf(stderr, "Culled\t\t%d\n", gCulled.load());
    tGlobal.stop();
    tGlobal.show();
    tLoad.show();
    tSeed.show();
    if (tThread.isrunning())
        tThread.stop();
    tThread.show();
    if (tPart1.isrunning())
        tPart1.stop();
    tPart1.show();
    for (int i = 0; i < nthreads; ++i) {
        fprintf(stderr, "Thread #%d examined %ld positions\n", i, gThreads[i].stats.npositions);
        gThreads[i].stats.tPush.show();
        gThreads[i].stats.tPop.show();
        gThreads[i].stats.tNextGen.show();
        gThreads[i].stats.tLegalMoves.show();
        gThreads[i].stats.tOutput.show();
        if (gThreads[i].stats.tPart2.isrunning())
            gThreads[i].stats.tPart2.stop();
        gThreads[i].stats.tPart2.show();
    }
#endif
}

int main(int argc, char **argv)
{
    gettimeofday(&begin, 0);
    TIMER_START(tGlobal);
    TIMER_START(tPart1);

    //
    // Parse command line. Open input and output files.
    //
    int opt;
    while ((opt = getopt(argc, argv, "j:w:")) != -1)
        switch (opt) {
        case 'j':
            nthreads = atoi(optarg);
            break;
        case 'w':
            maxbeams = atoi(optarg);
            break;
        default:
            goto usage;
        }

    if (argc <= optind || argc - optind > 2) {
usage:
        fprintf(stderr, "Usage: %s [-j njobs] [-w maxbeams] input.txt [output.txt]\n", argv[0]);
        return -1;
    }
    if (argc - optind < 2)
        gFout = stdout;
    else {
        gFout = fopen(argv[optind + 1], "w");
        if (!gFout) {
            perror(argv[optind + 1]);
            return -1;
        }
    }

    //
    // Prepare shared data for threads when they warm up.
    //
    gPrevBeam.reserve(9 * maxbeams);
    gCurrBeam.reserve(9 * maxbeams);

    //
    // Load initial position from input file
    //
    PositionPtr initial;
    Parser parse(argv[optind]);

    TIMER_START(tLoad);
    initial = (PositionPtr)(parse.loadInitial());
    TIMER_STOP(tLoad);

#ifdef DEBUG
    printf("Initial position:\n");
    initial->print();
#endif

    //
    // Seed previous beam with legal moves from initial position.
    //
    TIMER_START(tSeed);
    std::string dirs = initial->legalDirs(gThreads[0].stats);
    for (const char c : dirs) {
        // Score legal moves
        int dist = initial->distance(c);

        // If one is a winner, report and exit
        if (dist == 0) {
            initial->output(gThreads[0].stats, gFout, c);
            print_stats();
            exit(0);
        }

        // Otherwise, add legal moves to previous beam
        TIMER_START(gThreads[0].stats.tPush);
        gPrevBeam.emplace_back(initial, c, dist);
        TIMER_STOP(gThreads[0].stats.tPush);
    }
    TIMER_STOP(tSeed);

    // Exit when solution found or beam dries up (no solution)
    while (!gPrevBeam.empty()) {
        // Inner loops: Generate current positions from all those in previous beam
        tbb::parallel_for((size_t)0, gPrevBeam.size(), [](size_t i){
            const Move &move = gPrevBeam[i];
            // int worker_index = tbb::task_arena::current_thread_index();
            // Goes from 0 to tbb::this_task_arena::max_concurrency() - 1
            PositionPtr next = (PositionPtr)(move.pos->nextgen(gThreads[0].stats, move.dir));
            if (move.dir == '0' && *next == *move.pos) {
#ifdef STATS
                ++gCulled;
#endif
#ifdef DEBUG
                printf("Culled this position:\n");
                next->print();
#endif
            }
            else {
#ifdef DEBUG
                // Debugging
                printf("Considering position:\n");
                next->print();
#endif
                // Find legal moves
                std::string dirs = next->legalDirs(gThreads[0].stats);
                for (const char c : dirs) {
                    // Score legal moves
                    int dist = next->distance(c);

                    if (dist == 0) {
                        // If one is a winner, report it
                        if (!gFound.exchange(true))
                            next->output(gThreads[0].stats, gFout, c);
                    }
                    else {
                        // Otherwise, add legal moves to current beam
                        TIMER_START(gThreads[0].stats.tPush);
                        gCurrBeam.emplace_back(next, c, dist);
                        TIMER_STOP(gThreads[0].stats.tPush);
                    }
                }
            }
        });

        // If solution was found exit now
        if (gFound)
            break;

        // Current beam becomes previous beam
        gPrevBeam = std::vector<Move>(gCurrBeam.begin(), gCurrBeam.end());
        gCurrBeam.clear();

        // Keep only the best maxbeams moves
        if (gPrevBeam.size() > maxbeams) {
            nth_element(gPrevBeam.begin(), std::next(gPrevBeam.begin(), maxbeams), gPrevBeam.end(),
                [](const Move &a, const Move &b) { return a.score < b.score; });
            gPrevBeam.resize(maxbeams);
        }
    }

    if (!gFound)
        fputs("No solution found\n", gFout);
    print_stats();

    return 0;
}
