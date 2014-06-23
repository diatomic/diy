#ifndef DIY_IO_BOV_HPP
#define DIY_IO_BOV_HPP

#include <vector>
#include <algorithm>
#include <numeric>

#include "../types.hpp"
#include "../mpi.hpp"

namespace diy
{
namespace io
{
  // Reads and writes (TODO) subsets of a block of values into specified block bounds
  class BOV
  {
    public:
      typedef       std::vector<unsigned>                               Shape;
    public:
                    BOV(mpi::io::file&    f):
                      f_(f), offset_(0)                                 {}

      template<class S>
                    BOV(mpi::io::file&    f,
                        const S&          shape  = S(),
                        mpi::io::offset   offset = 0):
                      f_(f), offset_(offset)                            { set_shape(shape); }

      void          set_offset(mpi::io::offset offset)                  { offset_ = offset; }

      template<class S>
      void          set_shape(const S& shape)
      {
        shape_.clear();
        stride_.clear();
        for (unsigned i = 0; i < shape.size(); ++i)
        {
            shape_.push_back(shape[i]);
            stride_.push_back(1);
        }
        for (int i = shape_.size() - 2; i >=  0; --i)
          stride_[i] = stride_[i+1] * shape_[i+1];
      }

      const Shape&  shape() const                                       { return shape_; }

      template<class T>
      void          read(const DiscreteBounds& bounds, T* buffer)       { read(bounds, reinterpret_cast<char*>(buffer), sizeof(T)); }
      inline void   read(const DiscreteBounds& bounds,
                         char* buffer,
                         size_t word_size);

      template<class T>
      void          write(const DiscreteBounds& bounds, const T* buffer){ write(bounds, reinterpret_cast<const char*>(buffer), sizeof(T)); }
      inline void   write(const DiscreteBounds& bounds,
                          const char* buffer,
                          size_t word_size);

    protected:
      mpi::io::file&        file()                                        { return f_; }

    private:
      mpi::io::file&        f_;
      Shape                 shape_;
      std::vector<size_t>   stride_;
      size_t                offset_;
  };
}
}

void
diy::io::BOV::
read(const DiscreteBounds& bounds, char* buffer, size_t word_size)
{
  int       dim = shape_.size();
  long int  sz  = bounds.max[dim - 1] - bounds.min[dim - 1] + 1;
  long int  c   = 0;

  std::vector<size_t> bounds_stride(dim, 1);
  for (int i = dim - 2; i >= 0; --i)
    bounds_stride[i] = bounds_stride[i+1] * (bounds.max[i] - bounds.min[i] + 1);

  std::vector<int> v;
  for (unsigned i = 0; i < dim; ++i)
    v.push_back(bounds.min[i]);

  long int        mv = std::inner_product(v.begin(), v.end(), stride_.begin(), 0);
  mpi::io::offset o  = offset_ + mv*word_size;
  while (true)
  {
    // read data
    f_.read_at(o, buffer + c*word_size*sz, word_size*sz);
    ++c;

    // increment v and compute mv and c
    mv = 0;
    int i = dim - 2;
    while (i >= 0 && v[i] == bounds.max[i])
    {
      v[i] = bounds.min[i];
      mv -= (bounds.max[i] - bounds.min[i])*stride_[i];
      --i;
    }
    if (i == -1)
      break;
    v[i] += 1;
    mv += stride_[i];
    o += mv*word_size;
  }
}

void
diy::io::BOV::
write(const DiscreteBounds& bounds, const char* buffer, size_t word_size)
{
  int       dim = shape_.size();
  long int  sz  = bounds.max[dim - 1] - bounds.min[dim - 1] + 1;
  long int  c   = 0;

  std::vector<size_t> bounds_stride(dim, 1);
  for (int i = dim - 2; i >= 0; --i)
    bounds_stride[i] = bounds_stride[i+1] * (bounds.max[i] - bounds.min[i] + 1);

  std::vector<int> v;
  for (unsigned i = 0; i < dim; ++i)
    v.push_back(bounds.min[i]);

  long int        mv = std::inner_product(v.begin(), v.end(), stride_.begin(), 0);
  mpi::io::offset o  = offset_ + mv*word_size;
  while (true)
  {
    // write data
    f_.write_at(o, buffer + c*word_size*sz, word_size*sz);
    ++c;

    // increment v and compute mv and c
    mv = 0;
    int i = dim - 2;
    while (i >= 0 && v[i] == bounds.max[i])
    {
      v[i] = bounds.min[i];
      mv -= (bounds.max[i] - bounds.min[i])*stride_[i];
      --i;
    }
    if (i == -1)
      break;
    v[i] += 1;
    mv += stride_[i];
    o += mv*word_size;
  }
}

#endif
