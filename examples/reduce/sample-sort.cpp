#include <cmath>
#include <vector>
#include <cstdlib>
#include <limits>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/assigner.hpp>
#include <diy/algorithms.hpp>
#include <diy/mpi/io.hpp>
#include <diy/io/bov.hpp>

#include "../opts.h"

#include "sort.h"

typedef     diy::Link                   Link;
typedef     Block<Value>                ValueBlock;

void set_min_max(ValueBlock* b, const diy::Master::Proxy& cp, int nblocks)
{
    if (cp.gid() == 0)
        b->min = -std::numeric_limits<Value>::max();
    else
        b->min = b->samples[cp.gid() - 1];

    if (cp.gid() == nblocks - 1)
        b->max = std::numeric_limits<Value>::max();
    else
        b->max = b->samples[cp.gid()];
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  using namespace opts;
  Options ops;

  int               nblocks     = world.size();
  size_t            num_values  = 100;
  int               k           = 2;
  int               num_samples = 8;
  int               mem_blocks  = -1;
  int               threads     = 1;
  int               chunk_size  = 1;
  std::string       prefix      = "./DIY.XXXXXX";

  bool print, verbose, verify, help;
  ops
      >> Option(     "print",   print,      "print the result")
      >> Option('v', "verbose", verbose,    "verbose output")
      >> Option(     "verify",  verify,     "verify the result")
      >> Option('h', "help",    help,       "show help")
  ;

  Value             min = 0,
                    max = 1 << 20;

  ops
      >> Option('n', "number",  num_values,     "number of values per block")
      >> Option('k', "k",       k,              "use k-ary swap")
      >> Option('s', "samples", num_samples,    "number of samples per block")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option('m', "memory",  mem_blocks,     "number of blocks to keep in memory")
      >> Option('c', "chunk",   chunk_size,     "size of a chunk in which to read the data")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
  ;

  ops
      >> Option(     "min",     min,            "range min")
      >> Option(     "max",     max,            "range max")
  ;

  if (!ops.parse(argc,argv) || help)
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS] [INPUT.bov]\n";
          std::cout << "Generates random values in range [min,max] if not INPUT.bov is given.\n";
          std::cout << ops;
      }
      return 1;
  }

  std::string infile;
  ops >> PosOption(infile);

  diy::FileStorage          storage(prefix);
  diy::Master               master(world,
                                   threads,
                                   mem_blocks,
                                   &ValueBlock::create,
                                   &ValueBlock::destroy,
                                   &storage,
                                   &ValueBlock::save,
                                   &ValueBlock::load);

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  std::vector<int> gids;
  assigner.local_gids(world.rank(), gids);

  if (infile.empty())
  {
    srand(static_cast<unsigned int>(time(0)));

    for (unsigned i = 0; i < gids.size(); ++i)
    {
      int             gid = gids[i];
      ValueBlock*     b   = new ValueBlock(0);      // bins not used in sample-sort
      Link*           l   = new Link;

      // this could be replaced by reading values from a file
      b->generate_values(num_values, min, max);

      master.add(gid, b, l);
    }
    std::cout << "Blocks generated" << std::endl;
  } else
  {
    if (nblocks % world.size() != 0)
    {
      if (world.rank() == 0)
          fmt::print(stderr, "Number of blocks must be divisible by the number of processes (for collective MPI-IO)\n");
      return 1;
    }

    // determine the size of the file, divide by sizeof(Value)
    diy::mpi::io::file in(world, infile, diy::mpi::io::file::rdonly);
    size_t sz = in.size() / sizeof(Value);

    // We have to work around a weirdness in MPI-IO. MPI expresses various
    // sizes as integers, which is too small to reference large data. So
    // instead we create a FileBlock of larger size and read in terms of those
    // blocks.
    if (sz % (chunk_size * nblocks) != 0)
    {
      if (world.rank() == 0)
          fmt::print(stderr, "Expected data size to align with the number of blocks and chunk size\n");
      return 1;
    }

    std::vector<int> shape;
    shape.push_back(static_cast<int>(sz / chunk_size));
    diy::io::BOV reader(in, shape);

    int block_size = static_cast<int>(sz) / nblocks;

    for (unsigned i = 0; i < gids.size(); ++i)
    {
      int             gid = gids[i];
      ValueBlock*     b   = new ValueBlock(num_samples);
      Link*           l   = new Link;

      // read values from a file
      b->values.resize(static_cast<size_t>(block_size));
      diy::DiscreteBounds block_bounds(1);
      block_bounds.min[0] =  gid      * (block_size / chunk_size);
      block_bounds.max[0] = (gid + 1) * (block_size / chunk_size) - 1;

      reader.read(block_bounds, &b->values[0], true, chunk_size);

      master.add(gid, b, l);
    }

    if (world.rank() == 0)
      std::cout << "Array loaded" << std::endl;
  }

  diy::sort(master, assigner,
            &ValueBlock::values,
            &ValueBlock::samples,
            num_samples,
            k);

  if (print || verify)
    master.foreach([nblocks](ValueBlock* b, const diy::Master::ProxyWithLink& cp) { set_min_max(b, cp, nblocks); });

  if (print)
  {
    fmt::print("Printing blocks\n");
    master.foreach([verbose](ValueBlock* b, const diy::Master::ProxyWithLink& cp) { b->print_block(cp, verbose); });
  }
  if (verify)
  {
    fmt::print("Verifying blocks\n");
    master.foreach(&ValueBlock::verify_block);

    master.exchange();      // to process collectives
    typedef diy::Master::ProxyWithLink      Proxy;
    int     idx   = master.loaded_block();
    Proxy   proxy = master.proxy(idx);
    size_t  total = proxy.get<size_t>();
    std::cout << "Total values: " << total << " vs " << nblocks * num_values << std::endl;

    if (world.rank() == 0)
      std::cout << "Blocks verified" << std::endl;
  }

  std::cout << "[" << world.rank() << "] Storage count:    " << storage.count() << std::endl;
  std::cout << "[" << world.rank() << "] Storage max size: " << storage.max_size() << std::endl;
}
