Most DIY programs follow a similar sequence of initial steps to setup DIY before the actual parallel computing happens.
These steps include:

- Defining a block data structure
- Creating a `diy::Master` object to manage the blocks local to one MPI process
- Assigning the local blocks to the master with a `diy:Assigner` object
- Decomposing the global domain into blocks with the `diy::Decomposer` object

The following is an example of the steps needed to initialize DIY.

## Block

The block is the basic unit of everything (data, decomposition, communication) in DIY. Use it to define your data model
and any state associated with the data that will be needed to accompany the data throughout its lifetime. In addition to
the data in the block, you must define functions to `create` and `destroy` the block that DIY can call. (*Technically, if
you choose to manage block memory yourself and don't pass `create` and `destroy` functions to `diy::Master`
below, then you don't need to define `create` and `destroy` here. However, we recommend having `Master` manage block creation and destruction
to prevent possible leaks.*) If the blocks are intended to be moved in and out of
core, then the block must also define `save` and `load` functions. (`create`, `destroy`, and optionally `save` and
`load` could also be defined globally outside of the block, if you wish.)

~~~~{.cpp}
struct Block
{
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)  { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)        { diy::load(bb, *static_cast<Block*>(b)); }

  // your user-defined member functions
  ...

  // your user-defined data members
  ...
}
~~~~

## Master

diy::Master owns and manages the blocks that are assigned to the current MPI process. To set up a `Master` object, first
define the MPI communicator and the file storage object (if blocks will be
moved in and out of core), which you pass to the `Master` constructor.
`Master` manages loading/saving blocks, executing their callback functions, and
exchanging data between them.

~~~~{.cpp}
int main(int argc, char** argv)
{
  ...

  diy::mpi::communicator    world(comm);
  diy::FileStorage          storage("./DIY.XXXXXX");
  diy::Master               master(world,           // MPI world communicator
                                   num_threads,     // # blocks to execute concurrently (1 means sequence through local blocks)
                                   mem_blocks,      // # blocks to keep in memory (-1 means keep all blocks in memory)
                                   &Block::create,  // block create function
                                   &Block::destroy, // block destroy function
                                   &storage,        // storage location for out-of-core blocks (optional)
                                   &Block::save,    // block save function for out-of-core blocks (optional)
                                   &Block::load);   // block load function for out-of-core blocks (otpional)
  ...
}
~~~~

If all blocks are to remain in memory, there is no need to specify `storage`, `save`, and `load`, which are
for block loading/unloading.

~~~~{.cpp}
  diy::mpi::communicator    world(comm);
  diy::Master               master(world,            // MPI world communicator
                                   num_threads,      // # threads DIY can use to execute mulitple local blocks concurrently (optional, default 1)
                                   mem_blocks,       // # blocks to keep in memory, -1 means all blocks (optional)
                                   &Block::create,   // block create function (optional)
                                   &Block::destroy); // block destroy function (optional)
~~~~

## Assigner

diy::Assigner is an auxiliary object that determines what blocks lives on what MPI process.
Blocks can be assigned to processes contiguously or in round-robin fashion:

~~~~{.cpp}
diy::ContiguousAssigner   assigner(world.size(),   // total number of MPI ranks
                                   nblocks);       // total number of blocks in global domain

// --- or ---

diy::RoundRobinAssigner   assigner(world.size(),
                                   nblocks);
~~~~

## Decomposer

Any custom decomposition can be formed by creating blocks yourself and linking them together into communicating
neighborhoods manually. However, for a regular grid of blocks, DIY
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
diy::RegularDecomposer<Bounds> decomposer(dim,      // dimensionality of global domain
                                          domain,   // sizes of global domain
                                          nblocks); // global number of blocks
~~~~

Its member function `decompose` performs the actual decomposition. Besides the
local MPI rank and an instance of `Assigner`, it takes a callback responsible
for creating the block and adding it to a `Master`. In C++11, it's convenient to
use a lambda function for this purpose.

~~~~{.cpp}
decomposer.decompose(rank,                          // MPI rank of this process
                     assigner,                      // diy::Assigner object
                     [&](int gid,                   // block global id
                         const Bounds& core,        // block bounds without any ghost added
                         const Bounds& bounds,      // block bounds including any ghost region added
                         const Bounds& domain,      // global data bounds
                         const RCLink& link)        // neighborhood
                     {
                         Block*          b   = new Block;             // possibly use custom initialization
                         RCLink*         l   = new RCLink(link);      // link neighboring blocks
                         int             lid = master.add(gid, b, l); // add block to the master (mandatory)

                         // process any additional args here, load the data, etc.
                     });
~~~~

A shorter form is provided, if you only want to add the blocks to `Master`,
without any additional processing.

~~~~{.cpp}
decomposer.decompose(rank,          // MPI rank of this process
                     assigner,      // diy::Assigner object
                     master);       // diy::Master object
~~~~


## A complete example

Here is one version of each of the above options combined into a complete program, using a lambda function for the decomposer callback.

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
  int                       nprocs  = 8;                 // total number of MPI ranks
  int                       nblocks = 32;                // total number of blocks in global domain

  diy::ContiguousAssigner   assigner(nprocs, nblocks);   // assign blocks to MPI ranks

  Bounds domain;                                         // global data size

  diy::Master               master(world,                // communicator
                                   1,                    // 1 thread in this example
                                   -1,                   // all blocks in memory in this example
                                   &Block::create,       // block create function
                                   &Block::destroy);     // block destroy function

  diy::RegularDecomposer<Bounds> decomposer(dim,         // dimensionality of domain
                                            domain,      // global domain size
                                            nblocks);    // global number of blocks

  decomposer.decompose(world.rank(),                     // MPI rank of this process
                       assigner,                         // assigner object
                       [&](int gid,                      // block global id
                           const Bounds& core,           // block bounds without any ghost added
                           const Bounds& bounds,         // block bounds including any ghost region added
                           const Bounds& domain,         // global data bounds
                           const RGLink& link)           // neighborhood
                       {
                           Block*  b   = new Block;             // create a new block, perform any custom initialization
                           RGLink* l   = new RGLink(link);      // copy the link so that master owns a copy
                           int     lid = master.add(gid, b, l); // add block to the master (mandatory)
                       });
}
~~~~

