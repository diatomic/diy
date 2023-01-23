DIY can automatically execute multiple blocks resident
in memory concurrently if allowed to have access to more than one thread.
Similar to automatically switching from in-core to [out-of-core](ooc.md), DIY can thread blocks automatically with no change to
the program logic, and without recompiling if the number of threads is provided as a command-line argument.

 DIY's threading is fairly coarse-grained: one thread executes an entire callback function provided to
 `diy::Master::foreach()`.  Because each block is a separate object, and all state is maintained in the block, DIY's
 automatic threading is inherently thread-safe. Within a block callback function, the user is also free to write
 finer-grain custom threaded code, assuming that adequate harware resources exist for DIY's threads and the user's, and
 that there are no conflicts between different thread libraries (DIY uses pthreads).

DIY's threading feature can also be combined with [out-of-core](ooc.md) execution.

## Block

No changes are required to the block structure described in
the [Initialization module](initialization.md) .

~~~~{.cpp}
struct Block
{
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }

  // your user-defined member functions
  ...

  // your user-defined data members
  ...
}
~~~~

## Master

Recall from [Initialization](initialization.md) that diy::Master owns and manages the blocks that are assigned to the
current MPI process. It also executes callback functions on blocks, which is where computations on blocks occur.
To execute callback functions on multiple blocks resident in memory concurrently, simply change the `num_threads` argument to `Master`
to a value greater than 1. For example, assume we have a program with 32 total blocks, run on 8 MPI
processes. DIY's `Assigner` will assign 32 / 8 = 4 blocks to each MPI process. Assume all 4 blocks fit in memory. Assume
we can allow DIY 2 threads for executing callback functions. In this case, rather than DIY stepping serially through
the 4 blocks in a process each time a callback function is called, DIY will only make two iterations through the local
blocks; in each iteration two blocks will execute their callbacks concurrently.

If `Block` is defined as above, the first part of the code looks like this.

~~~~{.cpp}
#include <diy/assigner.hpp>
#include <diy/master.hpp>

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);             // diy's version of MPI_Init
  diy::mpi::communicator    world;                       // diy's version of MPI communicator

  int                       nprocs      = 8;             // total number of MPI ranks
  int                       nblocks     = 32;            // total number of blocks in global domain
  int                       num_threads = 2;             // number of threads DIY is allowed to use

  diy::ContiguousAssigner   assigner(nprocs, nblocks);   // assign blocks to MPI ranks

  diy::Master               master(world,                // communicator
                                   num_threads,          // number of threads DIY is allowed to use
                                   -1,                   // all blocks remain in memory in this example
                                   &Block::create,       // block create function
                                   &Block::destroy);     // block destroy function

...
~~~~

