// Stolen from booksim

#include "tagtable_stats.hpp"
#include <cassert>
//for float comparison
#include <cmath>
#include <limits>

tagtable_stats::tagtable_stats( double bin_size, int num_bins ) :
  _num_bins( num_bins ),
  _bin_size( bin_size )
{
  Clear();
}

void tagtable_stats::Clear( )
{
  _num_samples = 0;
  _sample_sum  = 0.0;
  _sample_squared_sum = 0.0;

  _hist.assign(_num_bins, 0);

  _min = std::numeric_limits<double>::quiet_NaN();
  _max = -std::numeric_limits<double>::quiet_NaN();
  
  //  _reset = true;
}

double tagtable_stats::Average( ) const
{
  return _sample_sum / (double)_num_samples;
}

double tagtable_stats::Variance( ) const
{
  return (_sample_squared_sum * (double)_num_samples - _sample_sum * _sample_sum) / ((double)_num_samples * (double)_num_samples);
}

bool AreSame(double a, double b) {
  return fabs(a-b) < std::numeric_limits<double>::epsilon();
}

//Calculate standard deviation from histogram *neglecting* bin 0
//Requires bin size is 1.0 (assumes bin index is value to calculate its deviation from the mean)
double tagtable_stats::StdDev() const
{
  assert(AreSame(_bin_size, 1.0));
  double avg = double(_sample_sum) / double(_num_samples - _hist.at(0));

  //sum the deviations squared
  double dev_square_sum = 0;  //sum of the square deviations from the mean
  for(uint32_t bin = 1; bin < _hist.size(); ++bin) {
    //multiply the square of the bin's deviation from the mean by the number of samples in the bin
    // & accumulate with deviation square sum variable
    dev_square_sum += ((float(bin) - avg) * (float(bin) - avg)) * float(_hist.at(bin));
  }

  //divide by number of samples
  double dev_square_sum_avg = dev_square_sum / double(_num_samples - _hist.at(0));

  //take square root
  return (std::sqrt(dev_square_sum_avg));
}

double tagtable_stats::Min( ) const
{
  return _min;
}

double tagtable_stats::Max( ) const
{
  return _max;
}

double tagtable_stats::Sum( ) const
{
  return _sample_sum;
}

double tagtable_stats::SquaredSum( ) const
{
  return _sample_squared_sum;
}

int tagtable_stats::NumSamples( ) const
{
  return _num_samples;
}

void tagtable_stats::AddSample( double val )
{
  ++_num_samples;
  _sample_sum += val;

  // NOTE: the negation ensures that NaN values are handled correctly!
  _max = !(val <= _max) ? val : _max;
  _min = !(val >= _min) ? val : _min;

  //double clamp between 0 and num_bins-1
  int b = (int)fmax(floor( val / _bin_size ), 0.0);
  b = (b >= _num_bins) ? (_num_bins - 1) : b;

  _hist[b]++;
}

void tagtable_stats::Display( std::ostream & os ) const
{
  os << *this << std::endl;
}

std::ostream & operator<<(std::ostream & os, const tagtable_stats & s) {
  std::vector<int> const & v = s._hist;
  os << "[ ";
  for(size_t i = 0; i < v.size(); ++i) {
    os << v[i];
    if(i != (v.size() - 1)) { os << ", "; }
  }
  os << "]";
  return os;
}
