#ifndef DIY_DECOMPOSITION_HPP
#define DIY_DECOMPOSITION_HPP

#include <vector>

#include "cover.hpp"

namespace diy
{
  // Decomposes a regular (discrete or continuous) domain into even blocks;
  // creates Links with Bounds along the way.
  template<class Bounds_>
  struct RegularDecomposer
  {
    typedef         Bounds_                                         Bounds;
    typedef         typename BoundsValue<Bounds>::type              Coordinate;
    typedef         typename RegularLinkSelector<Bounds>::type      Link;

    typedef         std::vector<bool>                               BoolVector;
    typedef         std::vector<Coordinate>                         CoordinateVector;

    // Calls create(int gid, const Bounds& bounds, const Link& link)
    // assigner:  decides how processors are assigned to blocks (maps a gid to a rank)
    //            also communicates the total number of blocks
    // wrap:      indicates dimensions on which to wrap the boundary
    // ghosts:    indicates how many ghosts to use in each dimension 
    // divisions: indicates how many cuts to make along each dimension
    //            (0 means "no constraint," i.e., leave it up to the algorithm)
    template<class Assigner, class Creator>
    static
    void            decompose(int                   dim,
                              int                   rank,
                              const Bounds&         domain,
                              const Assigner&       assigner,
                              const Creator&        create,
                              BoolVector            wrap      = BoolVector(),
                              CoordinateVector      ghosts    = CoordinateVector(),
                              CoordinateVector      divisions = CoordinateVector())
    {
      if (ghosts.empty())       ghosts.resize(dim);
      if (divisions.empty())    divisions.resize(dim);

      // TODO: take wrap, ghosts, and divisions into account

      int nblocks = assigner.nblocks();
    }
  };

  template<class Bounds, class Assigner, class Creator>
  void decompose(int                dim,
                 int                rank,
                 const Bounds&      domain,
                 const Assigner&    assigner,
                 const Creator&     create,
                 typename RegularDecomposer<Bounds>::BoolVector       wrap   = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::CoordinateVector ghosts = typename RegularDecomposer<Bounds>::CoordinateVector(),
                 typename RegularDecomposer<Bounds>::CoordinateVector divs   = typename RegularDecomposer<Bounds>::CoordinateVector())
  {
    RegularDecomposer<Bounds>::decompose(dim, rank, domain, assigner, create, wrap, ghosts, divs);
  }

}

#endif
