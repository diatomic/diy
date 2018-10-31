#include <cmath>
#include <cassert>

#include <diy/master.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

#include "../opts.h"

#include "point.h"

typedef     diy::ContinuousBounds               Bounds;
typedef     diy::RegularContinuousLink          RCLink;
typedef     diy::RegularDecomposer<Bounds>      Decomposer;

static const unsigned DIM = 3;
typedef     PointBlock<DIM>                     Block;
typedef     AddPointBlock<DIM>                  AddBlock;

struct Redistribute
{
       Redistribute(const Decomposer& decomposer_):
           decomposer(decomposer_)              {}

  void operator()(Block* b, const diy::ReduceProxy& rp) const
  {
      if (rp.in_link().size() == 0)
      {
          // queue points to the correct blocks
          for (size_t i = 0; i < b->points.size(); ++i)
          {
            int dest_gid = decomposer.point_to_gid(b->points[i]);
            diy::BlockID dest = rp.out_link().target(dest_gid);        // out_link targets are ordered as gids
            assert(dest.gid == dest_gid);
            rp.enqueue(dest, b->points[i]);

#if 0
            // DEBUG
            Decomposer::DivisionsVector coords;
            decomposer.gid_to_coords(dest_gid, coords);
            Bounds bounds;
            decomposer.fill_bounds(bounds, coords);

            for (int j = 0; j < DIM; ++j)
              if (b->points[i][j] < bounds.min[j] || b->points[i][j] > bounds.max[j])
              {
                  fmt::print(stderr, "!!! Point sent outside the target box !!!\n");
                  fmt::print(stderr, "    {} {} {}\n", b->points[i][0], b->points[i][1], b->points[i][2]);
                  fmt::print(stderr, "    {} {} {} - {} {} {}\n",
                                          bounds.min[0], bounds.min[1], bounds.min[2],
                                          bounds.max[0], bounds.max[1], bounds.max[2]);
              }
#endif
          }
          b->points.clear();
      } else
      {
          // in this example, box is not updated during the reduction, so just set it to bounds
          b->box = b->bounds;

          // add up the total number of points
          size_t total = 0;
          for (int i = 0; i < rp.in_link().size(); ++i)
          {
            int gid = rp.in_link().target(i).gid;
            assert(gid == i);

            diy::MemoryBuffer& incoming = rp.incoming(gid);
            size_t incoming_sz  = incoming.size() / sizeof(Block::Point);
            total += incoming_sz;
          }

          // resize and copy out the points
          b->points.resize(total);
          size_t sz = 0;
          for (int i = 0; i < rp.in_link().size(); ++i)
          {
            int gid = rp.in_link().target(i).gid;
            diy::MemoryBuffer& incoming = rp.incoming(gid);
            size_t incoming_sz  = incoming.size() / sizeof(Block::Point);
            std::copy((Block::Point*) &incoming.buffer[0],
                      (Block::Point*) &incoming.buffer[0] + incoming_sz,
                      &b->points[sz]);
            sz += incoming_sz;
          }
      }
  }

  const Decomposer& decomposer;
};

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  int                       nblocks     = world.size();
  size_t                    num_points  = 100;      // points per block
  int                       mem_blocks  = -1;
  int                       threads     = -1;
  int                       k           = 2;
  std::string               prefix      = "./DIY.XXXXXX";
  std::string               log_level   = "info";

  Bounds domain(DIM);
  for (unsigned i = 0; i < DIM; ++i)
  {
      domain.min[i] = 0;
      domain.max[i] = 100.;
  }

  bool  verbose, help;

  using namespace opts;
  Options ops;

  ops
      >> Option('n', "number",  num_points,     "number of points per block")
      >> Option('k', "k",       k,              "use k-ary swap")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option('m', "memory",  mem_blocks,     "number of blocks to keep in memory")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
      >> Option('l', "log",     log_level,      "log level")
  ;

  ops
      >> Option('x',  "max-x",  domain.max[0],  "domain max x")
      >> Option('y',  "max-y",  domain.max[1],  "domain max y")
      >> Option('z',  "max-z",  domain.max[2],  "domain max z")
  ;

  ops
      >> Option('v', "verbose", verbose, "print the block contents")
      >> Option('h', "help",    help,    "show help")
  ;

  if (!ops.parse(argc,argv) || help)
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
          std::cout << "Generates random particles in the domain and redistributes them into correct blocks.\n";
          std::cout << ops;
      }
      return 1;
  }

  diy::create_logger(log_level);

  diy::FileStorage          storage(prefix);
  diy::Master               master(world,
                                   threads,
                                   mem_blocks,
                                   &Block::create,
                                   &Block::destroy,
                                   &storage,
                                   &Block::save,
                                   &Block::load);

  int   dim = DIM;

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);
  AddBlock      create(master, num_points);

  Decomposer    decomposer(dim, domain, nblocks);
  decomposer.decompose(world.rank(), assigner, create);
  diy::all_to_all(master, assigner, Redistribute(decomposer), k);

  master.foreach([verbose](Block* b, const diy::Master::ProxyWithLink& cp) { b->print_block(cp, verbose); });
  master.foreach(&Block::verify_block);
}
