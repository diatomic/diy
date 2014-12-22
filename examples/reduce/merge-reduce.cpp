#include <cmath>
#include <vector>

#include <diy/master.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/merge.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

#include "../opts.h"

using namespace std;

typedef     diy::ContinuousBounds       Bounds;
typedef     diy::RegularContinuousLink  RCLink;

struct Block
{
                  Block(const Bounds& bounds_):
                      bounds(bounds_)                         {}

  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)  { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)        { diy::load(bb, *static_cast<Block*>(b)); }


  void            generate_data(size_t n)
  {
    data.resize(n);
    for (size_t i = 0; i < n; ++i)
      data[i] = i;
  }

  Bounds          bounds;
  vector<int>     data;
  private:
                  Block()                                     {}
};

struct AddBlock
{
        AddBlock(diy::Master& master_, size_t num_points_):
            master(master_),
            num_points(num_points_)
        {}

  void  operator()(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain, const RCLink& link) const
  {
    Block*          b   = new Block(core);
    RCLink*         l   = new RCLink(link);
    diy::Master&    m   = const_cast<diy::Master&>(master);

    int             lid = m.add(gid, b, l);

    b->generate_data(num_points);
  }

  diy::Master&  master;
  size_t        num_points;
};

void sum(void* b_, const diy::ReduceProxy& rp, const diy::RegularMergePartners& partners)
{
    Block*                      b        = static_cast<Block*>(b_);
    unsigned                    round    = rp.round();

    // step 1: dequeue and merge
    for (unsigned i = 0; i < rp.in_link().size(); ++i)
    {
      int nbr_gid = rp.in_link().target(i).gid;
      if (nbr_gid == rp.gid())
      {
          fprintf(stderr, "[%d:%d] Skipping receiving from self\n", rp.gid(), round);
          continue;
      }

      std::vector<int>    in_vals;
      rp.dequeue(nbr_gid, in_vals);
      fprintf(stderr, "[%d:%d] Received %d values from [%d]\n", rp.gid(), round, (int)in_vals.size(), nbr_gid);
      for (size_t j = 0; j < in_vals.size(); ++j)
        (b->data)[j] += in_vals[j];
    }

    // step 2: enqueue
    for (int i = 0; i < rp.out_link().size(); ++i)     // this is redundant since size should equal to 1
    {
      // only send to root of group, but not self
      if (rp.out_link().target(i).gid != rp.gid())
      {
        rp.enqueue(rp.out_link().target(i), b->data);
        fprintf(stderr, "[%d:%d] Sent %d valuess to [%d]\n", rp.gid(), round, (int)b->data.size(), rp.out_link().target(i).gid);
      } else
          fprintf(stderr, "[%d:%d] Skipping sending to self\n", rp.gid(), round);

    }
}

void print_block(void* b_, const diy::Master::ProxyWithLink& cp, void* verbose_)
{
    Block*   b       = static_cast<Block*>(b_);
    bool     verbose = *static_cast<bool*>(verbose_);

    fprintf(stderr, "[%d] Bounds: %f %f %f -- %f %f %f\n",
            cp.gid(),
            b->bounds.min[0], b->bounds.min[1], b->bounds.min[2],
            b->bounds.max[0], b->bounds.max[1], b->bounds.max[2]);

    if (verbose && cp.gid() == 0)
    {
      fprintf(stderr, "[%d] %lu vals: ", cp.gid(), b->data.size());
      for (size_t i = 0; i < b->data.size(); ++i)
        fprintf(stderr, "%d  ", b->data[i]);
      fprintf(stderr, "\n");
    }
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  using namespace opts;
  Options ops(argc, argv);

  // TODO: read from the command line
  int                       nblocks     = world.size();
  size_t                    num_points  = 10;      // points per block
  int                       mem_blocks  = -1;
  int                       threads     = 1;
  int                       dim         = 3;
  bool                      verbose     = ops >> Present('v', "verbose",    "verbose output");
  bool                      contiguous  = ops >> Present('c', "contiguous", "use contiguous partners");

  ops
      >> Option('d', "dim",     dim,            "dimension")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
  ;

  if (ops >> Present('h', "help", "show help"))
  {
      std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
      std::cout << ops;
      return 1;
  }

  diy::FileStorage          storage("./DIY.XXXXXX");
  diy::Communicator         comm(world);
  diy::Master               master(comm,
                                   &Block::create,
                                   &Block::destroy,
                                   mem_blocks,
                                   threads,
                                   &storage,
                                   &Block::save,
                                   &Block::load);

  Bounds domain;
  for (int i = 0; i < dim; ++i)
  {
      domain.min[i] = 0;
      domain.max[i] = 128.;
  }

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);
  AddBlock  create(master, num_points);
  diy::decompose(dim, world.rank(), domain, assigner, create);

  int k = 2;
  diy::RegularMergePartners  partners(dim, nblocks, k, contiguous);
//   fprintf(stderr, "%d %d %d\n", dim, nblocks, k);
//   fprintf(stderr, "partners.rounds(): %d\n", (int) partners.rounds());
  diy::reduce(master, assigner, partners, sum);

  master.foreach(print_block, &verbose);
}

