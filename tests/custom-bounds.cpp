#include <diy/master.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/io/block.hpp>

#include "opts.h"

using namespace std;

#if 0                                   // default float bounds

typedef diy::ContinuousBounds          Bounds;
typedef diy::RegularContinuousLink     RCLink;

#else

typedef diy::Bounds<double>            Bounds;
typedef diy::RegularLink<Bounds>       RCLink;

#endif

typedef diy::RegularDecomposer<Bounds> Decomposer;

// block
struct Block
{
    vector<int> vals;

    static
        void* create()          { return new Block; }

    static
        void destroy(void* b)   { delete static_cast<Block*>(b); }

    static
        void add(                               // add the block to the decomposition
            int              gid,               // block global id
            const Bounds&,                      // block bounds without any ghost added
            const Bounds&,                      // block bounds including any ghost region added
            const Bounds&,                      // global data bounds
            const RCLink&    link,              // neighborhood
            diy::Master&     master)            // diy master
    {
        Block*          b   = new Block;
        RCLink*         l   = new RCLink(link);
        diy::Master&    m   = const_cast<diy::Master&>(master);
        m.add(gid, b, l);
    }

    static
        void save(
                const void*        b_,
                diy::BinaryBuffer& bb)
        {
            Block* b = (Block*)b_;

            diy::save(bb, b->vals);
        }
    static
        void load(
                void*              b_,
                diy::BinaryBuffer& bb)
        {
            Block* b = (Block*)b_;

            diy::load(bb, b->vals);
        }

    void init_data(const diy::Master::ProxyWithLink&     cp,
                   int                                   nvals)
    {
        vals.resize(nvals);
        for (int i = 0; i < nvals; i++)
            vals[static_cast<size_t>(i)] = cp.gid() * nvals + i;
    }

    void print_data(const diy::Master::ProxyWithLink&     cp)
    {
        fmt::print(stderr, "gid {}:\n", cp.gid());
        for (size_t i = 0; i < vals.size(); i++)
            fmt::print(stderr, "{} ", vals[i]);
        fmt::print(stderr, "\n");
    }
    void compare_data(const diy::Master::ProxyWithLink&     cp,
                      const diy::Master&                    master,
                      const diy::Master&                    read_master)
    {
        int lid     = master.lid(cp.gid());
        Block* b    = static_cast<Block*>(read_master.block(lid));
        for (size_t i = 0; i < vals.size(); i++)
        {
            if (vals[i] != b->vals[i])
            {
                fmt::print(stderr, "Error: values mismatch\n");
                abort();
            }
        }
    }
};

int main(int argc, char** argv)
{
    // initialize MPI
    diy::mpi::environment  env(argc, argv);     // equivalent of MPI_Init(argc, argv)/MPI_Finalize()
    diy::mpi::communicator world;               // equivalent of MPI_COMM_WORLD

    int tot_blocks  = world.size();             // default number of global blocks
    int mem_blocks  = -1;                       // everything in core for now
    int num_threads = 1;                        // 1 thread for now
    int dom_dim     = 3;                        // domain dimensionality
    int nvals       = 100;                      // number of values per block

    // get command line arguments
    bool help;
    opts::Options ops;
    ops >> opts::Option('b', "tot_blocks",  tot_blocks, " total number of blocks")
        >> opts::Option('n', "nvals",       nvals,      " number of values per block")
        >> opts::Option('h', "help",        help,       " show help");

  if (!ops.parse(argc,argv) || help)
  {
        if (world.rank() == 0)
            std::cout << ops;
        return 1;
    }

    // initialize DIY
    diy::FileStorage          storage("./DIY.XXXXXX"); // used for blocks to be moved out of core
    diy::Master               master(world,
                                     num_threads,
                                     mem_blocks,
                                     &Block::create,
                                     &Block::destroy,
                                     &storage,
                                     &Block::save,
                                     &Block::load);
    diy::ContiguousAssigner   assigner(world.size(), tot_blocks);

    // set global domain bounds
    Bounds dom_bounds(dom_dim);
    for (int i = 0; i < dom_dim; ++i)
    {
        dom_bounds.min[i] = -1.0;
        dom_bounds.max[i] =  1.0;
    }

    // decompose the domain into blocks
    Decomposer decomposer(dom_dim, dom_bounds, tot_blocks);
    decomposer.decompose(world.rank(),
                         assigner,
                         [&](int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain, const RCLink& link)
                         { Block::add(gid, core, bounds, domain, link, master); });

    // initialize
    master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
            { b->init_data(cp, nvals); });

    // debug: print the blocks
    master.foreach(&Block::print_data);

    // save the results in diy format
    diy::io::write_blocks("test.out", world, master);

    // read the results back
    diy::Master read_master(world,
            1,
            -1,
            &Block::create,
            &Block::destroy);
    diy::ContiguousAssigner   read_assigner(world.size(), -1);   // number of blocks set by read_blocks()

    diy::io::read_blocks("test.out", world, read_assigner, read_master, &Block::load);
    fmt::print(stderr, "{} blocks read from file\n", read_master.size());

    // debug: print the blocks
    read_master.foreach(&Block::print_data);

    // compare
    master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
            { b->compare_data(cp, master, read_master); });
}
