#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/merge.hpp>

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
            const diy::Master::ProxyWithLink&  cp)
{
    diy::Link* l = cp.link();
    int my_gid   = cp.gid();

    // start with every block enqueueing particles to random neighbors
    int id = my_gid * 1000;
    if (b->count > 0)
        fmt::print(stderr, "[{}] enqueue {} particles\n", my_gid, b->count);
    while (b->count > 0)
    {
        int nbr = rand() % l->size();
        Particle p(id++, 1 + rand() % 20);
        fmt::print(stderr, "[{}] -> ({},{}) -> [{}]\n", my_gid, p.id, p.hops, l->target(nbr).gid);
        cp.enqueue(l->target(nbr), p);
        b->count--;

        b->expected_particles++;
        b->expected_hops += p.hops;
    }

    // then dequeue as long as something is incoming and enqueue as long as the hop count is not exceeded
    // bounce will be called by master multiple times until no more messages are in flight anywhere
    for (int i = 0; i < l->size(); ++i)
    {
        int nbr_gid = l->target(i).gid;
        while (cp.incoming(nbr_gid))
        {
            Particle p;
            cp.dequeue(nbr_gid, p);
            fmt::print(stderr, "[{}] <- ({},{}) <- [{}]\n", my_gid, p.id, p.hops, nbr_gid);

            p.hops--;
            b->finished_hops++;
            if (p.hops > 0)
            {
                int nbr = rand() % l->size();
                fmt::print(stderr, "[{}] -> ({},{}) -> [{}]\n", my_gid, p.id, p.hops, l->target(nbr).gid);
                cp.enqueue(l->target(nbr), p);
            } else
            {
                fmt::print(stderr, "[{}] finish particle ({},{})\n", my_gid, p.id, p.hops);
                b->finished_particles++;
            }
        }
    }

    return true;
}

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv);
    diy::mpi::communicator    world;

    // get command line arguments
    int         nblocks     = world.size();
    std::string log_level   = "info";
    using namespace opts;
    Options ops;
    ops
        >> Option('b', "blocks",  nblocks,        "number of blocks")
        >> Option('l', "log",     log_level,      "log level")
    ;
    if (!ops.parse(argc,argv))
    {
        if (world.rank() == 0)
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << ops;
        }
        return 1;
    }

    diy::create_logger(log_level);

    diy::FileStorage          storage("./DIY.XXXXXX");

    diy::Master               master(world,
                                     2,
                                     -1,
                                     &create_block,
                                     &destroy_block,
                                     &storage,
                                     &save_block,
                                     &load_block);

    srand(static_cast<unsigned int>(time(NULL) + world.rank()));

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

    // alternatively, can hold small messages to reduce communication frequency
    // TODO: add this option to the test once we decide we will keep it
//     master.iexchange(&bounce, 16, 100);

    fmt::print("[{}]: Checking correctness\n", world.rank());

    // check correctness: number of finished particles and hops matches the expected numbers
    diy::RegularDecomposer<diy::DiscreteBounds> decomposer(1, diy::interval(0, nblocks-1), nblocks);
    diy::RegularMergePartners  merge_partners(decomposer, 2, false);
    diy::reduce(master, assigner, merge_partners,
                [](Block*                           b,
                   const diy::ReduceProxy&          rp,
                   const diy::RegularMergePartners&)
                {
                    fmt::print("[{}]: round = {}, entered\n", rp.gid(), rp.round());

                    // step 1: dequeue
                    for (int i = 0; i < rp.in_link().size(); ++i)
                    {
                        int nbr_gid = rp.in_link().target(i).gid;
                        if (nbr_gid == rp.gid())
                            continue;

                        int ep; rp.dequeue(nbr_gid, ep);    b->expected_particles   += ep;
                        int eh; rp.dequeue(nbr_gid, eh);    b->expected_hops        += eh;
                        int fp; rp.dequeue(nbr_gid, fp);    b->finished_particles   += fp;
                        int fh; rp.dequeue(nbr_gid, fh);    b->finished_hops        += fh;
                    }

                    // step 2: enqueue
                    for (int i = 0; i < rp.out_link().size(); ++i)    // redundant since size should equal to 1
                    {
                        // only send to root of group, but not self
                        if (rp.out_link().target(i).gid != rp.gid())
                        {
                            rp.enqueue(rp.out_link().target(i), b->expected_particles);
                            rp.enqueue(rp.out_link().target(i), b->expected_hops);
                            rp.enqueue(rp.out_link().target(i), b->finished_particles);
                            rp.enqueue(rp.out_link().target(i), b->finished_hops);
                        }
                    }

                    if (rp.out_link().size() == 0 && rp.gid() == 0)     // tip of the reduction, check correctness
                    {
                        fmt::print("[{}]: checking correctness, {} vs {}, {} vs {}\n",
                                   rp.gid(),
                                   b->expected_particles, b->finished_particles,
                                   b->expected_hops, b->finished_hops);

                        if (b->expected_particles != b->finished_particles)
                        {
                           fmt::print(stderr, "In block {}, all_expected_particles != all_finished_particles: {} != {}\n",
                                              rp.gid(), b->expected_particles, b->finished_particles);
                           std::abort();
                        }
                        if (b->expected_hops != b->finished_hops)
                        {
                           fmt::print(stderr, "In block {}, all_expected_hops != all_finished_hops: {} != {}\n",
                                              rp.gid(), b->expected_hops, b->finished_hops);
                           std::abort();
                        }
                    }
                    fmt::print("[{}]: round = {}, leaving\n", rp.gid(), rp.round());
                });
}
