//
// Intel Threading Challenge 2011
// Maze of Life
// Rick LaMont <lamont@dotcsw.com>
// Compile with: g++ -O3 -o mazeoflife fixed.cpp -lpthread
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

#define STATS
// #define SINGLETHREAD
// #define OPTIMAL
// #define DEBUG

const int MAXTHREADS = 80;

// If true, every 10 turns prune duplicate board positions from set.
const bool REMOVE_REPETITIONS = true;

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <bits/stdtr1c++.h>
#include <sys/time.h>
#include <emmintrin.h>
#include <memory>
#ifdef STATS
#include <chrono>
using namespace std::chrono;
#endif
#ifdef SINGLETHREAD
#include <queue>
#else
// #define TBB_PREVIEW_CONCURRENT_PRIORITY_QUEUE 1
#include "tbb/include/tbb/concurrent_priority_queue.h"
#endif

#ifdef STATS
class Stopwatch {
public:
    explicit Stopwatch(bool start_now = false) :
	m_name("Stopwatch"),
	m_start(system_clock::time_point::min()),
	m_elapsed(microseconds::zero())
    {
	if (start_now)
	    start();
    }

    explicit Stopwatch(const char *name, bool start_now = false) :
	m_name(name),
	m_start(system_clock::time_point::min()),
	m_elapsed(microseconds::zero())
    {
	if (start_now)
	    start();
    }

    ~Stopwatch()
    {
	if (isrunning())
	    stop();
    }

    bool isrunning() const
    {
	return (m_start != system_clock::time_point::min());
    }

    void clear()
    {
	m_start = system_clock::time_point::min();
    }

    void start()
    {
	m_start = system_clock::now();
    }

    void stop() 
    {
	m_elapsed += duration_cast<microseconds>(system_clock::now() - m_start);
	m_start = system_clock::time_point::min();
    }

    void show()
    {
	if (m_elapsed != microseconds::zero())
	    fprintf(stderr, "%-20s: %8ld microseconds\n", m_name.c_str(), m_elapsed.count());
    }

private:
    std::string m_name;
    system_clock::time_point m_start;
    microseconds m_elapsed;
};

Stopwatch tGlobal("Global Time");
Stopwatch tLoad("Load");

#define TIMER_START(t) (t).start()
#define TIMER_STOP(t) (t).stop()
#else
#define TIMER_START(t)
#define TIMER_STOP(t)
#endif

#define INLINE   inline __attribute__ ((always_inline))

#define ALIGNED __attribute__ ((aligned(16)))


struct ThreadData {
    ThreadData()
#ifdef STATS
    : npositions(0), tNextGen("    Next Gen"), tLegalMoves("    Legal Moves"), tOutput("    Output")
#endif
    {}

#ifdef STATS
    long npositions;
    Stopwatch tNextGen;
    Stopwatch tLegalMoves;
    Stopwatch tOutput;
#endif
};

static ThreadData gMainThread;
#ifndef SINGLETHREAD
static ThreadData gThreads[MAXTHREADS];
#endif

//
// Internal representation of Game of Life grid. Store bitmaps for every
// cell, living or dead, that's in bounds. Representation is LSB first,
// the leftmost cell is 1. To shift right on-screen shift the bitmap left.
// Note that major x coordinates are always factors of BMPSIZE (32 or 64).
//
// Sort cells by increasing y values. Subsort by increasing x (left to
// right).
//
typedef int32_t tPos;
typedef uint64_t tBmp;

const int BMPSIZE = ((int)sizeof(tBmp) * CHAR_BIT);
const tBmp XBMPMASK = BMPSIZE - 1;
const tBmp XCELLMASK = ~XBMPMASK;

//
// File globals. These parameters are common to all positions.
//
static tPos gMaxX, gMaxY, gRightx;
static size_t gBoardHeight, gBoardWidth;
static tBmp gRightmask;		// To clip cells to right edge
static tPos gGoalX, gGoalY;
pthread_mutex_t gBlockMutex = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<bool> gDone (false);
FILE *gFout;			// Output file

//
// Like a chess program, represent a board position. It consists of the
// locations of living cells, the intelligent cell, and the history of
// moves that led to this position.
//
class Position {
public:
    Position();
    Position(const Position &rhs);

    void load(const char *const fname);
    void output(ThreadData *t, FILE *fp, const int dir);
    void print();

    std::shared_ptr<Position> nextgen(ThreadData *t, const int dir);
    std::vector<int> legalMoves(ThreadData *t) const;
    uint32_t length() const { return fMoves.length(); }
    uint32_t distance(const int dir) const;

private:
    std::vector<tBmp> fCells;		// Game of Life board

    //
    // Specific to maze of life extension
    //
    tPos fIntelligentX, fIntelligentY;  // Current position of smart cell
    std::string fMoves;			// How it arrived here

    inline void ADD2(tBmp &b0, tBmp &b1, const tBmp a0, const tBmp a1) const;
    inline void ADD3(tBmp &b0, tBmp &b1,
	const tBmp a0, const tBmp a1, const tBmp a2) const;
    inline tBmp ROL(const tBmp a0, const tBmp a1) const;
    inline tBmp ROR(const tBmp a0, const tBmp a1) const;

    void scanCoords(const char *&f, tPos *y, tPos *x) const;
    void printFilledColumn(tPos x, tPos y, tBmp b) const;
};

Position::Position()
{
}

Position::Position(const Position &rhs) :
    fIntelligentX(rhs.fIntelligentX),
    fIntelligentY(rhs.fIntelligentY),
    fMoves(rhs.fMoves),
    fCells(rhs.fCells)
{
}

//
// Input Description: The input to the program will be from a
// text file named on the command line of the application. The
// first line will be two integers denoting the dimensions
// of the grid, number of rows and number of columns. For
// consistency and shared point of reference, the upper left
// corner of the grid will be at the (1 1) location. The second
// line of the file will be the coordinates of the goal grid
// point. The third line will be the initial coordinates of the
// intelligent cell. Remaining lines will be the coordinates of
// the remainder of the "alive" cells in the initial state of
// the game. Each line of the file will contain 10 integers
// with at least one space between each. These represent
// coordinate pairs of five alive nodes. A tag of two zeroes
// (0 0) denotes the end of the live cell coordinates. The
// exception for 10 integers per line will last line of the
// file which may have fewer than 5 coordinates and the tag.
//

void Position::load(const char *const fname)
{
    TIMER_START(tLoad);
    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
	perror(fname);
	exit(-1);
    }

    /* Get the size of the file. */
    struct stat s;
    int status = fstat(fd, &s);
    int size = s.st_size;

    const char *f = (char *)mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (!f) {
	perror(fname);
	exit(-1);
    }

    scanCoords(f, &gMaxY, &gMaxX);
    --gMaxY;
    --gMaxX;
    scanCoords(f, &gGoalY, &gGoalX);
    --gGoalY;
    --gGoalX;

    // Clear board
    gBoardHeight = gMaxY + 1;
    gBoardWidth = gMaxX / BMPSIZE + 1;
    gRightx = gMaxX & XCELLMASK;
    gRightmask = ~((tBmp)0) >> (BMPSIZE - ((gMaxX + 1) & XBMPMASK));
    fCells.resize(gBoardHeight * gBoardWidth, 0);

    // Populate board
    tPos x, y;
    scanCoords(f, &y, &x);
    fIntelligentY = y - 1;
    fIntelligentX = x - 1;
    while (x != 0 || y != 0) {
	fCells[(y - 1) * gBoardWidth + ((x - 1) / BMPSIZE)] |= 1ULL << ((x - 1) & XBMPMASK);
	scanCoords(f, &y, &x);
    }
    munmap(const_cast<char *>(f), size);
    TIMER_STOP(tLoad);
}

void Position::scanCoords(const char *&f, tPos *y, tPos *x) const
{
    while (isspace(*f))
	++f;
    for (*y = *f++ - '0'; !isspace(*f); ++f)
	*y = *y * 10 + *f - '0';
    while (isspace(*f))
	++f;
    for (*x = *f++ - '0'; !isspace(*f); ++f)
	*x = *x * 10 + *f - '0';
}

// 
// The output to be generated by the application is a list of moves
// executed by the intelligent cell to reach the goal point. Each
// single move will be described by an integer digit from '0' to '8'. A
// move that leaves the intelligent cell on the current grid point
// is represented by '0'; for location changes, the eight surrounding
// cells are numbered consecutively starting in the upper left corner
// (diagonal) with '1' and proceeding clockwise to '8'. (For example,
// a move up (North) would be designated by a '2' and diagonal move down
// and to the right (Southeast) would be designated with a '5'.) Lines
// in the output file will contain 40 digits, no spaces in between,
// with the last line possibly being less than 40 characters. If there
// is no possible solution to the given puzzle, the output file should
// contain a message to state that fact.
// 
void Position::output(ThreadData *t, FILE *fp, const int dir)
{
    TIMER_START(t->tOutput);
    fMoves.push_back('0' + dir);
    for (int i = 0; i < fMoves.size(); i += 40) {
	std::string sub = fMoves.substr(i, 40);
	fprintf(fp, "%s\n", sub.c_str());
    }
    TIMER_STOP(t->tOutput);
}


//
// For debugging, print the current state of the board.
//
void Position::print()
{
    tPos x, y;

    printf("%d x %d board. Intelligent cell (%d, %d). Goal (%d, %d). Moves %s\n",
	gMaxX + 1,
	gMaxY + 1,
	fIntelligentX,
	fIntelligentY,
	gGoalX,
	gGoalY,
	fMoves.c_str());
    tBmp *p;
    for (y = 0, p = &fCells[0]; y < gBoardHeight; ++y) {
	for (x = 0; x <= gMaxX; x += BMPSIZE, ++p)
	    printFilledColumn(x, y, *p);
	putchar('\n');
    }
}

void Position::printFilledColumn(tPos x, tPos y, tBmp b) const
{
    for (int i = 0; i < BMPSIZE && x <= gMaxX; ++i, ++x, b >>= 1) {
	if (y == fIntelligentY && x == fIntelligentX)
	    putchar('I');
	else if (y == gGoalY && x == gGoalX)
	    putchar((b & 1) != 0 ? 'G' : 'g');
	else if ((b & 1) != 0)
	    putchar('*');
	else
	    putchar('.');
    }
}

uint32_t Position::distance(const int dir) const
{
    int x = fIntelligentX;
    int y = fIntelligentY;

    switch (dir) {
    case 0: break;
    case 1: --x; --y; break;
    case 2: --y; break;
    case 3: ++x; --y; break;
    case 4: ++x; break;
    case 5: ++x; ++y; break;
    case 6: ++y; break;
    case 7: --x; ++y; break;
    case 8: --x; break;
    }

    int dx = gGoalX - x;
    int dy = gGoalY - y;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

//
// Add one bit values in parallel.
// a0 and a1 are packed vectors of bits.
// Calculate b = a0 + a1
// Because it takes two bits to represent 1 + 1, let b0 be the lsb
// of the sum and b1 the msb.
//
inline void Position::ADD2(tBmp &b0, tBmp &b1, const tBmp a0, const tBmp a1) const
{
    b1 = a0 & a1;
    b0 = a0 ^ a1;
}

//
// Add one bit values in parallel.
// a0, a1 and a2 are packed vectors of bits.
// Calculate b = a0 + a1 + a2
// Because it takes two bits to represent 2 or 3, let b0 be the lsb
// of the sum and b1 the msb.
//
inline void Position::ADD3(tBmp &b0, tBmp &b1,
    const tBmp a0, const tBmp a1, const tBmp a2) const
{
    tBmp t0, t1;
    ADD2(b0, t0, a0, a1);
    ADD2(b0, t1, b0, a2);
    b1 = t0 | t1;
}

//
// Shift a0 one bit to the left, rotating in the msb of a1 from the right.
//
inline tBmp Position::ROL(const tBmp a0, const tBmp a1) const
{
    return a0 << 1 | a1 >> BMPSIZE-1;
}

//
// Shift a0 one bit to the right, rotating in the lsb of a1 from the left.
//
inline tBmp Position::ROR(const tBmp a0, const tBmp a1) const
{
    return a0 >> 1 | a1 << BMPSIZE-1;
}

//
// Updates fCells to next generation following Game of Life rules.
// Returns false if intelligent cell would die (fCells unnaffected)
// Reference:
// http://dotat.at/prog/life/life.html
// http://dotat.at/prog/life/life.c
// Watch out. His X-coords may be backwards.
//
std::shared_ptr<Position> Position::nextgen(ThreadData *t, const int dir)
{
    TIMER_START(t->tNextGen);
    std::shared_ptr<Position> ret = std::make_shared<Position>(*this);

    ret->fMoves.push_back('0' + dir);

    tPos yold = fIntelligentY;
    tPos xcellold = fIntelligentX & XCELLMASK;
    tBmp xbitold = ~(1ULL << (fIntelligentX & XBMPMASK));

    switch (dir) {
    case 0: break;
    case 1: --ret->fIntelligentX; --ret->fIntelligentY; break;
    case 2: --ret->fIntelligentY; break;
    case 3: ++ret->fIntelligentX; --ret->fIntelligentY; break;
    case 4: ++ret->fIntelligentX; break;
    case 5: ++ret->fIntelligentX; ++ret->fIntelligentY; break;
    case 6: ++ret->fIntelligentY; break;
    case 7: --ret->fIntelligentX; ++ret->fIntelligentY; break;
    case 8: --ret->fIntelligentX; break;
    }

    tPos ynew = ret->fIntelligentY;
    tPos xcellnew = ret->fIntelligentX & XCELLMASK;
    tBmp xbitnew = 1ULL << (ret->fIntelligentX & XBMPMASK);

    //
    // Output next generation to ret->fCells.
    //
    tBmp *p = &ret->fCells[0];
    const tBmp *prev, *curr, *next;
    register tBmp prevmid, currmid, nextmid;
    tBmp rear0, rear1;
    prevmid = currmid = nextmid = rear0 = rear1 = 0;

    //
    // Outer loop scans y from top to bottom.
    // When we're outputting cells on row y:
    //     curr points to old cells on row y
    //     prev points to row y-1
    //     next points to row y+1
    // If any of those input rows are empty then prev, curr and/or next
    // will be NULL.
    //
    prev = 0;
    curr = &fCells[0];
    next = &fCells[gBoardWidth];
    for (tPos ycurr = 0; ycurr <= gMaxY; ++ycurr) {
	if (ycurr == 1)
	    prev = &fCells[0];
	if (ycurr == gMaxY)
	    next = 0;

	//
	// Secondary loop scans x from left to right
	//
	for (tPos xmid = -BMPSIZE; xmid <= gMaxX; xmid += BMPSIZE) {
	    tPos xfore = xmid + BMPSIZE;

	    //
	    // Set bitmaps prevfore, currfore and nextfore to next column to the right.
	    // They are aligned vertically from top to bottom.
	    // Some or all may be empty.
	    //
	    register tBmp prevfore, currfore, nextfore;
	    prevfore = currfore = nextfore = 0;
	    if (xfore <= gMaxX) {
		if (prev)
		    prevfore = *prev++;
		currfore = *curr++;
		if (next)
		    nextfore = *next++;
	    }

	    if (dir != 0) {
		//
		// Clear old intelligent cell
		//
		if (xfore == xcellold) {
		    if (ycurr-1 == yold)
			prevfore &= xbitold;
		    else if (ycurr == yold)
			currfore &= xbitold;
		    else if (ycurr+1 == yold)
			nextfore &= xbitold;
		}

		//
		// Set new intelligent cell
		//
		if (xfore == xcellnew) {
		    if (ycurr-1 == ynew)
			prevfore |= xbitnew;
		    else if (ycurr == ynew)
			currfore |= xbitnew;
		    else if (ycurr+1 == ynew)
			nextfore |= xbitnew;
		}
	    }

	    if (xmid >= 0) {
		//
		// Let count0, count1, and count2 be bits 0-2 of the
		// neighbor counts for the set of cells under consideration.
		// Don't bother calculating bit 3 because it's only set
		// when a cell has 8 neighbors. The cell will die whenever
		// bit 2 gets set (4 or more neighbors) so catch large
		// counts at that point.
		//
		tBmp mid0, mid1, fore0, fore1;
		tBmp count0, count1, count2a, count2b;
		ADD3(fore0, fore1, ROR(prevmid, prevfore),
				   ROR(currmid, currfore),
				   ROR(nextmid, nextfore));
		ADD2(mid0, mid1, prevmid, nextmid);
		ADD3(count0, count1, fore0, mid0, rear0);
		ADD3(count1, count2a, count1, fore1, rear1);
		ADD2(count1, count2b, count1, mid1);
#if 0
    printf("(%d, %d)\n%08x %08x\n%08x %08x\n%08x %08x\n", xmid, ycurr, prevmid, prevfore, currmid, currfore, nextmid, nextfore);
    printf("fore0 %08x fore1 %08x mid0 %08x mid1 %08x\n", fore0, fore1, mid0, mid1);
    printf("count0 %08x count1 %08x count2a %08x count2b %08x\n", count0, count1, count2a, count2b);
    printf("rear0 %08x rear1 %08x\n", rear0, rear1);
#endif

		//
		// Let bmp be the new state of the cells. OR the current
		// state of the cell to the neighbor count to handle both
		// the living rule (2 or 3 neighbors) and the spontaneous
		// generation rule (3 neighbors).
		//
		tBmp bmp = (count0 | currmid) & count1 &
		    ~count2a & ~count2b;	// Mask those with 4+ neighbors

		//
		// Clip bmp to right boundary and output it.
		//
		if (xmid == gRightx)
		    bmp &= gRightmask;
		*p++ = bmp;
	    }

	    //
	    // Pre-calculate rear neighbor counts for next iteration.
	    // This way we don't need to keep rear bitmaps around.
	    //
	    ADD3(rear0, rear1, ROL(prevfore, prevmid),
			       ROL(currfore, currmid),
			       ROL(nextfore, nextmid));

	    //
	    // Advance to the right.
	    // Foreward bitmaps become middle bitmaps.
	    //
	    prevmid = prevfore;
	    currmid = currfore;
	    nextmid = nextfore;
	}
    }

    TIMER_STOP(t->tNextGen);
    return ret;
}

//
// Output set of directions (0 through 8) of legal moves for intelligent cell.
// A move is legal if:
// 1. It stays in bounds
// 2. Moves to a cell that is currently unoccupied
// 3. Will survive next generation
//
std::vector<int> Position::legalMoves(ThreadData *t) const
{
    TIMER_START(t->tLegalMoves);
    //
    // Mask out intelligent cell
    // Perform neighbor count
    // Set output bit if both:
    // 1. Cell was unoccupied
    // 2. Will survive if occubied by intelligent cell
    //
    const tBmp *prev, *curr, *next;

    tPos ycurr;
    tPos ylast = 1;				// Previous y value output
    tPos xdiv = fIntelligentX & XCELLMASK;	// Cache intelligent location
    tBmp xmask = ~(1ULL << (fIntelligentX & XBMPMASK));

    std::vector<int> directions;
    directions.reserve(9);
    tPos yi = fIntelligentY;
    tPos yn = fIntelligentY - 1;
    tPos ys = fIntelligentY + 1;
    tPos xw = fIntelligentX - 1;
    tPos xe = fIntelligentX + 1;
    tPos xidiv = fIntelligentX & XCELLMASK;
    tPos xwdiv = xw & XCELLMASK;
    tPos xediv = xe & XCELLMASK;
    tBmp ximask = (1UL << (fIntelligentX & XBMPMASK));
    tBmp xwmask = (1UL << (xw & XBMPMASK));
    tBmp xemask = (1UL << (xe & XBMPMASK));

    tPos ynclamp = (yn < 0) ? 0 : yn;
    tPos ynnclamp = (ynclamp - 1 < 0) ? 0 : ynclamp - 1;
    tPos ysclamp = (ys > gMaxY) ? gMaxY : ys;
    tPos xwclamp = (xw < 0) ? 0 : xw;
    tPos xeeclamp = (xe + 1 > gMaxX) ? gMaxX : xe + 1;
    tPos xwclampdiv = xwclamp & XCELLMASK;
    tPos xeeclampdiv = xeeclamp & XCELLMASK;

    register tBmp prevmid, currmid, nextmid;
    tBmp rear0, rear1;
    prevmid = currmid = nextmid = rear0 = rear1 = 0;

    prev = (ynclamp - 1 < 0) ? 0 : &fCells[(ynclamp - 1) * gBoardWidth];
    curr = (ynclamp < 0) ? 0 : &fCells[ynclamp * gBoardWidth];
    next = &fCells[(ynclamp + 1) * gBoardWidth];
    for (ycurr = ynclamp; ycurr <= ysclamp; ++ycurr) {
	if (ycurr == 1)
	    prev = &fCells[0];
	if (ycurr == gMaxY)
	    next = 0;

	//
	// Secondary loop scans x from left to right
	//
	for (tPos xmid = -BMPSIZE; xmid <= gMaxX; xmid += BMPSIZE) {
	    //
	    // Set bitmaps prevfore, currfore and nextfore.
	    // They are aligned vertically from top to bottom.
	    // Some or all may be empty.
	    //
	    register tBmp prevfore, currfore, nextfore;
	    prevfore = currfore = nextfore = 0;
	    if (xmid + BMPSIZE <= gMaxX) {
		if (prev)
		    prevfore = *prev++;
		currfore = *curr++;
		if (next)
		    nextfore = *next++;
	    }

	    //
	    // Mask out intelligent cell from fore.
	    // It will automatically slide to mid later.
	    //
	    if (xmid + BMPSIZE == xdiv) {
		if (ycurr-1 == fIntelligentY)
		    prevfore &= xmask;
		else if (ycurr == fIntelligentY)
		    currfore &= xmask;
		else if (ycurr+1 == fIntelligentY)
		    nextfore &= xmask;
	    }

	    if (xmid >= 0) {
		//
		// Let count0, count1, and count2 be bits 0-2 of the
		// neighbor counts for the set of cells under consideration.
		// Don't bother calculating bit 3 because it's only set
		// when a cell has 8 neighbors. The cell will die whenever
		// bit 2 gets set (4 or more neighbors) so catch large
		// counts at that point.
		//
		tBmp mid0, mid1, fore0, fore1;
		tBmp count0, count1, count2a, count2b;
		ADD3(fore0, fore1, ROR(prevmid, prevfore),
				   ROR(currmid, currfore),
				   ROR(nextmid, nextfore));
		ADD2(mid0, mid1, prevmid, nextmid);
		ADD3(count0, count1, fore0, mid0, rear0);
		ADD3(count1, count2a, count1, fore1, rear1);
		ADD2(count1, count2b, count1, mid1);

#if 0
    printf("(%d, %d)\n%08x %08x\n%08x %08x\n%08x %08x\n", xmid, ycurr, prevmid, prevfore, currmid, currfore, nextmid, nextfore);
    printf("fore0 %08x fore1 %08x mid0 %08x mid1 %08x\n", fore0, fore1, mid0, mid1);
    printf("count0 %08x count1 %08x count2a %08x count2b %08x\n", count0, count1, count2a, count2b);
    printf("rear0 %08x rear1 %08x\n", rear0, rear1);
#endif

		//
		// Let bmp be the legal places to move. Exclude any bits
		// set in the current state because they're already
		// occupied. Include those with bit 1 of the neighbor
		// count set because that's 2 or 3. Exclude those with
		// bit 2 set - too many neighbors.
		//
		tBmp bmp = ~currmid & count1 &
		    ~count2a & ~count2b;	// Mask those with 4+ neighbors

#ifdef DEBUG
    printf("(%d, %d) %025x\n", xmid, ycurr, bmp);
#endif

		//
		// If bmp is non-zero, examine it for 1 bits in
		// vicinity of intelligent cell. Save legal moves
		// in directions container.
		//
		if (bmp) {
		    if (ycurr == yn) {
			if (xmid == xwdiv && (bmp & xwmask) != 0)
			    directions.push_back(1);
			if (xmid == xidiv && (bmp & ximask) != 0)
			    directions.push_back(2);
			if (xmid == xediv && (bmp & xemask) != 0)
			    directions.push_back(3);
		    }
		    else if (ycurr == fIntelligentY) {
			if (xmid == xwdiv && (bmp & xwmask) != 0)
			    directions.push_back(8);
			if (xmid == xidiv && (bmp & ximask) != 0)
			    directions.push_back(0);
			if (xmid == xediv && (bmp & xemask) != 0)
			    directions.push_back(4);
		    }
		    else if (ycurr == ys) {
			if (xmid == xwdiv && (bmp & xwmask) != 0)
			    directions.push_back(7);
			if (xmid == xidiv && (bmp & ximask) != 0)
			    directions.push_back(6);
			if (xmid == xediv && (bmp & xemask) != 0)
			    directions.push_back(5);
		    }
		}
	    }

	    //
	    // Pre-calculate rear neighbor counts for next iteration.
	    // This way we don't need to keep rear bitmaps around.
	    //
	    ADD3(rear0, rear1, ROL(prevfore, prevmid),
			       ROL(currfore, currmid),
			       ROL(nextfore, nextmid));

	    //
	    // Advance to the right.
	    // Foreward bitmaps become middle bitmaps.
	    //
	    prevmid = prevfore;
	    currmid = currfore;
	    nextmid = nextfore;
        }
    }

    TIMER_STOP(t->tLegalMoves);
    return directions;
}

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

#ifdef SINGLETHREAD
std::priority_queue<Move, std::vector<Move>, CLessScore> gMoveQueue;
#else
tbb::concurrent_priority_queue<Move, CLessScore> gMoveQueue;
#endif

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
    ThreadData *data = (ThreadData *)d;
    Move move;
    while (!gDone) {
	// Choose best available move from priority queue
#ifdef SINGLETHREAD
	if (gMoveQueue.empty())
#else
	if (!gMoveQueue.try_pop(move))
#endif
	{
#ifdef SINGLETHREAD
	    return NULL;
#else
	    pthread_exit(NULL);
#endif
	}
#ifdef SINGLETHREAD
	move = gMoveQueue.top();
	gMoveQueue.pop();
#endif
#ifdef STATS
	++data->npositions;
#endif

	// Apply move and advance board to next generation
	std::shared_ptr<Position> next = move.pos->nextgen(data, move.dir);

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
// #ifdef OPTIMAL
		// if p is better than incumbent
		//     lock
		//     incumbent = p
		//     unlock
// #else
		pthread_mutex_lock(&gBlockMutex);
		if (!gDone.exchange(true)) {
		    next->output(data, gFout, *it);
		    fclose(gFout);
		}
		pthread_mutex_unlock(&gBlockMutex);
#ifdef SINGLETHREAD
		return NULL;
#else
		pthread_exit(NULL);
#endif
// #endif
	    }

	    // Otherwise, add legal moves to priority queue
#ifdef OPTIMAL
	    gMoveQueue.push(Move(next, *it, next->length() + dist));
#else
	    gMoveQueue.push(Move(next, *it, next->length() + dist));
#endif
	}
    }

#ifdef SINGLETHREAD
    return NULL;
#else
    pthread_exit(NULL);
#endif
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
#ifdef STATS
    TIMER_START(tGlobal);
#endif

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
    initial->load(argv[1]);

#ifdef DEBUG
    printf("Initial position:\n");
    initial->print();
#endif

    //
    // Seed priority queue with legal moves from initial position.
    //
    std::vector<int> dirs = initial->legalMoves(&gMainThread);
    for (auto it = dirs.begin(); it != dirs.end(); ++it) {
	// Score legal moves
	int dist = initial->distance(*it);

	// If one is a winner, report and exit
	if (dist == 0) {
	    initial->output(&gMainThread, gFout, *it);
	    fclose(gFout);
	}

	// Otherwise, add legal moves to priority queue
	gMoveQueue.push(Move(initial, *it, dist));
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
    for (i = 0; i < nthreads && !gDone; ++i)
	if (pthread_create(pthreads + i, &attr, threadsearch, (void *)(gThreads + i)) != 0)
	    fprintf(stderr, "failed to create thread\n");
    nthreads = i;

    //
    // Wait for all threads to exit
    //
    pthread_attr_destroy(&attr);
    for (i = 0; i < nthreads && !gDone; ++i)
	pthread_join(pthreads[i], NULL);

    if (!gDone)
	fprintf(gFout, "No solution found\n");
#endif

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
