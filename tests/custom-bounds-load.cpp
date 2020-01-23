#include <diy/master.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/io/block.hpp>

#include "opts.h"

using namespace std;

typedef diy::Bounds<double>            Bounds;

// block
struct Block
{
    vector<int> vals;

    static
        void* create()          { return new Block; }

    static
        void destroy(void* b)   { delete static_cast<Block*>(b); }

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

    // read the results back
    diy::Master read_master(world,
            num_threads,
            mem_blocks,
            &Block::create,
            &Block::destroy);
    diy::ContiguousAssigner   read_assigner(world.size(), -1);   // number of blocks set by read_blocks()

    diy::io::read_blocks("test.out", world, read_assigner, read_master, &Block::load);
    fmt::print(stderr, "{} blocks read from file\n", read_master.size());

    // debug: print the blocks
    read_master.foreach(&Block::print_data);
}
