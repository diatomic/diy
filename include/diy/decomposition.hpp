#ifndef DIY_DECOMPOSITION_HPP
#define DIY_DECOMPOSITION_HPP

#include <vector>
#include <algorithm>
#include <iostream>

#include "link.hpp"
#include "assigner.hpp"

namespace diy
{
namespace detail
{
  template<class Bounds_>
  struct BoundsHelper;

  template<>
  struct BoundsHelper<DiscreteBounds>
  {
    static int      from(int i, int n, int min, int max, bool)          { return min + (max - min + 1)/n * i; }
    static int      to(int i, int n, int min, int max, bool shared_face)
    {
      if (i == n - 1)
        return max;
      else
        return from(i+1, n, min, max, shared_face) - (shared_face ? 0 : 1);
    }
  };

  template<>
  struct BoundsHelper<ContinuousBounds>
  {
    static float    from(int i, int n, float min, float max, bool)      { return min + (max - min)/n * i; }
    static float    to(int i, int n, float min, float max, bool)        { return min + (max - min)/n * (i+1); }
  };
}

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
    typedef         std::vector<int>                                DivisionsVector;

    // assigner:  decides how processors are assigned to blocks (maps a gid to a rank)
    //            also communicates the total number of blocks
    // wrap:      indicates dimensions on which to wrap the boundary
    // ghosts:    indicates how many ghosts to use in each dimension 
    // divisions: indicates how many cuts to make along each dimension
    //            (0 means "no constraint," i.e., leave it up to the algorithm)
                    RegularDecomposer(int               dim_,
                                      const Bounds&     domain_,
                                      const Assigner&   assigner_,
                                      BoolVector        share_face_ = BoolVector(),
                                      BoolVector        wrap_       = BoolVector(),
                                      CoordinateVector  ghosts_     = CoordinateVector(),
                                      DivisionsVector   divisions_  = DivisionsVector()):
                      dim(dim_), domain(domain_), assigner(assigner_),
                      share_face(share_face_),
                      wrap(wrap_), ghosts(ghosts_), divisions(divisions_)
    {
      if (share_face.size() < dim)  share_face.resize(dim);
      if (wrap.size() < dim)        wrap.resize(dim);
      if (ghosts.size() < dim)      ghosts.resize(dim);
      if (divisions.size() < dim)   divisions.resize(dim);

      int nblocks = assigner.nblocks();
      fill_divisions(nblocks);
    }

    // Calls create(int gid, const Bounds& bounds, const Link& link)
    template<class Creator>
    void            decompose(int rank, const Creator& create)
    {
      int nblocks = assigner.nblocks();

      std::vector<int> gids;
      assigner.local_gids(rank, gids);
      for (int i = 0; i < gids.size(); ++i)
      {
        int gid = gids[i];

        DivisionsVector coords;
        gid_to_coords(gid, coords);

        Bounds core, bounds;
        fill_bounds(core,   coords);
        fill_bounds(bounds, coords, true);

        // Fill link with all the neighbors
        Link link(dim, core, bounds);
        std::vector<int>  offsets(dim, -1);
        offsets[0] = -2;
        while (!all(offsets, 1))
        {
          // next offset
          int i;
          for (i = 0; i < dim; ++i)
            if (offsets[i] == 1)
              offsets[i] = -1;
            else
              break;
          ++offsets[i];

          if (all(offsets, 0)) continue;      // skip ourselves

          DivisionsVector     nhbr_coords(dim);
          int                 dir      = 0;
          bool                inbounds = true;
          for (int i = 0; i < dim; ++i)
          {
            nhbr_coords[i] = coords[i] + offsets[i];

            // wrap
            if (nhbr_coords[i] < 0)
            {
              if (wrap[i])
              {
                nhbr_coords[i] = divisions[i] - 1;
                link.add_wrap(Direction(1 << 2*i));
              }
              else
                inbounds = false;
            }

            if (nhbr_coords[i] >= divisions[i])
            {
              if (wrap[i])
              {
                nhbr_coords[i] = 0;
                link.add_wrap(Direction(1 << (2*i + 1)));
              }
              else
                inbounds = false;
            }

            // NB: this needs to match the addressing scheme in dir_t (in constants.h)
            if (offsets[i] == -1)
              dir |= 1 << (2*i + 0);
            if (offsets[i] == 1)
              dir |= 1 << (2*i + 1);
          }
          if (!inbounds) continue;

          int nhbr_gid = coords_to_gid(nhbr_coords);
          BlockID bid; bid.gid = nhbr_gid; bid.proc = assigner.rank(nhbr_gid);
          link.add_neighbor(bid);

          Bounds nhbr_bounds;
          fill_bounds(nhbr_bounds, nhbr_coords);
          link.add_bounds(nhbr_bounds);

          link.add_direction(static_cast<Direction>(dir));
        }

        create(gid, core, bounds, domain, link);
      }
    }

    static bool     all(const std::vector<int>& v, int x)
    {
      for (unsigned i = 0; i < v.size(); ++i)
        if (v[i] != x)
          return false;
      return true;
    }

    int             point_to_gid();     // TODO

    void            gid_to_coords(int gid, DivisionsVector& coords)
    {
      int dim = divisions.size();
      for (int i = 0; i < dim; ++i)
      {
        coords.push_back(gid % divisions[i]);
        gid /= divisions[i];
      }
    }

    int             coords_to_gid(const DivisionsVector& coords)
    {
      int gid = 0;
      for (int i = coords.size() - 1; i >= 0; --i)
      {
        gid *= divisions[i];
        gid += coords[i];
      }
      return gid;
    }

    void            fill_bounds(Bounds& bounds, const DivisionsVector& coords, bool add_ghosts = false)
    {
      for (int i = 0; i < dim; ++i)
      {
        bounds.min[i] = detail::BoundsHelper<Bounds>::from(coords[i], divisions[i], domain.min[i], domain.max[i], share_face[i]);
        bounds.max[i] = detail::BoundsHelper<Bounds>::to  (coords[i], divisions[i], domain.min[i], domain.max[i], share_face[i]);
      }

      if (!add_ghosts)
        return;

      for (int i = 0; i < dim; ++i)
      {
        if (wrap[i])
        {
          bounds.min[i] -= ghosts[i];
          bounds.max[i] += ghosts[i];
        } else
        {
          bounds.min[i] = std::max(domain.min[i], bounds.min[i] - ghosts[i]);
          bounds.max[i] = std::min(domain.max[i], bounds.max[i] + ghosts[i]);
        }
      }
    }

    void            fill_divisions(int nblocks)
    {
      int prod = 1; int c = 0;
      for (unsigned i = 0; i < dim; ++i)
        if (divisions[i] != 0)
        {
          prod *= divisions[i];
          ++c;
        }

      if (nblocks % prod != 0)
      {
        std::cerr << "Incompatible requirements" << std::endl;
        return;
      }

      if (c == divisions.size())
        return;

      std::vector<unsigned> factors;
      factor(factors, nblocks/prod);

      // Fill the missing divs using LPT algorithm
      std::vector<unsigned> missing_divs(divisions.size() - c, 1);
      for (int i = factors.size() - 1; i >= 0; --i)
        *std::min_element(missing_divs.begin(), missing_divs.end()) *= factors[i];

      c = 0;
      for (unsigned i = 0; i < dim; ++i)
        if (divisions[i] == 0)
          divisions[i] = missing_divs[c++];
    }

    static
    void            factor(std::vector<unsigned>& factors, int n)
    {
      while (n != 1)
        for (unsigned i = 2; i <= n; ++i)
        {
          if (n % i == 0)
          {
            factors.push_back(i);
            n /= i;
            break;
          }
        }
    }

    int               dim;
    const Bounds&     domain;
    const Assigner&   assigner;
    BoolVector        share_face;
    BoolVector        wrap;
    CoordinateVector  ghosts;
    DivisionsVector   divisions;

  };

  template<class Bounds, class Assigner, class Creator>
  void decompose(int                dim,
                 int                rank,
                 const Bounds&      domain,
                 const Assigner&    assigner,
                 const Creator&     create,
                 typename RegularDecomposer<Bounds>::BoolVector       share_face = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::BoolVector       wrap       = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::CoordinateVector ghosts     = typename RegularDecomposer<Bounds>::CoordinateVector(),
                 typename RegularDecomposer<Bounds>::DivisionsVector  divs       = typename RegularDecomposer<Bounds>::DivisionsVector())
  {
    RegularDecomposer<Bounds>(dim, domain, assigner, share_face, wrap, ghosts, divs).decompose(rank, create);
  }

}

#endif
