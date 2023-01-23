Typically, out-of-core algorithms are distinct from their in-core counterparts, and much research has been conducted on
out-of-core algorithms for specific classes of problems. In DIY, switching from in-core to out-of-core is as simple as
changing the number of blocks allowed to reside in memory. If this is a command-line argument, no recompilation is
needed. This is another advantage of the *block parallel* programming model. When a program is written in terms of logical
blocks, the DIY runtime is free to migrate blocks and their associated message queues in and out-of-core, with no
change to the program logic.

## Block

Recall from the [Initialization module](initialization.md) that if blocks are intended to be moved in and out-of-core,
then the block must define `save` and `load` functions in addition to `create` and `destroy` (`create`, `destroy`,
`save`, and `load` could also be defined globally outside of the block, if you wish).

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

Recall from [Initialization](initialization.md) that diy::Master owns and manages the blocks that are assigned to the
current MPI process. For out-of-core operation, the `storage` object and the `load` and `save` objects are mandatory.
`Master` manages loading/saving blocks, executing their callback functions, and exchanging data between them including
when blocks are out-of-core.

To initiate out-of-core operation, simply change the `mem_blocks` argument to `Master` from -1 (meaning all blocks in
core) to a value greater than or equal to 1. For example, assume we have a program with 32 total blocks, run on 8 MPI
processes. DIY's `Assigner` will assign 32 / 8 = 4 blocks to each MPI process. If we only have sufficient memory to hold
2 blocks at a time in memory, setting `memblocks = 2` is all that is needed; DIY does the rest.

If `Block` is defined as above, the first part of the code looks like this.

~~~~{.cpp}
#include <diy/assigner.hpp>
#include <diy/master.hpp>

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);             // diy's version of MPI_Init
  diy::mpi::communicator    world;                       // diy's version of MPI communicator
  diy::FileStorage          storage("./DIY.XXXXXX");     // storage location for out-of-core blocks

  int                       nprocs      = 8;             // total number of MPI ranks
  int                       nblocks     = 32;            // total number of blocks in global domain
  int                       mem_blocks  = 2;             // number of blocks that will fit in memory

  diy::ContiguousAssigner   assigner(nprocs, nblocks);   // assign blocks to MPI ranks

  diy::Master               master(world,                // communicator
                                   1,                    // use 1 thread to execute blocks
                                   mem_blocks,           // # blocks in memory
                                   &Block::create,       // block create function
                                   &Block::destroy,      // block destroy function
                                   &storage,             // storage location for out-of-core blocks
                                   &Block::save,         // block save function for out-of-core blocks
                                   &Block::load);        // block load function for out-of-core blocks

...
~~~~
