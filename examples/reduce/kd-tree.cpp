#include <cmath>
#include <vector>
#include <cassert>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/reduce.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/partners/all-reduce.hpp>
#include <diy/partners/swap.hpp>
#include <diy/assigner.hpp>

#include "../opts.h"

typedef     diy::RegularContinuousLink  RCLink;
typedef     diy::ContinuousBounds       Bounds;

static const unsigned DIM = 3;

template<unsigned D>
struct SimplePoint
{
    float   coords[D];

    float&  operator[](unsigned i)                          { return coords[i]; }
    float   operator[](unsigned i) const                    { return coords[i]; }
};

float random(float min, float max)      { return min + float(rand() % 1024) / 1024 * (max - min); }

struct Block
{
  typedef         SimplePoint<DIM>                            Point;

                  Block(const Bounds& domain_, int bins_):
                      domain(domain_), bins(bins_)         {}

  static void*    create()                                      { return new Block; }
  static void     destroy(void* b)                              { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)          { diy::load(bb, *static_cast<Block*>(b)); }


  void            generate_points(size_t n)
  {
    box = domain;
    points.resize(n);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < DIM; ++j)
        points[i][j] = random(domain.min[j], domain.max[j]);
  }

  Bounds                domain;
  Bounds                box;
  std::vector<Point>    points;

  int                   bins;
  std::vector<size_t>   histogram;

  std::vector<Bounds>   block_bounds;                       // all block bounds, debugging purposes only

  private:
                  Block()                                  {}
};

struct WrapDomain
{
  bool          wrap;
  const Bounds& domain;
};

struct KDTreePartners
{
  // bool = are we in a swap (vs histogram) round
  // int  = round within that partner
  typedef           std::pair<bool, int>                    RoundType;

                    KDTreePartners(int dim, int nblocks, bool wrap_, const Bounds& domain_):
                        histogram(1, nblocks, 2),
                        swap(1, nblocks, 2, false),
                        wrap(wrap_),
                        domain(domain_)
  {
    for (unsigned i = 0; i < swap.rounds(); ++i)
    {
      // fill histogram rounds
      for (unsigned j = 0; j < histogram.rounds(); ++j)
      {
        rounds_.push_back(std::make_pair(false, j));
        dim_.push_back(i % dim);
        if (j == histogram.rounds() / 2 - 1 - i)
            j += 2*i;
      }

      // fill swap round
      rounds_.push_back(std::make_pair(true, i));
      dim_.push_back(i % dim);

      // fill link round
      rounds_.push_back(std::make_pair(true, -1));          // (true, -1) signals link round
      dim_.push_back(i % dim);
    }
  }

  size_t        rounds() const                              { return rounds_.size(); }
  size_t        swap_rounds() const                         { return swap.rounds(); }

  int           dim(int round) const                        { return dim_[round]; }
  bool          swap_round(int round) const                 { return rounds_[round].first; }
  int           sub_round(int round) const                  { return rounds_[round].second; }

  inline bool   active(int round, int gid, const diy::Master& m) const
  {
    if (round == rounds())
        return true;
    else if (swap_round(round) && sub_round(round) < 0)     // link round
        return true;
    else if (swap_round(round))
        return swap.active(sub_round(round), gid, m);
    else
        return histogram.active(sub_round(round), gid, m);
  }

  inline void   incoming(int round, int gid, std::vector<int>& partners, const diy::Master& m) const
  {
    if (round == rounds())
        link_neighbors(-1, gid, partners, m);
    else if (swap_round(round) && sub_round(round) < 0)       // link round
        swap.incoming(sub_round(round - 1) + 1, gid, partners, m);
    else if (swap_round(round))
        histogram.incoming(histogram.rounds(), gid, partners, m);
    else
    {
        if (round > 0 && sub_round(round) == 0)
            link_neighbors(-1, gid, partners, m);
        else if (round > 0 && sub_round(round - 1) != sub_round(round) - 1)        // jump through the histogram rounds
            histogram.incoming(sub_round(round - 1) + 1, gid, partners, m);
        else
            histogram.incoming(sub_round(round), gid, partners, m);
    }
  }

  inline void   outgoing(int round, int gid, std::vector<int>& partners, const diy::Master& m) const
  {
    if (round == rounds())
        swap.outgoing(sub_round(round-1) + 1, gid, partners, m);
    else if (swap_round(round) && sub_round(round) < 0)       // link round
        link_neighbors(-1, gid, partners, m);
    else if (swap_round(round))
        swap.outgoing(sub_round(round), gid, partners, m);
    else
        histogram.outgoing(sub_round(round), gid, partners, m);
  }

  inline void   link_neighbors(int, int gid, std::vector<int>& partners, const diy::Master& m) const
  {
    int         lid  = m.lid(gid);
    diy::Link*  link = m.link(lid);

    std::set<int> result;       // partners must be unique
    for (size_t i = 0; i < link->size(); ++i)
        result.insert(link->target(i).gid);

    for (std::set<int>::const_iterator it = result.begin(); it != result.end(); ++it)
        partners.push_back(*it);
  }

  diy::RegularAllReducePartners     histogram;
  diy::RegularSwapPartners          swap;

  std::vector<RoundType>            rounds_;
  std::vector<int>                  dim_;

  bool                              wrap;
  Bounds                            domain;
};

void compute_local_histogram(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block* b = static_cast<Block*>(b_);

    // compute and enqueue local histogram
    b->histogram.clear();
    b->histogram.resize(b->bins);
    float   width = (b->box.max[dim] - b->box.min[dim])/b->bins;
    for (size_t i = 0; i < b->points.size(); ++i)
    {
        float x = b->points[i][dim];
        int loc = (x - b->box.min[dim]) / width;
        if (loc < 0)
        {
            std::cerr << loc << " " << x << " " << b->box.min[dim] << std::endl;
            std::abort();
        }
        if (loc >= b->bins)
            loc = b->bins - 1;
        ++(b->histogram[loc]);
    }
    if (srp.out_link().target(0).gid != srp.gid())
        srp.enqueue(srp.out_link().target(0), b->histogram);
}

void add_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block* b = static_cast<Block*>(b_);

    // dequeue and add up the histograms
    for (unsigned i = 0; i < srp.in_link().size(); ++i)
    {
        int nbr_gid = srp.in_link().target(i).gid;
        if (nbr_gid != srp.gid())
        {
            std::vector<size_t> hist;
            srp.dequeue(nbr_gid, hist);
            for (size_t i = 0; i < hist.size(); ++i)
                b->histogram[i] += hist[i];
        }
    }
    if (srp.out_link().target(0).gid != srp.gid())
        srp.enqueue(srp.out_link().target(0), b->histogram);
}

void receive_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block* b = static_cast<Block*>(b_);

    if (srp.in_link().target(0).gid != srp.gid())
        srp.dequeue(srp.in_link().target(0).gid, b->histogram);
}

void forward_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block* b = static_cast<Block*>(b_);

    for (unsigned i = 0; i < srp.out_link().size(); ++i)
        if (srp.out_link().target(i).gid != srp.gid())
            srp.enqueue(srp.out_link().target(i), b->histogram);
}

void enqueue_exchange(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block*   b        = static_cast<Block*>(b_);

    int k = srp.out_link().size();

    // pick split points
    size_t total = 0;
    for (size_t i = 0; i < b->histogram.size(); ++i)
        total += b->histogram[i];

    size_t cur   = 0;
    float  width = (b->box.max[dim] - b->box.min[dim])/b->bins;
    float  split;
    for (size_t i = 0; i < b->histogram.size(); ++i)
    {
        if (cur + b->histogram[i] > total/2)
        {
            split = b->box.min[dim] + width*i + width/2;   // mid-point of the bin
            break;
        }

        cur += b->histogram[i];
    }

    // subset and enqueue
    if (srp.out_link().size() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Block::Point> > out_points(srp.out_link().size());
    for (size_t i = 0; i < b->points.size(); ++i)
    {
      float x = b->points[i][dim];
      int loc = x < split ? 0 : 1;
      out_points[loc].push_back(b->points[i]);
    }
    int pos = -1;
    for (int i = 0; i < k; ++i)
    {
      if (srp.out_link().target(i).gid == srp.gid())
      {
        b->points.swap(out_points[i]);
        pos = i;
      }
      else
        srp.enqueue(srp.out_link().target(i), out_points[i]);
    }
    if (pos == 0)
        b->box.max[dim] = split;
    else
        b->box.min[dim] = split;
}

void dequeue_exchange(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block*   b        = static_cast<Block*>(b_);

    for (unsigned i = 0; i < srp.in_link().size(); ++i)
    {
      int nbr_gid = srp.in_link().target(i).gid;
      if (nbr_gid == srp.gid())
          continue;

      std::vector<Block::Point>    in_points;
      srp.dequeue(nbr_gid, in_points);
      for (size_t j = 0; j < in_points.size(); ++j)
      {
        if (in_points[j][dim] < b->box.min[dim] || in_points[j][dim] > b->box.max[dim])
        {
            fprintf(stderr, "Warning: dequeued %f outside [%f,%f] (%d)\n",
                            in_points[j][dim], b->box.min[dim], b->box.max[dim], dim);
            std::abort();
        }
        b->points.push_back(in_points[j]);
      }
    }
}

void update_neighbor_bounds(Bounds& bounds, float split, int dim, bool lower)
{
    if (lower)
        bounds.max[dim] = split;
    else
        bounds.min[dim] = split;
}

bool intersects(const Bounds& x, const Bounds& y, int dim, bool wrap, const Bounds& domain)
{
    if (wrap)
    {
        if (x.min[dim] == domain.min[dim] && y.max[dim] == domain.max[dim])
            return true;
        if (y.min[dim] == domain.min[dim] && x.max[dim] == domain.max[dim])
            return true;
    }
    return x.min[dim] <= y.max[dim] && y.min[dim] <= x.max[dim];
}

float find_split(const Bounds& changed, const Bounds& original)
{
    diy::Direction dir = DIY_X0;
    for (int i = 0; i < DIM; ++i)
    {
        if (changed.min[i] != original.min[i])
            return changed.min[i];
        dir = static_cast<diy::Direction>(dir << 1);
        if (changed.max[i] != original.max[i])
            return changed.max[i];
        dir = static_cast<diy::Direction>(dir << 1);
    }
    assert(0);
    return -1;
}

void print_block(void* b_, const diy::Master::ProxyWithLink& cp, void* verbose_);

int divide_gid(int gid, bool lower, int round, int rounds)
{
    if (lower)
        gid &= ~(1 << (rounds - 1 - round));
    else
        gid |=  (1 << (rounds - 1 - round));
    return gid;
}

// round here is the outer iteration of the algorithm
void update_links(void* b_, const diy::ReduceProxy& srp, int dim, int round, int rounds, bool wrap, const Bounds& domain)
{
    Block*      b    = static_cast<Block*>(b_);
    int         gid  = srp.gid();
    int         lid  = srp.master()->lid(gid);
    RCLink*     link = static_cast<RCLink*>(srp.master()->link(lid));

    // (gid, dir) -> i
    std::map<std::pair<int,diy::Direction>, int> link_map;
    for (int i = 0; i < link->size(); ++i)
        link_map[std::make_pair(link->target(i).gid, link->direction(i))] = i;

    // NB: srp.enqueue(..., ...) should match the link
    std::vector<float>  splits(link->size());
    for (int i = 0; i < link->size(); ++i)
    {
        float split; diy::Direction dir;

        int in_gid = link->target(i).gid;
        while(srp.incoming(in_gid))
        {
            srp.dequeue(in_gid, split);
            srp.dequeue(in_gid, dir);

            // reverse dir
            int j = 0;
            while (dir >> (j + 1))
                ++j;

            if (j % 2 == 0)
                dir = static_cast<diy::Direction>(dir << 1);
            else
                dir = static_cast<diy::Direction>(dir >> 1);

            int k = link_map[std::make_pair(in_gid, dir)];
            //printf("%d %d %f -> %d\n", in_gid, dir, split, k);
            splits[k] = split;
        }
    }

    RCLink      new_link(DIM, b->box, b->box);

    diy::Direction left  = static_cast<diy::Direction>(1 <<   2*dim);
    diy::Direction right = static_cast<diy::Direction>(1 <<  (2*dim + 1));

    bool lower = !(gid & (1 << (rounds - 1 - round)));

    // fill out the new link
    for (int i = 0; i < link->size(); ++i)
    {
        diy::Direction  dir = link->direction(i);
        if (dir == left || dir == right)
        {
            if ((dir == left && lower) || (dir == right && !lower))
            {
                int nbr_gid = divide_gid(link->target(i).gid, dir != left, round, rounds);
                diy::BlockID nbr = { nbr_gid, srp.assigner().rank(nbr_gid) };
                new_link.add_neighbor(nbr);

                new_link.add_direction(dir);

                Bounds bounds = link->bounds(i);
                update_neighbor_bounds(bounds, splits[i], dim, dir != left);
                new_link.add_bounds(bounds);
            }
        } else // non-aligned side
        {
            for (int j = 0; j < 2; ++j)
            {
                int nbr_gid = divide_gid(link->target(i).gid, j == 0, round, rounds);

                Bounds  bounds  = link->bounds(i);
                update_neighbor_bounds(bounds, splits[i], dim, j == 0);

                if (intersects(bounds, new_link.bounds(), dim, wrap, domain))
                {
                    diy::BlockID nbr = { nbr_gid, srp.assigner().rank(nbr_gid) };
                    new_link.add_neighbor(nbr);
                    new_link.add_direction(dir);
                    new_link.add_bounds(bounds);
                }
            }
        }
    }

    // add link to the dual block
    int dual_gid = divide_gid(gid, !lower, round, rounds);
    diy::BlockID dual = { dual_gid, srp.assigner().rank(dual_gid) };
    new_link.add_neighbor(dual);

    Bounds nbr_bounds = link->bounds();     // old block bounds
    update_neighbor_bounds(nbr_bounds, find_split(new_link.bounds(), nbr_bounds), dim, !lower);
    new_link.add_bounds(nbr_bounds);

    if (lower)
        new_link.add_direction(right);
    else
        new_link.add_direction(left);

    // update the link; notice that this won't conflict with anything since
    // reduce is using its own notion of the link constructed through the
    // partners
    link->swap(new_link);
}

void split_to_neighbors(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block*      b    = static_cast<Block*>(b_);
    int         lid  = srp.master()->lid(srp.gid());
    RCLink*     link = static_cast<RCLink*>(srp.master()->link(lid));

    // determine split
    float split = find_split(b->box, link->bounds());

    for (size_t i = 0; i < link->size(); ++i)
    {
        srp.enqueue(link->target(i), split);
        srp.enqueue(link->target(i), link->direction(i));
    }
}

void partition(void* b_, const diy::ReduceProxy& srp, const KDTreePartners& partners)
{
    int dim;
    if (srp.round() < partners.rounds())
        dim = partners.dim(srp.round());
    else
        dim = partners.dim(srp.round() - 1);

    if (srp.round() == partners.rounds())
        update_links(b_, srp, dim, partners.sub_round(srp.round() - 2), partners.swap_rounds(), partners.wrap, partners.domain); // -1 would be the "uninformative" link round
    else if (partners.swap_round(srp.round()) && partners.sub_round(srp.round()) < 0)       // link round
    {
        dequeue_exchange(b_, srp, dim);         // from the swap round
        split_to_neighbors(b_, srp, dim);
    }
    else if (partners.swap_round(srp.round()))
    {
        receive_histogram(b_, srp);
        enqueue_exchange(b_, srp, dim);
    } else if (partners.sub_round(srp.round()) == 0)
    {
        if (srp.round() > 0)
        {
            int prev_dim = dim - 1;
            if (prev_dim < 0)
                prev_dim += DIM;
            update_links(b_, srp, prev_dim, partners.sub_round(srp.round() - 2), partners.swap_rounds(), partners.wrap, partners.domain);    // -1 would be the "uninformative" link round
        }

        compute_local_histogram(b_, srp, dim);
    } else if (partners.sub_round(srp.round()) < partners.histogram.rounds()/2)
        add_histogram(b_, srp);
    else
    {
        receive_histogram(b_, srp);
        forward_histogram(b_, srp);
    }
}

void print_block(void* b_, const diy::Master::ProxyWithLink& cp, void* verbose_)
{
  Block*   b         = static_cast<Block*>(b_);
  bool     verbose   = *static_cast<bool*>(verbose_);
  RCLink*  link      = static_cast<RCLink*>(cp.link());

  fprintf(stdout, "%d: [%f,%f,%f] - [%f,%f,%f] (%d neighbors)\n",
                  cp.gid(),
                  b->box.min[0], b->box.min[1], b->box.min[2],
                  b->box.max[0], b->box.max[1], b->box.max[2],
                  link->size());

  for (size_t i = 0; i < link->size(); ++i)
  {
      fprintf(stdout, "  (%d,%d,%d):", link->target(i).gid, link->target(i).proc, link->direction(i));
      const Bounds& bounds = link->bounds(i);
      fprintf(stdout, " [%f,%f,%f] - [%f,%f,%f]\n",
              bounds.min[0], bounds.min[1], bounds.min[2],
              bounds.max[0], bounds.max[1], bounds.max[2]);
  }

  if (verbose)
    for (size_t i = 0; i < b->points.size(); ++i)
      fprintf(stdout, "  %f %f %f\n", b->points[i][0], b->points[i][1], b->points[i][2]);
}

inline
bool
operator==(const diy::ContinuousBounds& x, const diy::ContinuousBounds& y)
{
    for (int i = 0; i < DIM; ++i)
    {
        if (x.min[i] != y.min[i])
            return false;
        if (x.max[i] != y.max[i])
            return false;
    }
    return true;
}

inline
bool
operator!=(const diy::ContinuousBounds& x, const diy::ContinuousBounds& y)
{
    return !(x == y);
}

void verify_block(void* b_, const diy::Master::ProxyWithLink& cp, void* wrap_domain)
{
  Block*   b    = static_cast<Block*>(b_);
  RCLink*  link = static_cast<RCLink*>(cp.link());

  bool          wrap    = static_cast<WrapDomain*>(wrap_domain)->wrap;
  const Bounds& domain  = static_cast<WrapDomain*>(wrap_domain)->domain;

  for (size_t i = 0; i < b->points.size(); ++i)
    for (int j = 0; j < DIM; ++j)
      if (b->points[i][j] < b->box.min[j] || b->points[i][j] > b->box.max[j])
        fprintf(stdout, "Warning: %f outside of [%f,%f] (%d)\n", b->points[i][j], b->box.min[j], b->box.max[j], j);

  // verify neighbor bounds
  for (int i = 0; i < link->size(); ++i)
  {
      int nbr_gid = link->target(i).gid;
      if (link->bounds(i) != b->block_bounds[nbr_gid])
      {
          fprintf(stderr, "Warning: bounds don't match %d -> %d\n", cp.gid(), link->target(i).gid);
          fprintf(stderr, "  expected: [%f,%f,%f] - [%f,%f,%f]\n",
                          link->bounds(i).min[0], link->bounds(i).min[1], link->bounds(i).min[2],
                          link->bounds(i).max[0], link->bounds(i).max[1], link->bounds(i).max[2]);
          fprintf(stderr, "  got:      [%f,%f,%f] - [%f,%f,%f]\n",
                          b->block_bounds[nbr_gid].min[0], b->block_bounds[nbr_gid].min[1], b->block_bounds[nbr_gid].min[2],
                          b->block_bounds[nbr_gid].max[0], b->block_bounds[nbr_gid].max[1], b->block_bounds[nbr_gid].max[2]);
      }
  }

  // verify that we intersect everybody in the link
  for (int i = 0; i < link->size(); ++i)
      for (int j = 0; j < DIM; ++j)
      {
          if (!intersects(b->box, link->bounds(i), j, wrap, domain))
              fprintf(stderr, "Warning: we don't intersect a block in the link: %d -> %d\n", cp.gid(), link->target(i).gid);
      }

  // verify that we don't intersect anybody not in the link
  for (size_t i = 0; i < b->block_bounds.size(); ++i)
  {
      if (i == cp.gid()) continue;
      int j = 0;
      for (; j < DIM; ++j)
      {
          if (!intersects(b->box, b->block_bounds[i], j, wrap, domain))
              break;
      }
      if (j == DIM)     // intersect
      {
          int k = 0;
          for (; k < link->size(); ++k)
          {
              if (link->target(k).gid == i)
                  break;
          }
          if (k == link->size())
              fprintf(stderr, "Warning: we intersect a block not in the link: %d -/-> %lu\n", cp.gid(), i);
      }
  }
}

// for debugging: everybody sends their bounds to everybody else
void exchange_bounds(void* b_, const diy::ReduceProxy& srp)
{
  Block*   b   = static_cast<Block*>(b_);

  if (srp.round() == 0)
      for (int i = 0; i < srp.out_link().size(); ++i)
          srp.enqueue(srp.out_link().target(i), b->box);
  else
  {
      b->block_bounds.resize(srp.in_link().size());
      for (int i = 0; i < srp.in_link().size(); ++i)
      {
        assert(i == srp.in_link().target(i).gid);
        srp.dequeue(srp.in_link().target(i).gid, b->block_bounds[i]);
      }
  }
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  using namespace opts;
  Options ops(argc, argv);

  int               nblocks     = world.size();
  size_t            num_points  = 100;
  int               hist        = 32;
  int               mem_blocks  = -1;
  int               threads     = 1;
  std::string       prefix      = "./DIY.XXXXXX";
  bool              verbose     = ops >> Present('v', "verbose", "verbose output");

  ops
      >> Option('n', "number",  num_points,     "number of points per block")
      >> Option(     "hist",    hist,           "histogram multiplier")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
  ;
  bool wrap = ops >> Present('w', "wrap", "use periodic boundary");

  if (ops >> Present('h', "help", "show help"))
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
          std::cout << ops;
      }
      return 1;
  }

  diy::FileStorage          storage(prefix);
  diy::Master               master(world,
                                   threads,
                                   mem_blocks,
                                   &Block::create,
                                   &Block::destroy,
                                   &storage,
                                   &Block::save,
                                   &Block::load);

  //srand(time(0));

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  Bounds domain;
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 1000;

  // initially fill the blocks with random points anywhere in the domain
  std::vector<int> gids;
  assigner.local_gids(world.rank(), gids);
  for (unsigned i = 0; i < gids.size(); ++i)
  {
    int             gid = gids[i];
    Block*          b   = new Block(domain, 2*hist);
    RCLink*         l   = new RCLink(DIM, domain, domain);

    if (wrap)
    {
        // link to self in every direction
        for (int j = 0; j < DIM; ++j)
            for (int k = 0; k < 2; ++k)
            {
                diy::BlockID nbr = { gid, world.rank() };
                l->add_neighbor(nbr);

                diy::Direction dir = static_cast<diy::Direction>(1 << (2*j + k));
                l->add_direction(dir);

                l->add_bounds(domain);
            }
    }

    // this could be replaced by reading values from a file
    b->generate_points(num_points);

    master.add(gid, b, l);
  }
  std::cout << "Blocks generated" << std::endl;

  KDTreePartners partners(DIM, nblocks, wrap, domain);
  diy::reduce(master, assigner, partners, &partition);

  // update master.expected to match the links
  int expected = 0;
  for (int i = 0; i < master.size(); ++i)
    expected += master.link(i)->size_unique();
  master.set_expected(expected);
  std::cout << "Expected set to " << expected << std::endl;

  // debugging
  master.foreach(&print_block, &verbose);
  diy::all_to_all(master, assigner, exchange_bounds);
  WrapDomain wrap_domain = { wrap, domain };
  master.foreach(&verify_block, &wrap_domain);
  if (world.rank() == 0)
    std::cout << "Blocks verified" << std::endl;
}
