
/*
 *
 * Title : Maze Of Life
 *         Solution for the Maze Of Life problem
 *         Intel Threading Challenge 2011
 *
 * File:   MazeOfLife.cpp
 *
 * Author: Miguel Fernandez (jmfernandez@48bits.com)
 *
 */
#include <stdio.h>

#include "tbb/scalable_allocator.h"

#include "tbb/task_scheduler_init.h"
#include "tbb/task.h"

#include "tbb/blocked_range.h"
#include "tbb/blocked_range2d.h"
#include "tbb/parallel_for.h"

#define TBB_PREVIEW_CONCURRENT_PRIORITY_QUEUE 1
#include "tbb/concurrent_priority_queue.h"
#include "tbb/concurrent_hash_map.h"
#include "tbb/tick_count.h"

using namespace std;
using namespace tbb;

#define WHITE_SPACE(x) (x == ' ' || x == '\r' || x == '\n')

static const int numer_of_executions = 1;
static const bool parallel_execution = true;


void* operator new (size_t size, const std::nothrow_t&) throw()
{
  return scalable_malloc(++size);
}

void* operator new (size_t size) throw(std::bad_alloc)
{
  void *result = operator new(size, std::nothrow);
  if(!result)
  {
    throw std::bad_alloc();
  }
  return result;
}

void* operator new [] (size_t size) throw (std::bad_alloc)
{
  return operator new(size);
}

void* operator new [] (size_t size, const std::nothrow_t&) throw()
{
  return operator new(size, std::nothrow);
}

void operator delete(void *ptr) throw()
{
  scalable_free(ptr);
}

void operator delete[](void *ptr) throw()
{
  scalable_free(ptr);
}

void operator delete(void *ptr, std::nothrow_t &) throw()
{
  scalable_free(ptr);
}

void operator delete [] (void *ptr, std::nothrow_t &) throw()
{
  scalable_free(ptr);
}

const char* skip_white_spaces(const char*_content)
{
  while(WHITE_SPACE(*_content))
  {
    ++_content;
  }
  return _content;
}

const char* read_integer(const char *_content,  int *_result)
{
  *_result = (*_content++ - '0');

  while(!WHITE_SPACE(*_content))
  {
   *_result = (*_result << 1) + (*_result << 3) + (*_content++ - '0');
  }
  return _content;
}

const char* read_point(const char *_content, int *_x, int *_y)
{
 _content = skip_white_spaces(_content);
 _content = read_integer(_content, _x);
 _content = skip_white_spaces(_content);
 _content = read_integer(_content, _y);
 return _content;
}

enum Direction
{
  dont_move   = 0,
  north_west  = 1,
  north       = 2,
  north_east  = 3,
  east        = 4,
  south_east  = 5,
  south       = 6,
  south_west  = 7,
  west        = 8,
};

class CGrid
{
public:
  CGrid()
  {
  }

  ~CGrid()
  {
    free(m_grid);
  }

  void initialize(int _width, int _height, int _x, int _y)
  {
    int i;

    m_width = _width;
    m_height = _height;
    int y = _y;
    int j = 1;

    m_grid = (int*) malloc(_width * _height * sizeof(int));
    if(m_grid)
    {
      for(i = 0; i < _width; ++i)
      {
        m_grid[i] = -1;
      }

      for(; j < _y; ++j, --y)
      {
        int k = 1;
        int x = _x;

        m_grid[i++] = -1;

        for(; k < _x; ++k, ++i)
        {
          m_grid[i] = (max(x, y) - 1) << 3;
          --x;
        }

        for(;k < m_width - 1; ++k, ++i)
        {
          m_grid[i] = (max(x, y) - 1) << 3;
          ++x;
        }

         m_grid[i++] = -1;
      }

      for(; j < m_height - 1; ++j, ++y)
      {
        int k = 1;
        int x = _x;

         m_grid[i++] = -1;
        for(; k < _x; ++k, ++i)
        {
          m_grid[i] = (max(x, y) - 1) << 3;
          --x;
        }

        for(;k < m_width - 1; ++k, ++i)
        {
          m_grid[i] = (max(x, y) - 1) << 3;
          ++x;
        }
         m_grid[i++] = -1;
      }

      for(int j = 0; j < _width; ++j, ++i)
      {
        m_grid[i] = -1;
      }
    }
  }

  int& operator [] (int _index)
  {
    return m_grid[_index];
  }

  const int & operator [] (int _index) const
  {
    return m_grid[_index];
  }

private:
  int *m_grid;
  int m_width;
  int m_height;
};


class CMaze
{
public:

  class CMazeTurn
  {
  public:
    typedef blocked_range2d<int, int>CMazeTurnBlockedRange;

    CMazeTurn(CMaze *_maze, CMaze *_result)
      :m_maze(_maze),m_result(_result)
    {
    }

    ~CMazeTurn()
    {
    }

    void operator() (const CMazeTurnBlockedRange &_range) const
    {
      for(int i = _range.rows().begin(); i < _range.rows().end(); ++i)
      {
        int j = _range.cols().begin();
        int index = CMaze::index(j,i);

        for(; j < _range.cols().end(); ++j , ++index)
        {
          char content = m_maze->get(index);

          if((content & 0xF) == 3 || content == 18)
          {
            m_result->set(index);
          }
        }
      }
    }

  private:
    CMaze *m_maze;
    CMaze *m_result;
  };


  CMaze()
  {
    m_parent = NULL;

    m_cells = (char*) scalable_calloc(m_limit, sizeof(char));
    
    m_g_score = 0;
    m_f_score = 0;
  }

  ~CMaze()
  {
    scalable_free(m_cells);
    m_cells = NULL;
  }

  CMaze(CMaze *_parent, int _intelligent_index, Direction _direction, int _g_score)
    : m_parent(_parent),
      m_direction(_direction),
      m_g_score(_g_score),   
      m_intelligent_index(_intelligent_index),
      m_f_score(m_g_score  + (m_grid[_intelligent_index]))
  {
    m_cells = (char*) scalable_calloc(m_limit, sizeof(char));
  }

  bool operator == (const CMaze &_right) const
  {
    int limit = m_limit - (m_width << 1)  - 2;

    return !memcmp(m_cells + m_width + 1, _right.m_cells + m_width + 1, limit);
  }

  void intelligent(int _x, int _y)
  {
    intelligent(index(_x, _y));
  }

  void intelligent(int _index)
  {
    m_intelligent_index = _index;
    m_f_score = m_g_score  + (m_grid[_index]);
  }

  int intelligent_index() const
  {
    return m_intelligent_index;
  }

  int g_score() const
  {
    return m_g_score;
  }

  int f_score() const
  {
    return m_f_score;
  }

  CMaze *next_turn(Direction _direction)
  {
    CMaze *Result = NULL;
    int intelligent_index;

    intelligent_index = go(m_intelligent_index, _direction);

    if(_direction == dont_move || (m_grid[intelligent_index] != -1 && m_cells[intelligent_index] < 16))
    {
      int neighbors = m_cells[intelligent_index];
      
      if(neighbors == 2 || neighbors == 3)
      {
        Result = new CMaze(this, intelligent_index, _direction, g_score() + 1);
        if(Result != NULL)
        {
          int maze_limit;


          set(intelligent_index);

          maze_limit =  CMaze::limit();

          if(parallel_execution && maze_limit > 50 * 50)
          {
            parallel_for(CMazeTurn::CMazeTurnBlockedRange(1, m_width-1, 50, 1, m_height-1, 50), CMazeTurn(this, Result)); 
          }
          else
          {
            char * cells = &(m_cells[m_width + 1]);
            int i = m_width + 1;

            for(int j = 1; j < m_height - 1; ++j)
            { 
              for(int k = 1; k < m_width - 1; ++k, ++i)
              {
                char content = *cells++;

                if((content & 0xF) == 3 || content == 18)
                {
                  Result->set(i);
                }
              }
              i += 2;
              cells += 2;
            }
          }
          
          Result->clear(intelligent_index);
          clear(intelligent_index);
        }
      }
    }
    return Result;
  }

  bool goal() const
  {
    return (m_intelligent_index == m_goal_index);
  }

  char get(int _x, int _y) const
  {
    return get(index(_x,_y));
  }

  void set(int _x, int _y)
  {
    set(index(_x, _y));
  }

  void clear(int _x, int _y)
  {
    clear(index(_x, _y));
  }

  void set(int _index)
  {
    unsigned int *one = (unsigned int *)(&(m_cells[_index -1]));
    unsigned int *two = (unsigned int *)(&(m_cells[_index - m_width -1]));
    unsigned int *three = (unsigned int *)(&(m_cells[_index + m_width -1]));

    *one += 0x00011001;
    *two += 0x00010101;
    *three += 0x00010101;
  }

  char get(int _index) const
  {
    return m_cells[_index];
  }

  void clear(int _index)
  {
    unsigned int *one = (unsigned int *)(&(m_cells[_index -1]));
    unsigned int *two = (unsigned int *)(&(m_cells[_index - m_width -1]));
    unsigned int *three = (unsigned int *)(&(m_cells[_index + m_width -1]));

    *one -= 0x00011001;
    *two -= 0x00010101;
    *three -= 0x00010101;
  }

  static void set_size_and_goal(int _width, int _height, int _x, int _y)
  {
    m_width = _width + 2;
    m_height = _height + 2;
    m_limit = m_width * m_height;
    m_goal_index = index(_x, _y);

    m_grid.initialize(m_width, m_height, _x, _y);
  }

  CMaze *parent() const
  {
    return m_parent;
  }

  Direction direction() const
  {
    return m_direction;
  }

  static int limit()
  {
    return m_limit;
  }

  static int index(int _x, int _y)
  {
    return _y * m_width + _x;
  }

private:

  static inline int go(int _x, Direction _direction)
  {
    switch(_direction)
    {
      case north_west:
        return _x - (m_width + 1);

      case north:
        return _x - m_width;

      case north_east:
        return _x - (m_width - 1);

      case east:
        return ++_x;

      case south_east:
        return _x + m_width + 1;

      case south:
        return _x + m_width;

      case south_west:
        return _x + (m_width - 1);

      case west:
        return --_x;
    }
    return _x;
  }


  void parent(CMaze *_parent)
  {
    m_parent = _parent;
  }

  void direction(Direction _direction)
  {
    m_direction = _direction;
  }

  static int m_width;
  static int m_height;
  static int m_limit;

  static CGrid m_grid;
  static int m_goal_index;

  int m_intelligent_index;
  int m_g_score;
  int m_f_score;
  Direction m_direction;
  CMaze *m_parent;
  char *m_cells;
};


class CLessScore
{
public:
  bool operator()(const CMaze *_left, const CMaze *_right) const
  {
    return (_right->f_score() < _left->f_score());
  }
};


struct HashMazeCompare {
  static size_t hash( const CMaze *_maze) 
  {
    return _maze->intelligent_index();
  }

  static bool equal( const CMaze *_left, const CMaze *_right) 
  {
    return *_left == *_right;
  }
};


typedef concurrent_priority_queue<CMaze*, CLessScore> CConcurrentOpenMazeQueue;
typedef concurrent_hash_map<CMaze*, CMaze*, HashMazeCompare>::iterator CMazeSetIterator;
typedef concurrent_hash_map<CMaze*, CMaze*, HashMazeCompare> CMazeSet;


int CMaze::m_width = 0;
int CMaze::m_limit = 0;
int CMaze::m_height = 0;
int CMaze::m_goal_index = 0;
class CGrid CMaze::m_grid;
CConcurrentOpenMazeQueue g_open_queue;
CMazeSet g_closed_set;
CMaze *g_goal;



void move(CMaze *_maze, Direction _direction)
{
  CMaze *new_maze = _maze->next_turn(_direction);
  if(new_maze)
  {
    CMazeSet::const_accessor a;
    if(!g_closed_set.find(a, new_maze))
    {
      g_open_queue.push(new_maze);
    }
    else
    {
      a.release();
      delete new_maze;
    }
  }
}


CMaze *load_initial_maze_from_buffer(const char *_buffer)
{
  CMaze *result;
  int x1, y1;
  int x2, y2;

  _buffer = read_point(_buffer, &x1, &y1);
  _buffer = read_point(_buffer, &x2, &y2);

  CMaze::set_size_and_goal(x1,y1,x2, y2);

  result = new CMaze;

  _buffer = read_point(_buffer, &x1, &y1);
  result->intelligent(x1, y1);

  _buffer = read_point(_buffer, &x1, &y1);
  while(x1 != 0 && y1 != 0)
  {
    result->set(x1, y1);
    _buffer = read_point(_buffer, &x1, &y1);
  }
  return result;
}

class CMoveTask : public task
{
public:
  CMoveTask(CMaze *_maze)
    :m_maze(_maze),m_worker(false)
  {
  }

  CMoveTask(CMaze *_maze, Direction _direction)
    :m_maze(_maze),m_direction(_direction),m_worker(true)
  {
  }

  ~CMoveTask()
  {
  }

  task* execute()
  {
    if(m_worker)
    {
      move(m_maze, m_direction);
    }
    else
    {
      task_list task_list;
     
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, north_west));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, north));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, north_east));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, east));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, south_east));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, south));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, south_west));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, west));
      task_list.push_back(*new(allocate_child()) CMoveTask(m_maze, dont_move));

      set_ref_count(10);
      spawn_and_wait_for_all(task_list);
    }
    return NULL;
  }


private:
  CMaze *m_maze;
  Direction m_direction;
  bool m_worker;
};


class CMazeTask : public task
{
public:
  CMazeTask(int _level)
    :m_level(_level)
  {
  }

  ~CMazeTask()
  {
  }

  task* execute()
  {
    if(m_level == 0)
    {
      task_list task_list;

      task_list.push_back(*new(allocate_child()) CMazeTask(1));
      task_list.push_back(*new(allocate_child()) CMazeTask(1));
      task_list.push_back(*new(allocate_child()) CMazeTask(1));
      task_list.push_back(*new(allocate_child()) CMazeTask(1));
      task_list.push_back(*new(allocate_child()) CMazeTask(1));
      task_list.push_back(*new(allocate_child()) CMazeTask(1));

      set_ref_count(7); 
      spawn_and_wait_for_all(task_list);
    }
    else
    {
      CMaze *maze;

      while(!g_goal && g_open_queue.try_pop(maze))
      {
        CMazeSet::accessor a;

        g_closed_set.insert(a, maze);
        a.release();
        
        if(maze->goal())
        {
          g_goal = maze;
          break;
        }
        {
          CMoveTask &task = *new(task::allocate_root())CMoveTask(maze);
          task::spawn_root_and_wait(task);
        }
      } 
    }
    return NULL;
  }

private:
  int m_level;
};

static CMaze* run_serial(CMaze *maze)
{
  do
  {
    
    CMazeSet::accessor a;
    g_closed_set.insert(a, maze);
    a.release();
    
    if(maze->goal())
    {
      return maze;
    }

    move( maze, north_west);
    move( maze, north);
    move( maze, north_east);
    move( maze, east);
    move( maze, south_east);
    move( maze, south);
    move( maze, south_west);
    move( maze, west);
    move( maze, dont_move);        
  }
  while(g_open_queue.try_pop(maze));
  return NULL;
}

static CMaze* run_parallel(CMaze *maze)
{
  task_scheduler_init init(task_scheduler_init::automatic , 0);
  CMazeSet::accessor a;
  int i = 0;
  
  g_closed_set.insert(a, maze);
  a.release();

  g_goal = NULL;
   
  do
  {
    CMazeSet::accessor a;
    
    g_closed_set.insert(a, maze);
    a.release();

    ++i;
    if(maze->goal())
    {
      return maze;
    }

    CMoveTask &move_task = *new(task::allocate_root())CMoveTask(maze);
    task::spawn_root_and_wait(move_task);

  }while(i < 25 && g_open_queue.try_pop(maze));

  CMazeTask &maze_task = *new(task::allocate_root())CMazeTask(0);
  task::spawn_root_and_wait(maze_task);

  return g_goal;
}

void run(const char *_input)
{
  CMaze *initial_maze = NULL;
    
  FILE *f = fopen(_input, "rb");
  if(f != NULL)
  {
    int file_size;
    char *content;
    
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    content = (char*) malloc(file_size);
    if(content != NULL)
    {
      fread(content, sizeof(char), file_size, f);
      initial_maze = load_initial_maze_from_buffer(content);
      free(content);
    }
    fclose(f);
  }
  else
  {
    printf("Unable to access file.\n");
  }

  if(initial_maze != NULL)
  {
    CMazeSetIterator itr;
    CMaze *maze;

    if(parallel_execution)
    {
      maze = run_parallel(initial_maze);
    }
    else
    {
      maze = run_serial(initial_maze);
    }

    if(maze != NULL)
    {
      char *solution;
      int pos = maze->g_score();

      solution = (char*) calloc((pos + 1), sizeof(char));
      if(solution)
      {
        Direction direction;
        CMaze * i = maze;


        direction = i->direction();
        i = i->parent();

        while(i != NULL)
        {
          solution[--pos] = char('0' + direction);
          direction = i->direction();
          i = i->parent(); 
        }
      } 
      printf("Solution Path: %s\n", solution);
      free(solution);
    }
    else
    {
      printf("Solution Path not found\n");
    }

    for(itr = g_closed_set.begin() ; itr != g_closed_set.end(); ++itr)
    {
      delete itr->first;
    }

    g_closed_set.clear();

    while(g_open_queue.try_pop(maze))
    {
      delete maze;
    }
  }
}


int main(int argc, char *argv[])
{
  int result = 0;

  try
  {
    tick_count init = tick_count::now();

    if(argc != 2)
    {
      printf("You have to provide an input file\n");
    }
    else
    {   
      for(int i = 0; i < numer_of_executions ; ++i)
      {
        run(argv[1]);
      }
    }
    printf("Time elapsed: %f ms\n", ((tick_count::now() - init).seconds() * 1000) / numer_of_executions);
  }
  catch(...)
  {
    printf("unexpected error\n");
    result = -1;
  }

  return result;
}

