#ifndef DIY_PICK_HPP
#define DIY_PICK_HPP

#include "link.hpp"

namespace diy
{
  template<class Bounds, class Point, class OutIter>
  void near(const RegularLink<Bounds>& link, const Point& p, float r, OutIter out, Bounds& domain);
  namespace detail
  {
    void wrap_bounds(Bounds& bounds, int wrap_dir, Bounds& domain, int dim);
  }
}

// finds the neighbors within radius r of a target point
// assumptions:
// 1. Point p needs to be in the current block
// 2. Only for a regular decomposition
template<class Bounds, class Point, class OutIter>
void
diy::
near(const RegularLink<Bounds>& link, const Point& p, float r, OutIter out, Bounds& domain)
{
  int d; // current dimension
  float dir[DIY_MAX_DIM]; // offset direction
  float new_pt[DIY_MAX_DIM]; // offset point
  Bounds neigh_bounds; // neighbor block bounds

  // for all neighbors of this block
  for (int n = 0; n < link.count(); n++)
  {
    // wrap neighbor bounds, if necessary, otherwise bounds will be unchanged
    neigh_bounds = link.bounds(n);
    detail::wrap_bounds(neigh_bounds, link.wrap() & link.direction(n), domain, link.dimension());

    // compute normalized vector from my block to the neighbor block
    // based on difference between mins
    for (d = 0; d < link.dimension(); d++)
    {
      dir[d] = neigh_bounds.min[d] - link.bounds().min[d];
      if (dir[d] > 0.0)
        dir[d] = 1.0f;
      else if (dir[d] < 0.0)
        dir[d] = -1.0f;
    }

    // new point is offset from old point by dist in direction of vector
    for(d = 0; d < link.dimension(); d++)
      new_pt[d] = p[d] + dir[d] * r;

    // check if neighbor is near enough
    for (d = 0; d < link.dimension(); d++)
    {
      // if shifted point did not move into or past the neighbor,
      // break and proceed to next neighbor
      // note dist can be large enough to shift the point beyond the neighbor
      // that means the point was definitely near enough to neighbor
      if ((p[d] < neigh_bounds.min[d] && new_pt[d] < neigh_bounds.min[d]) ||
          (p[d] > neigh_bounds.max[d] && new_pt[d] > neigh_bounds.max[d]))
        break;
    }

    if (d < link.dimension())
      continue; // next neighbor

    *out++ = n;
  } // for all neighbors
}

// wraps block bounds
// wrap dir is the wrapping direction from original block to wrapped neighbor block
// overall domain bounds and dimensionality are also needed
void
diy::detail::
wrap_bounds(Bounds& bounds, int wrap_dir, Bounds& domain, int dim)
{
  // wrapping toward the left transforms block bounds to the left, and vice versa
  if (dim > 0 && (wrap_dir & DIY_X0) == DIY_X0)
  {
    bounds.min[0] -= (domain.max[0] - domain.min[0]);
    bounds.max[0] -= (domain.max[0] - domain.min[0]);
  }
  if (dim > 0 && (wrap_dir & DIY_X1) == DIY_X1)
  {
    bounds.min[0] += (domain.max[0] - domain.min[0]);
    bounds.max[0] += (domain.max[0] - domain.min[0]);
  }

  if (dim > 1 && (wrap_dir & DIY_Y0) == DIY_Y0)
  {
    bounds.min[1] -= (domain.max[1] - domain.min[1]);
    bounds.max[1] -= (domain.max[1] - domain.min[1]);
  }
  if (dim > 1 && (wrap_dir & DIY_Y1) == DIY_Y1)
  {
    bounds.min[1] += (domain.max[1] - domain.min[1]);
    bounds.max[1] += (domain.max[1] - domain.min[1]);
  }

  if (dim > 2 && (wrap_dir & DIY_Z0) == DIY_Z0)
  {
    bounds.min[2] -= (domain.max[2] - domain.min[2]);
    bounds.max[2] -= (domain.max[2] - domain.min[2]);
  }
  if (dim > 2 && (wrap_dir & DIY_Z1) == DIY_Z1)
  {
    bounds.min[2] += (domain.max[2] - domain.min[2]);
    bounds.max[2] += (domain.max[2] - domain.min[2]);
  }

  if (dim > 3 && (wrap_dir & DIY_T0) == DIY_T0)
  {
    bounds.min[3] -= (domain.max[3] - domain.min[3]);
    bounds.max[3] -= (domain.max[3] - domain.min[3]);
  }
  if (dim > 3 && (wrap_dir & DIY_T1) == DIY_T1)
  {
    bounds.min[3] += (domain.max[3] - domain.min[3]);
    bounds.max[3] += (domain.max[3] - domain.min[3]);
  }
}

#endif
