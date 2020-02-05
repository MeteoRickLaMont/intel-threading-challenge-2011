#ifndef STOPWATCH_H
#define STOPWATCH_H

#ifdef STATS
#include <chrono>
using namespace std::chrono;

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

#define TIMER_START(t) (t).start()
#define TIMER_STOP(t) (t).stop()
#else
#define TIMER_START(t)
#define TIMER_STOP(t)
#endif


#endif
