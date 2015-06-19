\defgroup Initialization

Master initialization snippet:
\snippet examples/simple/simple.cpp Master initialization


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
  // often useful to save the block bounds with the block, but not mandatory
  Block(const Bounds& bounds_): bounds(bounds_)               {}
  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)
    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)
    { diy::load(bb, *static_cast<Block*>(b)); }
  // It's usually a good idea to initialize the data right here, whether by reading from a file, etc.
  void generate_data(size_t n)
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
  Block() {}          // the create() function above needs an empty constructor
};
//
// add blocks to diy
//
struct AddBlock
{
  // add any extra arguments to the constructor
  AddBlock(diy::Master& master_, size_t num_elems_): master(master_), num_elems(num_elems_) {}

  // operator() is always defined as follows; arguments are fixed
  void operator()(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain,
                   const RCLink& link) const
  {
    Block*        b = new Block(core);
    RCLink*       l = new RCLink(link);
    diy::Master&  m = const_cast<diy::Master&>(master);
    m.add(gid, b, l);
    b->gid = gid;
    // initializing the block data when the block is created will save one serialization and storage cycle
    b->generate_data(num_elems);
  }
  // members that are needed above are declared here
  diy::Master&  master;
  size_t num_elems;
};

int main(int argc, char** argv)
{
    ...

    diy::Bounds               domain;
    diy::mpi::communicator    world(comm);
    diy::FileStorage          storage("./DIY.XXXXXX");
    diy::Master               master(world,
                                     &Block::create,
                                     &Block::destroy,
                                     mem_blocks,
                                     num_threads,
                                     &storage,
                                     &Block::save,
                                     &Block::load);
    diy::ContiguousAssigner   assigner(world.size(), tot_blocks);
    AddBlock                  create(master, arg1, arg2, etc);
    diy::decompose(dim, world.rank(), domain, assigner, create);

    ...
}
~~~~

In addition the the comments in the above code, more details about various sections of the code follow below.

### Continuous and Discrete Decompositions

Regular decompositions can be continous (floating-point extents that share common boundaries) or discrete (i,j,k integer extents that may or may not overlap). They are defined as follows:

~~~~{.cpp}
typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;
// or
typedef  diy::DiscreteBounds         Bounds;
typedef  diy::RegularLink            RLink;
~~~~

### Block and AddBlock Objects

A block object must be defined and must include the following functions:

~~~~{.cpp}
static void*    create();
static void     destroy(void* b);
static void     save(const void* b, diy::BinaryBuffer& bb);
static void     load(void* b, diy::BinaryBuffer& bb);
~~~~
An object to add blocks (AddBlock in the above example) is also required. It must include the following definition of the () operator:

~~~~{.cpp}
// gid: the block global id
// core: block bounds without any additional ghost region
// bounds: block bounds including any additional ghost region
// domain: overall global domain bounds
// link: neighbors of this block (use continuous or regular link, depending on decomposition type)
void operator()(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain,
                const RCLink& link) const
{
  Block*        b = new Block(core);                   // arguments depend on the Block constructor above
  RCLink*       l = new RCLink(link);                  // or RLink for discrete (not continuous) link
  diy::Master&  m = const_cast<diy::Master&>(master);
  m.add(gid, b, l);
  // followed by any other initialization of the block data
}
~~~~

### Initialize DIY

Finally, launch diy as follows:

~~~~{.cpp}

diy::Bounds               domain;                              // the global domain bounds
diy::mpi::communicator    world(comm);                         // comm is the MPI communicator; world is diy's wrapper around comm
diy::FileStorage          storage("./DIY.XXXXXX");             // diy's out of core files are stored with this naming convention
diy::Communicator         diy_comm(world);                     // diy's communicator object for all diy algorithms
diy::Master               master(diy_comm,                     // the diy master object
                                 &Block::create,               // block create function
                                 &Block::destroy,              // block destroy function
                                 mem_blocks,                   // number of blocks allowed in memory at a time (-1 = all)
                                 num_threads,                  // number of threads diy can use (-1 = all)
                                 &storage,                     // the storage object above
                                 &Block::save,                 // block serialization, save out of core
                                 &Block::load);                // block serialization, load into core
diy::ContiguousAssigner   assigner(world.size(), tot_blocks);  // assign contiguous blocks to processes; tot_blocks is total number of blocks in the domain
// or
diy::RoundRobinAssigner   assigner(world.size(), tot_blocks);  // assign blocks to processes in round robin order
AddBlock                  create(master, arg1, arg2, etc);     // add blocks to the master
diy::decompose(dim, world.rank(), domain, assigner, create);   // decompose the domain in dim dimensions

~~~~
