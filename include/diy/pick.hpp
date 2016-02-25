#ifndef DIY_PICK_HPP
#define DIY_PICK_HPP

#include "link.hpp"

namespace diy
{
  template<class Bounds, class Point, class OutIter>
  void near(const RegularLink<Bounds>& link, const Point& p, float r, OutIter out,
            const Bounds& domain);

  template<class Bounds, class Point, class OutIter>
  void in(const RegularLink<Bounds>& link, const Point& p, OutIter out, const Bounds& domain);

  template<class Point>
  float distance(int dim, const ContinuousBounds& bounds, const Point& p);

  inline
  float distance(int dim, const ContinuousBounds& bounds1, const ContinuousBounds& bounds2);

  template<class Bounds>
  void wrap_bounds(Bounds& bounds, int wrap_dir, const Bounds& domain, int dim);

  namespace detail
  {
    template<class Point>
    void shift(float new_pt[DIY_MAX_DIM], const Point& p, float r, Direction dir, int dim);
  }
}

//! Finds the neighbors within radius r of a target point. Assumptions:
//! 1. Point p needs to be in the current block
//! 2. Only for a regular decomposition
template<class Bounds, class Point, class OutIter>
void
diy::
near(const RegularLink<Bounds>& link,  //!< neighbors
     const Point& p,                   //!< target point (must be in current block)
     float r,                          //!< target radius (>= 0.0)
     OutIter out,                      //!< insert iterator for output set of neighbors
     const Bounds& domain)             //!< global domain bounds
{
  int d; // current dimension
  float new_pt[DIY_MAX_DIM]; // offset point
  Bounds neigh_bounds; // neighbor block bounds

  // for all neighbors of this block
  for (int n = 0; n < link.size(); n++)
  {
    // wrap neighbor bounds, if necessary, otherwise bounds will be unchanged
    neigh_bounds = link.bounds(n);
    wrap_bounds(neigh_bounds, link.wrap() & link.direction(n), domain, link.dimension());

    detail::shift(new_pt, p, r, link.direction(n), link.dimension());

    // check if neighbor is near enough
    for (d = 0; d < link.dimension(); d++)
    {
      // if shifted point did not move into or past the neighbor,
      // break and proceed to next neighbor
      // note dist can be large enough to shift the point beyond the neighbor
      // that means the point was definitely near enough to neighbor
      if (((link.direction(n) & (1 << (2*d + 1)))   && new_pt[d] < neigh_bounds.min[d]) ||
          ((link.direction(n) & (1 << (2*d)))       && new_pt[d] > neigh_bounds.max[d]))
        break;
    }

    if (d < link.dimension())
      continue; // next neighbor

    *out++ = n;
  } // for all neighbors
}

//! Find the distance between point `p` and box `bounds`.
template<class Point>
float
diy::
distance(int dim, const ContinuousBounds& bounds, const Point& p)
{
    float res = 0;
    for (int i = 0; i < dim; ++i)
    {
        // avoids all the annoying case logic by finding
        // diff = max(bounds.min[i] - p[i], 0, p[i] - bounds.max[i])
        float diff = 0, d;

        d = bounds.min[i] - p[i];
        if (d > diff) diff = d;
        d = p[i] - bounds.max[i];
        if (d > diff) diff = d;

        res += diff*diff;
    }
    return sqrt(res);
}

float
diy::
distance(int dim, const ContinuousBounds& bounds1, const ContinuousBounds& bounds2)
{
    float res = 0;
    for (int i = 0; i < dim; ++i)
    {
        float diff = 0, d;

        float d1 = bounds1.max[i] - bounds2.min[i];
        float d2 = bounds2.max[i] - bounds1.min[i];

        if (d1 > 0 && d2 > 0)
            diff = 0;
        else if (d1 <= 0)
            diff = -d1;
        else if (d2 <= 0)
            diff = -d2;

        res += diff*diff;
    }
    return sqrt(res);
}

//! Finds the neighbor(s) containing the target point. Assumptions:
//! 1. Only for a regular decomposition
template<class Bounds, class Point, class OutIter>
void
diy::
in(const RegularLink<Bounds>& link,  //!< neighbors
   const Point& p,                   //!< target point
   OutIter out,                      //!< insert iterator for output set of neighbors
   const Bounds& domain)             //!< global domain bounds
{
  int d; // current dimension
  Bounds neigh_bounds; // neighbor block bounds

  // for all neighbors of this block
  for (int n = 0; n < link.size(); n++)
  {
    // wrap neighbor bounds, if necessary, otherwise bounds will be unchanged
    neigh_bounds = link.bounds(n);
    wrap_bounds(neigh_bounds, link.wrap() & link.direction(n), domain, link.dimension());

    // check if p is in neighbor
    for (d = 0; d < link.dimension(); d++)
    {
      if (p[d] < neigh_bounds.min[d] || p[d] > neigh_bounds.max[d])
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
template<class Bounds>
void
diy::
wrap_bounds(Bounds& bounds, int wrap_dir, const Bounds& domain, int dim)
{
  for (int i = 0; i < dim; ++i)
  {
    int left  = 1 << (2*dim);
    int right = 1 << (2*dim + 1);
    if (wrap_dir & left)
    {
      bounds.min[dim] -= (domain.max[dim] - domain.min[dim]);
      bounds.max[dim] -= (domain.max[dim] - domain.min[dim]);
    }
    if (wrap_dir & right)
    {
      bounds.min[dim] += (domain.max[dim] - domain.min[dim]);
      bounds.max[dim] += (domain.max[dim] - domain.min[dim]);
    }
  }
}

template<class Point>
void
diy::detail::
shift(float new_pt[DIY_MAX_DIM], const Point& p, float r, Direction dir, int dim)
{
  for (int i = 0; i < dim; ++i)
  {
    int left  = 1 << (2*dim);
    int right = 1 << (2*dim + 1);
    if (dir & left)
      new_pt[dim] = p[dim] - r;
    if (dir & right)
      new_pt[dim] = p[dim] + r;
  }
}


#endif
