#ifndef DIY_COVER_HPP
#define DIY_COVER_HPP

#include <vector>
#include <map>

#include "types.hpp"

namespace diy
{
  // Local view of a distributed representation of a cover, a completely unstructured link
  class Link
  {
    public:
      virtual   ~Link()                             {}  // need to be able to delete derived classes

      int       count() const                       { return neighbors_.size(); }
      BlockID   target(int i) const                 { return neighbors_[i]; }
      int       find(int gid) const;

      void      add_neighbor(const BlockID& block)  { neighbors_.push_back(block); }

    private:
      std::vector<BlockID>  neighbors_;
  };

  // for a regular decomposition, it makes sense to address the neighbors by direction
  class RegularLink: public virtual Link
  {
    public:
      typedef   std::map<Direction, int>            DirMap;

    public:
                RegularLink(int dim, Direction wrap = Direction(0)):
                  dim_(dim), wrap_(wrap)            {}

      int       direction(Direction dir) const;     // convert direction to a neighbor (-1 if no neighbor)
      int       dimension() const                   { return dim_; }
      void      add_direction(Direction dir)        { int c = directions_.size(); directions_[dir] = c; }

      void      add_wrap(Direction dir)             { wrap_ = static_cast<Direction>(wrap_ | dir); }
      Direction wrap() const                        { return wrap_; }

    private:
      int       dim_;
      DirMap    directions_;
      Direction wrap_;
  };

  // stores block bounds associated with each neighbor
  template<class Bounds_>
  class BoundsLink: public virtual Link
  {
    public:
      typedef       Bounds_     Bounds;

                    BoundsLink(int dim, const Bounds& core, const Bounds& bounds):
                        dim_(dim),
                        core_(core),
                        local_bounds_(bounds)           {}

      const Bounds& core() const                        { return core_; }
      const Bounds& bounds() const                      { return local_bounds_; }
      const Bounds& bounds(int i) const                 { return bounds_[i]; }
      void          add_bounds(const Bounds& bounds)    { bounds_.push_back(bounds); }

    // identify neighbors of a regular decomposition within a
    // specified radius r of a target point p located inside the
    // current block
    template<class Point, class OutIter>
    void near(const Point& p, float r, OutIter out)
    {
      int d; // current dimension
      float dir[DIY_MAX_DIM]; // offset direction
      float new_pt[DIY_MAX_DIM]; // offset point

      // for all neighbors of lid
      for (int n = 0; n < count(); n++)
      {
        // compute normalized vector from my block to the neighbor block
        // based on difference between mins
        for (d = 0; d < dim_; d++)
        {
          dir[d] = bounds(n).min[d] - bounds().min[d];
          if (dir[d] > 0.0)
            dir[d] = 1.0f;
          else if (dir[d] < 0.0)
            dir[d] = -1.0f;
        }

        // new point is offset from old point by dist in direction of vector
        for(d = 0; d < dim_; d++)
        {
          new_pt[d] = p[d] + dir[d] * r;
        }

        // check if neighbor is near enough
        for (d = 0; d < dim_; d++)
        {
          // if shifted point did not move into or past the neighbor,
          // break and proceed to next neighbor
          // note dist can be large enough to shift the point beyond the neighbor
          // that means the point was definitely near enough to neighbor
          if ((p[d] < bounds(n).min[d] && new_pt[d] < bounds(n).min[d]) ||  
              (p[d] > bounds(n).max[d] && new_pt[d] > bounds(n).max[d]))
          {
            break;
          }
        }

        if (d < dim_)
        {
          continue; // next neighbor
        }

        *out++ = n;
      } // for all neighbors
    }

    private:
      int                   dim_;
      Bounds                core_;
      Bounds                local_bounds_;
      std::vector<Bounds>   bounds_;
  };

  typedef       BoundsLink<DiscreteBounds>          GridLink;
  typedef       BoundsLink<ContinuousBounds>        ContinuousLink;


  // dimension gets duplicated between the two parents, but I don't think it's a big deal
  class RegularGridLink: public RegularLink, public GridLink
  {
    public:
                RegularGridLink(int dim, const Bounds& core, const Bounds& bounds):
                  RegularLink(dim),
                  GridLink(dim, core, bounds)       {}

  };

  class RegularContinuousLink: public RegularLink, public ContinuousLink
  {
    public:
                RegularContinuousLink(int dim, const Bounds& core, const Bounds& bounds):
                  RegularLink(dim),
                  ContinuousLink(dim, core, bounds) {}
  };

  // Other cover candidates: KDTreeLink, AMRGridLink

  // Selector between regular discrete and contious links given bounds type
  template<class Bounds_>
  struct RegularLinkSelector;

  template<>
  struct RegularLinkSelector<DiscreteBounds>
  {
    typedef     RegularGridLink         type;
  };

  template<>
  struct RegularLinkSelector<ContinuousBounds>
  {
    typedef     RegularContinuousLink   type;
  };
}

int
diy::Link::
find(int gid) const
{
  for (unsigned i = 0; i < count(); ++i)
  {
    if (target(i).gid == gid)
      return i;
  }
  return -1;
}

int
diy::RegularLink::
direction(Direction dir) const
{
  DirMap::const_iterator it = directions_.find(dir);
  if (it == directions_.end())
    return -1;
  else
    return it->second;
}

#endif
