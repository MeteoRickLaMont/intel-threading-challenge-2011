/*
 *
 * Title : Consecutive Primes
 *         Solution for the Consecutive Primes problem
 *         Intel Threading Challenge 2011
 *
 * File:   primes.cpp
 *
 * Notes:
 *         Compile it for intel64
 *
 * Author: Miguel Fernandez (jmfernandez@48bits.com)
 *
 */

#include <math.h> 
#include <float.h>


#include <cstring>
#include <list>
#include <fstream>
#include <iostream>
#include <algorithm>
using namespace std;

#include "tbb/task_scheduler_init.h"

#include "tbb/tick_count.h"
#include "tbb/concurrent_queue.h"
#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
using namespace tbb;

static const int numer_of_executions = 1;
static const bool parallel_execution = true;

static const double inverse_prime_list [] =
{
  (double) 1/2,   (double) 1/3,   (double) 1/5,     
  (double) 1/7,   (double)1/11,   (double)1/13,    
  (double)1/17,   (double)1/19,   (double)1/23,    
  (double)1/29,   (double)1/31,   (double)1/37,   
  (double)1/41,   (double)1/43,   (double)1/47,    
  (double)1/53,   (double)1/59,   (double)1/61,    
  (double)1/67,   (double)1/71,   (double)1/73,    
  (double)1/79,   (double)1/83,   (double)1/89,   
  (double)1/97,   (double)1/101,  (double)1/103,   
  (double)1/107,  (double)1/109,  (double)1/113,   
  (double)1/127,  (double)1/131,  (double)1/137
};

static const unsigned long prime_list [] =
{
      2,     3,     5,     7,    11,    13,    17,    19,    23,    29,   31,     37,    41,    43,    47, 
     53,    59,    61,    67,    71,    73,    79,    83,    89,    97,   101,   103,   107,   109,   113, 
    127,   131,   137,   139,   149,   151,   157,   163,   167,   173,   179,   181,   191,   193,   197, 
    199,   211,   223,   227,   229,   233,   239,   241,   251,   257,   263,   269,   271,   277,   281, 
    283,   293,   307,   311,   313,   317,   331,   337,   347,   349,   353,   359,   367,   373,   379, 
    383,   389,   397,   401,   409,   419,   421,   431,   433,   439,   443,   449,   457,   461,   463, 
    467,   479,   487,   491,   499,   503,   509,   521,   523,   541,   547,   557,   563,   569,   571,
    577,   587,   593,   599,   601,   607,   613,   617,   619,   631,   641,   643,   647,   653,   659, 
    661,   673,   677,   683,   691,   701,   709,   719,   727,   733,   739,   743,   751,   757,   761, 
    769,   773,   787,   797,   809,   811,   821,   823,   827,   829,   839,   853,   857,   859,   863, 
    877,   881,   883,   887,   907,   911,   919,   929,   937,   941,   947,   953,   967,   971,   977,
    983,   991,   997,  1009,  1013,  1019,  1021,  1031,  1033,  1039,  1049,  1051,  1061,  1063,  1069,  
};

static const unsigned long last_prime_list_index ((sizeof(prime_list) / sizeof(prime_list[0])) - 1);


class CMemSet
{
public:

  CMemSet(void *_dst, int _val)
    :m_dst(_dst),m_val(_val)
  {
  }

  ~CMemSet()
  {
  }

  CMemSet & operator = (const CMemSet &_right)
  {
    m_dst = _right.m_dst;
    m_val = _right.m_val;
    return *this;
  }

  void operator()(const blocked_range<size_t>& _range) const
  {
    void *dst = (char*)m_dst + _range.begin();
    size_t size = _range.end() - _range.begin();

    memset(dst, m_val, size);
  }

private:
  void *m_dst;
  int m_val;
};

void parallel_memset(void *_dst, int _val, size_t _size)
{
  CMemSet mem_set(_dst, _val);
  parallel_for(blocked_range<size_t>(0, _size, 100000), mem_set);
}

class CKPairPrimesSum
{
public:

  CKPairPrimesSum()
  {
  }

  CKPairPrimesSum(unsigned long _begin, unsigned long _end, unsigned long long _sum)
    :m_begin(_begin),m_end(_end),m_sum(_sum),m_perfect_power(false)
  {
    m_sum_fp = double(m_sum);
  }

  CKPairPrimesSum(const CKPairPrimesSum &_source)
    :m_begin(_source.m_begin),
     m_end(_source.m_end),
     m_sum(_source.m_sum),
     m_perfect_power(_source.m_perfect_power),
     m_sum_fp(_source.m_sum_fp),
     m_base(_source.m_base),
     m_power(_source.m_power)
  {
  }


  ~CKPairPrimesSum()
  {
  }


  unsigned long compute_e_base_2(unsigned long long _sum) const
  {
    unsigned long e = 1;

    _sum >>= 1;
    while(!(_sum & 1))
    {
      _sum >>= 1;
      ++e;
    }
    return e;
  }

  unsigned long compute_e(unsigned long long _sum, unsigned long  _prime) const
  {
    unsigned long e = 1;

    _sum /= _prime;
    while(!(_sum % _prime))
    {
      _sum /= _prime;
      ++e;
    }
    return e;
  }

 
  unsigned long trial_div(unsigned long _limit) const
  {
    const unsigned long *primes;
    const unsigned long *limit;

    if(!(m_sum & 1))
    {
      return compute_e_base_2(m_sum);
    }

    primes = prime_list + 1;
    limit = prime_list + _limit + 1;


    while(primes < limit)
    {
      unsigned long p = *primes++;

      if(!(m_sum % p))
      {
        return compute_e(m_sum, p);
      }
    }


    return 0;
  }


  bool perfect_power(unsigned long _max_power)
  {
    static const unsigned long limit = 19;
    static const double log_limit_divisor = 4.2626798770413155;
    unsigned long div = trial_div(limit);

    if(div != 0)
    {
      int i;
      
      if(!(div & 1) && perfect_pth_power(2, (double)1/2))
      {
        return true;
      }

      i = 1;
      _max_power = min(_max_power, div);

      while(_max_power >= prime_list[i])
      {
        if(div%prime_list[i] == 0)
        {
          if(perfect_pth_power(prime_list[i], inverse_prime_list[i]))
          {
            return true;
          }
        }
        ++i;
      }
    }
    else
    {
      unsigned long log_limit = (unsigned long)(log(m_sum_fp)/log_limit_divisor);
      return perfect_power_slow(min(_max_power, log_limit));
    }
    return false;
  }

  static unsigned long long i_pow(unsigned long _base, unsigned long _power)
  {
    unsigned long long result = _base; 
    switch(_power)
    {
    case 2:
      return result * result;
    case 3:
      return result * result * result;
    case 5:
      {
        result *= _base;
        result *= result;
        result *= _base;

        return result;
      }
    case 7:
      {
        result *= _base;
        result = result * result * result;
        result *= _base;
        return result;
      }
    default:
      {
        unsigned long long base = _base;

        base *= base;
        _power >>= 1;

        while (_power)     
        {         
          if(_power & 1)
          {
            result *= base;         
          }
          _power >>= 1;         
          base *= base;     
        }      
        return result; 
      }
    }
  }

  bool perfect_pth_power(unsigned long _i_power, double _inv_power)
  {
    unsigned long i_base;
    double base = pow(m_sum_fp, _inv_power);
    
    i_base = (unsigned long)(base + 0.5);

    if(fabs(i_base - base) < 0.00000000001)
    {
      unsigned long long result = i_pow(i_base, _i_power);
      if(result == m_sum)
      {
        m_base = i_base;
        m_power = _i_power;
        return true;
      }
    }
    return false;
  }

  bool perfect_power_slow(unsigned long _max_power) 
  {
    int i = 0;

    while(_max_power >= prime_list[i])
    {
      if(perfect_pth_power(prime_list[i], inverse_prime_list[i]))
      {
        return true;
      }
      ++i;
    }
    return false;
  }

  unsigned long begin() const
  {
    return m_begin;
  }

  unsigned long end() const
  {
    return m_end;
  }

  unsigned long long sum() const
  {
    return m_sum;
  }

  unsigned long base() const
  {
    return m_base;
  }

  unsigned long power() const
  {
    return m_power;
  }

private:
  unsigned long m_begin;
  unsigned long m_end;
  unsigned long long m_sum;
  double m_sum_fp;
  bool m_perfect_power;
  unsigned long m_base;
  unsigned long m_power;
};

class CConsecutivePrimesSolution
{
public:
  CConsecutivePrimesSolution()
  {
    m_solution.clear();
  }

  ~CConsecutivePrimesSolution()
  {
    m_solution.clear();
  }

  void push(const CKPairPrimesSum & _sum)
  {
    m_solution.push(_sum);
  }

  void print(ofstream &_stream)
  {
    if(!m_solution.empty())
    {
      CKPairPrimesSum sum;

      while(m_solution.try_pop(sum))
      {
        _stream << "sum(" << sum.begin() << ":" << sum.end() << ") = " << sum.sum();
        _stream << " = "  << sum.base()  << "**" << sum.power() << endl;
      }
    }
    else
    {
      _stream << "There are no consecutive prime sums that equal a perfect power." << endl;
    }
  }
private:
  concurrent_queue<CKPairPrimesSum> m_solution;
};


class CPowerSearch
{
public:

  CPowerSearch(const unsigned long *_primes, unsigned long _primes_count, unsigned long _max_power, CConsecutivePrimesSolution &_solution)
    :m_primes(_primes), m_primes_count(_primes_count), m_max_power(_max_power), m_solution(_solution)
  {
  }

  ~CPowerSearch()
  {
  }

  CPowerSearch & operator = (const CPowerSearch &_right)
  {
    m_primes = _right.m_primes;
    m_primes_count = _right.m_primes_count;
    m_max_power = _right.m_max_power;
    m_solution = _right.m_solution;
    return *this;
  }

  void operator()(const blocked_range<size_t>& _range) const
  {
    const unsigned long *primes_end = m_primes + m_primes_count;
    const unsigned long *primes_limit = m_primes +  _range.end();

    for(const unsigned long *prime_begin = m_primes + _range.begin(); prime_begin < primes_limit; ++prime_begin)
    {
      unsigned long long sum = *prime_begin;

      for(const unsigned long *primes_current = prime_begin + 1 ; primes_current < primes_end; ++primes_current)
      {
        sum += *primes_current;

        CKPairPrimesSum primes_sum(*prime_begin, *primes_current, sum);
        if(primes_sum.perfect_power(m_max_power))
        {
          m_solution.push(primes_sum);
        }
      }
    }
  }

private:
  CConsecutivePrimesSolution &m_solution;
  const unsigned long *m_primes;
  unsigned long m_primes_count;
  unsigned long m_max_power;
};

class CCompositeCheck
{
public:
  CCompositeCheck(const unsigned long *_primes, unsigned long _composites_init, bool *_composites, unsigned long _composites_count)
    :m_primes(_primes),m_composites_init(_composites_init),m_composites(_composites),m_composites_count(_composites_count)
  {
  }

  ~CCompositeCheck()
  {
  }

  void operator()(const blocked_range<size_t>& _range) const
  {
    run(_range.begin(), _range.end());
  }

  void run(unsigned long _init, unsigned long _end) const
  {
    const unsigned long *primes_end = m_primes + _end;

    for( const unsigned long *primes = m_primes + _init; primes < primes_end; ++primes)
    {
      unsigned long delta = *primes; 
      unsigned long init = m_composites_init % delta;

      if(init != 0)
      {
        init = delta - init;
        if((init & 1) == 1)
        {
          init += delta;
        }
        init >>= 1;
      }

      for(unsigned long j = init; j < m_composites_count; j += delta)
      {
        m_composites[j] = true;
      }
    }
  }

private:
  const unsigned long *m_primes;
  unsigned long m_composites_init;
  bool *m_composites;
  unsigned long m_composites_count;
};

class CKPairPrimes
{
public:
  CKPairPrimes(unsigned long _init, unsigned long _end, bool _parallel)
    :m_init(_init),m_end(_end),m_primes_count(0)
  {
    unsigned long max_primes = (_end - _init) >> 1;
    m_primes = new unsigned long [max_primes];

    if(_init < prime_list[last_prime_list_index])
    {
      unsigned long m = prime_list_binary_search(_init);

      while(m <= last_prime_list_index && prime_list[m] <= _end)
      {
        m_primes[m_primes_count++] = prime_list[m++];
      }

      _init = prime_list[last_prime_list_index] + 2;
    }

    _init |= 1;

    if(_init <= _end)
    {
      unsigned long Last = (unsigned long)(sqrt(double(_end)));
      unsigned long Limit;
      unsigned long composites_length = ((_end - _init) >> 1);
      bool *composites = new bool [++composites_length];
      CCompositeCheck cmp_check_pl(prime_list, _init, composites, composites_length);
      unsigned long i;

      Last += (Last & 1);
      Limit = prime_list_binary_search(Last);

      if(_parallel)
      {
        parallel_memset(composites, 0, composites_length * sizeof(bool));
        parallel_for(blocked_range<size_t>(1, Limit, 10), cmp_check_pl);
      }
      else
      {
        memset(composites, 0, composites_length * sizeof(bool));
        cmp_check_pl.run(1, Limit);
      }

      if(prime_list[last_prime_list_index] < Last)
      {
        CKPairPrimes aux(prime_list[last_prime_list_index], Last, false);
        CCompositeCheck cmp_check(aux.m_primes, _init, composites, composites_length);

        if(_parallel)
        { 
          parallel_for(blocked_range<size_t>(0, aux.m_primes_count, 10), cmp_check);
        }
        else
        {
          cmp_check.run(0, aux.m_primes_count);
        }
      }

      for(i = 0; i  < composites_length ; ++i)
      {
        if(!composites[i])
        {
          m_primes[m_primes_count++] = _init + (i << 1);
        }
      }
      delete [] composites;
    }
  }

  ~CKPairPrimes()
  {
    delete [] m_primes;
  }

  static unsigned long prime_list_binary_search(unsigned long _number)
  {
    unsigned long m = 0;
    unsigned long l = 0;
    unsigned long r = sizeof(prime_list) / sizeof(prime_list[0]);

    while(l < r)
    {
      m = (l + r) >> 1;
      if(_number < prime_list[m])
      {
        r = m;
      }
      else
      {
        l = m + 1;
      }
    }

    if(prime_list[m] < _number)
    {
      ++m;
    }
    return m;
  }


  void powers(CConsecutivePrimesSolution & _solution, unsigned long _max, bool _parallel)
  {
    if(_parallel && m_primes_count > 150)
    {
      CPowerSearch power_search(m_primes, m_primes_count, _max, _solution);
      parallel_for(blocked_range<size_t>(0, m_primes_count, 10), power_search);
    }
    else
    {
      const unsigned long *primes_limit = m_primes + m_primes_count;

      for(const unsigned long *primes = m_primes; primes < primes_limit; ++primes)
      {
        unsigned long long sum = *primes;
        for(const unsigned long *primes_end = primes + 1 ; primes_end < primes_limit; ++primes_end)
        {
          sum += *primes_end;

          CKPairPrimesSum primes_sum(*primes, *primes_end, sum);
          if(primes_sum.perfect_power(_max))
          {
            _solution.push(primes_sum);
          }
        }
      }
    }
  }


private:

  unsigned long  m_init;
  unsigned long  m_end;
  unsigned long* m_primes;
  unsigned long  m_primes_count;
};

class CConsecutivePrimes
{
public:

  CConsecutivePrimes(const char * _init, const char *_end, const char * _max_power, const char *_output)
  {
    m_init = atoul(_init);
    m_end = atoul(_end);
    m_max_power = atoul(_max_power);

    m_output.open(_output, ios::out);
    if(!m_output.is_open())
    {
      throw invalid_argument("Error opening output file");
    }
  }

  ~CConsecutivePrimes()
  {
    m_output.close();
  }

  void run(void)
  {
    CConsecutivePrimesSolution solution;
    double spected_primes;
    double end = (double) m_end;
    double init = (double) m_init;

    end /= log(end);

    if(init > 1)
    {
      init /= log(init);
    }

    spected_primes = end  - init;
    if(parallel_execution && spected_primes > 125)
    {
      task_scheduler_init init(task_scheduler_init::automatic , 0);
      CKPairPrimes pair_primes(m_init, m_end, true);
      pair_primes.powers(solution, m_max_power, true);
    }
    else
    {
      CKPairPrimes pair_primes(m_init, m_end, false);
      pair_primes.powers(solution, m_max_power, false);
    }

    solution.print(m_output);
  }

private:
  unsigned long m_init;
  unsigned long m_end;
  unsigned long m_max_power;
  ofstream m_output;

  static unsigned long atoul(const char* _number)
  {
    unsigned long result = 0;

    if(*_number != '\0')
    {
      result = *_number++ - '0';
      while(*_number != 0)
      {
        result *= 10;
        result += *_number++ - '0';
      }
    }
    return result;
  }
};

int main(int argc, char *argv[])
{
  int result = 0;

  try
  {
    tick_count init = tick_count::now();
    tick_count::interval_t time;

    if(argc != 5)
    {
      cout << "You have to provide a valid input\n" << endl;
    }
    else
    {   
      for(int i = 0; i < numer_of_executions; ++i)
      {
        CConsecutivePrimes consecutive_primes(argv[1], argv[2], argv[3], argv[4]);
        consecutive_primes.run();
      }
    }

    time = tick_count::now() - init;
    if(time.seconds() / numer_of_executions < 120)
    {
      cout << "Time elapsed: " << (time.seconds() * 1000) / numer_of_executions << " ms" << endl;
    }
    else
    {
      cout << "Time elapsed: " << time.seconds() / numer_of_executions << " s" << endl;
    }
  }
  catch(...)
  {
    cout << "unexpected error\n" << endl;
    result = -1;
  }

  return result;
}


