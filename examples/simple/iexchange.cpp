#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

struct Block
{
    Block(): count(0)                   {}

    int   count;
};

void* create_block()                      { return new Block; }
void  destroy_block(void* b)              { delete static_cast<Block*>(b); }
void  save_block(const void* b,
                 diy::BinaryBuffer& bb)   { diy::save(bb, *static_cast<const Block*>(b)); }
void  load_block(void* b,
                 diy::BinaryBuffer& bb)   { diy::load(bb, *static_cast<Block*>(b)); }

// debug: test enqueue with original synchronous exchange
void enq(Block* b, const diy::Master::ProxyWithLink& cp)
{
    diy::Link* l = cp.link();

    // start with every block enqueueing its count the first time
    if (!b->count)
    {
        for (size_t i = 0; i < l->size(); ++i)
            cp.enqueue(l->target(i), b->count);
        b->count++;
    }
}

// debug: test dequeue with original synchronous exchange
void deq(Block* b, const diy::Master::ProxyWithLink& cp)
{
    diy::Link* l = cp.link();
    int my_gid   = cp.gid();

    for (size_t i = 0; i < l->size(); ++i)
    {
        fmt::print(stderr, "entering deq\n");
        int nbr_gid = l->target(i).gid;
        if (cp.incoming(nbr_gid).size())
        {
            int recvd_val;
            cp.dequeue(nbr_gid, recvd_val);
            fmt::print(stderr, "[{}] deq [{}] <- {} <- [{}]\n", my_gid, my_gid, recvd_val, nbr_gid);
        }
    }
}

// callback for asynchronous iexchange
bool foo(
        Block*                              b,
        const diy::Master::IProxyWithLink&  icp)
{
    diy::Link* l = icp.link();
    int my_gid   = icp.gid();

    // start with every block enqueueing its count the first time
    if (!b->count)
    {
        for (size_t i = 0; i < l->size(); ++i)
        {
            icp.enqueue(l->target(i), b->count);
            fmt::print(stderr, "enq [{}] -> {} -> [{}]\n", my_gid, b->count, l->target(i).gid);
        }
        b->count++;
    }

    // then dequeue as long as something is incoming and enqueue as long as count is below some threshold
    // foo will be called by master multiple times until no more messages are in flight anywhere
    int max_count = 2;
    bool inc_count = false;
    for (size_t i = 0; i < l->size(); ++i)
    {
        int nbr_gid = l->target(i).gid;
        if (icp.incoming(nbr_gid).size())
        {
            int recvd_val;
            bool retval;
            do
            {
                retval = icp.dequeue(nbr_gid, recvd_val);
                fmt::print(stderr, "deq [{}] <- {} <- [{}], size {}\n", my_gid, recvd_val, nbr_gid, icp.incoming(nbr_gid).size());
            } while (icp.incoming(nbr_gid).size() && retval);

            // TODO: any way to hide this in master or proxy?
            icp.incoming(nbr_gid).clear();

            if (b->count <= max_count)
            {
                icp.enqueue(l->target(i), b->count);
                fmt::print(stderr, "enq [{}] -> {} -> [{}]\n", my_gid, b->count, nbr_gid);
                inc_count = true;
            }
        }
    }

    if (inc_count)
        b->count++;

    // pretend to be done when b->count exceeds some threshold
//     bool done = b->count > max_count ? true : false;
    bool done = true;
//     fmt::print(stderr, "returning: gid={} count={} done={}\n", my_gid, b->count, done);
    return (done);
}

int main(int argc, char* argv[])
{
//     diy::create_logger("trace");

    diy::mpi::environment     env(argc, argv);
    diy::mpi::communicator    world;

    int                       nblocks = 2 * world.size();

    diy::FileStorage          storage("./DIY.XXXXXX");

    diy::Master               master(world,
                                     1,
                                     -1,
                                     &create_block,
                                     &destroy_block,
                                     &storage,
                                     &save_block,
                                     &load_block);

    srand(time(NULL));

    diy::RoundRobinAssigner   assigner(world.size(), nblocks);

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

        master.add(gid, new Block, link);    // add the current local block to the master
    }

#if 0
    // test synchronous version
    master.foreach(&enq);
    master.exchange();
    master.foreach(&deq);
#else
    // dequeue, enqueue, exchange all in one nonblocking routine
    master.iexchange(&foo);
#endif

    if (world.rank() == 0)
        fmt::print(stderr,
                   "Total iterations: {}\n", master.block<Block>(master.loaded_block())->count);

}
