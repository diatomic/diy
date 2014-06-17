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
  class RegularLink: public Link
  {
    public:
      typedef   std::map<Direction, int>            DirMap;

    public:
                RegularLink(int dim):
                  dim_(dim)                         {}

      int       direction(Direction dir) const;     // convert direction to a neighbor (-1 if no neighbor)
      int       dimension() const                   { return dim_; }
      void      add_direction(Direction dir)        { int c = directions_.size(); directions_[dir] = c; }

    private:
      int       dim_;
      DirMap    directions_;
  };

  // stores block bounds associated with each neighbor
  template<class Bounds_>
  class BoundsLink
  {
    public:
      typedef   Bounds_     Bounds;

      Bounds    bounds(int i) const                 { return bounds_[i]; }
      void      add_bounds(const Bounds& bounds)    { bounds_.push_back(bounds); }

    private:
      std::vector<Bounds>   bounds_;
  };

  typedef       BoundsLink<DiscreteBounds>          GridLink;
  typedef       BoundsLink<ContinuousBounds>        ContinuousLink;


  class RegularGridLink: public RegularLink, public GridLink
  {
    public:
                RegularGridLink(int dim):
                  RegularLink(dim)                  {}

  };

  class RegularContinuousLink: public RegularLink, public ContinuousLink
  {
    public:
                RegularContinuousLink(int dim):
                  RegularLink(dim)                  {}
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
