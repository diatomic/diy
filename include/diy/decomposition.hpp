#ifndef DIY_DECOMPOSITION_HPP
#define DIY_DECOMPOSITION_HPP

#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "link.hpp"
#include "assigner.hpp"
#include "master.hpp"

namespace diy
{
namespace detail
{
  template<class Bounds_, class Enable = void>
  struct BoundsHelper;

  // discrete bounds
  template<class Bounds>
  struct BoundsHelper<Bounds, typename std::enable_if<std::is_integral<typename Bounds::Coordinate>::value>::type>
  {
    using Coordinate = typename Bounds::Coordinate;

    static Coordinate   floor_div(Coordinate x, Coordinate y)
    {
        Coordinate q = x / y;
        Coordinate r = x % y;
        if (r != 0 && ((r < 0) != (y < 0)))
            --q;
        return q;
    }

    static Coordinate   from(int i, int n, Coordinate min, Coordinate max, bool)          { return min + (max - min + 1)/n * i; }
    static Coordinate   to  (int i, int n, Coordinate min, Coordinate max, bool shared_face)
    {
      if (i == n - 1)
        return max;
      else
        return from(i+1, n, min, max, shared_face) - (shared_face ? 0 : 1);
    }

    static int          lower(Coordinate x, int n, Coordinate min, Coordinate max, bool shared)
    {
        Coordinate width = (max - min + 1)/n;
        Coordinate res = floor_div(x - min, width);
        if (res >= n && x <= max) res = n - 1;

        if (res > 0 && shared && x == from(res, n, min, max, shared))
            --res;
        return res;
    }
    static int          upper(Coordinate x, int n, Coordinate min, Coordinate max, bool shared)
    {
        Coordinate width = (max - min + 1)/n;
        Coordinate res = floor_div(x - min, width) + 1;
        if (res > n && x <= max) res = n;
        if (res < n && shared && x == from(res, n, min, max, shared))
            ++res;
        return res;
    }
  };

  // continuous bounds
  template<class Bounds>
  struct BoundsHelper<Bounds, typename std::enable_if<std::is_floating_point<typename Bounds::Coordinate>::value>::type>
  {
    using Coordinate = typename Bounds::Coordinate;

    static Coordinate   from(int i, int n, Coordinate min, Coordinate max, bool)      { return min + (max - min) / static_cast<Coordinate>(n) * static_cast<Coordinate>(i); }
    static Coordinate   to  (int i, int n, Coordinate min, Coordinate max, bool)      { return min + (max - min) / static_cast<Coordinate>(n) * static_cast<Coordinate>(i + 1); }

    static int          lower(Coordinate x, int n, Coordinate min, Coordinate max, bool)   { Coordinate width = (max - min) / static_cast<Coordinate>(n); auto res = static_cast<int>(std::floor((x - min)/width)); if (min + static_cast<Coordinate>(res)*width == x) return (res - 1); else return res; }
    static int          upper(Coordinate x, int n, Coordinate min, Coordinate max, bool)   { Coordinate width = (max - min) / static_cast<Coordinate>(n); auto res = static_cast<int>(std::ceil ((x - min)/width)); if (min + static_cast<Coordinate>(res)*width == x) return (res + 1); else return res; }
  };

  template<class Coordinate>
  typename std::enable_if<std::is_integral<Coordinate>::value, Coordinate>::type
  wrap_period(Coordinate min, Coordinate max)
  {
      return max - min + 1;
  }

  template<class Coordinate>
  typename std::enable_if<std::is_floating_point<Coordinate>::value, Coordinate>::type
  wrap_period(Coordinate min, Coordinate max)
  {
      return max - min;
  }

  template<class Coordinate>
  typename std::enable_if<std::is_integral<Coordinate>::value, int>::type
  wrap_shift(Coordinate x, Coordinate min, Coordinate max)
  {
      Coordinate size = wrap_period(min, max);
      Coordinate q = (x - min) / size;
      Coordinate r = (x - min) % size;
      if (r != 0 && ((r < 0) != (size < 0)))
          --q;
      return static_cast<int>(q);
  }

  template<class Coordinate>
  typename std::enable_if<std::is_floating_point<Coordinate>::value, int>::type
  wrap_shift(Coordinate x, Coordinate min, Coordinate max)
  {
      return static_cast<int>(std::floor((x - min) / wrap_period(min, max)));
  }

  template<class Coordinate>
  typename std::enable_if<std::is_integral<Coordinate>::value, Coordinate>::type
  normalize_wrap(Coordinate x, Coordinate min, Coordinate max)
  {
      Coordinate size = wrap_period(min, max);
      Coordinate offset = (x - min) % size;
      if (offset < 0)
          offset += size;
      return min + offset;
  }

  template<class Coordinate>
  typename std::enable_if<std::is_floating_point<Coordinate>::value, Coordinate>::type
  normalize_wrap(Coordinate x, Coordinate min, Coordinate max)
  {
      Coordinate size = wrap_period(min, max);
      Coordinate offset = std::fmod(x - min, size);
      if (offset < 0)
          offset += size;
      return min + offset;
  }

  inline int normalize_coord(int coord, int size)
  {
      coord %= size;
      if (coord < 0)
          coord += size;
      return coord;
  }
}

  //! \ingroup Decomposition
  //! Decomposes a regular (discrete or continuous) domain into even blocks;
  //! creates Links with Bounds along the way.
  template<class Bounds_>
  struct RegularDecomposer
  {
    using Bounds     = Bounds_;
    using Coordinate = typename BoundsValue<Bounds>::type;
    using Link       = RegularLink<Bounds>;

    using Creator = std::function<void(int,      Bounds, Bounds, Bounds, Link)>;
    using Updater = std::function<void(int, int, Bounds, Bounds, Bounds, Link)>;

    typedef         std::vector<bool>                               BoolVector;
    typedef         std::vector<Coordinate>                         CoordinateVector;
    typedef         std::vector<int>                                DivisionsVector;

    /// @param dim        dimensionality of the decomposition
    /// @param domain     bounds of global domain
    /// @param nblocks    total number of global blocks
    /// @param share_face indicates dimensions on which to share block faces
    /// @param wrap       indicates dimensions on which to wrap the boundary
    /// @param ghosts     indicates how many ghosts to use in each dimension
    /// @param divisions  indicates how many cuts to make along each dimension
    ///                   (0 means "no constraint," i.e., leave it up to the algorithm)
                    RegularDecomposer(int               dim_,
                                      const Bounds&     domain_,
                                      int               nblocks_,
                                      BoolVector        share_face_ = BoolVector(),
                                      BoolVector        wrap_       = BoolVector(),
                                      CoordinateVector  ghosts_     = CoordinateVector(),
                                      DivisionsVector   divisions_  = DivisionsVector()):
                      dim(dim_), domain(domain_), nblocks(nblocks_),
                      share_face(share_face_),
                      wrap(wrap_), ghosts(ghosts_), divisions(divisions_)
    {
      if ((int) share_face.size() < dim)  share_face.resize(dim);
      if ((int) wrap.size() < dim)        wrap.resize(dim);
      if ((int) ghosts.size() < dim)      ghosts.resize(dim);
      if ((int) divisions.size() < dim)   divisions.resize(dim);

      fill_divisions(divisions);
    }

    // Calls create(int gid, const Bounds& bounds, const Link& link)
    void            decompose(int rank, const StaticAssigner& assigner, const Creator& create);

    void            decompose(int rank, const StaticAssigner& assigner, Master& master, const Updater& update);

    void            decompose(int rank, const StaticAssigner& assigner, Master& master);

    // find lowest gid that owns a particular point
    template<class Point>
    int             lowest_gid(const Point& p) const;

    DivisionsVector gid_to_coords(int gid) const                                { DivisionsVector coords; gid_to_coords(gid, coords); return coords; }
    void            gid_to_coords(int gid, DivisionsVector& coords) const       { gid_to_coords(gid, coords, divisions); }
    int             coords_to_gid(const DivisionsVector& coords) const          { return coords_to_gid(coords, divisions); }
    void            fill_divisions(std::vector<int>& divisions) const;

    void            fill_bounds(Bounds& bounds, const DivisionsVector& coords, bool add_ghosts = false) const;
    void            fill_bounds(Bounds& bounds, int gid, bool add_ghosts = false) const;

    static bool     all(const std::vector<int>& v, int x);
    static void     gid_to_coords(int gid, DivisionsVector& coords, const DivisionsVector& divs);
    static int      coords_to_gid(const DivisionsVector& coords, const DivisionsVector& divs);

    static void     factor(std::vector<unsigned>& factors, int n);

    // Point to GIDs functions
    template<class Point>
    void            point_to_gids(std::vector<int>& gids, const Point& p) const;

    //! returns gid of a block that contains the point; ignores ghosts
    template<class Point>
    int             point_to_gid(const Point& p) const;

    template<class Point>
    int             num_gids(const Point& p) const;

    template<class Point>
    void            top_bottom(int& top, int& bottom, const Point& p, int axis) const;


    int               dim;
    Bounds            domain;
    int               nblocks;
    BoolVector        share_face;
    BoolVector        wrap;
    CoordinateVector  ghosts;
    DivisionsVector   divisions;

  };

  /**
   * \ingroup Decomposition
   * \brief Decomposes the domain into a prescribed pattern of blocks.
   *
   * @param dim        dimension of the domain
   * @param rank       local rank
   * @param assigner   decides how processors are assigned to blocks (maps a gid to a rank)
   *                   also communicates the total number of blocks
   * @param create     the callback functor
   * @param wrap       indicates dimensions on which to wrap the boundary
   * @param ghosts     indicates how many ghosts to use in each dimension
   * @param divs       indicates how many cuts to make along each dimension
   *                   (0 means "no constraint," i.e., leave it up to the algorithm)
   *
   * `create(...)` is called with each block assigned to the local domain. See [decomposition example](#decomposition-example).
   */
  template<class Bounds>
  void decompose(int                    dim,
                 int                    rank,
                 const Bounds&          domain,
                 const StaticAssigner&  assigner,
                 const typename RegularDecomposer<Bounds>::Creator&   create,
                 typename RegularDecomposer<Bounds>::BoolVector       share_face = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::BoolVector       wrap       = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::CoordinateVector ghosts     = typename RegularDecomposer<Bounds>::CoordinateVector(),
                 typename RegularDecomposer<Bounds>::DivisionsVector  divs       = typename RegularDecomposer<Bounds>::DivisionsVector())
  {
    RegularDecomposer<Bounds>(dim, domain, assigner.nblocks(), share_face, wrap, ghosts, divs).decompose(rank, assigner, create);
  }

  /**
   * \ingroup Decomposition
   * \brief Decomposes the domain into a prescribed pattern of blocks.
   *
   * @param dim        dimension of the domain
   * @param rank       local rank
   * @param assigner   decides how processors are assigned to blocks (maps a gid to a rank)
   *                   also communicates the total number of blocks
   * @param master     gets the blocks once this function returns
   * @param wrap       indicates dimensions on which to wrap the boundary
   * @param ghosts     indicates how many ghosts to use in each dimension
   * @param divs       indicates how many cuts to make along each dimension
   *                   (0 means "no constraint," i.e., leave it up to the algorithm)
   *
   * `master` must have been supplied a create function in order for this function to work.
   */
  template<class Bounds>
  void decompose(int                    dim,
                 int                    rank,
                 const Bounds&          domain,
                 const StaticAssigner&  assigner,
                 Master&                master,
                 typename RegularDecomposer<Bounds>::BoolVector       share_face = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::BoolVector       wrap       = typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::CoordinateVector ghosts     = typename RegularDecomposer<Bounds>::CoordinateVector(),
                 typename RegularDecomposer<Bounds>::DivisionsVector  divs       = typename RegularDecomposer<Bounds>::DivisionsVector())
  {
    RegularDecomposer<Bounds>(dim, domain, assigner.nblocks(), share_face, wrap, ghosts, divs).decompose(rank, assigner, master);
  }

  /**
   * \ingroup Decomposition
   * \brief A "null" decompositon that simply creates the blocks and adds them to the master
   *
   * @param rank       local rank
   * @param assigner   decides how processors are assigned to blocks (maps a gid to a rank)
   *                   also communicates the total number of blocks
   * @param master     gets the blocks once this function returns
   */
  inline
  void decompose(int                    rank,
                 const StaticAssigner&  assigner,
                 Master&                master)
  {
    std::vector<int>  local_gids;
    assigner.local_gids(rank, local_gids);

    for (size_t i = 0; i < local_gids.size(); ++i)
      master.add(local_gids[i], master.create(), new diy::Link);
  }

    /**
     * \ingroup Decomposition
     * \brief Add a decomposition (modify links) of an existing set of blocks that were
     * added to the master previously
     *
     * @param rank       local rank
     * @param assigner   decides how processors are assigned to blocks (maps a gid to a rank)
     *                   also communicates the total number of blocks
     */
  template<class Bounds>
  void decompose(int                    dim,
                 int                    rank,
                 const Bounds&          domain,
                 const StaticAssigner&  assigner,
                 Master&                master,
                 const typename RegularDecomposer<Bounds>::Updater&   update,
                 typename RegularDecomposer<Bounds>::BoolVector       share_face =
                 typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::BoolVector       wrap       =
                 typename RegularDecomposer<Bounds>::BoolVector(),
                 typename RegularDecomposer<Bounds>::CoordinateVector ghosts     =
                 typename RegularDecomposer<Bounds>::CoordinateVector(),
                 typename RegularDecomposer<Bounds>::DivisionsVector  divs       =
                 typename RegularDecomposer<Bounds>::DivisionsVector())
  {
      RegularDecomposer<Bounds>(dim, domain, assigner.nblocks(), share_face, wrap, ghosts, divs).
          decompose(rank, assigner, master, update);
  }

  //! Decomposition example: \example decomposition/test-decomposition.cpp
  //! Direct master insertion example: \example decomposition/test-direct-master.cpp
}

// decomposes domain and adds blocks to the master
template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
decompose(int rank, const StaticAssigner& assigner, Master& master)
{
  decompose(rank, assigner, [&master](int gid, const Bounds&, const Bounds&, const Bounds&, const Link& link)
  {
    void*     b = master.create();
    Link*     l = new Link(link);
    master.add(gid, b, l);
  });
}

template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
decompose(int rank, const StaticAssigner& assigner, const Creator& create)
{
  std::vector<int> gids;
  assigner.local_gids(rank, gids);
  for (int i = 0; i < (int)gids.size(); ++i)
  {
    int gid = gids[i];

    DivisionsVector coords;
    gid_to_coords(gid, coords);

    Bounds core(dim), bounds(dim);
    fill_bounds(core,   coords);
    fill_bounds(bounds, coords, true);

    // Fill link with all the neighbors
    Link link(dim, core, bounds);
    std::vector<int>  offsets(dim, -1);
    offsets[0] = -2;
    while (!all(offsets, 1))
    {
      // next offset
      int j;
      for (j = 0; j < dim; ++j)
        if (offsets[j] == 1)
          offsets[j] = -1;
        else
          break;
      ++offsets[j];

      if (all(offsets, 0)) continue;      // skip ourselves

      DivisionsVector     nhbr_coords(dim);
      Direction           dir(dim,0), wrap_dir(dim,0);
      bool                inbounds = true;
      for (int k = 0; k < dim; ++k)
      {
        nhbr_coords[k] = coords[k] + offsets[k];

        // wrap
        if (nhbr_coords[k] < 0)
        {
          if (wrap[k])
          {
            nhbr_coords[k] = divisions[k] - 1;
            wrap_dir[k] = -1;
          }
          else
            inbounds = false;
        }

        if (nhbr_coords[k] >= divisions[k])
        {
          if (wrap[k])
          {
            nhbr_coords[k] = 0;
            wrap_dir[k] = 1;
          }
          else
            inbounds = false;
        }

        // NB: this needs to match the addressing scheme in dir_t (in constants.h)
        if (offsets[k] == -1 || offsets[k] == 1)
          dir[k] = offsets[k];
      }
      if (!inbounds) continue;

      int nhbr_gid = coords_to_gid(nhbr_coords);
      BlockID bid; bid.gid = nhbr_gid; bid.proc = assigner.rank(nhbr_gid);
      link.add_neighbor(bid);

      Bounds nhbr_core(dim);
      fill_bounds(nhbr_core, nhbr_coords);
      link.add_core(nhbr_core);

      Bounds nhbr_bounds(dim);
      fill_bounds(nhbr_bounds, nhbr_coords, true);
      link.add_bounds(nhbr_bounds);

      link.add_direction(dir);
      link.add_wrap(wrap_dir);
    }

    create(gid, core, bounds, domain, link);
  }
}

// decomposes domain but does not add blocks to master, assumes they were added already
template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
decompose(int rank, const StaticAssigner& assigner, Master& master, const Updater& update)
{
    decompose(rank, assigner, [&master,&update](int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain_, const Link& link)
    {
        int lid = master.lid(gid);
        Link* l = new Link(link);
        master.replace_link(lid, l);
        update(gid, lid, core, bounds, domain_, *l);
    });
}

template<class Bounds>
bool
diy::RegularDecomposer<Bounds>::
all(const std::vector<int>& v, int x)
{
  for (unsigned i = 0; i < v.size(); ++i)
    if (v[i] != x)
      return false;
  return true;
}

template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
gid_to_coords(int gid, DivisionsVector& coords, const DivisionsVector& divs)
{
  coords.clear();
  for (int i = 0; i < static_cast<int>(divs.size()); ++i)
  {
    coords.push_back(gid % divs[i]);
    gid /= divs[i];
  }
}

template<class Bounds>
int
diy::RegularDecomposer<Bounds>::
coords_to_gid(const DivisionsVector& coords, const DivisionsVector& divs)
{
  int gid = 0;
  for (int i = static_cast<int>(coords.size()) - 1; i >= 0; --i)
  {
    gid *= divs[i];
    gid += coords[i];
  }
  return gid;
}

//! \ingroup Decomposition
//! Gets the bounds, with or without ghosts, for a block specified by its block coordinates
template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
fill_bounds(Bounds& bounds,                  //!< (output) bounds
            const DivisionsVector& coords,   //!< coordinates of the block in the decomposition
            bool add_ghosts)                 //!< whether to include ghosts in the output bounds
    const
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
      bounds.min[i] = (std::max)(domain.min[i], bounds.min[i] - ghosts[i]);
      bounds.max[i] = (std::min)(domain.max[i], bounds.max[i] + ghosts[i]);
    }
  }
}

//! \ingroup Decomposition
//! Gets the bounds, with or without ghosts, for a block specified by its gid
template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
fill_bounds(Bounds& bounds,                  //!< (output) bounds
            int gid,                         //!< global id of the block
            bool add_ghosts)                 //!< whether to include ghosts in the output bounds
    const
{
    DivisionsVector coords;
    gid_to_coords(gid, coords);
    if (add_ghosts)
        fill_bounds(bounds, coords, true);
    else
        fill_bounds(bounds, coords);
}

namespace diy { namespace detail {
// current state of division in one dimension used in fill_divisions below
template<class Coordinate>
struct Div
{
    int dim;                                 // 0, 1, 2, etc. e.g. for x, y, z etc.
    int nb;                                  // number of blocks so far in this dimension
    Coordinate b_size;                       // block size so far in this dimension

    // sort on descending block size unless tied, in which case
    // sort on ascending num blocks in current dim unless tied, in which case
    // sort on ascending dimension
    bool operator<(Div rhs) const
    {
        // sort on second value of the pair unless tied, in which case sort on first
        if (b_size == rhs.b_size)
        {
            if (nb == rhs.nb)
                return(dim < rhs.dim);
            return(nb < rhs.nb);
        }
        return(b_size > rhs.b_size);
    }
};
} }

template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
fill_divisions(std::vector<int>& divisions_) const
{
    // prod = number of blocks unconstrained by user; c = number of unconstrained dimensions
    int prod = 1; int c = 0;
    for (int i = 0; i < dim; ++i)
        if (divisions_[i] != 0)
        {
            prod *= divisions_[i];
            ++c;
        }

    if (nblocks % prod != 0)
        throw std::runtime_error("Total number of blocks cannot be factored into provided divs");

    if (c == (int) divisions_.size())               // nothing to do; user provided all divs
        return;

    // factor number of blocks left in unconstrained dimensions
    // factorization is sorted from smallest to largest factors
    std::vector<unsigned> factors;
    factor(factors, nblocks/prod);

    using detail::Div;
    std::vector< Div<Coordinate> > missing_divs;              // pairs consisting of (dim, #divs)

    // init missing_divs
    for (int i = 0; i < dim; i++)
    {
        if (divisions_[i] == 0)
        {
            Div<Coordinate> div;
            div.dim = i;
            div.nb = 1;
            div.b_size = domain.max[i] - domain.min[i];
            missing_divs.push_back(div);
        }
    }

    // iterate over factorization of number of blocks (factors are sorted smallest to largest)
    for (auto f = factors.rbegin(); f != factors.rend(); ++f)
    {
        // fill in missing divs by dividing dimension w/ largest block size
        // except when this would be illegal (resulting in bounds.max < bounds.min;
        // only a problem for discrete bounds

        // sort on decreasing block size
        std::sort(missing_divs.begin(), missing_divs.end());

        // split the dimension with the largest block size (first element in vector)
        Coordinate min =
            detail::BoundsHelper<Bounds>::from(0,
                                               missing_divs[0].nb * (*f),
                                               domain.min[missing_divs[0].dim],
                                               domain.max[missing_divs[0].dim],
                                               share_face[missing_divs[0].dim]);
        Coordinate max =
            detail::BoundsHelper<Bounds>::to(0,
                                             missing_divs[0].nb * (*f),
                                             domain.min[missing_divs[0].dim],
                                             domain.max[missing_divs[0].dim],
                                             share_face[missing_divs[0].dim]);
        if (max >= min)
        {
            missing_divs[0].nb    *= (*f);
            missing_divs[0].b_size = max - min;
        }
        else
        {
            std::ostringstream oss;
            oss << "Unable to decompose domain into " << nblocks << " blocks: " << min << " " << max;
            throw std::runtime_error(oss.str());
        }
    }

    // assign the divisions
    for (size_t i = 0; i < missing_divs.size(); i++)
        divisions_[missing_divs[i].dim] = missing_divs[i].nb;
}

template<class Bounds>
void
diy::RegularDecomposer<Bounds>::
factor(std::vector<unsigned>& factors, int n)
{
  while (n != 1)
    for (int i = 2; i <= n; ++i)
    {
      if (n % i == 0)
      {
        factors.push_back(i);
        n /= i;
        break;
      }
    }
}

// Point to GIDs
// TODO: add an optional ghosts argument to ignore ghosts (if we want to find the true owners, or something like that)
template<class Bounds>
template<class Point>
void
diy::RegularDecomposer<Bounds>::
point_to_gids(std::vector<int>& gids, const Point& p) const
{
    gids.clear();

    std::vector< std::pair<int, int> > ranges(dim);
    for (int i = 0; i < dim; ++i)
    {
        top_bottom(ranges[i].second, ranges[i].first, p, i);
        if (ranges[i].second <= ranges[i].first)
            return;
    }

    // look up gids for all combinations
    DivisionsVector coords(dim), location(dim);
    while(location.back() < ranges.back().second - ranges.back().first)
    {
        for (int i = 0; i < dim; ++i)
        {
            coords[i] = ranges[i].first + location[i];
            if (wrap[i])
                coords[i] = detail::normalize_coord(coords[i], divisions[i]);
        }
        gids.push_back(coords_to_gid(coords, divisions));

        location[0]++;
        unsigned i = 0;
        while (i < dim-1 && location[i] == ranges[i].second - ranges[i].first)
        {
            location[i] = 0;
            ++i;
            location[i]++;
        }
    }
}

template<class Bounds>
template<class Point>
int
diy::RegularDecomposer<Bounds>::
point_to_gid(const Point& p) const
{
    int gid = 0;
    for (int axis = dim - 1; axis >= 0; --axis)
    {
      Coordinate x = p[axis];
      if (wrap[axis])
          x = detail::normalize_wrap(x, domain.min[axis], domain.max[axis]);
      else if (x < domain.min[axis] || x > domain.max[axis])
          return -1;

      int bottom  = detail::BoundsHelper<Bounds>::lower(x, divisions[axis], domain.min[axis], domain.max[axis], share_face[axis]);
          bottom  = (std::max)(0, (std::min)(divisions[axis] - 1, bottom));

      // coupled with coords_to_gid
      gid *= divisions[axis];
      gid += bottom;
    }

    return gid;
}

template<class Bounds>
template<class Point>
int
diy::RegularDecomposer<Bounds>::
num_gids(const Point& p) const
{
    int res = 1;
    for (int i = 0; i < dim; ++i)
    {
        int top, bottom;
        top_bottom(top, bottom, p, i);
        res *= top - bottom;
    }
    return res;
}

template<class Bounds>
template<class Point>
void
diy::RegularDecomposer<Bounds>::
top_bottom(int& top, int& bottom, const Point& p, int axis) const
{
    Coordinate x = p[axis];
    if (wrap[axis])
        x = detail::normalize_wrap(x, domain.min[axis], domain.max[axis]);
    else if (x < domain.min[axis] || x > domain.max[axis])
    {
        bottom = 0;
        top = 0;
        return;
    }

    Coordinate l = x - ghosts[axis];
    Coordinate r = x + ghosts[axis];

    if (wrap[axis])
    {
        Coordinate period = detail::wrap_period(domain.min[axis], domain.max[axis]);
        int l_shift = detail::wrap_shift(l, domain.min[axis], domain.max[axis]);
        int r_shift = detail::wrap_shift(r, domain.min[axis], domain.max[axis]);
        Coordinate l_wrapped = l - static_cast<Coordinate>(l_shift) * period;
        Coordinate r_wrapped = r - static_cast<Coordinate>(r_shift) * period;

        bottom = l_shift * divisions[axis] + detail::BoundsHelper<Bounds>::lower(l_wrapped, divisions[axis], domain.min[axis], domain.max[axis], share_face[axis]);
        top    = r_shift * divisions[axis] + detail::BoundsHelper<Bounds>::upper(r_wrapped, divisions[axis], domain.min[axis], domain.max[axis], share_face[axis]);

        if (top - bottom > divisions[axis])
        {
            bottom = 0;
            top = divisions[axis];
        }
    } else
    {
        top     = detail::BoundsHelper<Bounds>::upper(r, divisions[axis], domain.min[axis], domain.max[axis], share_face[axis]);
        bottom  = detail::BoundsHelper<Bounds>::lower(l, divisions[axis], domain.min[axis], domain.max[axis], share_face[axis]);
        bottom  = (std::max)(0, bottom);
        top     = (std::min)(divisions[axis], top);
    }
}

// find lowest gid that owns a particular point
template<class Bounds>
template<class Point>
int
diy::RegularDecomposer<Bounds>::
lowest_gid(const Point& p) const
{
    // TODO: optimize - no need to compute all gids
    std::vector<int> gids;
    point_to_gids(gids, p);
    if (gids.empty())
        return -1;
    std::sort(gids.begin(), gids.end());
    return gids[0];
}

#endif
