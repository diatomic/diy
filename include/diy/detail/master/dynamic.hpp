#pragma once

#include "../algorithms/load-balance-sampling.hpp"
#include "diy/assigner.hpp"
#include "diy/resolve.hpp"
#include "diy/thirdparty/fmt/core.h"
#include "diy/thirdparty/fmt/format-inl.h"
#include <cstdio>
#include <iterator>

namespace diy
{

namespace detail
{

// send requests for work info
inline void dynamic_send_req(const diy::Master::ProxyWithLink&  cp,                        // communication proxy
                             std::set<int>&                     procs)                     // processes to query
{
    // send requests for work info to sample_procs
    for (auto proc_iter = procs.begin(); proc_iter != procs.end(); proc_iter++)
    {
        int gid    = *proc_iter;
        int proc   = *proc_iter;
        diy::BlockID dest_block = {gid, proc};
        cp.enqueue(dest_block, MsgType::work_info_req);
    }
}

// send block
inline void dynamic_send_block(const diy::Master::ProxyWithLink&   cp,                 // communication proxy for aux_master
                               diy::Master&                        master,             // real master with multiple blocks per process
                               diy::detail::AuxBlock*              ab,                 // current block
                               const WorkInfo&                     my_work_info,       // my work info
                               float                               quantile)           // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    MoveInfo move_info = {-1, -1, -1};

    // my rank's position in the sampled work info, sorted by proc_work
    int my_work_idx = (int)(ab->sample_work_info.size());                   // index where my work would be in the sample_work
    for (auto i = 0; i < ab->sample_work_info.size(); i++)
    {
        if (my_work_info.proc_work < ab->sample_work_info[i].proc_work)
        {
            my_work_idx = i;
            break;
        }
    }

    // send my heaviest block if it passes the quantile cutoff
    if (my_work_idx >= quantile * ab->sample_work_info.size())
    {
        // pick the destination process to be the mirror image of my work location in the samples
        // ie, the heavier my process, the lighter the destination process
        int target = (int)(ab->sample_work_info.size()) - my_work_idx;

        auto src_work_info = my_work_info;
        auto dst_work_info = ab->sample_work_info[target];

        // sanity check that the move makes sense
        if (src_work_info.proc_work - dst_work_info.proc_work > src_work_info.top_work &&   // improve load balance
                src_work_info.proc_rank != dst_work_info.proc_rank &&                       // not self
                src_work_info.nlids > 1)                                                    // don't leave a proc with no blocks
        {
            move_info.move_gid = my_work_info.top_gid;
            move_info.src_proc = my_work_info.proc_rank;
            move_info.dst_proc = ab->sample_work_info[target].proc_rank;

            int move_lid = master.lid(move_info.move_gid);

            auto free_blocks_access = ab->free_blocks.access();
            if ((*free_blocks_access)[move_lid] >= 0)    // block is free to move
            {
                // debug
                fmt::print(stderr, "dynamic_send_block(): move_lid {} is free, locking and moving the block\n", move_lid);

                (*free_blocks_access)[move_lid] = -1;     // lock the block
                free_blocks_access.unlock();

                // destination in aux_master, where gid = proc
                diy::BlockID dest_block = {move_info.dst_proc, move_info.dst_proc};

                // enqueue the message type
                cp.enqueue(dest_block, MsgType::block);

                // enqueue the gid of the moving block
                cp.enqueue(dest_block, move_info.move_gid);

                fmt::print(stderr, "sending gid {} to rank {}\n", move_info.move_gid, move_info.dst_proc);

                // enqueue the block
                void* send_b = master.block(move_lid);
                diy::MemoryBuffer bb;
                master.saver()(send_b, bb);
                cp.enqueue(dest_block, bb.buffer);

                // enqueue the link for the block
                diy::Link* send_link = master.link(move_lid);
                diy::LinkFactory::save(bb, send_link);
                cp.enqueue(dest_block, bb.buffer);

                // remove the block from the master
                master.destroyer()(master.release(move_lid));

                // update free_blocks in case another block is moved in the same iteration
                // not done currently, but multi-block moving is a possible future implementation
                ab->reset_free_blocks(master.size());
            }
            else // block is not free
            {
                // debug
                fmt::print(stderr, "dynamic_send_block(): lid {} is locked, skipping moving it.\n", move_lid);
            }
            // NB, free_blocks_access will unlock automatically when it goes out of scope here
        }
    }
}

// receive requests for work info, send work info, and receive work info
inline void dynamic_recv(const diy::Master::ProxyWithLink&  cp,                     // communication proxy
                         diy::Master&                       master,                 // real master with multiple blocks per process
                         WorkInfo&                          my_work_info,           // my work info
                         std::vector<WorkInfo>&             sample_work_info,       // work info sampled from others
                         int&                               nwork_info_recvd)       // number of sample_work_info items so far, will be updated by this function
{
    std::vector<int> incoming_gids;

    do
    {
        cp.incoming(incoming_gids);

        // for anything incoming, dequeue data received in the last exchange
        for (int i = 0; i < incoming_gids.size(); i++)
        {
            int gid = incoming_gids[i];
            while (cp.incoming(gid))
            {
                MsgType t;
                cp.dequeue(gid, t);
                if (t == MsgType::work_info_req)
                {
                    // fmt::print(stderr, "t {}: recvd req for work info from gid {}\n", t, gid);
                    diy::BlockID dest_block = {gid, gid};
                    // fmt::print(stderr, "sending work info to gid {}\n", gid);
                    cp.enqueue(dest_block, MsgType::work_info);
                    cp.enqueue(dest_block, my_work_info);
                }
                else if (t == MsgType::work_info)
                {
                    // fmt::print(stderr, "t {}: recvd work info from gid {} saving sample_work_info[{}] sample_work_info size {}\n",
                    //            t, gid, nwork_info_recvd, sample_work_info.size());
                    cp.dequeue(gid, sample_work_info[nwork_info_recvd++]);
                    // fmt::print(stderr, "work_info: proc_rank {} top_gid {} top_work {} proc_work {} nlids {}\n",
                    //            sample_work_info[nwork_info_recvd + nrecvd].proc_rank, sample_work_info[nwork_info_recvd + nrecvd].top_gid,
                    //            sample_work_info[nwork_info_recvd + nrecvd].top_work, sample_work_info[nwork_info_recvd + nrecvd].proc_work,
                    //            sample_work_info[nwork_info_recvd + nrecvd].nlids);
                }
                else if (t == MsgType::block)
                {
                    // dequeue the gid of the moving block
                    int move_gid;
                    cp.dequeue(gid, move_gid);

                    fmt::print(stderr, "receiving gid {}\n", move_gid);

                    // dequeue the block
                    void* recv_b = master.creator()();
                    diy::MemoryBuffer bb;
                    cp.dequeue(gid, bb.buffer);
                    master.loader()(recv_b, bb);

                    // dequeue the link
                    diy::Link* recv_link;
                    cp.dequeue(gid, bb.buffer);
                    recv_link = diy::LinkFactory::load(bb);

                    // add block to the master
                    master.add(move_gid, recv_b, recv_link);
                }
            }
        }
    } while (cp.fill_incoming());
}

// callback function for iexchange to execute dynamic load balancing
bool iexchange_balance(diy::detail::AuxBlock*              ab,                     // current block
                       const diy::Master::ProxyWithLink&   cp,                     // communication proxy for aux_master
                       Master&                             master,                 // the real master with multiple blocks per process
                       float                               sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                       float                               quantile,               // quantile cutoff above which to move blocks (0.0 - 1.0)
                       WorkInfo&                           my_work_info)           // my work info, empty, filled later
{
    auto nprocs = master.communicator().size();     // global number of procs
    auto my_proc = master.communicator().rank();    // rank of my proc
    bool done;

    // sample other procs for their workload
    int nsamples = 0;
    std::set<int> sample_procs;                     // procs to ask for work info when sampling
    if (my_work_info.proc_work && !ab->sent_reqs)   // only if I have work and have not already sent requests
    {
        // pick a random sample of processes, w/o duplicates, and excluding myself
        nsamples = static_cast<int>(sample_frac * (nprocs - 1));
        for (auto i = 0; i < nsamples; i++)
        {
            int rand_proc;
            do
            {
                std::uniform_int_distribution<> distrib(0, nprocs - 1); // inclusive
                rand_proc = distrib(master.mt_gen);
            } while (sample_procs.find(rand_proc) != sample_procs.end() || rand_proc == my_proc);
            sample_procs.insert(rand_proc);
        }
        ab->sample_work_info.resize(nsamples);

        // send the requests for work info
        dynamic_send_req(cp, sample_procs);
        ab->sent_reqs = true;
    }

    if (!ab->sent_block && ab->sample_work_info.size())
    {
        dynamic_send_block(cp, master, ab, my_work_info, quantile);
        ab->sent_block = true;
    }

    // receive requests for work info and blocks
    dynamic_recv(cp, master, my_work_info, ab->sample_work_info, ab->nwork_info_recvd);

    // I think I'm done after I sent requests for work info and any blocks
    // Receives will come in automatically via iexchange, even after I think I'm done
    if (ab->sent_reqs && ab->sent_block)
        done = true;
    else
        done = false;

    return done;
}

template<class G>
void dynamic_balance(Master*                         master,                 // the real master with multiple blocks per process
                     Master*                         aux_master,             // auxiliary master with 1 block per process for communicating between procs
                     DynamicAssigner*                dynamic_assigner,       // dynamic assigner
                     float                           sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                     float                           quantile,               // quantile cutoff above which to move blocks (0.0 - 1.0)
                     const G&                        get_work)               // callback function to get amount of local work
{
    using Block = typename detail::block_traits<G>::type;

    // compile my work info
    diy::detail::WorkInfo my_work_info = { master->communicator().rank(), -1, 0, 0, static_cast<int>(master->size()) };
    my_work_info = { master->communicator().rank(), -1, 0, 0, static_cast<int>(master->size()) };
    for (auto i = 0; i < master->size(); i++)
    {
        Block* b = static_cast<Block*>(master->block(i));
        Work w = get_work(b, master->gid(i));
        my_work_info.proc_work += w;
        if (my_work_info.top_gid == -1 || my_work_info.top_work < w)
        {
            my_work_info.top_gid    = master->gid(i);
            my_work_info.top_work   = w;
        }
    }

    // do the actual load balancing using iexchange
    aux_master->iexchange([&](diy::detail::AuxBlock* b, const diy::Master::ProxyWithLink& cp) -> bool
      { return iexchange_balance(b, cp, *master, sample_frac, quantile, my_work_info); } );

    // fix links
    diy::fix_links(*master, *dynamic_assigner);
}

}

}
