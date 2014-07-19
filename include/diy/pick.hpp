#ifndef DIY_PICK_HPP
#define DIY_PICK_HPP

#include "link.hpp"

namespace diy
{
    // assumptions:
    // 1. Point p needs to be in the current block
    // 2. Only for a regular decomposition
    template<class Bounds, class Point, class OutIter>
    void near(const BoundsLink<Bounds>& link, const Point& p, float r, OutIter out);
}

// TP start
template<class Bounds, class Point, class OutIter>
void
diy::
near(const BoundsLink<Bounds>& link, const Point& p, float r, OutIter out)
{
  int d; // current dimension
  float dir[DIY_MAX_DIM]; // offset direction
  float new_pt[DIY_MAX_DIM]; // offset point

  // for all neighbors of lid
  for (int n = 0; n < link.count(); n++)
  {
    // compute normalized vector from my block to the neighbor block
    // based on difference between mins
    for (d = 0; d < link.dimension(); d++)
    {
      dir[d] = link.bounds(n).min[d] - link.bounds().min[d];
      if (dir[d] > 0.0)
        dir[d] = 1.0f;
      else if (dir[d] < 0.0)
        dir[d] = -1.0f;
    }

    // new point is offset from old point by dist in direction of vector
    for(d = 0; d < link.dimension(); d++)
    {
      new_pt[d] = p[d] + dir[d] * r;
    }

    // check if neighbor is near enough
    for (d = 0; d < link.dimension(); d++)
    {
      // if shifted point did not move into or past the neighbor,
      // break and proceed to next neighbor
      // note dist can be large enough to shift the point beyond the neighbor
      // that means the point was definitely near enough to neighbor
      if ((p[d] < link.bounds(n).min[d] && new_pt[d] < link.bounds(n).min[d]) ||
          (p[d] > link.bounds(n).max[d] && new_pt[d] > link.bounds(n).max[d]))
      {
        break;
      }
    }

    if (d < link.dimension())
    {
      continue; // next neighbor
    }

    *out++ = n;
  } // for all neighbors
}
// TP end

#endif
