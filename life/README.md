# Round 1: The Maze of Life

## Problem description

Maze of Life is a puzzle/game devised by Andrea Gilbert (http://www.clickmazes.com/). Starting with a Game of Life initial setup and the standard rules to compute successive generations, one cell in the initial setup is designated the intelligent cell and one point on the grid is designated as the goal. Between generations of the Game of Life, the intelligent cell is allowed to move to an empty grid point surrounding the current location of the cell or the intelligent cell can remain at the current grid point. Computation of the next generation of dead and alive cells is done with the inclusion of the intelligent cell as any other cell. The purpose of the game is to keep the intelligent cell alive and move it to reach the goal grid point. If the intelligent cell dies without reaching the goal grid point, the game is over.

Write a threaded code to find a solution to an input instance of the Maze of Life. Input to the application will be from a text file listed first on the command line. The file will contain the initial configuration of the grid for the start of the game. Output of the application will be the path of the intelligent cell from the initial grid point to the goal grid point and will be stored in the second file listed on the command line. The path must ensure that the intelligent cell remains alive from start to reaching the goal as the cell interacts with the other alive cells from one generation to the next.

## Input Description

The input to the program will be from a text file named on the command line of the application. The first line will be two integers denoting the dimensions of the grid, number of rows and number of columns. For consistency and shared point of reference, the upper left corner of the grid will be at the (1 1) location. The second line of the file will be the coordinates of the goal grid point. The third line will be the initial coordinates of the intelligent cell. Remaining lines will be the coordinates of the remainder of the "alive" cells in the initial state of the game. Each line of the file will contain 10 integers with at least one space between each. These represent coordinate pairs of five alive nodes. A tag of two zeroes (0 0) denotes the end of the live cell coordinates. The exception for 10 integers per line will last line of the file which may have fewer than 5 coordinates and the tag.

## Key files in this directory

* [**Miguel Fernandez's original submission**](jmfernandez.cpp) was the fastest overall but only came in third place. More on that later.
* [**Rick LaMont's original submission**](nblock.cpp) placed seventh.
* [**A 2020 rewrite**](greedy.cpp) combined and improved upon the best elements of the above solutions for a truly fast solution.
* [**A 2023 variation**](beam.cpp) finally solved the problem of finding the optimal solution in a scalable manner.

The 2020 and 2023 rewrites share helper files [board.cpp](board.cpp), [parser.cpp](parser.cpp), and [scanner.cpp](scanner.cpp).

## Notes on implemenations and scoring

It was announced in advance that scoring would be based on program speed and that a bonus was available for programs that found the optimal (shortest) path. Some inferred the weighting would be 80% speed and 20% optimal. It came as a shock when the points for finding the optimal solution was equal to the points for coming in first place. It was essentially 50% speed and 50% optimal except only one competitor could come in first place whereas the full bonus for optimal path could be awarded to all competitors regardless of speed.

Full scoring was as follows:
* 50 points for submitting an entry
* 50 points if it compiled, ran, and solved the sample problem
* 25 points for posting 5 comments in the forums
* 5 points on each test case for finding the optimal path on a problem
* 5/rank points each test case for fastest solutions (5, 2.5, 1.667, etc.)
* 225 perfect score, if both fastest and optimal on each problem

First and second place went to "VoVanx86" and "kivyakin" who tried to find the optimal path. One of the ten test cases was sufficiently complex that these two programs failed to terminate within one minute and scored zero. They made up for it on the other nine test cases. Meanwhile, the fastest submission in the field by Miguel Fernandez only came in third place.

Miguel's approach was to adapt the [A* search algorithm](https://en.wikipedia.org/wiki/A*_search_algorithm) to The Maze of Life. It was designed for finding the shortest path through a static graph but nicely fit the constantly changing minefield that is the Game of Life.

Rick LaMont's solution was based on the lesser known paper [Parallel Best-N Block-First (PBNF) by Ethan Burns et al. (2009)](https://www.cs.unh.edu/~sna4/papers/pbnf-socs-09.pdf). The basic concept is a "zone defense" where each thread works on a different part of the board to minimize contention and locks. Starting 12 days late on a 21 day project, Rick never got it to the point where adding more threads made it go faster.

With the benefit of hindsight of the ten test cases and how it would be scored, Rick set out to write a new solution that would build upon the best elements of his and Miguel's submissions. Here are the main improvements he made:
* Miguel had the right idea with the A* algorithm but drop the closed set. With a constantly changing graph, the odds of repeating a previous board position a too low to merit consideration.
* Rick had the right game board data structure except it was designed for large, sparse boards. Change it to fixed height and width. Do not skip over empty vertical or horizontal areas.
* Loading the input data file is a bottleneck. Use a custom algorithm to parse positive integers quickly.
* Exit immediately when solution is found. Don't wait for threads to join.
* Tune the number of threads to the size of problem. Single-thread for very small inputs.
* Spawn one thread to start the other threads while the main thread is parsing file, etc.
* No mutexes. Use atomics and TBB's lock free `concurrent_priority_queue<>`
* Intentionally leak resources including memory. When a solution is found, don't waste time cleaning up.
* Store board + move in the priority queue. Apply the move and do next generation of Game of Life upon pop. This seeds the priority queue faster, especially from the initial position. It also detects a winning move without having to push and pop the priority queue.
* Prevent false sharing by putting each thread's local data in a separate cache line.
* Use TBB scalable allocator (still to-do!)

The result of the above observations was greedy.cpp, a truly fast scalable multi-threaded solution optimized for the ten scoring inputs. One can modify this algorithm to search for shortes path solutions but it doesn't scale well and blows up on one of the ten cases (just like the first and second place finishers found out).

For optimal paths a completely different algorithm is called for. In beam.cpp we see the application the [beam search](https://en.wikipedia.org/wiki/Beam_search) algorithm. With the proper tuning, it can find the optimal solution to all ten cases in a reasonable amount of time. If it could travel back in time, this program would win Round 1.

---

## Rick LaMont's post-mortem of NBlock, posted on Intel's contest forum 2011

I almost hate to talk about Maze of Life because it was such an interesting problem and my solution had potential but regrettably did not scale to multiple threads. Part of my difficulty stemmed from getting a late start on the contest and only having 9 days to complete Maze of Life. Working the full 21 days really helps on any problem.

My top level design is analagous to a chess-playing program in that you need three things:
1. A compact representation of board positions;
2. A way to quickly generate new positions; and
3. A payoff function to evaluate the quality of a postion.

To address the first two requirements I turned to the substantial body of literature on Game of Life programs. My data structure is "sparse" in that it only represents regions that contain living cells. Each cell is represented by a bit (0 = dead, 1 = alive) in a 64 bit integer which, in turn, covers a horizontal run of 64 cells. Advancing one generation in the Game of Life uses bitwise operations to process 64 cells in parallel.

Now that I know more about SSE, I could probably up the word size to 128 and get 128-way parallelism. It wouldn't have mattered much for this contest because 80% of the problem grids were smaller than 64 in width.

The payoff function is interesting because it can totally change the complexion of the program. It scores each position based on how likely it is to lead to the desired solution - lower numbers being better. I wrote two payoffs, greedy and optimal:

* Greedy returns the approximate Manhattan distance from the intelligent cell to the goal. It actually weights the major axis of the Manhattan distance so heavily that the minor axis is only used to break ties.
* Optimal is like the A* algorithm. It returns the distance already traveled plus the best case number of remaining steps. This is an optimistic estimate of the length of a path through this position.

The main algorithm uses a priority queue of positions, sorted by payoff, to work on the most promising leads first:
```
    push initial position onto the heap
    while position at top of heap is not at the goal
        pop position p from the heap
        for direction d in 0 to 8
            if p + d is in bounds and p[d] is a free cell
                position n = p with intelligent cell moved in direction d
                advance n to next generation
                if the intelligent cell survived
                    push n onto the heap
        delete p
    report the solution
```
That's my basic single-threaded solution. With the greedy payoff function it finds sub-optimal solutions quickly. With the optimal payoff, it always finds the shortest path but can take a long time depending on the complexity of the problem.

All attempts to multithread this thing only made it slower. The more threads I started the slower it ran, regardless of threading strategy. Now that I look at it, the problem may have been that each iteration of the loop allocated several new positions and only deleted one. It was constantly growing in terms of dynamic memory, and those newly allocated positions had to be loaded into the data cache. Hmm.

My first attempt at multi-threading simply protected the heap with a mutex and turned all the threads loose on the main loop. When that failed, I assumed it was due to contention for the mutex.

After a series of other failures, I found a paper on Parallel Best NBlock First by Ethan Burns et al. PBNF is a clever algorithm that can be appliesd to Maze of Life. I won't cover the whole thing but recommend reading the paper(s).

The basic idea is that the game grid is divided into equal-sized rectangular zones (typically squares), say of size 8x8 cells. Each thread stakes out a zone and only processes positions where the intelligent cell is in its own zone. The active zones are spaced sufficiently far apart so that if the intelligent cell moves out of one thread's zone, it will enter a passive zone (i.e. one not currently being processed by a thread). This precludes the need for mutexes on most data structures. Periodically each thread checks to see if there is a more promising zone that it should be working on.

This was the solution that I submitted, using the greedy payoff function as its heuristic. It still didn't speed up with threads but this is the Threading Challenge. It wouldn't be in the spirit of the competition to submit a single-threaded solution.

## Historical Commentary

> The following commentary is sourced from my contemporaneous developer's blog in 2011. While some of the language reflects the competitive "heat of the moment" and the excitement of my followers at the time, please view it through a historical lens. I have the utmost respect for the brilliant engineers who competed alongside me. The Intel Threading Challenge was a high-water mark for manycore optimization and I am honored to have been a part of it.
> - Rick LaMont

**5/3/2011:**
The challenge for the first round is to write a multithreaded program to solve a kind of puzzle. Programs will be judged on a 40 core computer next week. The contestant whose program finds any correct solution in the shortest execution time wins 80 points. Everyone, including the winner, has an opportunity to pick up an additional 20 points if their program finds an optimal (fewest steps) solution. Points from each round accumulate toward the grand prize.

So here's the thing: My program can run in a mode where it usually finds an optimal solution (for a sure 20 points) or in another mode where it finds a sub-optimal solution much faster (a chance at 80 points). How should I configure it for the judging? Hint: Screw the 20 points, I'm going for the win!

I'll let you know how it goes next week. This is my first experience in competitive programming.

**5/13/2011:**
The first round (Maze of Life) was an interesting problem but I don't expect to win. The judging will take a couple of weeks. Maybe I can pick up second or third place. My single-threaded solution was really fast but every attempt to multi-thread it only made it go slower. I went ahead and submitted a multi-threaded version. This contest is all about threading so it wouldn't be in the spirit of competition to turn in single-threaded code. Besides, it wouldn't have stood a chance against a well designed threaded solution.

**6/27/2011:**
The results from the first round of my programming contest are in. I did not expect to do well on this round because my program scaled poorly with multiple threads (it actually got slower with more threads according to my tests). There were bonus points available for finding optimal solutions, regardless of speed, but my program didn't even attempt to find optimal paths.

I did much better than expected thanks in part to a generous scoring system. Out of 28 contestants I placed 7th with 142.75 points. The winner only got 169.37 points so I'm well within striking distance for the grand prize over the final two rounds. Here's the breakdown of my score:

* 50 points for submitting an entry (door prize!)
* 50 points because it compiled, ran, and solved the sample problem
* 25 points for posting 5 or more comments in the contest forums (I like to talk)
* 12.75 points for having the 4th fastest program overall (mine came in second place on three of the hard problems)
* 5 points for finding the optimal path on one of the problems (even a blind squirrel finds a nut once in a while)

The biggest surprise in the way it was scored is that finding the optimal solution was worth just as much as having the fastest program. If I would have known that in advance, I would have submitted a slower version of my program that always found the optimal path and earned a cool 175 points. I had actually written such a version but decided not to submit it because my understanding was that speed would be weighted more heavily in the scoring.

The two people with the fastest programs finished 3rd and 6th. First and second place went to guys who participated in the contest forums and wrote programs that usually found optimal solutions. That seems wrong to me but I can't complain about the position I find myself in. Rounds 2 and 3 will be much better for me.
