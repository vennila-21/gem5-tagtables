// Stolen from booksim

#ifndef _TAGTABLE_STATS_HPP_
#define _TAGTABLE_STATS_HPP_

#include <iostream>
#include <sstream>
#include <limits>
#include <cmath>
#include <cstdio>
#include <vector>

class tagtable_stats {
  int    _num_samples;
  double _sample_sum;
  double _sample_squared_sum;

  //bool _reset;
  double _min;
  double _max;

  int    _num_bins;
  double _bin_size;

  std::vector<int> _hist;

public:
  tagtable_stats( double bin_size = 1.0, int num_bins = 10 );

  void Clear( );
  double GetBinSize() const { return _bin_size; }

  double Average( ) const;
  double Variance( ) const;
  double StdDev( ) const;
  double Max( ) const;
  double Min( ) const;
  double Sum( ) const;
  double SquaredSum( ) const;
  int    NumSamples( ) const;

  void AddSample( double val );
  inline void AddSample( int val ) {
    AddSample( (double)val );
  }
  inline void AddSample( uint64_t val ) {
    AddSample( (double)val );
  }
  inline void AddSample( uint32_t val ) {
    AddSample( (double)val );
  }

  int GetBin(int b){ return _hist[b];}

  void Display( std::ostream & os = std::cout ) const;

  friend std::ostream & operator<<(std::ostream & os, const tagtable_stats & s);

};

std::ostream & operator<<(std::ostream & os, const tagtable_stats & s);

#endif //_TAGTABLE_STATS_HPP_
