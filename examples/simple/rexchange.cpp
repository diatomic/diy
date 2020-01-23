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

// enqueue data inside the link
void enq(Block* b, const diy::Master::ProxyWithLink& cp)
{
    diy::Link* l = cp.link();

    for (int i = 0; i < l->size(); ++i)
        cp.enqueue(l->target(i), b->count);
    b->count++;
}

// dequeue data inside the link
void deq(Block* b, const diy::Master::ProxyWithLink& cp)
{
    diy::Link* l = cp.link();

    for (int i = 0; i < l->size(); ++i)
    {
        int gid = l->target(i).gid;
        if (cp.incoming(gid).size())
        {
            cp.dequeue(cp.link()->target(i).gid, b->count);
            fmt::print(stderr, "Dequeue: gid {} received value {} from link gid {}\n",
                    cp.gid(), b->count, l->target(i).gid);
            b->count++;
        }
    }
}

// enqueue remote data
// there is still a link, but you can send to any BlockID = (gid, proc)
void remote_enq(
        Block*,
        const diy::Master::ProxyWithLink&   cp,
        const diy::Assigner&                assigner)
{
    // as a test, send my gid to block 2 gids away (in a ring), which is outside the link
    // (the link has only adjacent block gids)
    int my_gid              = cp.gid();
    int dest_gid            = (my_gid + 2) % assigner.nblocks();
    int dest_proc           = assigner.rank(dest_gid);
    diy::BlockID dest_block = {dest_gid, dest_proc};
    cp.enqueue(dest_block, my_gid);
}

// dequeue remote data
// there is still a link, but exchange(remote = true) exchanged messages from any block
void remote_deq(Block*, const diy::Master::ProxyWithLink& cp)
{
    std::vector<int> incoming_gids;
    cp.incoming(incoming_gids);
    for (size_t i = 0; i < incoming_gids.size(); i++)
        if (cp.incoming(incoming_gids[i]).size())
        {
            int recvd_data;
            cp.dequeue(incoming_gids[i], recvd_data);
            fmt::print(stderr, "Remote dequeue: gid {} received value {} from gid {}\n",
                    cp.gid(), recvd_data, incoming_gids[i]);
        }
}

int main(int argc, char* argv[])
{
//     diy::create_logger("trace");

    diy::mpi::environment     env(argc, argv);
    diy::mpi::communicator    world;
    diy::FileStorage          storage("./DIY.XXXXXX");
    int                       nblocks    = 24 * world.size();
//     int                       threads    = 1;
    int                       threads    = 2;
    int                       mem_blocks = -1;

    diy::Master               master(world,
                                     threads,
                                     mem_blocks,
                                     &create_block,
                                     &destroy_block,
                                     &storage,
                                     &save_block,
                                     &load_block);

    srand(static_cast<unsigned int>(time(NULL)));

    diy::RoundRobinAssigner   assigner(world.size(), nblocks);

    // this example creates a linear chain of blocks with links between adjacent blocks
    std::vector<int> gids;                     // global ids of local blocks
    assigner.local_gids(world.rank(), gids);   // get the gids of local blocks
    for (size_t i = 0; i < gids.size(); ++i)   // for the local blocks in this processor
    {
        int gid = gids[i];

        diy::Link*   link = new diy::Link;   // link is this block's neighborhood
        diy::BlockID neighbor;               // one neighbor in the neighborhood
        if (gid < nblocks - 1)               // RH neighbors for all but the last block in the global domain
        {
            neighbor.gid  = gid + 1;                     // gid of the neighbor block
            neighbor.proc = assigner.rank(neighbor.gid); // process of the neighbor block
            link->add_neighbor(neighbor);                // add the neighbor block to the link
        }
        if (gid > 0)                         // LH neighbors for all but the first block in the global domain
        {
            neighbor.gid  = gid - 1;
            neighbor.proc = assigner.rank(neighbor.gid);
            link->add_neighbor(neighbor);
        }

        master.add(gid, new Block, link);    // add the current local block to the master
    }

    int num_iters = 2;
    for (int i = 0; i < num_iters; i++)
    {
        // exchange some data inside the links
        master.foreach(&enq);
        master.exchange();
        master.foreach(&deq);

        // (remote) exchange some data outside the links
        master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { remote_enq(b, cp, assigner); });
        bool remote = true;
        master.exchange(remote);
        master.foreach(&remote_deq);
    }

    if (world.rank() == 0)
        fmt::print(stderr,
                   "Total iterations: {}\n", master.block<Block>(master.loaded_block())->count);

}
