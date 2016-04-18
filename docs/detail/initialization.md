\defgroup Initialization

## Getting started

The following is an example of the steps needed to initialize diy:

### Start with a definition of a block.

The block is the basic unit of everything (data, decomposition, communication) in diy. Use it to define your data model and any state associated with the data that will be needed to accompany the data throughout its lifetime. In addition to the data in the block, it should define functions to ```create``` and ```destroy``` the block that diy can call. If the blocks are intended to be moved in and out of core, then the block must also define ```save``` and ```load``` functions.

There are 2 common ways to set up the block.

- The "short form" is used when blocks do not need any arguments when created. Here is an example:

~~~~{.cpp}
#include <diy/master.hpp>

struct Block
{
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)
    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)
    { diy::load(bb, *static_cast<Block*>(b)); }

  // other functions and data members
}
~~~~

- The "long form" is used when custom arguments are needed to initialize blocks upon creation. This saves one I/O cycle if blocks are moved in and out of core and also provides useful information to the block such as the bounds. A second object called ```AddBlock``` is a functor that gets and stores any custom arguments and overloads the function call with access to the custom arguments:

~~~~{.cpp}
#include <diy/master.hpp>

typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;

struct Block
{
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)
    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)
    { diy::load(bb, *static_cast<Block*>(b)); }

  // other functions and data members
};

struct AddBlock
{
  AddBlock(diy::Master& master_) : master(master_) {}   // additional args as needed

  // this is the function that is needed for diy::decompose
  void  operator()(int gid,                   // block global id
                   const Bounds& core,        // block bounds without any ghost added
                   const Bounds& bounds,      // block bounds including any ghost region added
                   const Bounds& domain,      // global data bounds
                   const RCLink& link)        // neighborhood
    const
    {
      Block*          b   = new Block();
      RGLink*         l   = new RGLink(link);
      diy::Master&    m   = const_cast<diy::Master&>(master);
      int             lid = m.add(gid, b, l); // add block to the master (mandatory)
      // process any additional args here, using them to initialize the block
}

  diy::Master&  master;
  // store additional args here
};

~~~~

### Create the master object.

Define your global data bounds, the MPI communicator, and the file storage object (if blocks will be moved in and out of core). Then define the diy ```Master``` object. The ```Master``` manages loading/saving blocks, executing their callback functions, and exchanging data between them. If the block long form (with ```AddBlock``` functor) is used, define the ```AddBlock``` object as well.

~~~~{.cpp}

int main(int argc, char** argv)
{
  ...

  diy::Bounds               domain;
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
  // if AddBlock is used (block long form), then:
  AddBlock                  addblock(master) // could have extra args
  ...
}

~~~~

When all blocks will remain in memory, there is a shorter form of the ```Master``` constructor that can be used. In this case there is no need to specify most of the arguments because they relate to block loading/unloading.

~~~~{.cpp}

diy::Master               master(world,
                                 num_threads);

~~~~

### Assign blocks to processes.

Blocks can be assigned to processes contiguously or in round-robin fashion:

~~~~{.cpp}

diy::ContiguousAssigner   assigner(world.size(),   // total number of MPI ranks
                                   nblocks);       // total number of blocks in global domain

// --- or ---

diy::RoundRobinAssigner   assigner(world.size(),
                                   nblocks);

~~~~

### Decompose the domain.

Any custom decomposition can be formed by assigning links (communication neighborhoods of blocks) manually. However, for a regular grid of blocks, diy provides a regular decomposition of blocks with either continuous (floating-point extents that share common boundaries) or discrete (integer extents that may or may not overlap) bounds.


~~~~{.cpp}

typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;

// --- or ---

typedef  diy::DiscreteBounds         Bounds;
typedef  diy::RegularGridLink        RGLink;

~~~~

Diy offers two versions of a ```decompose``` function, depending on whether the short or long form of the block is used. Recall that the long form has an ```AddBlock``` functor, while the short form does not. In the short form, one of the arguments to ```decompose``` is ```master```, and in the long form, provide ```addblock``` instead:

~~~~{.cpp}

// in the short form, provide the master object in lieu of an AddBlock object
diy::decompose(dim,
               rank,
               domain,
               assigner,
               master);

// --- or ---

// in the long form, provide the AddBlock object
diy::decompose(dim,
               rank,
               domain,
               assigner,
               addblock);

~~~~

Strictly speaking, the second (long form) decomposition does not require an entire ```AddBlock``` object, as long as the block create function has the same signature as ```AddBlock```. That pattern is:

~~~~{.cpp}

void create(int gid,
            const Bounds& core,
            const Bounds& bounds,
            const Bounds& domain,
            const diy::Link& link);
...

diy::decompose(dim,
               rank,
               domain,
               assigner,
               create);

~~~~
```decompose``` is actually a convenience function that creates a ```RegularDecomposer``` object, calls its ```decompose``` method, and then destroys the ```RegularDecomposer```. The user may want persistent access to ```RegularDecomposer``` because it offers useful information about the decomposition (block bounds, numbers of blocks in each dimension, and so forth.). In that case, we suggest defining ```RegularDecomposer``` and calling its ```decompose``` function yourself:

~~~~{.cpp}

diy::RegularDecomposer<Bounds> decomposer(dim,
                                          domain,
                                          nblocks);

// in the short form, use diy's AddBlock object
decomposer.decompose(rank,
                     assigner,
                     diy::detail::AddBlock<Bounds>(&master));

// --- or ---

// in the long form, provide your own AddBlock object
decomposer.decompose(rank,
                     assigner,
                     addblock);

~~~~

### Put it all together.

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

  diy::decompose(dim,
                 world.rank(),                           // MPI rank of this process
                 domain,                                 // global domain size
                 assigner,                               // assigner object
                 master);                                // master object
}

~~~~

Below is one more complete example, this time using the long form of the block:

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

struct AddBlock
{
  AddBlock(diy::Master& master_) : master(master_) {}

  // this is the function that is needed for diy::decompose
  void  operator()(int gid,                              // block global id
                   const Bounds& core,                   // block bounds without any ghost
                   const Bounds& bounds,                 // block bounds including ghost region
                   const Bounds& domain,                 // global data bounds
                   const RGLink& link)                   // communication neighborhood
      const
      {
          Block*          b   = new Block();
          RGLink*         l   = new RGLink(link);
          diy::Master&    m   = const_cast<diy::Master&>(master);
          int             lid = m.add(gid, b, l);        // add block to the master (mandatory)
      }

  diy::Master&  master;
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
  AddBlock                  addblock(master);            // object for adding new blocks to master

  diy::decompose(dim,
                 world.rank(),                           // MPI rank of this process
                 domain,                                 // global domain size
                 assigner,                               // assigner object
                 addblock);                              // add block object
}

~~~~

Documentation about the ```Master``` object appears below. The ```RegularDecomposer``` is documented in the [Decomposition](\ref Decomposition) page.


