# Round 1: The Maze of Life

## Problem description

Maze of Life is a puzzle/game devised by Andrea Gilbert (http://www.clickmazes.com/). Starting with a Game of Life initial setup and the standard rules to compute successive generations, one cell in the initial setup is designated the intelligent cell and one point on the grid is designated as the goal. Between generations of the Game of Life, the intelligent cell is allowed to move to an empty grid point surrounding the current location of the cell or the intelligent cell can remain at the current grid point. Computation of the next generation of dead and alive cells is done with the inclusion of the intelligent cell as any other cell. The purpose of the game is to keep the intelligent cell alive and move it to reach the goal grid point. If the intelligent cell dies without reaching the goal grid point, the game is over.

Write a threaded code to find a solution to an input instance of the Maze of Life. Input to the application will be from a text file listed first on the command line. The file will contain the initial configuration of the grid for the start of the game. Output of the application will be the path of the intelligent cell from the initial grid point to the goal grid point and will be stored in the second file listed on the command line. The path must ensure that the intelligent cell remains alive from start to reaching the goal as the cell interacts with the other alive cells from one generation to the next.

## Key files in this directory

* [**Miguel Fernandez's original submission**](jmfernandez.cpp) was the fastest overall but only came in third place. More on that later.
* [**Rick LaMont's original submission**](nblock.cpp) placed seventh.
* [**A 2020 rewrite**](greedy.cpp) combined and improved upon the best elements of the above solutions for a truly fast solution.
* [**A 2023 variation**](beam.cpp) finally solved the problem of finding the optimal solution in a scalable manner.

The 2020 and 2023 rewrites share helper files [board.cpp](board.cpp), [parser.cpp](parser.cpp), and [scanner.cpp](scanner.cpp).

## Notes on implemenations and scoring

It was announced in advance that scoring would be based on program speed and that a bonus was available for programs that found the optimal (shortest) path. Some inferred the weighting would be 80% speed and 20% optimal. It came as a shock when the points for finding the optimal solution was equal to the points for coming in first place. It was essentially 50% speed and 50% optimal except only one competitor could come in first place whereas the full bonus for optimal path could be awarded to all competitors regardless of speed.

First and second place went to the two programs that tried to find the optimal path. One of the ten test cases was sufficiently complex that these two programs failed to terminate within one minute and scored zero. They made up for it on the other nine test cases. Meanwhile, the fastest submission in the field by Miguel Fernandez only came in third place.

Miguel's approach was to adapt the [A* search algorithm](https://en.wikipedia.org/wiki/A*_search_algorithm) to The Maze of Life. It was designed for finding the shortest path through a static graph but nicely fit the constantly changing minefield that is the Game of Life.

Rick LaMont's solution was based on the lesser known paper [Parallel Best-N Block-First (PBNF) by Ethan Burns et al. (2009)](https://www.cs.unh.edu/~sna4/papers/pbnf-socs-09.pdf). The basic concept is a "zone defense" where each thread works on a different part of the board to minimize contention and locks. Starting 12 days late on a 21 day project, Rick never got it to the point where adding more threads made it go faster.

With the benefit of hindsight of the ten test cases and how it would be scored, Rick set out to write a new solution that would build upon the best elements of his and Miguel's submissions. Here are the main improvements he made:
* Miguel had the right idea with the A* algorithm but drop the closed set. With a constantly changing graph, the odds of repeating a previous board position a too low to merit consideration.
* Rick had the right game board data structure except it was designed for large, sparse boards. Change it to fixed height and width. Do not skip over empty vertical or horizontal areas.
- Loading the input data file is a bottleneck. Use a custom algorithm to parse positive integers quickly.
- Exit immediately when solution is found. Don't wait for threads to join.
- Tune the number of threads to the size of problem. Single-thread for very small inputs.
- Spawn one thread to start the other threads while the main thread is parsing file, etc.
- No mutexes. Use atomics and TBB's lock free concurrent_priority_queue<>
- Intentionally leak resources including memory. When a solution is found, don't waste time cleaning up.
- Store board + move in the priority queue. Apply the move and do next generation of Game of Life upon pop. This seeds the priority queue faster, especially from the initial position. It also detects a winning move without having to push and pop the priority queue.
- Prevent false sharing by putting each thread's local data in a separate cache line.
- Use TBB scalable allocator (still to-do!)

The result of the above observations was greedy.cpp, a truly fast scalable multi-threaded solution optimized for the ten scoring inputs. One can modify this algorithm to search for shortes path solutions but it doesn't scale well and blows up on one of the ten cases (just like the first and second place finishers found out).

For optimal paths a completely different algorithm is called for. In beam.cpp we see the application the [beam search](https://en.wikipedia.org/wiki/Beam_search) algorithm. With the proper tuning, it can find the optimal solution to all ten cases in a reasonable amount of time. If it could travel back in time, this program would win Round 1.

## Rick LaMont's contemporaneous developer's blog:

5/3/2011:
The challenge for the first round is to write a multithreaded program to solve this kind of puzzle. Programs will be judged on a 40 core computer next week. The contestant whose program finds any correct solution in the shortest execution time wins 80 points. Everyone, including the winner, has an opportunity to pick up an additional 20 points if their program finds an optimal (fewest steps) solution. Points from each round accumulate toward the grand prize.

So here's the thing: My program can run in a mode where it usually finds an optimal solution (for a sure 20 points) or in another mode where it finds a sub-optimal solution much faster (a chance at 80 points). How should I configure it for the judging? Hint: Screw the 20 points, I'm going for the win!

I'll let you know how it goes next week. This is my first experience in competitive programming.

5/13/2011:
The first round (Maze of Life) was an interesting problem but I don't expect to win. The judging will take a couple of weeks. Maybe I can pick up second or third place. My single-threaded solution was really fast but every attempt to multi-thread it only made it go slower. I went ahead and submitted a multi-threaded version. This contest is all about threading so it wouldn't be in the spirit of competition to turn in single-threaded code. Besides, it wouldn't have stood a chance against a well designed threaded solution.

6/27/2011:
The results from the first round of my programming contest are in. I did not expect to do well on this round because my program scaled poorly with multiple threads (it actually got slower with more threads according to my tests). There were bonus points available for finding optimal solutions, regardless of speed, but my program didn't even attempt to find optimal paths.

I did much better than expected thanks in part to a generous scoring system. Out of 28 contestants I placed 7th with 142.75 points. The winner only got 169.37 points so I'm well within striking distance for the grand prize over the final two rounds. Here's the breakdown of my score:

- 50 points for submitting an entry (door prize!)
- 50 points because it compiled, ran, and solved the sample problem
- 25 points for posting 5 or more comments in the contest forums (I like to talk)
- 12.75 points for having the 4th fastest program overall (mine came in second place on three of the hard problems)
- 5 points for finding the optimal path on one of the problems (even a blind squirrel finds a nut once in a while)

The biggest surprise in the way it was scored is that finding the optimal solution was worth just as much as having the fastest program. If I would have known that in advance, I would have submitted a slower version of my program that always found the optimal path and earned a cool 175 points. I had actually written such a version but decided not to submit it because my understanding was that speed would be weighted more heavily in the scoring.

The two people with the fastest programs finished 3rd and 6th. First and second place went to guys who participated in the contest forums and wrote programs that usually found optimal solutions. That seems wrong to me but I can't complain about the position I find myself in. Rounds 2 and 3 will be much better for me.
