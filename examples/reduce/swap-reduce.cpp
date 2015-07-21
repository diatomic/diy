#include <cmath>

#include <diy/master.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/swap.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

#include "../opts.h"

#include "point.h"

typedef     diy::ContinuousBounds       Bounds;
typedef     diy::RegularContinuousLink  RCLink;

static const unsigned DIM = 3;
typedef     PointBlock<DIM>             Block;
typedef     AddPointBlock<DIM>          AddBlock;

void redistribute(void* b_, const diy::ReduceProxy& srp, const diy::RegularSwapPartners& partners)
{
    Block*                      b        = static_cast<Block*>(b_);
    unsigned                    round    = srp.round();

    //fprintf(stderr, "in_link.size():  %d\n", srp.in_link().size());
    //fprintf(stderr, "out_link.size(): %d\n", srp.out_link().size());

    // step 1: dequeue and merge
    // dequeue all the incoming points and add them to this block's vector
    // could use srp.incoming() instead
    for (unsigned i = 0; i < srp.in_link().size(); ++i)
    {
      int nbr_gid = srp.in_link().target(i).gid;
      if (nbr_gid == srp.gid())
          continue;

      std::vector<Block::Point>    in_points;
      srp.dequeue(nbr_gid, in_points);
      fprintf(stderr, "[%d] Received %d points from [%d]\n", srp.gid(), (int) in_points.size(), nbr_gid);
      for (size_t j = 0; j < in_points.size(); ++j)
        b->points.push_back(in_points[j]);
    }

    // step 2: subset and enqueue
    //fprintf(stderr, "[%d] out_link().size(): %d\n", srp.gid(), srp.out_link().size());
    if (srp.out_link().size() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Block::Point> > out_points(srp.out_link().size());
    int group_size = srp.out_link().size();
    int cur_dim    = partners.dim(round);
    for (size_t i = 0; i < b->points.size(); ++i)
    {
      int loc = floor((b->points[i][cur_dim] - b->box.min[cur_dim]) / (b->box.max[cur_dim] - b->box.min[cur_dim]) * group_size);
      out_points[loc].push_back(b->points[i]);
    }
    int pos = -1;
    for (int i = 0; i < group_size; ++i)
    {
      if (srp.out_link().target(i).gid == srp.gid())
      {
        b->points.swap(out_points[i]);
        pos = i;
      }
      else
      {
        srp.enqueue(srp.out_link().target(i), out_points[i]);
        fprintf(stderr, "[%d] Sent %d points to [%d]\n", srp.gid(), (int) out_points[i].size(), srp.out_link().target(i).gid);
      }
    }
    float new_min = b->box.min[cur_dim] + (b->box.max[cur_dim] - b->box.min[cur_dim])/group_size*pos;
    float new_max = b->box.min[cur_dim] + (b->box.max[cur_dim] - b->box.min[cur_dim])/group_size*(pos + 1);
    b->box.min[cur_dim] = new_min;
    b->box.max[cur_dim] = new_max;
}

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

  Bounds domain;
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 100.;

  using namespace opts;
  Options ops(argc, argv);

  ops
      >> Option('n', "number",  num_points,     "number of points per block")
      >> Option('k', "k",       k,              "use k-ary swap")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option('m', "memory",  mem_blocks,     "number of blocks to keep in memory")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
  ;

  ops
      >> Option('x',  "max-x",  domain.max[0],  "domain max x")
      >> Option('y',  "max-y",  domain.max[1],  "domain max y")
      >> Option('z',  "max-z",  domain.max[2],  "domain max z")
  ;
  bool  verbose = ops >> Present('v', "verbose", "print the block contents");

  if (ops >> Present('h', "help", "show help"))
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
          std::cout << "Generates random particles in the domain and redistributes them into correct blocks.\n";
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

  int   dim = DIM;

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);
  AddBlock  create(master, num_points);
  diy::decompose(dim, world.rank(), domain, assigner, create);

  diy::RegularSwapPartners  partners(dim, nblocks, k, false);
  //fprintf(stderr, "%d %d %d\n", dim, nblocks, k);
  //fprintf(stderr, "partners.rounds(): %d\n", (int) partners.rounds());
  diy::reduce(master, assigner, partners, &redistribute);

  master.foreach(&Block::print_block, &verbose);
  master.foreach(&Block::verify_block);
}
