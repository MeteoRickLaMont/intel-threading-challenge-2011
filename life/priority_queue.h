#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#ifdef SINGLETHREAD
#include <queue>
template<class T, class Compare>
class priority_queue : public std::priority_queue<T, std::vector<T>, Compare>
{
public:
    bool try_pop(T &t) {
        if (std::priority_queue<T, std::vector<T>, Compare>::empty())
            return false;
        t = std::priority_queue<T, std::vector<T>, Compare>::top();
        std::priority_queue<T, std::vector<T>, Compare>::pop();
        return true;
    }
};
#else
#include <tbb/concurrent_priority_queue.h>
template<class T, class Compare>
using priority_queue = tbb::concurrent_priority_queue<T, Compare>;
#endif

#endif
