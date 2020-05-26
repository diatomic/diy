//
// An example of using all-to-all to compute a global sum.
// In this example, the sum is used to determine whether all blocks are done with their work.
// Each block has a local value 'my_work' that is 0 or 1, and the global value 'tot_work'
// is the sum of the local my_work and is 0 only if my_work is 0 for all blocks
//

#include <diy/master.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

#include <diy/thirdparty/fmt/format.h>

using namespace std;

typedef     diy::ContinuousBounds       Bounds;
typedef     diy::RegularContinuousLink  RCLink;

// --- block structure ---//

// The contents of a block are completely user-defined, but
// a block must have functions defined to create, destroy, save, and load it.
// Create and destroy allocate and free the block, while save and load serialize and
// deserialize the block (can be omitted if all blocks always remain in core)
//
// NB: using the "short form" of a block (without an AddBlock functor) in this example
//
struct Block
{
    Block() {}
    static void*    create()                                   // allocate a new block
        { return new Block; }
    static void     destroy(void* b)                           // free a block
        { delete static_cast<Block*>(b); }
    static void     save(const void* b, diy::BinaryBuffer& bb) // serialize the block and write it
        { diy::save(bb, *static_cast<const Block*>(b)); }
    static void     load(void* b, diy::BinaryBuffer& bb)       // read the block and deserialize it
        { diy::load(bb, *static_cast<Block*>(b)); }

    void generate_data()                                       // initialize block values
        {
            my_work  = 1;
        }
    // block data
    int             my_work;                                   // whether this block has work to do
    int             tot_work;                                  // whether any blocks have work to do
};

// --- callback functions ---//

//
// callback function for the sum
// when using all-to-all, write the callback as if it is only called once at the beginning
// round and once at the end; diy will take care of the intermediate rounds for you
//
void sum(Block* b,                                  // local block
         const diy::ReduceProxy& rp)                // communication proxy
{
    if (!rp.in_link().size())                       // initialize global sum in first round
        b->tot_work = 0;

    // step 1: enqueue
    for (int i = 0; i < rp.out_link().size(); ++i)
        rp.enqueue(rp.out_link().target(i), b->my_work);

    // step 2: dequeue
    for (int i = 0; i < rp.in_link().size(); ++i)
    {
        int in_val;
        rp.dequeue(rp.in_link().target(i).gid, in_val);
        b->tot_work += in_val;
    }
}

//
// prints the value of tot_work
//
void get_tot_work(Block* b,                             // local block
                  const diy::Master::ProxyWithLink& cp) // communication proxy
{
    fmt::print(stderr, "[{}] tot_work = {}\n", cp.gid(), b->tot_work);
}

//
// sets my_work to be done for some of my blocks
//
void set_some_done(Block* b,                             // local block
                   const diy::Master::ProxyWithLink& cp) // communication proxy
{
    b->my_work  = (cp.gid() % 2 ? 1 : 0);                // eg, setting every other block to be done
}

//
// sets my_work to be done for all of my blocks
//
void set_all_done(Block* b,                          // local block
                  const diy::Master::ProxyWithLink&) // communication proxy
{
    b->my_work = 0;
}

// --- main program ---//

int main(int argc, char* argv[])
{
    diy::mpi::environment  env(argc, argv);            // equivalent of MPI_Init/MPI_Finalize()
    diy::mpi::communicator world;                      // equivalent of MPI_COMM_WORLD
    int                    nblocks     = world.size(); // global number of blocks, eg 1 per process
    int                    mem_blocks  = -1;           // all blocks in memory
    int                    threads     = 1;            // no multithreading
    int                    dim         = 3;            // 3-d blocks

    // diy initialization
    diy::FileStorage       storage("./DIY.XXXXXX"); // used for blocks moved out of core
    diy::Master            master(world,            // master is the top-level diy object
                                  threads,
                                  mem_blocks,
                                  &Block::create,
                                  &Block::destroy,
                                  &storage,
                                  &Block::save,
                                  &Block::load);

    // set some global data bounds (their initialization is omitted in this example)
    Bounds domain(dim);

    // assign blocks to processes
    diy::RoundRobinAssigner   assigner(world.size(), nblocks);

    // decompose the domain into blocks
    // the last argument is master because we are using the "short form" of blocks in this example
    diy::decompose(dim, world.rank(), domain, assigner, master);

    // NB my_work was set to 1 for each block when it was created
    // i.e., all blocks still have work to do at this point

    // all to all reduction to determine whether all blocks are done
    int k = 2;                                      // the radix of the k-ary reduction tree
    diy::all_to_all(master, assigner, &sum, k);

    // print the result
    if (world.rank() == 0)
        fmt::print(stderr, "None of the blocks are done; tot_work will be > 0 for all blocks:\n");
    // printing all blocks in this example to show they have the same value
    master.foreach(&get_tot_work);           // callback function for each local block

    // set some blocks to be done, reduce again, and print the result
    master.foreach(&set_some_done);
    diy::all_to_all(master, assigner, &sum, k);
    if (world.rank() == 0)
        fmt::print(stderr, "Some of the blocks are done, but tot_work will still be > 0 for all blocks:\n");
    master.foreach(&get_tot_work);

    // now set all blocks to be done, reduce again, and print the result
    master.foreach(&set_all_done);
    diy::all_to_all(master, assigner, &sum, k);
    if (world.rank() == 0)
        fmt::print(stderr, "Only now that every block is done will tot_work be 0 for all blocks:\n");
    master.foreach(&get_tot_work);
}
