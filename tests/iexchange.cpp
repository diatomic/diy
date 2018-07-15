#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

#include "opts.h"

struct Block
{
    Block(int c = 0): count(c)              {}

    int     count;
    int     expected_particles  = 0;
    int     expected_hops       = 0;
    int     finished_particles  = 0;
    int     finished_hops       = 0;
};

struct Particle
{
    Particle(int id_ = -1, int h = 0):
        id(id_), hops(h)                    {}

    int   id;
    int   hops;
};

void* create_block()                        { return new Block; }
void  destroy_block(void*           b)      { delete static_cast<Block*>(b); }
void  save_block(const void*        b,
                 diy::BinaryBuffer& bb)     { diy::save(bb, *static_cast<const Block*>(b)); }
void  load_block(void*              b,
                 diy::BinaryBuffer& bb)     { diy::load(bb, *static_cast<Block*>(b)); }


// callback for asynchronous iexchange
// return: true = I'm done unless more work arrives; false = I'm not done, please call me again
bool bounce(Block*                              b,
            const diy::Master::IProxyWithLink&  icp)
{
    diy::Link* l = icp.link();
    int my_gid   = icp.gid();

    //fmt::print(stderr, "Block {} with {} count\n", my_gid, b->count);

    // start with every block enqueueing particles to random neighbors
    int id = my_gid * 1000;
    if (b->count > 0)
        fmt::print(stderr, "[{}] enqueue {} particles\n", my_gid, b->count);
    while (b->count > 0)
    {
        int nbr = rand() % l->size();
        Particle p(id++, 1 + rand() % 20);
        fmt::print(stderr, "[{}] -> ({},{}) -> [{}]\n", my_gid, p.id, p.hops, l->target(nbr).gid);
        icp.enqueue(l->target(nbr), p);
        b->count--;

        b->expected_particles++;
        b->expected_hops += p.hops;
    }

    // then dequeue as long as something is incoming and enqueue as long as the hop count is not exceeded
    // bounce will be called by master multiple times until no more messages are in flight anywhere
    for (size_t i = 0; i < l->size(); ++i)
    {
        int nbr_gid = l->target(i).gid;
        if (icp.incoming(nbr_gid))      // FIXME: make this while
        {
            Particle p;
            icp.dequeue(nbr_gid, p);
            fmt::print(stderr, "[{}] <- ({},{}) <- [{}]\n", my_gid, p.id, p.hops, nbr_gid);

            p.hops--;
            b->finished_hops++;
            if (p.hops > 0)
            {
                int nbr = rand() % l->size();
                fmt::print(stderr, "[{}] -> ({},{}) -> [{}]\n", my_gid, p.id, p.hops, l->target(nbr).gid);
                icp.enqueue(l->target(nbr), p);
            } else
            {
                fmt::print(stderr, "[{}] finish particle ({},{})\n", my_gid, p.id, p.hops);
                b->finished_particles++;
            }
        }
    }

    return (b->count == 0);      // this will always be true, but the logic is that we have no work left inside the block
}

int main(int argc, char* argv[])
{
     //diy::create_logger("debug");

    diy::mpi::environment     env(argc, argv);
    diy::mpi::communicator    world;

    // get command line arguments
    int nblocks = world.size();
    using namespace opts;
    Options ops(argc, argv);
    ops
        >> Option('b', "blocks",  nblocks,        "number of blocks")
    ;

    diy::FileStorage          storage("./DIY.XXXXXX");

    diy::Master               master(world,
                                     1,
                                     -1,
                                     &create_block,
                                     &destroy_block,
                                     &storage,
                                     &save_block,
                                     &load_block);

    srand(time(NULL) + world.rank());

    diy::ContiguousAssigner   assigner(world.size(), nblocks);

    // this example creates a linear chain of blocks
    std::vector<int> gids;                     // global ids of local blocks
    assigner.local_gids(world.rank(), gids);   // get the gids of local blocks
    for (size_t i = 0; i < gids.size(); ++i)   // for the local blocks in this processor
    {
        int gid = gids[i];

        diy::Link*   link = new diy::Link;   // link is this block's neighborhood
        diy::BlockID neighbor;               // one neighbor in the neighborhood
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

        master.add(gid, new Block(1 + rand() % 10), link);    // add the current local block to the master
    }

    // dequeue, enqueue, exchange all in one nonblocking routine
    master.iexchange(&bounce);

    master.foreach([](Block* b, const diy::Master::ProxyWithLink& cp)
                   {
                     cp.all_reduce(b->expected_particles, std::plus<int>());
                     cp.all_reduce(b->expected_hops,      std::plus<int>());
                     cp.all_reduce(b->finished_particles, std::plus<int>());
                     cp.all_reduce(b->finished_hops,      std::plus<int>());
                   });
    master.exchange();      // process collectives
    master.foreach([](Block* b, const diy::Master::ProxyWithLink& cp)
                   {
                     int all_expected_particles = cp.get<int>();
                     int all_expected_hops      = cp.get<int>();
                     int all_finished_particles = cp.get<int>();
                     int all_finished_hops      = cp.get<int>();
                     if (all_expected_particles != all_finished_particles)
                     {
                        fmt::print(stderr, "In block {}, all_expected_particles != all_finished_particles: {} != {}\n",
                                           cp.gid(), all_expected_particles, all_finished_particles);
                        std::abort();
                     }
                     if (all_expected_hops != all_finished_hops)
                     {
                        fmt::print(stderr, "In block {}, all_expected_hops != all_finished_hops: {} != {}\n",
                                           cp.gid(), all_expected_hops, all_finished_hops);
                        std::abort();
                     }
                   });

    if (world.rank() == 0)
        fmt::print(stderr, "Total iterations: {}\n", master.block<Block>(master.loaded_block())->count);
}
