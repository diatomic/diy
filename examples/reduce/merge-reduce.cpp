//
// Merge reduction merges blocks together, computing a sum of their values. At
// each round, one block of a group of k blocks is the root of the group. The
// other blocks send their data to the root, which computes the sum, and the
// root block (only) proceeds to the next round. After log_k(numblocks) rounds,
// one block contains the global sum of the values.
//

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

// --- block structure ---//

// the contents of a block are completely user-defined
// however, a block must have functions defined to create, destroy, save, and load it
// create and destroy allocate and free the block, while
// save and load serialize and deserialize the block
// these four functions are called when blocks are cycled in- and out-of-core
// they can be member functions of the block, as below, or separate standalone functions
struct Block
{
    Block(const Bounds& bounds_):
        bounds(bounds_)                         {}

    static void*    create()                                   // allocate a new block
    { return new Block; }
    static void     destroy(void* b)                           // free a block
    { delete static_cast<Block*>(b); }
    static void     save(const void* b, diy::BinaryBuffer& bb) // serialize the block and write it
    {
        diy::save(bb, static_cast<const Block*>(b)->bounds);
        diy::save(bb, static_cast<const Block*>(b)->data);
    }
    static void     load(void* b, diy::BinaryBuffer& bb)       // read the block and deserialize it
    {
        diy::load(bb, static_cast<Block*>(b)->bounds);
        diy::load(bb, static_cast<Block*>(b)->data);
    }

    void            generate_data(size_t n)                    // initialize block values
    {
        data.resize(n);
        for (size_t i = 0; i < n; ++i)
            data[i] = static_cast<int>(i);
    }
    // block data
    Bounds          bounds { 0 };
    vector<int>     data;
private:
    Block()                                     {}
};

// diy::decompose needs to have a function defined to create a block
// here, it is wrapped in an object to add blocks with an overloaded () operator
// it could have also been written as a standalone function
struct AddBlock
{
    AddBlock(diy::Master& master_, size_t num_points_):
        master(master_),
        num_points(num_points_)
        {}

    // this is the function that is needed for diy::decompose
    void  operator()(int gid,                // block global id
                     const Bounds& core,     // block bounds without any ghost added
                     const Bounds&,          // block bounds including any ghost region added
                     const Bounds&,          // global data bounds
                     const RCLink& link)     // neighborhood
        const
        {
            Block*          b   = new Block(core);
            RCLink*         l   = new RCLink(link);
            diy::Master&    m   = const_cast<diy::Master&>(master);

            m.add(gid, b, l); // add block to the master (mandatory)

            b->generate_data(num_points);          // initialize block data (typical)
        }

    diy::Master&  master;
    size_t        num_points;
};

// --- callback functions ---//

//
// callback function for merge operator, called in each round of the reduction
// one block is the root of the group
// link is the neighborhood of blocks in the group
// root block of the group receives data from other blocks in the group and reduces the data
// nonroot blocks send data to the root
//
void sum(Block* b,                                  // local block
         const diy::ReduceProxy& rp,                // communication proxy
         const diy::RegularMergePartners&)          // partners of the current block
{
    unsigned   round    = rp.round();               // current round number

    // step 1: dequeue and merge
    for (int i = 0; i < rp.in_link().size(); ++i)
    {
        int nbr_gid = rp.in_link().target(i).gid;
        if (nbr_gid == rp.gid())
        {
            fmt::print(stderr, "[{}:{}] Skipping receiving from self\n", rp.gid(), round);
            continue;
        }

        std::vector<int>    in_vals;
        rp.dequeue(nbr_gid, in_vals);
        fmt::print(stderr, "[{}:{}] Received {} values from [{}]\n",
                   rp.gid(), round, (int)in_vals.size(), nbr_gid);
        for (size_t j = 0; j < in_vals.size(); ++j)
            (b->data)[j] += in_vals[j];
    }

    // step 2: enqueue
    for (int i = 0; i < rp.out_link().size(); ++i)    // redundant since size should equal to 1
    {
        // only send to root of group, but not self
        if (rp.out_link().target(i).gid != rp.gid())
        {
            rp.enqueue(rp.out_link().target(i), b->data);
            fmt::print(stderr, "[{}:{}] Sent {} valuess to [{}]\n",
                       rp.gid(), round, (int)b->data.size(), rp.out_link().target(i).gid);
        } else
            fmt::print(stderr, "[{}:{}] Skipping sending to self\n", rp.gid(), round);

    }
}

//
// prints the block values
//
void print_block(Block* b,                             // local block
                 const diy::Master::ProxyWithLink& cp, // communication proxy
                 bool verbose)                         // user-defined additional arguments
{
    fmt::print(stderr, "[{}] Bounds: {} {} {} -- {} {} {}\n",
               cp.gid(),
               b->bounds.min[0], b->bounds.min[1], b->bounds.min[2],
               b->bounds.max[0], b->bounds.max[1], b->bounds.max[2]);

    if (verbose && cp.gid() == 0)
    {
        fmt::print(stderr, "[{}] {} vals: ", cp.gid(), b->data.size());
        for (size_t i = 0; i < b->data.size(); ++i)
            fmt::print(stderr, "{}  ", b->data[i]);
        fmt::print(stderr, "\n");
    }
}

// --- main program ---//

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv); // equivalent of MPI_Init(argc, argv)/MPI_Finalize()
    diy::mpi::communicator    world;           // equivalent of MPI_COMM_WORLD

    int                       nblocks     = world.size(); // global number of blocks
                                                          // in this example, one per process
    size_t                    num_points  = 10;           // points per block
    int                       mem_blocks  = -1;           // all blocks in memory
    int                       threads     = 1;            // no multithreading
    int                       dim         = 3;            // 3-d blocks

    // get command line arguments
    using namespace opts;
    Options ops;

    bool verbose, contiguous, help;
    ops
        >> Option('v', "verbose",    verbose,    "verbose output")
        >> Option('c', "contiguous", contiguous, "use contiguous partners")
        >> Option('h', "help",       help,       "show help")
        ;

    ops
        >> Option('d', "dim",     dim,            "dimension")
        >> Option('b', "blocks",  nblocks,        "number of blocks")
        >> Option('t', "thread",  threads,        "number of threads")
        ;

    if (!ops.parse(argc, argv) || help)
    {
        std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
        std::cout << ops;
        return 1;
    }

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
    AddBlock               create(master, num_points); // an object for adding new blocks to master

    // set some global data bounds
    Bounds domain { dim };
    for (int i = 0; i < dim; ++i)
    {
        domain.min[i] = 0;
        domain.max[i] = 128.;
    }

    // choice of contiguous or round robin assigner
    diy::ContiguousAssigner   assigner(world.size(), nblocks);
    //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

    // decompose the domain into blocks
    diy::RegularDecomposer<Bounds> decomposer(dim, domain, nblocks);
    decomposer.decompose(world.rank(), assigner, create);

    // merge-based reduction: create the partners that determine how groups are formed
    // in each round and then execute the reduction

    int k = 2;                               // the radix of the k-ary reduction tree

    // partners for merge over regular block grid
    diy::RegularMergePartners  partners(decomposer,  // domain decomposition
                                        k,           // radix of k-ary reduction
                                        contiguous); // contiguous = true: distance doubling
                                                     // contiguous = false: distance halving

    // reduction
    diy::reduce(master,                              // Master object
                assigner,                            // Assigner object
                partners,                            // RegularMergePartners object
                &sum);                               // merge operator callback function

    master.foreach([verbose](Block* b, const diy::Master::ProxyWithLink& cp)
                   { print_block(b, cp, verbose); });  // callback function for each local block
}
