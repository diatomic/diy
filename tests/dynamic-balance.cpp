#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/master.hpp>
#include <diy/algorithms.hpp>

#include "../opts.h"
#include "balance.hpp"

#include <cstdlib>
#include <cstring>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

namespace
{
void require_dynamic_queue_test(bool condition, const char* message)
{
    if (!condition)
    {
        fmt::print(stderr, "dynamic queue migration test failed: {}\n", message);
        std::abort();
    }
}

void add_queue_record(diy::Master::IncomingQueues& in_qs, int from, diy::MemoryBuffer&& bb, bool reset = true)
{
    if (reset)
        bb.reset();
    in_qs[from].access()->emplace_back(std::move(bb));
}

void test_dynamic_incoming_queue_migration(const diy::mpi::communicator& world)
{
    diy::Master send_master(world);
    diy::Master recv_master(world);
    diy::Master aux_send(world);
    diy::Master aux_recv(world);

    int send_block = 0;
    int recv_block = 0;
    int aux_send_block = 0;
    int aux_recv_block = 0;

    const int move_gid = 1000 + world.rank();
    const int recv_move_gid = 2000 + world.rank();
    const int aux_send_gid = 3000 + world.rank();
    const int aux_recv_gid = 4000 + world.rank();

    send_master.add(move_gid, &send_block, new diy::Link);
    recv_master.add(recv_move_gid, &recv_block, new diy::Link);
    aux_send.add(aux_send_gid, &aux_send_block, new diy::Link);
    aux_recv.add(aux_recv_gid, &aux_recv_block, new diy::Link);

    diy::Master::IncomingQueues& send_incoming = send_master.incoming(move_gid);

    diy::MemoryBuffer first;
    diy::save(first, 41);
    add_queue_record(send_incoming, 7, std::move(first));

    diy::MemoryBuffer second;
    diy::save(second, 42);
    add_queue_record(send_incoming, 7, std::move(second));

    diy::MemoryBuffer with_blob;
    diy::save(with_blob, 43);
    const char blob_data[] = {'d', 'i', 'y'};
    with_blob.save_binary_blob(blob_data, sizeof(blob_data));
    add_queue_record(send_incoming, 8, std::move(with_blob));

    diy::MemoryBuffer partially_read;
    diy::save(partially_read, 0);
    diy::save(partially_read, 44);
    partially_read.reset();
    int ignored;
    diy::load(partially_read, ignored);
    add_queue_record(send_incoming, 9, std::move(partially_read), false);

    diy::MemoryBuffer partially_consumed_blob;
    const char consumed_blob[] = {'o', 'l', 'd'};
    const char next_blob[] = {'n', 'e', 'w'};
    partially_consumed_blob.save_binary_blob(consumed_blob, sizeof(consumed_blob));
    partially_consumed_blob.save_binary_blob(next_blob, sizeof(next_blob));
    diy::BinaryBlob consumed = partially_consumed_blob.load_binary_blob();
    require_dynamic_queue_test(consumed.size == sizeof(consumed_blob), "source 11 consumed blob size");
    add_queue_record(send_incoming, 11, std::move(partially_consumed_blob), false);

    diy::Master::ProxyWithLink send_cp = aux_send.proxy(0);
    diy::BlockID dest_block = {aux_recv_gid, world.rank()};
    diy::detail::dynamic_send_incoming_queues(send_cp, dest_block, send_incoming);
    diy::MemoryBuffer wire(std::move(send_cp.outgoing(dest_block)));
    wire.reset();

    diy::Master::ProxyWithLink recv_cp = aux_recv.proxy(0);
    recv_cp.incoming(aux_send_gid) = std::move(wire);
    diy::detail::dynamic_recv_incoming_queues(recv_cp, recv_master, aux_send_gid, recv_move_gid);

    diy::Master::IncomingQueues& recv_incoming = recv_master.incoming(recv_move_gid);
    auto from_seven = recv_incoming[7].access();
    auto from_eight = recv_incoming[8].access();
    auto from_nine = recv_incoming[9].access();
    auto from_eleven = recv_incoming[11].access();

    require_dynamic_queue_test(from_seven->size() == 2, "source 7 queue count");
    require_dynamic_queue_test(from_eight->size() == 1, "source 8 queue count");
    require_dynamic_queue_test(from_nine->size() == 1, "source 9 queue count");
    require_dynamic_queue_test(from_eleven->size() == 1, "source 11 queue count");

    int value;
    diy::load(from_seven->front().buffer(), value);
    require_dynamic_queue_test(value == 41, "first source 7 value");
    from_seven->pop_front();

    diy::load(from_seven->front().buffer(), value);
    require_dynamic_queue_test(value == 42, "second source 7 value");

    diy::load(from_eight->front().buffer(), value);
    require_dynamic_queue_test(value == 43, "source 8 value");
    diy::BinaryBlob blob = from_eight->front().buffer().load_binary_blob();
    require_dynamic_queue_test(blob.size == sizeof(blob_data), "source 8 blob size");
    require_dynamic_queue_test(std::memcmp(blob.pointer.get(), blob_data, blob.size) == 0, "source 8 blob contents");

    diy::load(from_nine->front().buffer(), value);
    require_dynamic_queue_test(value == 44, "source 9 preserved read position");

    diy::BinaryBlob migrated_next_blob = from_eleven->front().buffer().load_binary_blob();
    require_dynamic_queue_test(migrated_next_blob.size == sizeof(next_blob), "source 11 next blob size");
    require_dynamic_queue_test(std::memcmp(migrated_next_blob.pointer.get(), next_blob, migrated_next_blob.size) == 0, "source 11 next blob contents");
}
}

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv);                          // diy equivalent of MPI_Init
    diy::mpi::communicator    world;                                    // diy equivalent of MPI communicator
    test_dynamic_incoming_queue_migration(world);

    int                       bpr = 4;                                  // blocks per rank
    int                       iters = 1;                                // number of iterations to run
    int                       max_time = 1;                             // maximum time to compute a block (sec.)
    double                    wall_time;                                // wall clock execution time for entire code
    float                     sample_frac = 0.5f;                       // fraction of world procs to sample (0.0 - 1.0)
    float                     quantile = 0.8f;                          // quantile cutoff above which to move blocks (0.0 - 1.0)
    int                       vary_work = 0;                            // whether to vary workload between iterations
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
        >> Option('v', "vary_work",     vary_work,      "vary workload per iteration (0 or 1, default 0)")
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

    // copy dynamic assigner from master
    diy::DynamicAssigner    dynamic_assigner(world, world.size(), nblocks);

    // debug: timing
    world.barrier();
    wall_time = MPI_Wtime();

    // perform some iterative algorithm
    for (auto n = 0; n < iters; n++)
    {
        // debug
        if (world.rank() == 0)
            fmt::print(stderr, "iteration {}\n", n);

        if (vary_work)
        {
            if (distribution == 0)
                master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                               { b->assign_work<std::uniform_real_distribution<double>, std::mt19937>(cp, 0, noise_factor, uni_distr, mt_gen); });
            else if (distribution == 1)
                master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                               { b->assign_work<std::normal_distribution<double>, std::mt19937>(cp, 0, noise_factor, norm_distr, mt_gen); });
            else if (distribution == 2)
                master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                               { b->assign_work<std::exponential_distribution<double>, std::mt19937>(cp, 0, noise_factor, exp_distr, mt_gen); });

            // collect summary stats before beginning iteration
            if (world.rank() == 0)
                fmt::print(stderr, "Summary stats before beginning iteration {}\n", n);
            summary_stats(master, moved_blocks);
        }

        // some block computation
        master.dynamic_foreach(
                [&](Block* b, const diy::Master::ProxyWithLink& cp) { b->compute(cp, max_time); },
                &get_block_work,
                dynamic_assigner,
                sample_frac,
                quantile,
                moved_blocks);

        // exchange any communication between blocks
        master.exchange();

        // receive the communication
        master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->recv_comm(cp); });

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

        if (vary_work)
        {
            // collect summary stats after ending iteration
            if (world.rank() == 0)
                fmt::print(stderr, "Summary stats after ending iteration {}\n", n);
            summary_stats(master, moved_blocks);
        }
    }

    // debug: timing
    world.barrier();
    wall_time = MPI_Wtime() - wall_time;
    if (world.rank() == 0)
        fmt::print(stderr, "Total elapsed wall time {:.3} sec.\n", wall_time);

    // load balance summary stats
    if (world.rank() == 0)
        fmt::print(stderr, "Summary stats upon completion\n");
    summary_stats(master, moved_blocks);
}
