#ifndef DIY_TYPES_HPP
#define DIY_TYPES_HPP

#include <iostream>
#include "constants.h"
#include "dynamic-point.hpp"

// From https://stackoverflow.com/a/21265197/44738
#if defined(__cplusplus) && (__cplusplus >= 201402L)
#  define DEPRECATED(msg) [[deprecated(#msg)]]
#else
#  if defined(__GNUC__) || defined(__clang__)
#    define DEPRECATED(msg) __attribute__((deprecated(#msg)))
#  elif defined(_MSC_VER)
#    define DEPRECATED(msg) __declspec(deprecated(#msg))
#  else
#    pragma message("WARNING: You need to implement DEPRECATED for this compiler")
#    define DEPRECATED(msg)
#  endif
#endif

namespace diy
{
    struct BlockID
    {
        int gid, proc;

        BlockID() = default;
        BlockID(int _gid, int _proc) : gid(_gid), proc(_proc) {}
    };

    template<class Coordinate_>
    struct Bounds
    {
        using Coordinate = Coordinate_;
        using Point      = diy::DynamicPoint<Coordinate>;

        Point min, max;

        DEPRECATED("Default Bounds constructor should not be used; old behavior is preserved for compatibility. Pass explicitly the dimension of the Bounds instead.")
        Bounds():
            Bounds(DIY_MAX_DIM)                                             {}
        Bounds(int dim): min(dim), max(dim)                                 {}
        Bounds(const Point& _min, const Point& _max) : min(_min), max(_max) {}
    };
    using DiscreteBounds   = Bounds<int>;
    using ContinuousBounds = Bounds<float>;

    //! Helper to create a 1-dimensional discrete domain with the specified extents
    inline
    diy::DiscreteBounds
    interval(int from, int to)            { DiscreteBounds domain(1); domain.min[0] = from; domain.max[0] = to; return domain; }

    struct Direction: public DynamicPoint<int>
    {
        using Parent = DynamicPoint<int>;

        using Parent::dimension;
        using Parent::operator[];

              Direction(int dim, int dir):
                  DynamicPoint(dim)
      {
          if (dim > 0 && dir & DIY_X0) (*this)[0] -= 1;
          if (dim > 0 && dir & DIY_X1) (*this)[0] += 1;
          if (dim > 1 && dir & DIY_Y0) (*this)[1] -= 1;
          if (dim > 1 && dir & DIY_Y1) (*this)[1] += 1;
          if (dim > 2 && dir & DIY_Z0) (*this)[2] -= 1;
          if (dim > 2 && dir & DIY_Z1) (*this)[2] += 1;
          if (dim > 3 && dir & DIY_T0) (*this)[3] -= 1;
          if (dim > 3 && dir & DIY_T1) (*this)[3] += 1;
      }

        DEPRECATED("Direction without dimension is deprecated")
              Direction(int dir = 0):
                  Direction(DIY_MAX_DIM, dir)       // if we are decoding the old constants, we assume DIY_MAX_DIM dimensional space
      {
      }

      bool
      operator==(const diy::Direction& y) const
      {
        for (size_t i = 0; i < dimension(); ++i)
            if ((*this)[i] != y[i]) return false;
        return true;
      }

      // lexicographic comparison
      bool
      operator<(const diy::Direction& y) const
      {
        for (size_t i = 0; i < dimension(); ++i)
        {
            if ((*this)[i] < y[i]) return true;
            if ((*this)[i] > y[i]) return false;
        }
        return false;
      }
    };

    // Selector of bounds value type
    template<class Bounds_>
    struct BoundsValue
    {
        using type = typename Bounds_::Coordinate;
    };

    inline
    bool
    operator<(const diy::BlockID& x, const diy::BlockID& y)
    { return x.gid < y.gid; }

    inline
    bool
    operator==(const diy::BlockID& x, const diy::BlockID& y)
    { return x.gid == y.gid; }

    // Serialization
    template<class C>
    struct Serialization<Bounds<C>>
    {
        static void         save(BinaryBuffer& bb, const Bounds<C>& b)
        {
            diy::save(bb, b.min);
            diy::save(bb, b.max);
        }

        static void         load(BinaryBuffer& bb, Bounds<C>& b)
        {
            diy::load(bb, b.min);
            diy::load(bb, b.max);
        }
    };
    template<>
    struct Serialization<Direction>
    {
        static void         save(BinaryBuffer& bb, const Direction& d)
        {
            diy::save(bb, static_cast<const Direction::Parent&>(d));
        }

        static void         load(BinaryBuffer& bb, Direction& d)
        {
            diy::load(bb, static_cast<Direction::Parent&>(d));
        }
    };
}

#endif
