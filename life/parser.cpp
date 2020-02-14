#include "parser.h"
#include "board.h"

Parser::Parser(const char *const fname) : scan(fname)
{
    m_ydim = ++scan;
    m_xdim = ++scan;
    Position::setDimensions(m_ydim, m_xdim);
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
Position *Parser::loadInitial()
{
    tPos x, y;
    const size_t bheight = Position::getBoardHeight();
    const size_t bwidth = Position::getBoardWidth();

    // Note: Dimensions already parsed in constructor

    // Set goal
    y = ++scan;
    x = ++scan;
    Position::setGoal(y, x);

    // Populate board
    tPos yintel, xintel;
    std::vector<tBmp> cells(bheight * bwidth);
    yintel = y = ++scan;
    xintel = x = ++scan;
    while (x != 0 || y != 0) {
	cells[(y - 1) * bwidth + ((x - 1) / Position::BMPSIZE)] |=
	    1ULL << ((x - 1) & Position::XBMPMASK);
	y = ++scan;
	x = ++scan;
    }

    return new Position(yintel, xintel, std::move(cells));
}
