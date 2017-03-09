\defgroup Tutorial

## Getting started

The following is an example of the steps needed to initialize DIY.

### Block

The block is the basic unit of everything (data, decomposition, communication)
in DIY. Use it to define your data model and any state associated with the data
that will be needed to accompany the data throughout its lifetime. In addition
to the data in the block, it should define functions to `create` and `destroy`
the block that DIY can call. If the blocks are intended to be moved in and out
of core, then the block must also define `save` and `load` functions.

~~~~{.cpp}
#include <diy/master.hpp>

struct Block
{
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)  { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)        { diy::load(bb, *static_cast<Block*>(b)); }

  // other functions and data members
}
~~~~

### Master

diy::Master owns and manages the blocks. To set up a `Master` object, first
define the MPI communicator and the file storage object (if blocks will be
moved in and out of core), which you pass to the `Master` constructor. The
`Master` manages loading/saving blocks, executing their callback functions, and
exchanging data between them.

~~~~{.cpp}

int main(int argc, char** argv)
{
  ...

  diy::mpi::communicator    world(comm);
  diy::FileStorage          storage("./DIY.XXXXXX");
  diy::Master               master(world,
                                   num_threads,
                                   mem_blocks,
                                   &Block::create,
                                   &Block::destroy,
                                   &storage,
                                   &Block::save,
                                   &Block::load);
  ...
}

~~~~

Some of the arguments to the constructor are optional. If all blocks are to
remain in memory, there is a shorter form of the `Master` constructor that can
be used, since there is no need to specify most of the arguments because
they relate to block loading/unloading.

~~~~{.cpp}

diy::Master               master(world,
                                 num_threads);

~~~~

### Assigner

diy::Assigner is an auxiliary object that determines what blocks lives on what MPI process.
Blocks can be assigned to processes contiguously or in round-robin fashion:

~~~~{.cpp}

diy::ContiguousAssigner   assigner(world.size(),   // total number of MPI ranks
                                   nblocks);       // total number of blocks in global domain

// --- or ---

diy::RoundRobinAssigner   assigner(world.size(),
                                   nblocks);

~~~~

### Decomposition

Any custom decomposition can be formed by assigning links (communication
neighborhoods of blocks) manually. However, for a regular grid of blocks, DIY
provides a regular decomposition of blocks with either continuous
(floating-point extents that share common boundaries) or discrete (integer
extents that may or may not overlap) bounds.


~~~~{.cpp}

typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;

// --- or ---

typedef  diy::DiscreteBounds         Bounds;
typedef  diy::RegularGridLink        RGLink;

~~~~

`RegularDecomposer` helps decompose a domain into a regular grid of blocks.
It's initialized with the dimension of the domain, its extents, and the number
of blocks used in the decomposition.

~~~~{.cpp}

diy::RegularDecomposer<Bounds> decomposer(dim,
                                          domain,
                                          nblocks);

~~~~

Its member function `decompose` performs the actual decomposition. Besides the
local MPI rank and an instance of `Assigner`, it takes a callback responsible
for creating the block and adding it to a `Master`. In C++11, it's convenient to
use a lambda function for this purpose.

~~~~{.cpp}

decomposer.decompose(rank, assigner,
                     [&](int gid,                   // block global id
                         const Bounds& core,        // block bounds without any ghost added
                         const Bounds& bounds,      // block bounds including any ghost region added
                         const Bounds& domain,      // global data bounds
                         const RCLink& link)        // neighborhood
                     {
                         Block*          b   = new Block;             // possibly use custom initialization
                         RGLink*         l   = new RGLink(link);
                         int             lid = master.add(gid, b, l); // add block to the master (mandatory)

                         // process any additional args here, load the data, etc.
                     });

~~~~

A shorter form is provided, if you only want to add the blocks to `Master`,
without any additional processing.

~~~~{.cpp}

decomposer.decompose(rank,
                     assigner,
                     master);

~~~~


### Combined code

Here is one version of each of the above options combined into a complete program. This example uses the short form of blocks:

~~~~{.cpp}

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/master.hpp>

typedef     diy::DiscreteBounds       Bounds;
typedef     diy::RegularGridLink      RGLink;

struct Block
{
  Block() {}
  static void*    create()            { return new Block; }
  static void     destroy(void* b)    { delete static_cast<Block*>(b); }
};

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);             // diy's version of MPI_Init
  diy::mpi::communicator    world;                       // diy's version of MPI communicator

  int                       dim     = 3;                 // dimensionality
  int                       size    = 8;                 // total number of MPI ranks
  int                       nblocks = 32;                // total number of blocks in global domain
  diy::ContiguousAssigner   assigner(size, nblocks);
  Bounds domain;                                         // global data size
  diy::Master               master(world,
                            1,                           // 1 thread in this example
                            -1,                          // all blocks in memory in this example
                            &Block::create,              // block create function
                            &Block::destroy);            // block destroy function

  diy::RegularDecomposer<Bounds> decomposer(dim,
                                            domain,      // global domain size
                                            nblocks);
  decomposer.decompose(world.rank(),                     // MPI rank of this process
                       assigner,                         // assigner object
                       master);                          // master object
}

~~~~

Below is one more complete example, using a lambda function to initialize the block:

~~~~{.cpp}

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/master.hpp>

typedef     diy::DiscreteBounds       Bounds;
typedef     diy::RegularGridLink      RGLink;

struct Block
{
  Block() {}
  static void*    create()            { return new Block; }
  static void     destroy(void* b)    { delete static_cast<Block*>(b); }
};

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);             // diy's version of MPI_Init
  diy::mpi::communicator    world;                       // diy's version of MPI communicator

  int                       dim     = 3;                 // dimensionality
  int                       size    = 8;                 // total number of MPI ranks
  int                       nblocks = 32;                // total number of blocks in global domain
  diy::ContiguousAssigner   assigner(size, nblocks);
  Bounds domain;                                         // global data size
  diy::Master               master(world,
                            1,                           // 1 thread in this example
                            -1,                          // all blocks in memory in this example
                            &Block::create,              // block create function
                            &Block::destroy);            // block destroy function

  diy::RegularDecomposer<Bounds> decomposer(dim,
                                            domain,      // global domain size
                                            nblocks);
  decomposer.decompose(world.rank(),                     // MPI rank of this process
                       assigner,                         // assigner object
                       [&](int gid,                      // block global id
                           const Bounds& core,           // block bounds without any ghost added
                           const Bounds& bounds,         // block bounds including any ghost region added
                           const Bounds& domain,         // global data bounds
                           const RCLink& link)           // neighborhood
                       {
                           Block*  b   = new Block;             // possibly use custom initialization
                           RGLink* l   = new RGLink(link);
                           int     lid = master.add(gid, b, l); // add block to the master (mandatory)
                       });
}

~~~~

diy::Master API has a separate documentation page. The `RegularDecomposer` is documented in the [Decomposition](\ref Decomposition) page.
