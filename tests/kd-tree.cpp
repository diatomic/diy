#include <cmath>
#include <vector>
#include <cassert>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/reduce.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/partners/swap.hpp>
#include <diy/assigner.hpp>

#include <diy/algorithms.hpp>

#include "opts.h"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

typedef     diy::RegularContinuousLink  RCLink;
typedef     diy::ContinuousBounds       Bounds;

static const unsigned DIM = 2;

template<unsigned D>
struct SimplePoint
{
    float   coords[D];

    float&  operator[](unsigned i)                          { return coords[i]; }
    float   operator[](unsigned i) const                    { return coords[i]; }
};

struct Block
{
  typedef         SimplePoint<DIM>                            Point;

                  Block(const Bounds& domain_):
                      domain(domain_)                           {}

  static void*    create()                                      { return new Block; }
  static void     destroy(void* b)                              { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)          { diy::load(bb, *static_cast<Block*>(b)); }

  void            generate_points(size_t n)
  {
    points.resize(n);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < DIM; ++j)
      {
        float min = domain.min[j];
        float max = domain.max[j];
        float u = float(rand() % 1024) / 1024;
        points[i][j] = min + u * (max - min);
      }
  }

  void            generate_points_exponential(size_t n)
  {
    points.resize(n);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < DIM; ++j)
      {
        float min = domain.min[j];
        float max = domain.max[j];
        float u = float(rand() % 1024) / 1024;
        float x = min - log(u) * 10 * log(2) / (max - min);     // median at min + (max - min) / 10
        if (x < min)
          points[i][j] = min;
        else if (x > max)
          points[i][j] = max;
        else
          points[i][j] = x;
      }
  }

  Bounds                domain;
  std::vector<Point>    points;

  std::vector<Bounds>   block_bounds;                       // all block bounds, debugging purposes only

  private:
                  Block()                                  {}
};

struct WrapDomain
{
  bool          wrap;
  const Bounds& domain;
};

void print_block(void* b_, const diy::Master::ProxyWithLink& cp, void* verbose_)
{
  Block*   b         = static_cast<Block*>(b_);
  bool     verbose   = *static_cast<bool*>(verbose_);
  RCLink*  link      = static_cast<RCLink*>(cp.link());

  fprintf(stdout, "%d: [%f,%f,%f] - [%f,%f,%f] (%d neighbors): %lu points\n",
                  cp.gid(),
                  link->bounds().min[0], link->bounds().min[1], link->bounds().min[2],
                  link->bounds().max[0], link->bounds().max[1], link->bounds().max[2],
                  link->size(), b->points.size());

  for (int i = 0; i < link->size(); ++i)
  {
      fprintf(stdout, "  (%d,%d,(%d,%d,%d)):",
                      link->target(i).gid, link->target(i).proc,
                      link->direction(i)[0],
                      link->direction(i)[1],
                      link->direction(i)[2]);
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
    for (unsigned i = 0; i < DIM; ++i)
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

void verify_block(void* b_, const diy::Master::ProxyWithLink& cp, void* wrap_domain)
{
  Block*   b    = static_cast<Block*>(b_);
  RCLink*  link = static_cast<RCLink*>(cp.link());

  bool          wrap    = static_cast<WrapDomain*>(wrap_domain)->wrap;
  const Bounds& domain  = static_cast<WrapDomain*>(wrap_domain)->domain;

  for (size_t i = 0; i < b->points.size(); ++i)
    for (unsigned j = 0; j < DIM; ++j)
    {
      INFO("Point " << i << " outside bounds in dimension " << j);
      CHECK(b->points[i][j] >= link->bounds().min[j]);
      CHECK(b->points[i][j] <= link->bounds().max[j]);
    }

  // verify neighbor bounds
  for (int i = 0; i < link->size(); ++i)
  {
      int nbr_gid = link->target(i).gid;
      INFO("Checking that bounds in the link match actual remote block bounds for gid = " << nbr_gid);
      CHECK (link->bounds(i) == b->block_bounds[nbr_gid]);
  }

  // verify wrap
  if (wrap)
      for (int i = 0; i < link->size(); ++i)
      {
          for (unsigned j = 0; j < DIM; ++j)
          {
              if (link->wrap(i)[j] == -1)
              {
                  INFO("Checking wrap matches: " << cp.gid() << " -> " << link->target(i).gid);
                  CHECK(link->bounds().min[j]  == domain.min[j]);
                  CHECK(link->bounds(i).max[j] == domain.max[j]);
              }
              if (link->wrap(i)[j] ==  1)
              {
                  INFO("Checking wrap matches: " << cp.gid() << " -> " << link->target(i).gid);
                  CHECK(link->bounds().max[j]  == domain.max[j]);
                  CHECK(link->bounds(i).min[j] == domain.min[j]);
              }
          }
      }


  // verify that we intersect everybody in the link
  for (int i = 0; i < link->size(); ++i)
      for (unsigned j = 0; j < DIM; ++j)
      {
          INFO("Checking neighbor intersection: " << cp.gid() << " -> " << link->target(i).gid);
          CHECK(intersects(link->bounds(), link->bounds(i), j, wrap, domain));
      }

  // verify that we don't intersect anybody not in the link
  for (int i = 0; i < (int) b->block_bounds.size(); ++i)
  {
      if (i == cp.gid()) continue;
      unsigned j = 0;
      for (; j < DIM; ++j)
      {
          if (!intersects(link->bounds(), b->block_bounds[i], j, wrap, domain))
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

          INFO("Checking whether we intersect a block not in the link: " << cp.gid() << " -/-> " << i);
          CHECK(k != link->size());
      }
  }
}

// for debugging: everybody sends their bounds to everybody else
void exchange_bounds(void* b_, const diy::ReduceProxy& srp)
{
  Block*   b   = static_cast<Block*>(b_);

  if (srp.round() == 0)
      for (int i = 0; i < srp.out_link().size(); ++i)
      {
          RCLink* link = static_cast<RCLink*>(srp.master()->link(srp.master()->lid(srp.gid())));
          srp.enqueue(srp.out_link().target(i), link->bounds());
      }
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

void min_max(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
  Block*   b   = static_cast<Block*>(b_);
  cp.all_reduce(b->points.size(), diy::mpi::minimum<size_t>());
  cp.all_reduce(b->points.size(), diy::mpi::maximum<size_t>());
}

struct KDTreeFixture
{
    // command-line arguments
    static int          nblocks;
    static size_t       num_points;
    static int          hist;
    static int          mem_blocks;
    static int          threads;
    static std::string  prefix;
    static bool         verbose;

    static bool         wrap;
    static bool         sample;
    static bool         exponential;

    diy::mpi::communicator    world;
};

int         KDTreeFixture::nblocks     = 0;
size_t      KDTreeFixture::num_points  = 100;
int         KDTreeFixture::hist        = 32;
int         KDTreeFixture::mem_blocks  = -1;
int         KDTreeFixture::threads     = 1;
std::string KDTreeFixture::prefix      = "./DIY.XXXXXX";
bool        KDTreeFixture::verbose     = false;
bool        KDTreeFixture::wrap        = false;
bool        KDTreeFixture::sample      = false;
bool        KDTreeFixture::exponential = false;


TEST_CASE_METHOD(KDTreeFixture, "k-d tree is built", "[kdtree]")
{
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
    Block*          b   = new Block(domain);
    RCLink*         l   = new RCLink(DIM, domain, domain);

    // this could be replaced by reading values from a file
    if (exponential)
      b->generate_points_exponential(num_points);
    else
      b->generate_points(num_points);

    master.add(gid, b, l);
  }
  std::cout << "Blocks generated" << std::endl;

  if (sample)
    diy::kdtree_sampling(master, assigner, DIM, domain, &Block::points, 2*hist, wrap);
  else
    diy::kdtree(master, assigner, DIM, domain, &Block::points, 2*hist, wrap);

  // debugging
  master.foreach(&print_block, &verbose);
  diy::all_to_all(master, assigner, &exchange_bounds);
  WrapDomain wrap_domain = { wrap, domain };
  master.set_threads(1);        // catch.hpp isn't thread-safe
  master.foreach(&verify_block, &wrap_domain);
  if (world.rank() == 0)
    std::cout << "Blocks verified" << std::endl;
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  Catch::Session session;

  using namespace opts;
  Options ops(argc, argv);

  KDTreeFixture::nblocks     = world.size();
  KDTreeFixture::verbose     = ops >> Present('v', "verbose", "verbose output");

  ops
      >> Option('n', "number",  KDTreeFixture::num_points,     "number of points per block")
      >> Option(     "hist",    KDTreeFixture::hist,           "histogram multiplier")
      >> Option('b', "blocks",  KDTreeFixture::nblocks,        "number of blocks")
      >> Option('t', "thread",  KDTreeFixture::threads,        "number of threads")
      >> Option(     "prefix",  KDTreeFixture::prefix,         "prefix for external storage")
  ;
  KDTreeFixture::wrap        = ops >> Present('w', "wrap", "use periodic boundary");
  KDTreeFixture::sample      = ops >> Present('s', "sample", "use sampling k-d tree");
  KDTreeFixture::exponential = ops >> Present('e', "exponential", "use exponential distribution of points");

  if (ops >> Present('h', "help", "show help"))
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
          std::cout << ops;
      }
      return 1;
  }

  return session.run();
}
