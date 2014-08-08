#ifndef DIY_COVER_HPP
#define DIY_COVER_HPP

#include <vector>
#include <map>
#include <algorithm>

#include "types.hpp"

namespace diy
{
  // Local view of a distributed representation of a cover, a completely unstructured link
  class Link
  {
    public:
      virtual   ~Link()                             {}  // need to be able to delete derived classes

      int       count() const                       { return neighbors_.size(); }
      inline
      int       count_unique() const;
      BlockID   target(int i) const                 { return neighbors_[i]; }
      int       find(int gid) const;

      void      add_neighbor(const BlockID& block)  { neighbors_.push_back(block); }

    private:
      std::vector<BlockID>  neighbors_;
  };

  // for a regular decomposition, it makes sense to address the neighbors by direction
  // and store local and neighbor bounds
  template<class Bounds_>
  class RegularLink: public Link
  {
    public:
      typedef   Bounds_                             Bounds;

      typedef   std::map<Direction, int>            DirMap;
      typedef   std::vector<Direction>              DirVec;

    public:
                RegularLink(int dim, const Bounds& core, const Bounds& bounds, Direction wrap = Direction(0)):
                  dim_(dim), wrap_(wrap)            {}

      int       dimension() const                   { return dim_; }

      // direction
      int       direction(Direction dir) const;     // convert direction to a neighbor (-1 if no neighbor)
      Direction direction(int i) const              { return dir_vec_[i]; }
      void      add_direction(Direction dir)        { int c = dir_map_.size(); dir_map_[dir] = c; dir_vec_.push_back(dir); }

      // wrap
      void      add_wrap(Direction dir)             { wrap_ = static_cast<Direction>(wrap_ | dir); }
      Direction wrap() const                        { return wrap_; }

      // bounds
      const Bounds& core() const                        { return core_; }
      const Bounds& bounds() const                      { return bounds_; }
      const Bounds& bounds(int i) const                 { return nbr_bounds_[i]; }
      void          add_bounds(const Bounds& bounds)    { nbr_bounds_.push_back(bounds); }

    private:
      int       dim_;

      DirMap    dir_map_;
      DirVec    dir_vec_;
      Direction wrap_;

      Bounds                core_;
      Bounds                bounds_;
      std::vector<Bounds>   nbr_bounds_;
  };

  typedef       RegularLink<DiscreteBounds>         RegularGridLink;
  typedef       RegularLink<ContinuousBounds>       RegularContinuousLink;


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
diy::Link::
count_unique() const
{
    std::vector<BlockID> tmp(neighbors_.begin(), neighbors_.end());
    std::sort(tmp.begin(), tmp.end());
    return std::unique(tmp.begin(), tmp.end()) - tmp.begin();
}

template<class Bounds>
int
diy::RegularLink<Bounds>::
direction(Direction dir) const
{
  DirMap::const_iterator it = dir_map_.find(dir);
  if (it == dir_map_.end())
    return -1;
  else
    return it->second;
}

#endif
