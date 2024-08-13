// Simple neighbor communication by creating a linear chain of blocks, each
// block connected to two neighbors (predecessor and successor), except for the
// first and the last blocks, which have only one or the other. Each block
// computes an average of its values and those of its neighbors. The average is
// stored in the block, and the blocks are written to a file in storage.
// Also demonstrates the use of the all_reduce proxy collective in conjunction
// with the neighbor communication.
//

#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

#include <diy/io/block.hpp>

#include "opts.h"

#include "block.h"

#define UNUSED(expr) do { (void)(expr); } while (0)

// --- callback functions ---//

//
// compute sum of local values and enqueue the sum
//
void local_sum(Block* b,                             // local block
               const diy::Master::ProxyWithLink& cp) // communication proxy
{
    diy::Link*    l = cp.link();

    // compute local sum
    int total = 0;
    for (unsigned i = 0; i < b->values.size(); ++i)
        total += b->values[i];

    std::cout << "Total     (" << cp.gid() << "): " << total        << std::endl;

    // for all neighbor blocks
    // enqueue data to be sent to this neighbor block in the next exchange
    for (int i = 0; i < l->size(); ++i)
        cp.enqueue(l->target(i), total);

    // diy collectives (optional) are piggybacking on the enqueue/exchange/dequeue mechanism.
    // They are invoked by posting the collective at the time of the enqueue and getting the
    // result at the time of the dequeue.
    // all_reduce() is the posting of the collective.
    cp.all_reduce(total, std::plus<int>());
}

//
// average the values received from the neighbors
//
void average_neighbors(Block* b,                             // local block
                       const diy::Master::ProxyWithLink& cp) // communication proxy
{
    diy::Link*    l = cp.link(); UNUSED(l);

    // diy collectives (optional) are piggybacking on the enqueue/exchange/dequeue mechanism.
    // They are invoked by posting the collective at the time of the enqueue and getting the
    // result at the time of the dequeue.
    // cp.get() is the result retrieval.
    int all_total = cp.get<int>(); UNUSED(all_total);

    // gids of incoming neighbors in the link
    std::vector<int> in;
    cp.incoming(in);

    // for all neighbor blocks
    // dequeue data received from this neighbor block in the last exchange
    int total = 0;
    for (unsigned i = 0; i < in.size(); ++i)
    {
        int v;
        cp.dequeue(in[i], v);
        total += v;
    }

    // compute average and print it
    b->average = float(total) / in.size();
    std::cout << "Average   (" << cp.gid() << "): " << b->average   << std::endl;
}

// --- main program ---//

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv); // equivalent of MPI_Init(argc, argv)/MPI_Finalize()
    diy::mpi::communicator    world;           // equivalent of MPI_COMM_WORLD

    int                       nblocks   = 128; // global number of blocks
    int                       threads   = 4;   // allow diy 4 threads for multithreading blocks
    int                       in_memory = 8;   // allow diy to keep 8 local blocks in memory
    std::string               prefix    = "./DIY.XXXXXX"; // prefix for temp files for blocks
                                                          // moved out of core

    bool help;

    // get command line arguments
    using namespace opts;
    Options ops;
    ops
        >> Option('b', "blocks",  nblocks,        "number of blocks")
        >> Option('t', "thread",  threads,        "number of threads")
        >> Option('m', "memory",  in_memory,      "maximum blocks to store in memory")
        >> Option(     "prefix",  prefix,         "prefix for external storage")
        >> Option('h', "help",    help,           "show help")
        ;
    if (!ops.parse(argc,argv) || help)
    {
        if (world.rank() == 0)
            std::cout << ops;
        return 1;
    }

    // diy initialization
    diy::FileStorage          storage(prefix); // used for blocks to be moved out of core
    diy::Master               master(world,    // master is the diy top-level object
                                     threads,
                                     in_memory,
                                     &create_block,
                                     &destroy_block,
                                     &storage,
                                     &save_block,
                                     &load_block);

    // choice of contiguous or round robin assigner
    // diy::ContiguousAssigner   assigner(world.size(), nblocks);
    diy::RoundRobinAssigner   assigner(world.size(), nblocks);

    // this example creates a linear chain of blocks
    std::vector<int> gids;                     // global ids of local blocks
    assigner.local_gids(world.rank(), gids);   // get the gids of local blocks
    for (unsigned i = 0; i < gids.size(); ++i) // for the local blocks in this processor
    {
        int gid = gids[i];

        diy::Link*    link = new diy::Link;  // link is this block's neighborhood
        diy::BlockID  neighbor;
        if (gid < nblocks - 1)               // all but the last block in the global domain
        {
            neighbor.gid  = gid + 1;                     // gid of the neighbor block
            neighbor.proc = assigner.rank(neighbor.gid); // process of the neighbor block
            link->add_neighbor(neighbor);                // add the neighbor block to the link
        }
        if (gid > 0)                         // all but the first block in the global domain
        {
            neighbor.gid  = gid - 1;
            neighbor.proc = assigner.rank(neighbor.gid);
            link->add_neighbor(neighbor);
        }

        Block* b = new Block;                // create a new block
        // assign some values for the block
        // in this example, simply based on the block global id
        for (unsigned j = 0; j < 3; ++j)
            b->values.push_back(gid * 3 + j);

        master.add(gid, b, link);            // add the current local block to the master
    }

    // compute, exchange, compute
    master.foreach(&local_sum);          // callback function executed on each local block
    master.exchange();                   // exchange data between blocks in the link
    master.foreach(&average_neighbors);  // callback function executed on each local block

    // save the results in diy format
    diy::io::write_blocks("blocks.out", world, master);

    if (world.rank() == 0)
        master.prof.output(std::cout);
}
