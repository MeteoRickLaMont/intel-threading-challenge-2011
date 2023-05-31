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

// #define SINGLETHREAD
// #define DEBUG

const int MAXTHREADS = 80;

// If true, every 10 turns prune duplicate board positions from set.
const bool REMOVE_REPETITIONS = true;

#include <bits/stdtr1c++.h>
#include <sys/time.h>
#include "stopwatch.h"
#include "board.h"
#include "parser.h"
#include "priority_queue.h"

#ifdef STATS
Stopwatch tGlobal("Global Time");
#endif

static BoardStats gMainThread;
#ifndef SINGLETHREAD
static BoardStats gThreads[MAXTHREADS];
#endif

//
// File globals. These parameters are common to all positions.
//
static std::atomic<const char *> gIncumbent{nullptr};
static FILE *gFout;			// Output file

struct Move {
    Move() = default;
    Move(std::shared_ptr<Position> from, int d, int s = 0) :
	pos(from), dir(d), score(s)
    {}

    std::shared_ptr<Position> pos;
    int dir;
    int score;
};

// concurrent_priority_queue puts the highest value first,
// so reverse the logic here to select the lowest payoff.
struct CLessScore {
    bool operator()(const Move &lhs, const Move &rhs) const {
	return rhs.score < lhs.score;
    }
};

priority_queue<Move, CLessScore> *gMoveQueue;

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
    Move move;
    for (;;) {
	// Choose best available move from priority queue
	if (!gMoveQueue->try_pop(move))
	{
	    return NULL;	// No moves available. We're done.
	}
#ifdef STATS
	++data->npositions;
#endif

	// Apply move and advance board to next generation
	auto next = std::shared_ptr<Position>(move.pos->nextgen(*data, move.dir));

#ifdef DEBUG
	// Debugging
	printf("Considering position:\n");
	next->print();
#endif

	// If the best available move is mathematically eliminated from
	// beating the incumbent, we're done.
	if (gIncumbent.load() && next->length() + next->distance(0) >= strlen(gIncumbent.load())) {
	    fprintf(stderr, "Move %s + %d has score %d, length %d + distance %d >= %s\n",
		move.pos->getMoves().c_str(), move.dir, move.score, next->length(), next->distance(0), gIncumbent.load());
	    return NULL;
	}

	// Find legal moves
	std::string dirs = next->legalMoves(*data);
	for (const char &c : dirs) {
	    // Score legal moves
	    int dist = next->distance(c);

	    // If one is a winner, see if it's better than incumbent
	    if (dist == 0 && (!gIncumbent.load() || next->length() + 1 < strlen(gIncumbent.load()))) {
		std::string winner = next->getMoves();
		winner.push_back(c);
		const char *other = strdup(winner.c_str());
		while (other && (!gIncumbent.load() || strlen(other) < strlen(gIncumbent.load()))) {
		    fprintf(stderr, "Installing new incumbent %s\n", other);
		    other = gIncumbent.exchange(other);
		}
	    }
	    else {
		// Otherwise, add legal move to priority queue
		gMoveQueue->push(Move(next, c, next->length() + 3 * dist));
	    }
	}
    }

    return NULL;
}

inline double elapsed(timeval &then)
{
    timeval now;
    gettimeofday(&now, 0);
    return now.tv_sec - then.tv_sec + (now.tv_usec - then.tv_usec)/1000000.;
}

int main(int argc, char **argv)
{
    static timeval begin;
    gettimeofday(&begin, 0);
    TIMER_START(tGlobal);

    //
    // Parse command line. Open input and output files.
    //
    if (argc != 3) {
	fprintf(stderr, "Usage: %s input.txt output.txt\n", argv[0]);
	return -1;
    }
    gFout = fopen(argv[2], "w");
    if (!gFout) {
	perror(argv[2]);
	return -1;
    }

    //
    // Load initial position from input file
    //
    std::shared_ptr<Position> initial = std::make_shared<Position>();
    {
	TIMER_START(tLoad);
	Parser parse(argv[1]);
	initial.reset(parse.loadInitial());
	TIMER_STOP(tLoad);
    }

#ifdef DEBUG
    printf("Initial position:\n");
    initial->print();
#endif

    //
    // Seed priority queue with legal moves from initial position.
    //
    gMoveQueue = new priority_queue<Move, CLessScore>();
    std::string dirs = initial->legalMoves(gMainThread);
    for (const char &c : dirs) {
	// Score legal moves
	int dist = initial->distance(c);

	// If one is a winner, report and exit
	if (dist == 0) {
	    initial->output(gMainThread, gFout, c);
	    fclose(gFout);
	}

	// Otherwise, add legal moves to priority queue
	gMoveQueue->push(Move(initial, c, dist));
    }

#ifdef SINGLETHREAD
    threadsearch(&gMainThread);
#else
    //
    // Start threads
    //
    pthread_t pthreads[MAXTHREADS];
    int nthreads = sysconf(_SC_NPROCESSORS_CONF);
    if (nthreads > MAXTHREADS) nthreads = MAXTHREADS;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    int i;
    for (i = 0; i < nthreads; ++i)
	if (pthread_create(pthreads + i, &attr, threadsearch, (void *)(gThreads + i)) != 0)
	    fprintf(stderr, "failed to create thread\n");

    //
    // Wait for all threads to exit
    //
    pthread_attr_destroy(&attr);
    for (i = 0; i < nthreads; ++i)
	pthread_join(pthreads[i], NULL);
#endif

    if (gIncumbent.load())
	Position::output(gMainThread, gFout, gIncumbent.load());
    else
	fprintf(gFout, "No solution found\n");
    fclose(gFout);

    //
    // Report stats and exit
    //
    double msecs = elapsed(begin) * 1000;
    fprintf(stderr, "Elapsed time: %g milliseconds\n", msecs);

#ifdef STATS
    tGlobal.stop();
    tGlobal.show();
    tLoad.show();
    fprintf(stderr, "Main thread examined %ld positions\n", gMainThread.npositions);
    gMainThread.tNextGen.show();
    gMainThread.tLegalMoves.show();
    gMainThread.tOutput.show();
#ifndef SINGLETHREAD
    for (i = 0; i < nthreads; ++i) {
	fprintf(stderr, "Thread #%d examined %ld positions\n", i, gThreads[i].npositions);
	gThreads[i].tNextGen.show();
	gThreads[i].tLegalMoves.show();
	gThreads[i].tOutput.show();
    }
#endif
#endif

    return 0;
}
