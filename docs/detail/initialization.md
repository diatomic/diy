\defgroup Initialization

The following is an example of the steps needed to initialize diy:

~~~~{.cpp}
#include <diy/master.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;

//
// the block is the basic unit of decomposition and work
//
struct Block
{
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)
    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)
    { diy::load(bb, *static_cast<Block*>(b)); }
  void generate_data(size_t n)  // Usually a good idea to initialize the data here
  {
    data.resize(n);
    for (size_t i = 0; i < n; ++i)
      data[i] = ...;
  }

  // the following are examples of other members of the block; modify as needed
  Bounds bounds;      // often a good idea to have the block bounds available
  vector<float> data; // usually there will be some block data
  int gid;            // diy can often tell you the gid during communication, but you may also want to save it
private:
};

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
    diy::ContiguousAssigner   assigner(world.size(), tot_blocks);
    diy::decompose(dim, world.rank(), domain, assigner, master);

    ...
}
~~~~

See the example in [test-direct-master.cpp](\ref decomposition/test-direct-master.cpp). More details about various sections of the example follow below.

### Continuous and Discrete Decompositions

Regular decompositions can be continous (floating-point extents that share common boundaries) or discrete (i,j,k integer extents that may or may not overlap). They are defined as follows:

~~~~{.cpp}
typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;
// or
typedef  diy::DiscreteBounds         Bounds;
typedef  diy::RegularLink            RLink;
~~~~

### Block Objects

A block object includes the following functions:

~~~~{.cpp}
static void*    create();                                      // allocate a new block
static void     destroy(void* b);                              // free a block
static void     save(const void* b, diy::BinaryBuffer& bb);    // serialize a block to storage
static void     load(void* b, diy::BinaryBuffer& bb);          // deserialize a block from storage
~~~~

### Initialize DIY

Finally, launch diy as follows:

~~~~{.cpp}

diy::Bounds               domain;                              // the global domain bounds
diy::mpi::communicator    world(comm);                         // comm is the MPI communicator; world is diy's wrapper around comm
diy::FileStorage          storage("./DIY.XXXXXX");             // diy's out of core files are stored with this naming convention
diy::Communicator         diy_comm(world);                     // diy's communicator object for all diy algorithms
diy::Master               master(diy_comm,                     // the diy master object
                                 num_threads,                  // number of threads diy can use (-1 = all)
                                 mem_blocks,                   // number of blocks allowed in memory at a time (-1 = all)
                                 &Block::create,               // block create function
                                 &Block::destroy,              // block destroy function
                                 &storage,                     // the storage object above
                                 &Block::save,                 // block serialization, save out of core
                                 &Block::load);                // block serialization, load into core
diy::ContiguousAssigner   assigner(world.size(), tot_blocks);  // assign contiguous blocks to processes; tot_blocks is total number of blocks in the domain
// or
diy::RoundRobinAssigner   assigner(world.size(), tot_blocks);  // assign blocks to processes in round robin order
diy::decompose(dim, world.rank(), domain, assigner, master);   // decompose the domain in dim dimensions

~~~~

Notes:

When all blocks will remain in memory, there is a shorter form of the `master` constructor that can be used. In this case there is no need to specify most of the arguments because they relate to block loading/unloading.
~~~~{.cpp}
diy::Master               master(diy_comm, num_threads);
~~~~

An alternative form of decomposition does not require an entire block object, and only takes a block create function. The pattern is:

~~~~{.cpp}
void create(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain,
            const diy::Link& link)
{
    ...
}
diy::decompose(dim, world.rank(), domain, assigner, create);
~~~~

See the example in [test-decomposition.cpp](\ref decomposition/test-decomposition.cpp). (This example does not even create a master object, only a decomposition.)
