#include <vector>
#include <random>
#include <time.h>

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/master.hpp>
#include <diy/algorithms.hpp>

#include "opts.h"
#include "balance.hpp"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv);                          // diy equivalent of MPI_Init
    diy::mpi::communicator    world;                                    // diy equivalent of MPI communicator
    int                       bpr = 4;                                  // blocks per rank
    int                       iters = 1;                                // number of iterations to run
    int                       max_time = 1;                             // maximum time to compute a block (sec.)
    float                     sample_frac = 0.5f;                       // fraction of world procs to sample (0.0 - 1.0)
    float                     quantile = 0.8f;                          // quantile cutoff above which to move blocks (0.0 - 1.0)
    double                    wall_time;                                // wall clock execution time for entire code
    double                    comp_time = 0.0;                          // total computation time (sec.)
    double                    balance_time = 0.0;                       // total load balancing time (sec.)
    double                    t0;                                       // starting time (sec.)
    float                     noise_factor = 0.0;                       // multiplier for noise in predicted -> actual work
    int                       distribution = 0;                         // type of distribution for assigning work (0: uniform (default), 1: normal, 2: exponential)
    unsigned int              seed = 0;                                 // seed for random number generator (0: ignore)
    bool                      help;

    using namespace opts;
    Options ops;
    ops
        >> Option('h', "help",          help,           "show help")
        >> Option('b', "bpr",           bpr,            "number of diy blocks per mpi rank")
        >> Option('i', "iters",         iters,          "number of iterations")
        >> Option('t', "max_time",      max_time,       "maximum time to compute a block (in seconds)")
        >> Option('f', "sample_frac",   sample_frac,    "fraction of world procs to sample (0.0 - 1.0)")
        >> Option('q', "quantile",      quantile,       "quantile cutoff above which to move blocks (0.0 - 1.0)")
        >> Option('n', "noise_factor",  noise_factor,   "multiplier for noise in predicted -> actual work")
        >> Option('d', "distribution",  distribution,   "distribution for assigning work (0 uniform (default), 1 normal, 2 exponential)")
        >> Option('s', "seed",          seed,           "seed for random number generator (default: 0 = ignore")
        ;

    if (!ops.parse(argc,argv) || help)
    {
        if (world.rank() == 0)
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "Tests work stealing\n";
            std::cout << ops;
        }
        return 1;
    }

//     diy::create_logger("trace");

    int                       nblocks = world.size() * bpr;             // total number of blocks in global domain
    diy::ContiguousAssigner   static_assigner(world.size(), nblocks);

    Bounds domain(3);                                                   // global data size
    domain.min[0] = domain.min[1] = domain.min[2] = 0;
    domain.max[0] = domain.max[1] = domain.max[2] = 255;

    // record of block movements
    std::vector<diy::detail::MoveInfo> moved_blocks;
     // seed random number generator for user code, broadcast seed, offset by rank
     time_t t;
     if (world.rank() == 0)
         t = time(0);
     diy::mpi::broadcast(world, t, 0);
     srand((unsigned)t + world.rank());

    // create master for managing blocks in this process
    diy::Master master(world,
                       1,                                               // one thread
                       -1,                                              // all blocks in memory
                       &Block::create,
                       &Block::destroy,
                       0,
                       &Block::save,
                       &Block::load);

    // create a regular decomposer and call its decompose function
    diy::RegularDecomposer<Bounds> decomposer(3,
                                              domain,
                                              nblocks);
    decomposer.decompose(world.rank(), static_assigner,
                         [&](int gid,                                   // block global id
                             const Bounds&,                             // core block bounds without any ghost added (unused)
                             const Bounds& bounds,                      // block bounds including ghost region
                             const Bounds&,                             // global domain bounds (unused)
                             const RGLink& link)                        // neighborhood
                         {
                             Block*     b   = new Block;
                             RGLink*    l   = new RGLink(link);
                             b->gid         = gid;
                             b->bounds      = bounds;
                             master.add(gid, b, l);
                         });

    // seed random number generator, broadcast seed, offset by rank
    std::random_device rd;                      // seed source for the random number engine
    unsigned int s;
    if (seed)
        s = seed;
    else
        s = rd();
    diy::mpi::broadcast(master.communicator(), s, 0);
    std::mt19937 mt_gen(s + master.communicator().rank());          // mersenne_twister random number generator

    // assign work
    std::uniform_real_distribution<double> uni_distr(0.0, 1.0);
    std::normal_distribution<double> norm_distr(0.5 , 0.5);
    std::exponential_distribution<double> exp_distr(3.0);
    if (distribution == 0)
        master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                       { b->assign_work<std::uniform_real_distribution<double>, std::mt19937>(cp, 0, noise_factor, uni_distr, mt_gen); });
    else if (distribution == 1)
        master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                       { b->assign_work<std::normal_distribution<double>, std::mt19937>(cp, 0, noise_factor, norm_distr, mt_gen); });
    else if (distribution == 2)
        master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                       { b->assign_work<std::exponential_distribution<double>, std::mt19937>(cp, 0, noise_factor, exp_distr, mt_gen); });

    // debug: print each block
    // master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
    //      { b->show_block(cp); });

    // collect summary stats before beginning
    if (world.rank() == 0)
        fmt::print(stderr, "Summary stats before beginning\n");
    summary_stats(master, moved_blocks);

    // timing
    world.barrier();                                                    // barrier to synchronize clocks across procs, do not remove
    wall_time = MPI_Wtime();
    t0 = MPI_Wtime();

    // copy dynamic assigner from master
    diy::DynamicAssigner    dynamic_assigner(world, world.size(), nblocks);
    diy::record_local_gids(master, dynamic_assigner);

    // timing
    world.barrier();
    balance_time += (MPI_Wtime() - t0);

    // this barrier is mandatory, do not remove
    // dynamic assigner needs to be fully updated and sync'ed across all procs before proceeding
    world.barrier();

    // perform some iterative algorithm
    for (auto n = 0; n < iters; n++)
    {
        // debug
        if (world.rank() == 0)
            fmt::print(stderr, "iteration {}\n", n);

        // timing
        world.barrier();
        t0 = MPI_Wtime();

        // some block computation
        master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->compute(cp, max_time, n); });

        // timing
        world.barrier();
        comp_time += (MPI_Wtime() - t0);
        t0 = MPI_Wtime();

        // sampling load balancing method
        diy::load_balance_sampling(master, dynamic_assigner, &get_block_work, moved_blocks, sample_frac, quantile);

        // timing
        world.barrier();
        balance_time += (MPI_Wtime() - t0);

        // for record keeping, append the block work to the moved blocks
        int lid;
        for (auto i = 0; i < moved_blocks.size(); i++)
        {
            if ((lid = master.lid(moved_blocks[i].move_gid) >= 0))
            {
                Block* b = static_cast<Block*>(master.block(lid));
                moved_blocks[i].pred_work = b->pred_work;
                moved_blocks[i].act_work  = b->act_work;
            }
        }
    }

    world.barrier();                                    // barrier to synchronize clocks over procs, do not remove
    wall_time = MPI_Wtime() - wall_time;
    if (world.rank() == 0)
    if (world.rank() == 0)
        fmt::print(stderr, "Total elapsed wall time {:.4} s = computation time {:.4} s + balancing time {:.4} s.\n",
                wall_time, comp_time, balance_time);

    // load balance summary stats
    if (world.rank() == 0)
        fmt::print(stderr, "Summary stats upon completion\n");
    summary_stats(master, moved_blocks);
}
