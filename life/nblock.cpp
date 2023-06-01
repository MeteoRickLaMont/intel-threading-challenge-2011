//
// Intel Threading Challenge 2011 submission
// Maze of Life
// Rick LaMont <lamont@dotcsw.com>
// Compile with: g++ -O3 -o mazeoflife nblock.cpp -lpthread
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

// #define STATS
// #define SINGLETHREAD

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <algorithm>
#include <functional>
#include <utility>

using std::vector;
using std::set;
using std::priority_queue;
using std::sort;
using std::pair;
using std::string;

//
// Internal representation of Game of Life grid. Only store living cells.
// If pos > 0 then it's an x-coordinate and bmp contains a packed bit
// vector of 8*sizeof(tBmp) cells (0 = dead, 1 = living). If pos < 0 it's
// a y-coord and bmp is undefined. pos == 0 is a sentinel value at the end.
// Note that x coordinates are always factors of BMPSIZE (32 or 64).
//
// Sort cells by increasing y values. Subsort by decreasing x (right to
// left).
//
typedef int tPos;
typedef uint64_t tBmp;

struct Cell {
    Cell(const tPos p, const tBmp b = 0) : pos(p), bmp(b) {}
    bool operator==(const Cell &rhs) const {
        return pos == rhs.pos && bmp == rhs.bmp;
    }

    tPos pos;
    tBmp bmp;
};

#ifdef SINGLETHREAD
const int NTHREADS = 1;
#else
const int NTHREADS = 80;
#endif
#ifdef STATS
static long npositions = 0;
#endif
const int BMPSIZE = ((int)sizeof(tBmp) * CHAR_BIT);
const tBmp XBMPMASK = BMPSIZE - 1;
const tBmp XCELLMASK = ~XBMPMASK;
const tPos XOFFSET = 1024;              // Move away from 0 (sentinel value)
const tPos YOFFSET = INT_MIN + 1024;    // Make y negative
const int MAXBLOCKS = 10000;
const int SQRT_MAXBLOCKS = 100;

//
// File globals. These parameters are common to all positions.
//
static tPos gMinX, gMinY, gMaxX, gMaxY, gRightx;
static unsigned gExtinction;    // Max number of steps without progress
static tBmp gRightmask;         // To clip cells to right edge
static tPos gGoalX, gGoalY;
static int gBlkXDim, gBlkYDim;  // Dimensions of gBlocks
static int gBlkXFactor, gBlkYFactor;    // Cell to block index conversion
static int gMinExpansions = 64;
pthread_mutex_t gBlockMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gBlockCond = PTHREAD_COND_INITIALIZER;
FILE *gFout;                    // Output file - protected by mutex above

//
// Like a chess program, represent a board position. It consists of the
// locations of living cells, the intelligent cell, the path it took to
// got here, and various scoring values to evaluate the worthiness of
// this position.
//
class Position {
public:
    Position();
    Position(const Position &rhs);

    int getx() const { return fIntelligentX - XOFFSET; }
    int gety() const { return fIntelligentY - YOFFSET; }
    void load(FILE *fp, const char *const fname, int line);
    void output(FILE *fp);

    inline bool legalmove(const int dir) const;// True if dirs[i] in bounds
    Position *move(const int dir);      // Create new position by moving
    bool nextgen();                     // Advance game of life generation
    void livingNeighbors(bool *const dirs) const; // True if cell dirs[i] lives
    bool atgoal() const {               // True if smart cell arrived at goal
        return (fIntelligentX == gGoalX) && (fIntelligentY == gGoalY);
    }
    uint32_t payoff() const { return fPayoff; } // Estimated proximity to goal

private:
    vector<Cell> fCells;                // Game of Life board

    //
    // Specific to maze of life extension
    //
    tPos fIntelligentX, fIntelligentY;  // Current position of smart cell
    string fMoves;                      // How it arrived here
    unsigned fClosest;                  // Closest this path ever came to goal
    unsigned fNStuck;                   // Generations without making progress
    uint32_t fPayoff;                   // Lower payoff = better position

    void recalcPayoff();                // Updates fPayoff
    inline void ADD2(tBmp &b0, tBmp &b1, const tBmp a0, const tBmp a1);
    inline void ADD3(tBmp &b0, tBmp &b1,
        const tBmp a0, const tBmp a1, const tBmp a2);
    inline tBmp ROL(const tBmp a0, const tBmp a1);
    inline tBmp ROR(const tBmp a0, const tBmp a1);
};

//
// Sort positions by payoff. Positions with low payoffs are more likely
// to lead to a solution than those with higher payoffs.
//
// STL's priority_queue puts the highest value first, so reverse the logic
// here to select the lowest payoff.
//
struct PositionCompare
    : public std::binary_function<Position *, Position *, bool>
{
    bool operator()(const Position *const lhs, const Position *const rhs) const
    {
        return lhs->payoff() > rhs->payoff();
    }
};

Position::Position() :
    fClosest(INT_MAX),
    fNStuck(0)
{
    fCells.reserve(1024);
}

Position::Position(const Position &rhs) :
    fIntelligentX(rhs.fIntelligentX),
    fIntelligentY(rhs.fIntelligentY),
    fMoves(rhs.fMoves),
    fClosest(rhs.fClosest),
    fNStuck(rhs.fNStuck),
    fPayoff(rhs.fPayoff)
{
    fCells.reserve(rhs.fCells.size() + 10);
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
typedef pair<tPos, tPos> coord;

//
// Sort coords in order of INcreasing Y and DEcreasing X.
//
bool coordPredicate(const coord &c1, const coord &c2)
{
    if (c1.second != c2.second)
        return c1.second < c2.second;
    else
        return c1.first > c2.first;
}

void Position::load(FILE *fp, const char *const fname, int line)
{
    if (fscanf(fp, "%d %d\n", &fIntelligentY, &fIntelligentX) != 2) {
errparse:
        fprintf(stderr, "parse error on line %d of %s\n", line, fname);
        exit(-1);
    }
    fIntelligentY = YOFFSET + fIntelligentY - 1;
    fIntelligentX = XOFFSET + fIntelligentX - 1;
    ++line;

    tPos x[5], y[5];
    vector<coord> coords;
    coords.push_back(coord(fIntelligentX, fIntelligentY));
    int i, n;
    while ((n = fscanf(fp, "%d %d %d %d %d %d %d %d %d %d\n", &y[0], &x[0],
            &y[1], &x[1], &y[2], &x[2], &y[3], &x[3], &y[4], &x[4])) > 0) {
        if (n % 2 != 0)
            goto errparse;
        ++line;
        n /= 2;
        for (i = 0; i < n; ++i) {
            if (x[i] == 0 && y[i] == 0)
                if (i < n - 1)
                    goto errparse;
                else
                    break;
            coords.push_back(coord(XOFFSET + x[i] - 1, YOFFSET + y[i] - 1));
        }
    }

    //
    // Sort live cells by y-coordinate. Subsort by x-coordinate.
    //
    sort(coords.begin(), coords.end(), coordPredicate);

    //
    // Convert to internal format. Each horizontal run of BMPSIZE cells
    // is represented by a bitmap (lsb = leftmost cell of run).
    //
    tPos ylast = 0;                             // Zero values unused
    tPos xlast = 0;
    tBmp bmp = 0;
    vector<coord>::const_iterator c, cend = coords.end();
    for (c = coords.begin(); c != cend; ++c) {
        if (c->second != ylast || (c->first & XCELLMASK) != xlast) {
            //
            // Output previous bmp at xlast,ylast
            //
            if (xlast > 0)
                fCells.push_back(Cell(xlast, bmp));

            //
            // Output new y-coord
            // Negative pos indicates this is y only
            //
            if (c->second != ylast) {
                ylast = c->second;
                fCells.push_back(Cell(ylast));
            }

            //
            // Start a new bitmap at x
            //
            bmp = 0;
            xlast = c->first & XCELLMASK;
        }

        //
        // Build up bitmap at x
        //
        bmp |= 1ULL << (c->first & XBMPMASK);
    }
    //
    // Output final bmp (if any) and sentinel 01
    //
    if (bmp)
        fCells.push_back(Cell(xlast, bmp));
    fCells.push_back(Cell(0));
    recalcPayoff();
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
void Position::output(FILE *fp)
{
    for (int i = 0; i < fMoves.size(); i += 40) {
        string sub = fMoves.substr(i, 40);
        fprintf(fp, "%s\n", sub.c_str());
    }
}

//
// Returns true if moving in direction dir stays in bounds.
//
inline bool Position::legalmove(const int dir) const
{
    if (dir == 0) return true;

    switch (dir) {
    case 0:
        break;
    case 1:
        if (fIntelligentX <= gMinX) return false;
        if (fIntelligentY <= gMinY) return false;
        break;
    case 2:
        if (fIntelligentY <= gMinY) return false;
        break;
    case 3:
        if (fIntelligentX >= gMaxX) return false;
        if (fIntelligentY <= gMinY) return false;
        break;
    case 4:
        if (fIntelligentX >= gMaxX) return false;
        break;
    case 5:
        if (fIntelligentX >= gMaxX) return false;
        if (fIntelligentY >= gMaxY) return false;
        break;
    case 6:
        if (fIntelligentY >= gMaxY) return false;
        break;
    case 7:
        if (fIntelligentX <= gMinX) return false;
        if (fIntelligentY >= gMaxY) return false;
        break;
    case 8:
        if (fIntelligentX <= gMinX) return false;
        break;
    }

    return true;
}

//
// Create new position by moving intelligent cell in direction dir
// (0 = stay put, 1 = NW, 2 = N, 3 = NE, 4 = E, etc.)
//
// If dir == 0 it returns this. Otherwise, a new position is allocated
// and should be deleted by the caller.
//
Position *Position::move(const int dir)
{
    if (dir == 0) {
        fMoves.push_back('0');
        return this;
    }

    Position *next = new Position(*this);

    switch (dir) {
    case 0: break;
    case 1: --next->fIntelligentX; --next->fIntelligentY; break;
    case 2: --next->fIntelligentY; break;
    case 3: ++next->fIntelligentX; --next->fIntelligentY; break;
    case 4: ++next->fIntelligentX; break;
    case 5: ++next->fIntelligentX; ++next->fIntelligentY; break;
    case 6: ++next->fIntelligentY; break;
    case 7: --next->fIntelligentX; ++next->fIntelligentY; break;
    case 8: --next->fIntelligentX; break;
    }
    tPos yold = fIntelligentY;
    tPos xcellold = fIntelligentX & XCELLMASK;
    tBmp xbitold = 1ULL << (fIntelligentX & XBMPMASK);
    tPos ynew = next->fIntelligentY;
    tPos xcellnew = next->fIntelligentX & XCELLMASK;
    tBmp xbitnew = 1ULL << (next->fIntelligentX & XBMPMASK);

    next->fMoves.push_back('0' + dir);

    //
    // Copy old cells to new.
    // Along the way, clear old intelligent bit and set new one.
    //
    tPos x, y, ylast = 1;
    tBmp bmp;
    bool addednew = false;
    vector<Cell>::const_iterator c;
    for (c = fCells.begin(); c->pos; ++c) {
        if (c->pos < 0)
            y = c->pos;
        else {
            x = c->pos;
            bmp = c->bmp;

            //
            // Clear old intelligent cell
            //
            if (y == yold && x == xcellold)
                bmp &= ~xbitold;

            //
            // Set new intelligent cell
            //
            if (!addednew)
                if (y == ynew) {
                    if (x == xcellnew) {
                        bmp |= xbitnew;
                        addednew = true;
                    }
                    else if (x < xcellnew) {
                        if (y != ylast) {
                            next->fCells.push_back(Cell(y));
                            ylast = y;
                        }
                        next->fCells.push_back(Cell(xcellnew, xbitnew));
                        addednew = true;
                    }
                }
                else if (y > ynew) {
                    next->fCells.push_back(Cell(ynew));
                    next->fCells.push_back(Cell(xcellnew, xbitnew));
                    addednew = true;
                }
            if (bmp) {
                if (y != ylast) {
                    next->fCells.push_back(Cell(y));
                    ylast = y;
                }
                next->fCells.push_back(Cell(c->pos, bmp));
            }
        }
    }
    if (!addednew) {
        next->fCells.push_back(Cell(ynew));
        next->fCells.push_back(Cell(xcellnew, xbitnew));
    }
    next->fCells.push_back(Cell(0));

    return next;
}

//
// Add one bit values in parallel.
// a0 and a1 are packed vectors of bits.
// Calculate b = a0 + a1
// Because it takes two bits to represent 1 + 1, let b0 be the lsb
// of the sum and b1 the msb.
//
inline void Position::ADD2(tBmp &b0, tBmp &b1, const tBmp a0, const tBmp a1)
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
    const tBmp a0, const tBmp a1, const tBmp a2)
{
    tBmp t0, t1;
    ADD2(b0, t0, a0, a1);
    ADD2(b0, t1, b0, a2);
    b1 = t0 | t1;
}

//
// Shift a0 one bit to the left, rotating in the msb of a1 from the right.
//
inline tBmp Position::ROL(const tBmp a0, const tBmp a1)
{
    return a0 << 1 | a1 >> BMPSIZE-1;
}

//
// Shift a0 one bit to the right, rotating in the lsb of a1 from the left.
//
inline tBmp Position::ROR(const tBmp a0, const tBmp a1)
{
    return a0 >> 1 | a1 << BMPSIZE-1;
}

//
// Updates fCells to next generation followin Game of Life rules.
// Returns false if intelligent cell would die (fCells unnaffected)
//
bool Position::nextgen()
{
    //
    // Give up if stuck in same place for too long.
    // gExtinction should be chosen pessimistically so as not to
    // prune positions on a valid solution path.
    //
    if (fNStuck > gExtinction)
        return false;

    //
    // Output next generation to newcells.
    // swap it (O(1)) into fCells before returning true.
    //
    vector<Cell> newcells;
    vector<Cell>::const_iterator prev, curr, next;
    newcells.reserve(fCells.size() * 4);        // Handle population explosion

    tPos ylast = 1;                             // Previous y value output
    tPos xdiv = fIntelligentX & XCELLMASK;      // Cache intelligent location
    tBmp xmask = 1ULL << (fIntelligentX & XBMPMASK);

    register tBmp prevmid, currmid, nextmid;
    tBmp rear0, rear1;
    prevmid = currmid = nextmid = rear0 = rear1 = 0;

    //
    // Outer loop scans y from top to bottom.
    // When we're outputting cells on row y:
    //     curr points to old cells on row y
    //     prev points to row y-1
    //     next points to row y+1
    //
    prev = next = curr = fCells.begin();
    for (;;) {
        tPos ycurr;
        if (prev == next) {
            if (next->pos == 0)
                goto done;
            ycurr = next++->pos - 1;
            if (ycurr < gMinY) {
                ++ycurr;
                while (next->pos > 0)
                    ++next;
                ++curr;
                if (next->pos == ycurr + 1)
                    ++next;
            }
        }
        else {
            if (prev->pos == ycurr++)
                ++prev;
            if (curr->pos == ycurr)
                ++curr;
            if (next->pos == ycurr + 1)
                ++next;
        }
        if (ycurr > gMaxY)
            goto done;

        //
        // Secondary loop scans x from right to left
        //
        for (;;) {
            //
            // Let xfore be the rightmost cell of rows prev, curr and next.
            //
            tPos xfore = prev->pos;
            if (xfore < curr->pos)
                xfore = curr->pos;
            if (xfore < next->pos)
                xfore = next->pos;
            if (xfore <= 0)
                break;          // All three rows have been processed

            //
            // xmid is where we will write the next cell if non-empty.
            // We're actually looking ahead one cell with xfore. Activity
            // at xfore can spill over into xmid.
            //
            tPos xmid = xfore + BMPSIZE;

            //
            // Innermost loop scans a connected series of living cells
            // from right to left.
            //
            for (;;) {
                //
                // If xmid has gone off the left edge, we're done with this row.
                // Advance prev, curr and next pointers to Y coords.
                //
                if (xmid < gMinX) {
                    while (prev->pos > 0)
                        ++prev;
                    while (curr->pos > 0)
                        ++curr;
                    while (next->pos > 0)
                        ++next;
                    break;
                }

                //
                // Set bitmaps prevfore, currfore and nextfore.
                //
                register tBmp prevfore, currfore, nextfore;
                if (prev->pos == xfore) {
                    prevfore = prev++->bmp;
                    if (curr->pos == xfore)
                        currfore = curr++->bmp;
                    else
                        currfore = 0;
                    if (next->pos == xfore)
                        nextfore = next++->bmp;
                    else
                        nextfore = 0;
                }
                else {
                    prevfore = 0;
                    if (curr->pos == xfore) {
                        currfore = curr++->bmp;
                        if (next->pos == xfore)
                            nextfore = next++->bmp;
                        else
                            nextfore = 0;
                    }
                    else {
                        currfore = 0;
                        if (next->pos == xfore)
                            nextfore = next++->bmp;
                        else {
                            nextfore = 0;
                            //
                            // If prevfore, currfore and nextfore are all zero,
                            // it may be time to move on to the next group of
                            // living cells in the x direction. Check rear
                            // counts and mid bitmaps first.
                            //
                            if (!prevmid && !currmid && !nextmid &&
                                !rear0 && !rear1)
                                break;
                        }
                    }
                }

                if (xmid <= gMaxX) {
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
                    ADD3(fore0, fore1, ROL(prevmid, prevfore),
                                       ROL(currmid, currfore),
                                       ROL(nextmid, nextfore));
                    ADD2(mid0, mid1, prevmid, nextmid);
                    ADD3(count0, count1, fore0, mid0, rear0);
                    ADD3(count1, count2a, count1, fore1, rear1);
                    ADD2(count1, count2b, count1, mid1);

                    //
                    // Let bmp be the new state of the cells. OR the current
                    // state of the cell to the neighbor count to handle both
                    // the living rule (3 or 4 neighbors) and the spontaneous
                    // generation rule (3 neighbors).
                    //
                    tBmp bmp = (count0 | currmid) & count1 &
                        ~count2a & ~count2b;    // Mask those with 4+ neighbors

                    //
                    // Exit early if intelligent cell will die.
                    //
                    if (ycurr == fIntelligentY && xmid == xdiv &&
                        (bmp & xmask) == 0)
                        return false;

                    //
                    // If bmp is non-zero, clip it to right boundary and
                    // output it.
                    //
                    if (bmp && (xmid != gRightx || (bmp &= gRightmask) != 0)) {
                        if (ycurr != ylast) {
                            newcells.push_back(Cell(ycurr));
                            ylast = ycurr;
                        }
                        newcells.push_back(Cell(xmid, bmp));
                    }
                }

                //
                // Pre-calculate rear neighbor counts for next iteration.
                // This way we don't need to keep rear bitmaps around.
                //
                ADD3(rear0, rear1, ROR(prevfore, prevmid),
                                   ROR(currfore, currmid),
                                   ROR(nextfore, nextmid));

                //
                // Advance to the left.
                // Foreward bitmaps become middle bitmaps.
                //
                prevmid = prevfore;
                currmid = currfore;
                nextmid = nextfore;
                xmid = xfore;
                xfore -= BMPSIZE;               // Advance to the left
            }
        }
    }

done:
    newcells.push_back(Cell(0));
    // if (fCells == newcells) return false; // No progress = dead end.
    fCells.swap(newcells);
    recalcPayoff();
    return true;
}

//
// For i in 1...8,
// dirs[i] = true indicates that cell in direction i is occupied
// dirs[0] = false because the intelligent cell can remain put
//
void Position::livingNeighbors(bool *const dirs) const
{
    tPos yi = fIntelligentY;
    tPos yn = yi - 1;
    tPos ys = yi + 1;
    tPos xi = fIntelligentX & XCELLMASK;
    tPos xw = (fIntelligentX - 1) & XCELLMASK;
    tPos xe = (fIntelligentX + 1) & XCELLMASK;
    tBmp ximask = (1UL << (fIntelligentX & XBMPMASK));
    tBmp xwmask = (1UL << ((fIntelligentX - 1) & XBMPMASK));
    tBmp xemask = (1UL << ((fIntelligentX + 1) & XBMPMASK));

    for (int i = 0; i <= 8; ++i)
        dirs[i] = false;

    //
    // Scan for north Y coord
    //
    vector<Cell>::const_iterator c;
    for (c = fCells.begin(); c->pos > 0 || c->pos < yn; ++c)
        ;
    if (c->pos == yn) {
        for (++c; c->pos > 0 && c->pos > xe; ++c)       // Scan for east X
            ;
        if (c->pos == xe)
            dirs[3] = (c->bmp & xemask) != 0;
        if (c->pos == xi || c->pos > 0 && (++c)->pos == xi)
            dirs[2] = (c->bmp & ximask) != 0;
        if (c->pos == xw || c->pos > 0 && (++c)->pos == xw)
            dirs[1] = (c->bmp & xwmask) != 0;
        while (c->pos > 0)                              // Advance to next Y
            ++c;
    }
    if (c->pos == yi) {
        for (++c; c->pos > 0 && c->pos > xe; ++c)
            ;
        if (c->pos == xe)
            dirs[4] = (c->bmp & xemask) != 0;
        if (c->pos == xw || c->pos > 0 && (++c)->pos == xw)
            dirs[8] = (c->bmp & xwmask) != 0;
        while (c->pos > 0)
            ++c;
    }
    if (c->pos == ys) {
        for (++c; c->pos > 0 && c->pos > xe; ++c)
            ;
        if (c->pos == xe)
            dirs[5] = (c->bmp & xemask) != 0;
        if (c->pos == xi || c->pos > 0 && (++c)->pos == xi)
            dirs[6] = (c->bmp & ximask) != 0;
        if (c->pos == xw || c->pos > 0 && (++c)->pos == xw)
            dirs[7] = (c->bmp & xwmask) != 0;
    }
}

//
// Evaluate this position for its proximity to a solution.
// Smaller numbers indicate better positions.
// The value is stored in fPayoff and can be accessed by calling payoff()
//
void Position::recalcPayoff()
{
    int dx = gGoalX - fIntelligentX;
    int dy = gGoalY - fIntelligentY;
    int major, minor;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx > dy) {
        major = dx;
        minor = dy;
    }
    else {
        major = dy;
        minor = dx;
    }

#ifdef OPTIMAL
    //
    // Optimal algorithm:
    //
    // This payoff function will find an optimal solution when singlethreaded.
    //
    // When multithreaded, care must be taken to wait until all open nodes
    // with a payoff lower than the incumbent solution have been expanded.
    //
    fPayoff = fMoves.size() + major;
#else
    //
    // Greedy algorithm: 
    //
    // Value positions that are close to the goal, regardless of how many
    // steps have already been taken. This will find a sub-optimal solution
    // much faster than the optimal function above.
    //
    // Add a penalty for too many generations without making progress toward
    // the goal. This will break up situations where a path becomes blocked
    // near the goal and threads keep working on it to no avail. The penalty
    // will turn their focus to positions further back on the path.
    //
    if (major < fClosest) {
        fClosest = major;
        fNStuck = 0;
    }
    else if (++fNStuck > 8)             // No progress for 8 generations is OK
        major += fNStuck - 8;
    fPayoff = (major << 16) + minor;
#endif
}

//
// Threading strategy is based on Parallel Best NBlock First (PBNF)
// by Burns et al. (2009).
//
// We arrange rectangular areas of adjadent cells into blocks, forming a
// coarser abstract grid overlaid on the original. The location of the
// intelligent cell dictates in which blcok to insert a position.
//
// Each thread is responsible for a zone. It expands positions within a
// single block, possibly producing new positions in the 8 adjacent blocks.
// These adjacent blocks are off-limits for other threads, so we can add
// positions to them lock-free.
//
// Locking only occurs on the abstract graph when a thread releases one
// block to acquire a more promising one.
//
int gTotalSigma = 0;                    // Value > 0 when threads are working

class Block {
public:
    Block() : fBlkX(0), fBlkY(0), fEmpty(true), fBest(INT_MAX),
        fHot(false), fSigma(0), fHotSigma(0) {}

    int fBlkX, fBlkY;                   // Coordinates of this block
    void setxy(const int x, const int y) { fBlkX = x; fBlkY = y; }

    bool empty() const { return fEmpty; }
    void push(Position *const p) {
        fHeap.push(p);
        fEmpty = false;
        fBest = fHeap.top()->payoff();
    }
    Position *pop() {
        Position *p = fHeap.top();
        fHeap.pop();
        fEmpty = fHeap.empty();
        fBest = fEmpty ? INT_MAX : fHeap.top()->payoff();
        return p;
    }

    bool sigma() const { return fSigma; }
    bool hot() const { return fHot; }
    bool hotsigma() const { return fHotSigma; }
    inline void incrSigma();
    void decrSigma() { --fSigma; --gTotalSigma; }
    void incrHotSigma() { ++fHotSigma; }
    void decrHotSigma() { --fHotSigma; }
    unsigned best() const { return fBest; }
    static int index(const int x, const int y) { return y * gBlkXDim + x; }
    void scope(int &xmin, int &ymin, int &xmax, int &ymax) const;
    void sethot();
    void setcold();
    void release();
    static Block *nextblock(Block *b, const bool forceswitch = false);
    bool shouldswitch(int &exp, bool &sethot) const;

private:
    //
    // push and pop are only called when gBlockMutex is locked.
    // Cache return values for empty and best so they can be
    // called concurrently without touching fHeap
    //
    bool fEmpty;
    unsigned fBest;

    bool fHot;
    int fSigma;                         // Number of Blocks suspended by us
    int fHotSigma;                      // # of hot Blocks suspended by us
    priority_queue<Position *, vector<Position *>, PositionCompare> fHeap;
};

struct BlockCompare
{
    bool operator()(const Block *const lhs, const Block *const rhs) const
    {
        return lhs->best() < rhs->best();
    }
};

//
// Blocks contain a heap of positions, sorted by best payoff value.
// Keep a global heap of free blocks, sorted by best contained position.
//
class BlockHeap {
public:
    BlockHeap() : fEmpty(true), fBest(INT_MAX) {}

    bool empty() const { return fEmpty; }
    unsigned best() const { return fBest; }
    void push(Block *const b) {
        fSet.insert(b);
        fEmpty = false;
        fBest = (*fSet.begin())->best();
    }
    Block *pop() {
        set<Block *, BlockCompare>::iterator bestiter = fSet.begin();
        Block *b = *bestiter;
        fSet.erase(bestiter);
        fEmpty = fSet.empty();
        fBest = fEmpty ? INT_MAX : (*fSet.begin())->best();
        return b;
    }
    void remove(Block *const b) {
        if (fSet.erase(b)) {
            fEmpty = fSet.empty();
            fBest = fEmpty ? INT_MAX : (*fSet.begin())->best();
        }
    }

private:
    //
    // push and pop are only called when gBlockMutex is locked.
    // Cache return values for empty and best so they can be
    // called concurrently without touching fHeap
    //
    bool fEmpty;
    unsigned fBest;

    //
    // Use a set instead of a priority_queue.
    // It automatically prunes duplicate pushes and also supports
    // the remove operation.
    //
    set<Block *, BlockCompare> fSet;
};

//
// Block globals
//
static Block gBlocks[MAXBLOCKS];
static BlockHeap gFreeList;
static bool gDone = false;
#ifdef OPTIMAL
static Position *incumbent;
#endif

//
// A block's sigma indicates how many active threads have it in scope.
// Sigma must be zero before a block can be added to the free list.
//
inline void Block::incrSigma() {
    if (!fSigma)
        gFreeList.remove(this);
    ++fSigma;
    ++gTotalSigma;
}

//
// When working on block (x, y) the blocks at (x +/- 1, y +/- 1) are
// said to be within the "duplicate detection scope". Threads are
// scheduled such that their duplicate detections scopes do not
// overlap. The "interference scope" is defined as (x +/- 2, y +/- 2).
// It represents those blocks whose duplicate detection scopes overlap
// with ours.
//
void Block::scope(int &xmin, int &ymin, int &xmax, int &ymax) const
{
    xmin = fBlkX - 2; if (xmin < 0) xmin = 0;
    ymin = fBlkY - 2; if (ymin < 0) ymin = 0;
    xmax = fBlkX + 2; if (xmax >= gBlkXDim) xmax = gBlkXDim - 1;
    ymax = fBlkY + 2; if (ymax >= gBlkYDim) ymax = gBlkYDim - 1;
}

//
// Hot blocks are to prevent livelock, where the goal block never becomes
// free because there are two or more threads buzzing right around it.
// See papers by Ethan Burns et al.
//
void Block::sethot()
{
    pthread_mutex_lock(&gBlockMutex);
    if (!fHot && fSigma) {
        int x, y, blkXmin, blkYmin, blkXmax, blkYmax;
        unsigned mybest = best();
        scope(blkXmin, blkYmin, blkXmax, blkYmax);

        //
        // See if there's another hot block around here who's
        // hotter than me.
        //
        for (y = blkYmin; y <= blkYmax; ++y)
            for (x = blkXmin; x <= blkXmax; ++x)
                if (y != fBlkY || x != fBlkX) {
                    const Block &n = gBlocks[index(x, y)];
                    if (n.hot() && n.best() < mybest)
                        goto finish;
                }

        //
        // Nope. I am the hottest!
        //
        fHot = true;
        for (y = blkYmin; y <= blkYmax; ++y)
            for (x = blkXmin; x <= blkXmax; ++x)
                if (y != fBlkY || x != fBlkX) {
                    Block &n = gBlocks[index(x, y)];
                    if (n.hot())
                        n.setcold();
                    if (!n.sigma() && !n.hotsigma() && !n.empty()) {
                        gFreeList.push(&n);
                    }
                    n.incrHotSigma();
                }
    }

finish:
    pthread_mutex_unlock(&gBlockMutex);
}

void Block::setcold()
{
    int x, y, blkXmin, blkYmin, blkXmax, blkYmax;

    fHot = false;
    scope(blkXmin, blkYmin, blkXmax, blkYmax);

    for (y = blkYmin; y <= blkYmax; ++y)
        for (x = blkXmin; x <= blkXmax; ++x)
            if (y != fBlkY || x != fBlkX) {
                Block &n = gBlocks[index(x, y)];
                n.decrHotSigma();
                if (!n.sigma() && !n.hotsigma() && !n.empty()) {
                    if (n.hot())
                        n.setcold();
                    gFreeList.push(&n);
                    pthread_cond_signal(&gBlockCond);
                }
            }
}

//
// Releasing this block decrements the sigma of all other blocks in
// the interference zone. Several of those usually become free (sigma = 0).
//
void Block::release()
{
    int x, y, blkXmin, blkYmin, blkXmax, blkYmax;

    scope(blkXmin, blkYmin, blkXmax, blkYmax);

    for (y = blkYmin; y <= blkYmax; ++y)
        for (x = blkXmin; x <= blkXmax; ++x) {
            Block &n = gBlocks[index(x, y)];
            n.decrSigma();
            if (y != fBlkY || x != fBlkX) {
                if (!n.sigma() && !n.hotsigma() && !n.empty()) {
                    if (n.hot())
                        n.setcold();
                    gFreeList.push(&n);
                    pthread_cond_signal(&gBlockCond);
                }
            }
        }
}

//
// This thread wishes to release block b and acquire a more promising
// block. Return value is new block (may be b if mutex was busy, or NULL
// if another thread found a solution).
//
// forceswitch is true when this thread just marked another block as hot.
//
Block *Block::nextblock(Block *b, const bool forceswitch)
{
    int x, y, blkXmin, blkYmin, blkXmax, blkYmax;

    if (forceswitch || !b || b->empty())
        pthread_mutex_lock(&gBlockMutex);
    else if (pthread_mutex_trylock(&gBlockMutex) != 0)
        return b;
    if (b) {
        //
        // If b is better than those in its scope and in the free list,
        // then don't switch.
        //
        unsigned mybest = b->best();
        unsigned bestscope = INT_MAX;
        b->scope(blkXmin, blkYmin, blkXmax, blkYmax);
        for (y = blkYmin; y <= blkYmax; ++y)
            for (x = blkXmin; x <= blkXmax; ++x)
                if (y != b->fBlkY || x != b->fBlkX) {
                    Block &bprime = gBlocks[index(x, y)];
                    unsigned payoff = bprime.best();
                    if (payoff < bestscope)
                        bestscope = payoff;
                }
        unsigned bestfree = gFreeList.best();
        if (mybest < bestscope && mybest < bestfree) {
            pthread_mutex_unlock(&gBlockMutex);
            return b;
        }
        b->release();
    }

    //
    // Decided to switch. Find the best new Block with zero sigma.
    //
    Block *n = 0;
    if (!gTotalSigma && gFreeList.empty()) {
        if (!gDone) {
            fprintf(gFout, "No solution found.\n");
            fclose(gFout);
            gDone = true;
        }
        pthread_cond_broadcast(&gBlockCond);
    }
    while (!gDone && gFreeList.empty())
        pthread_cond_wait(&gBlockCond, &gBlockMutex);
    if (!gDone) {
        n = gFreeList.pop();
        n->scope(blkXmin, blkYmin, blkXmax, blkYmax);
        for (y = blkYmin; y <= blkYmax; ++y)
            for (x = blkXmin; x <= blkXmax; ++x) {
                Block &nprime = gBlocks[index(x, y)];
                nprime.incrSigma();
            }
    }

    pthread_mutex_unlock(&gBlockMutex);
    return n;
}

//
// Returns true is this thread should switch to a more promising block.
//
bool Block::shouldswitch(int &exp, bool &force) const
{
    force = false;
    if (empty())
        return true;
    if (exp < gMinExpansions)
        return false;
    exp = 0;

    int x, y, blkXmin, blkYmin, blkXmax, blkYmax;
    unsigned mybest = best();
    unsigned bestscope = INT_MAX;
    Block *hottest;
    scope(blkXmin, blkYmin, blkXmax, blkYmax);
    for (y = blkYmin; y <= blkYmax; ++y)
        for (x = blkXmin; x <= blkXmax; ++x)
            if (y != fBlkY || x != fBlkX) {
                Block &bprime = gBlocks[index(x, y)];
                unsigned payoff = bprime.best();
                if (payoff < bestscope) {
                    bestscope = payoff;
                    hottest = &bprime;
                }
            }
    unsigned bestfree = gFreeList.best();
    if (bestfree < mybest || bestscope < mybest) {
        if (bestscope < bestfree) {
            hottest->sethot();
            force = true;
        }
        return true;
    }

    pthread_mutex_lock(&gBlockMutex);
    for (y = blkYmin; y <= blkYmax; ++y)
        for (x = blkXmin; x <= blkXmax; ++x)
            if (y != fBlkY || x != fBlkX) {
                Block &bprime = gBlocks[index(x, y)];
                if (bprime.hot())
                    bprime.setcold();
            }
    pthread_mutex_unlock(&gBlockMutex);
    return false;
}

//
// Main entry point for a thread. Acquires free blocks, expands positions,
// and periodically switches to better blocks.
//
static void *threadsearch(void *data)
{
    Block *b = 0;
    int exp;
    bool force = false;
    while (!gDone && (b = Block::nextblock(b, force)) != 0) {
        exp = 0;
        while (!gDone && !b->shouldswitch(exp, force)) {
            Position *p = b->pop();
#ifdef STATS
            ++npositions;
#endif
#ifdef OPTIMAL
            // if p is worse than incumbent
            //     prune p
#endif
            if (p->atgoal()) {
#ifdef OPTIMAL
                // if p is better than incumbent
                //     lock
                //     incumbent = p
                //     unlock
#else
                pthread_mutex_lock(&gBlockMutex);
                if (!gDone) {
                    p->output(gFout);
                    fclose(gFout);
                    gDone = true;
                }
                pthread_mutex_unlock(&gBlockMutex);
                pthread_cond_broadcast(&gBlockCond);
#ifdef SINGLETHREAD
                return NULL;
#else
                pthread_exit(NULL);
#endif
#endif
            }

            //
            // Expand children by making moves.
            //
            Position *next;
            bool living[9];
            p->livingNeighbors(living);
            for (int d = 1; d <= 8; ++d)
                if (p->legalmove(d) && !living[d]) {
                    next = p->move(d);
                    //
                    // Only save viable positions where the intelligent
                    // cell survives.
                    //
                    if (!next->nextgen())
                        delete next;
                    else {
                        int bx = next->getx() / gBlkXFactor;
                        int by = next->gety() / gBlkYFactor;
                        gBlocks[Block::index(bx, by)].push(next);
                    }
            }

            //
            // Do dir 0 last. It's always legal, never wins, and doesn't
            // do a copy on move.
            //
            next = p->move(0);          // NB: next == p
            if (!next->nextgen())
                delete next;
            else {
                int bx = next->getx() / gBlkXFactor;
                int by = next->gety() / gBlkYFactor;
                gBlocks[Block::index(bx, by)].push(next);
            }
            ++exp;
        }
    }
#ifndef SINGLETHREAD
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

    //
    // Parse command line. Open input and output files.
    //
    FILE *fin;
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.txt output.txt\n", argv[0]);
        return -1;
    }
    fin = fopen(argv[1], "r");
    if (!fin) {
        perror(argv[1]);
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
    Position *initial = new Position;
    int line = 1;
    int i, n;
    if (fscanf(fin, "%d %d\n", &gMaxY, &gMaxX) != 2) {
errparse:
        fprintf(stderr, "parse error on line %d of %s\n", line, argv[1]);
        exit(-1);
    }
    ++line;

    //
    // Gliders move at 4 generations per cell.
    // Use that as a pessimistic assumption for extinction.
    //
    gExtinction = (gMaxX > gMaxY ? gMaxX : gMaxY) * 4;

    if (fscanf(fin, "%d %d\n", &gGoalY, &gGoalX) != 2)
        goto errparse;
    gGoalY = YOFFSET + gGoalY - 1;
    gGoalX = XOFFSET + gGoalX - 1;
    ++line;

    initial->load(fin, argv[1], line);
    fclose(fin);

    //
    // Initialize blocks. We want as close to MAXBLOCKS as possible
    // without going over.
    //
    gBlkXFactor = (gMaxX + SQRT_MAXBLOCKS - 1) / SQRT_MAXBLOCKS;
    gBlkXDim = (gMaxX + gBlkXFactor - 1) / gBlkXFactor;
    gBlkYDim = MAXBLOCKS / gBlkXDim;
    gBlkYFactor = (gMaxY + gBlkYDim - 1) / gBlkYDim;
    gBlkYDim = (gMaxY + gBlkYFactor - 1) / gBlkYFactor;
    gMinExpansions = 3 *
        (gBlkXFactor > gBlkYFactor ? gBlkYFactor : gBlkXFactor);
    // printf("Block dimensions %d x %d (%d total)\n", gBlkXDim, gBlkYDim, gBlkXDim * gBlkYDim);
    // printf("Factors %d x %d\n", gBlkXFactor, gBlkYFactor);
    // printf("Min expansions = %d\n", gMinExpansions);
    int bx, by;
    for (i = 0, by = 0; by < gBlkYDim; ++by)
        for (bx = 0; bx < gBlkXDim; ++bx, ++i)
            gBlocks[i].setxy(bx, by);

    //
    // Convert to XOFFSET, YOFFSET space (positive x, negative y)
    //
    gMinX = XOFFSET;
    gMinY = YOFFSET;
    gMaxX = XOFFSET + gMaxX - 1;
    gMaxY = YOFFSET + gMaxY - 1;
    gRightx = gMaxX & XCELLMASK;
    gRightmask = ~((tBmp)0) >> (BMPSIZE - ((gMaxX + 1) & XBMPMASK));

    //
    // Seed first block with initial position.
    // Add block to free list.
    //
    bx = initial->getx() / gBlkXFactor;
    by = initial->gety() / gBlkYFactor;
    Block *blk = gBlocks + Block::index(bx, by);
    blk->push(initial);
    gFreeList.push(blk);

#ifdef SINGLETHREAD
    threadsearch(0);
#else
    //
    // Start threads
    //
    pthread_t pthreads[NTHREADS];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    for (i = 0; i < NTHREADS; ++i)
        if (pthread_create(pthreads + i, &attr, threadsearch, (void *)i) != 0)
            fprintf(stderr, "failed to create thread\n");

    //
    // Wait for all threads to exit
    //
    pthread_attr_destroy(&attr);
    for (i = 0; i < NTHREADS; ++i)
        pthread_join(pthreads[i], NULL);
#endif

    //
    // Report stats and exit
    //
    double msecs = elapsed(begin) * 1000;
    fprintf(stderr, "Elapsed time: %g milliseconds\n", msecs);
#ifdef STATS
    fprintf(stderr, "Examined %ld positions (%g per msec)\n",
        npositions, npositions / msecs);
#endif
    return 0;
}
