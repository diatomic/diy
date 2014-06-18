#ifndef DIY_IO_BOV_HPP
#define DIY_IO_BOV_HPP

#include <vector>
#include <algorithm>
#include <numeric>

#include "../types.hpp"

namespace diy
{
namespace io
{
  // Reads and writes (TODO) subsets of a block of values into specified block bounds
  class BOVReader
  {
    public:
                    BOVReader(std::ifstream&                in,
                              const std::vector<unsigned>&  shape,
                              size_t                        offset = 0):
                      in_(in), shape_(shape), offset_(offset)
      {
        stride_.push_back(1);
        for (unsigned i = 1; i < shape_.size(); ++i)
          stride_.push_back(stride_[i-1] * shape_[i-1]);
      }

      inline void   read(const DiscreteBounds& bounds, char* buffer, size_t word_size);

      template<class T>
      void          read(const DiscreteBounds& bounds, T* buffer)       { read(bounds, reinterpret_cast<char*>(buffer), sizeof(T)); }

    private:
      std::ifstream&        in_;
      std::vector<unsigned> shape_;
      std::vector<size_t>   stride_;
      size_t                offset_;
  };
}
}

void
diy::io::BOVReader::
read(const DiscreteBounds& bounds, char* buffer, size_t word_size)
{
  long int sz = bounds.max[0] - bounds.min[0] + 1;
  long int c  = 0;

  std::vector<size_t>   bounds_stride;
  bounds_stride.push_back(1);
  for (int i = 1; i < shape_.size(); ++i)
    bounds_stride.push_back(bounds_stride[i-1] * (bounds.max[i] - bounds.min[i] + 1));

  std::vector<int>      v, l;
  for (unsigned i = 0; i < shape_.size(); ++i)
    v.push_back(bounds.min[i]);

  long int mv = std::inner_product(v.begin(), v.end(), stride_.begin(), 0);
  in_.seekg(offset_ + mv*word_size, in_.beg);
  while (true)
  {
    // read data
    in_.read(buffer + c*word_size*sz, word_size*sz);
    ++c;

    // increment v and compute mv and c
    mv = -sz;
    int i = 1;
    while (i < v.size() && v[i] == bounds.max[i])
    {
      v[i] = bounds.min[i];
      mv -= (bounds.max[i] - bounds.min[i])*stride_[i];
      ++i;
    }
    if (i == v.size())
      break;
    v[i] += 1;
    mv += stride_[i];
    in_.seekg(mv*word_size, in_.cur);
  }
}

#endif
