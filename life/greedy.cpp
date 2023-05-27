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

// #define DEBUG

const int MAXTHREADS = 80;

#include <bits/stdtr1c++.h>
#include <sys/time.h>		// For gettimeofday()
#include <unistd.h>
#include <new>
#include "stopwatch.h"
#include "board.h"
#include "parser.h"
#include "priority_queue.h"

#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_constructive_interference_size;
    using std::hardware_destructive_interference_size;
#else
    // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
    constexpr std::size_t hardware_constructive_interference_size = 64;
    constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

#ifdef STATS
Stopwatch tGlobal("Global Time");
Stopwatch tSeed("Seed queue");
Stopwatch tThread("Start threads");
Stopwatch tPart1("main");
#endif

//
// File globals. These parameters are common to all positions.
//
static BoardStats gMainThread;
static BoardStats gThreads[MAXTHREADS];
struct aligned_pthread_t {
    alignas(hardware_destructive_interference_size) pthread_t t;
};
static aligned_pthread_t pthreads[MAXTHREADS];
static pthread_attr_t pthreadattr;

static std::atomic<bool> gFound{false};	// true when solution found
static std::atomic<int> gInFlight{0};	// number of threads working on a Position
static timeval begin;
static int nthreads = -1;
static float weight = -1.0;
static FILE *gFout;		// Output file

struct Move {
    Move() = default;
    Move(Position *from, int d, float s = 0) :
	pos(from), dir(d), score(s)
    {}

    Position *pos;
    int dir;
    float score;
};

// concurrent_priority_queue puts the highest value first,
// so reverse the logic here to select the lowest payoff.
struct CLessScore {
    bool operator()(const Move &lhs, const Move &rhs) const {
	return rhs.score < lhs.score;
    }
};

// Make this a pointer so that it won't be destructed when one thread
// calls exit(). Otherwise, the other threads that are still working
// could have memory errors during program shutdown.
priority_queue<Move, CLessScore> *gMoveQueue;

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
    fprintf(stderr, "Main thread examined %ld positions\n", gMainThread.npositions);
    gMainThread.tPush.show();
    gMainThread.tPop.show();
    gMainThread.tNextGen.show();
    gMainThread.tLegalMoves.show();
    gMainThread.tOutput.show();
    gMainThread.tPart2.show();
    for (int i = 0; i < nthreads; ++i) {
	fprintf(stderr, "Thread #%d examined %ld positions\n", i, gThreads[i].npositions);
	gThreads[i].tPush.show();
	gThreads[i].tPop.show();
	gThreads[i].tNextGen.show();
	gThreads[i].tLegalMoves.show();
	gThreads[i].tOutput.show();
	if (gThreads[i].tPart2.isrunning())
	    gThreads[i].tPart2.stop();
	gThreads[i].tPart2.show();
    }
#endif
}

//
// Main entry point for a thread.
//     Choose best available move from priority queue
//     Apply move to advance board to next generation
//     Find and score legal moves
//     If one is a winner, report and exit
//     Otherwise, add legal moves to priority queue
//     Repeat
//
static void *threadsearch(void *d)
{
    BoardStats *data = (BoardStats *)d;
    TIMER_START(data->tPart2);
    Move move;
    for (;;) {
	// Choose best available move from priority queue
	TIMER_START(data->tPop);
	if (gMoveQueue->try_pop(move)) {
	    TIMER_STOP(data->tPop);
	    ++gInFlight;
	}
	else {
	    TIMER_STOP(data->tPop);
	    if (gInFlight)
		continue;
	    TIMER_STOP(data->tPart2);
	    return NULL;
	}
#ifdef STATS
	++data->npositions;
#endif

	// Apply move and advance board to next generation
	Position *next = move.pos->nextgen(data, move.dir);

#ifdef DEBUG
	// Debugging
	printf("Considering position:\n");
	next->print();
#endif

	// Find legal moves
	std::vector<int> dirs = next->legalMoves(data);
	for (auto it = dirs.begin(); it != dirs.end(); ++it) {
	    // Score legal moves
	    int dist = next->distance(*it);

	    // If one is a winner, report and exit
	    if (dist == 0) {
		if (gFound.exchange(true))
		    return NULL;	// Already another winner
		next->output(data, gFout, *it);
		TIMER_STOP(data->tPart2);
		print_stats();
		exit(0);
	    }

	    // Otherwise, add legal moves to priority queue
	    TIMER_START(data->tPush);
	    gMoveQueue->push(Move(next, *it, next->length() + weight * dist));
	    TIMER_STOP(data->tPush);
	}
	--gInFlight;
    }

    TIMER_STOP(data->tPart2);
    return NULL;
}

//
// Start threads
//
static void *startthreads(void *d)
{
    TIMER_START(tThread);
    for (int i = 1; i < nthreads; ++i)
	pthread_create(&pthreads[i].t, &pthreadattr,
	    threadsearch, (void *)(gThreads + i));
    TIMER_STOP(tThread);
    pthread_attr_destroy(&pthreadattr);
    return threadsearch(d);
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
    while ((opt = getopt(argc, argv, "j:x:")) != -1)
	switch (opt) {
	case 'j': nthreads = atoi(optarg); break;
	case 'x': weight = atof(optarg); break;
	default:
	    goto usage;
	}

    if (argc <= optind || argc - optind > 2) {
usage:
	fprintf(stderr, "Usage: %s [-j njobs] [-x weight] input.txt [output.txt]\n", argv[0]);
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
    gMoveQueue = new priority_queue<Move, CLessScore>();
    ++gInFlight;	// Main thread while generating seed positions

    //
    // Load initial position from input file
    //
    Position *initial;
    {
	Parser parse(argv[optind]);

	//
	// Decide number of threads
	//
	size_t ncells = parse.getTotalCells();
	if (nthreads < 0) {
	    if (ncells < 10000)
		nthreads = 0;
	    else
		nthreads = sysconf(_SC_NPROCESSORS_CONF) / 2 - 1; // Assumes hyperthreading. -1 for main thread
	    if (nthreads > MAXTHREADS) nthreads = MAXTHREADS;
	}

	//
	// Start one thread to start other threads
	//
	if (nthreads > 0)  {
	    pthread_attr_init(&pthreadattr);
	    pthread_attr_setdetachstate(&pthreadattr, PTHREAD_CREATE_JOINABLE);
	    TIMER_START(tThread);
	    pthread_create(&pthreads[0].t, &pthreadattr,
		startthreads, (void *)gThreads);
	    TIMER_STOP(tThread);
	}

	//
	// Decide heuristic weight
	//
	if (weight < 0) {
            weight = 5.0;
	    // if (ncells > 10000)
            //     weight = 10.2;
	}

	TIMER_START(tLoad);
	initial = parse.loadInitial();
	TIMER_STOP(tLoad);
    }

#ifdef DEBUG
    printf("Initial position:\n");
    initial->print();
#endif

    //
    // Seed priority queue with legal moves from initial position.
    //
    TIMER_START(tSeed);
    std::vector<int> dirs = initial->legalMoves(&gMainThread);
    for (auto it = dirs.begin(); it != dirs.end(); ++it) {
	// Score legal moves
	int dist = initial->distance(*it);

	// If one is a winner, report and exit
	if (dist == 0) {
	    initial->output(&gMainThread, gFout, *it);
	    print_stats();
	    exit(0);
	}

	// Otherwise, add legal moves to priority queue
	TIMER_START(gMainThread.tPush);
	gMoveQueue->push(Move(initial, *it, dist));
	TIMER_STOP(gMainThread.tPush);
    }
    --gInFlight;
    TIMER_STOP(tSeed);

    //
    // Use main thread to search,
    // regardless of whether nthreads > 0
    //
    TIMER_STOP(tPart1);
    threadsearch(&gMainThread);
    TIMER_START(tPart1);

    //
    // Wait for all other threads to exit
    //
    for (int i = 0; i < nthreads; ++i)
	pthread_join(pthreads[i].t, NULL);

    //
    // If all threads exited normally then no solution was found.
    //
    fprintf(gFout, "No solution found\n");
    print_stats();

    return 0;
}
