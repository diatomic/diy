//
// In swap-reduction, block data are split into k pieces that are swapped between the k members
// of a group in each round. This example begins with an unsorted set of points that do not lie
// in the bounds of the blocks, and the swap reduction is used to sort the points in the correct
// blocks with respect to the block bounds.
//

#include <cmath>

#include <diy/master.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/swap.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "opts.h"
#include "point.h"

typedef     diy::ContinuousBounds       Bounds;
typedef     diy::RegularContinuousLink  RCLink;

static const unsigned DIM = 3;
typedef     PointBlock<DIM>             Block;
typedef     AddPointBlock<DIM>          AddBlock;

// --- callback functions ---//

//
// callback function for redistribute operator, called in each round of the reduction
//
void redistribute(void* b_,                                 // local block
                  const diy::ReduceProxy& srp,              // communication proxy
                  const diy::RegularSwapPartners& partners) // partners of the current block
{
    Block*        b        = static_cast<Block*>(b_);
    unsigned      round    = srp.round();                   // current round number

    // step 1: dequeue
    // dequeue all the incoming points and add them to this block's vector
    // could use srp.incoming() instead
    for (int i = 0; i < srp.in_link().size(); ++i)
    {
        int nbr_gid = srp.in_link().target(i).gid;
        if (nbr_gid == srp.gid())
            continue;

        std::vector<Block::Point>    in_points;
        srp.dequeue(nbr_gid, in_points);
        fmt::print(stderr, "[{}:{}] Received {} points from [{}]\n",
                   srp.gid(), round, (int) in_points.size(), nbr_gid);
        for (size_t j = 0; j < in_points.size(); ++j)
            b->points.push_back(in_points[j]);
    }

    // step 2: sort and enqueue
    if (srp.out_link().size() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Block::Point> > out_points(srp.out_link().size());
    int group_size = srp.out_link().size();  // number of outbound partners
    int cur_dim    = partners.dim(round);    // current dimension along which groups are formed
    // sort points into vectors corresponding to neighbor blocks
    for (size_t i = 0; i < b->points.size(); ++i) // for all points
    {
        auto loc = static_cast<size_t>(floor((b->points[i][cur_dim] - b->box.min[cur_dim]) /
                                            (b->box.max[cur_dim] - b->box.min[cur_dim]) * group_size));
        out_points[loc].push_back(b->points[i]);
    }
    int pos = -1;
    // enqueue points to neighbor blocks
    for (int i = 0; i < group_size; ++i)     // for all neighbors
    {
        if (srp.out_link().target(i).gid == srp.gid())
        {
            b->points.swap(out_points[i]);
            pos = i;
        }
        else
        {
            srp.enqueue(srp.out_link().target(i), out_points[i]);
            fmt::print(stderr, "[{}] Sent {} points to [{}]\n",
                       srp.gid(), (int) out_points[i].size(), srp.out_link().target(i).gid);
        }
    }

    // step 3: readjust box boundaries for next round
    float new_min = b->box.min[cur_dim] + (b->box.max[cur_dim] -
                                           b->box.min[cur_dim])/group_size*pos;
    float new_max = b->box.min[cur_dim] + (b->box.max[cur_dim] -
                                           b->box.min[cur_dim])/group_size*(pos + 1);
    b->box.min[cur_dim] = new_min;
    b->box.max[cur_dim] = new_max;
}

struct SwapReduceFixture
{
    static int          nblocks;
    static size_t       num_points;
    static int          mem_blocks;
    static int          threads;
    static int          k;
    static std::string  prefix;

    static bool         verbose;

    diy::mpi::communicator    world;
};

int         SwapReduceFixture::nblocks     = 0;
size_t      SwapReduceFixture::num_points  = 100;            // points per block
int         SwapReduceFixture::mem_blocks  = -1;             // all blocks in memory
int         SwapReduceFixture::threads     = 1;
int         SwapReduceFixture::k           = 2;              // radix for k-ary reduction
std::string SwapReduceFixture::prefix      = "./DIY.XXXXXX"; // for saving block files out of core
bool        SwapReduceFixture::verbose     = false;

// check that block values are in the block bounds (debug)
void          verify_block(Block* b,
                           const diy::Master::ProxyWithLink& cp) // communication proxy
{
    const RCLink* link = dynamic_cast<RCLink*>(cp.link());

    for (unsigned j = 0; j < DIM; ++j)
    {
        const Bounds& core = link->core();
        CHECK(b->box.min[j] == core.min[j]);
        CHECK(b->box.max[j] == core.max[j]);
    }

    for (size_t i = 0; i < b->points.size(); ++i)
        for (unsigned j = 0; j < DIM; ++j)
        {
            CHECK(b->points[i][j] >= b->box.min[j]);
            CHECK(b->points[i][j] <= b->box.max[j]);
        }
}

TEST_CASE_METHOD(SwapReduceFixture, "point sorting", "[swap-reduce]")
{
    int   dim = DIM;

    // set some global data bounds (defaults set before option parsing)
    Bounds domain(dim);
    for (int i = 0; i < dim; ++i)
    {
        domain.min[i] = 0;
        domain.max[i] = 100.;
    }

    // diy initialization
    diy::FileStorage          storage(prefix);            // used for blocks moved out of core
    diy::Master               master(world,               // top-level diy object
                                     threads,
                                     mem_blocks,
                                     &Block::create,
                                     &Block::destroy,
                                     &storage,
                                     &Block::save,
                                     &Block::load);
    AddBlock                  create(master, num_points); // object for adding new blocks to master

    // choice of contiguous or round robin assigner
    diy::ContiguousAssigner   assigner(world.size(), nblocks);
    //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

    // decompose the domain into blocks
    diy::RegularDecomposer<Bounds> decomposer(dim, domain, nblocks);
    decomposer.decompose(world.rank(), assigner, create);

    // swap-based reduction: create the partners that determine how groups are formed
    // in each round and then execute the reduction

    // partners for swap over regular block grid
    diy::RegularSwapPartners  partners(decomposer,  // domain decomposition
                                       k,       // radix of k-ary reduction
                                       false);  // contiguous = true: distance doubling
                                                // contiguous = false: distance halving

    diy::reduce(master,                         // Master object
                assigner,                       // Assigner object
                partners,                       // RegularSwapPartners object
                &redistribute);                 // swap operator callback function

    // callback functions for local block
    bool v = verbose;
    master.foreach([v](Block* b, const diy::Master::ProxyWithLink& cp) { b->print_block(cp, v); });
    master.set_threads(1);        // catch.hpp isn't thread-safe
    master.foreach(&verify_block);
}

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv); // equivalent of MPI_Init(argc, argv)/MPI_Finalize()
    diy::mpi::communicator    world;           // equivalent of MPI_COMM_WORLD

    Catch::Session session;

    bool help, verbose;

    // get command line arguments
    using namespace opts;
    Options ops;
    ops
        >> Option('n', "number",  SwapReduceFixture::num_points,     "number of points per block")
        >> Option('k', "k",       SwapReduceFixture::k,              "use k-ary swap")
        >> Option('b', "blocks",  SwapReduceFixture::nblocks,        "number of blocks")
        >> Option('t', "thread",  SwapReduceFixture::threads,        "number of threads")
        >> Option('m', "memory",  SwapReduceFixture::mem_blocks,     "number of blocks to keep in memory")
        >> Option(     "prefix",  SwapReduceFixture::prefix,         "prefix for external storage")
        >> Option('v', "verbose", verbose,                           "print the block contents")
        >> Option('h', "help",    help,                              "show help")
        ;
    if (!ops.parse(argc,argv) || help)
    {
        if (world.rank() == 0)
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "Generates random particles in the domain and redistributes them into correct blocks.\n";
            std::cout << ops;
        }
        return 1;
    }

    return session.run();
}
