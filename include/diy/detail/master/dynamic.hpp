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
                               float                               quantile)           // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    // my rank's position in the sampled work info, sorted by proc_work
    int my_work_idx = (int)(ab->sample_work_info.size());                   // index where my work would be in the sample_work
    for (auto i = 0; i < ab->sample_work_info.size(); i++)
    {
        if (ab->my_work_info.proc_work < ab->sample_work_info[i].proc_work)
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

        auto src_work_info = ab->my_work_info;
        auto dst_work_info = ab->sample_work_info[target];

        // sanity check that the move makes sense
        if (src_work_info.proc_work - dst_work_info.proc_work > src_work_info.top_work &&   // improve load balance
                src_work_info.proc_rank != dst_work_info.proc_rank &&                       // not self
                src_work_info.nlids > 1)                                                    // don't leave a proc with no blocks
        {
            int move_gid = ab->my_work_info.top_gid;
            int move_lid = master.lid(move_gid);
            int dst_proc = ab->sample_work_info[target].proc_rank;
            int move_work = ab->my_work_info.top_work;

            auto free_blocks_access = ab->free_blocks.access();    // locked upon creation, unlocks automatically when leaving scope
            if ((*free_blocks_access)[move_lid].first >= 0)    // block is free to move
            {
                (*free_blocks_access)[move_lid].first = -1;     // lock the block
                free_blocks_access.unlock();

                // destination in aux_master, where gid = proc
                diy::BlockID dest_block = {dst_proc, dst_proc};

                // enqueue the message type
                cp.enqueue(dest_block, MsgType::block);

                // enqueue the gid of the moving block
                cp.enqueue(dest_block, move_gid);

                // enqueue the work of the moving block
                cp.enqueue(dest_block, move_work);

                // debug
                fmt::print(stderr, "dynamic_send_block(): gid {} is free, sending to rank {}\n", move_gid, dst_proc);

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

                // remove the block from the free blocks list
                ab->remove_free_block(move_lid);
            }
        }
    }
}

// receive requests for work info, send work info, and receive work info
inline void dynamic_recv(const diy::Master::ProxyWithLink&  cp,                     // communication proxy
                         diy::Master&                       master,                 // real master with multiple blocks per process
                         diy::detail::AuxBlock*             ab)                     // current auxiliary block
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
                    diy::BlockID dest_block = {gid, gid};
                    cp.enqueue(dest_block, MsgType::work_info);
                    cp.enqueue(dest_block, ab->my_work_info);
                }
                else if (t == MsgType::work_info)
                    cp.dequeue(gid, ab->sample_work_info[ab->nwork_info_recvd++]);
                else if (t == MsgType::block)
                {
                    // dequeue the gid of the moving block
                    int move_gid;
                    cp.dequeue(gid, move_gid);

                    // dequeue the work of the moving block
                    Work move_work;
                    cp.dequeue(gid, move_work);

                    fmt::print(stderr, "dynamic_recv(): receiving gid {}\n", move_gid);

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

                    // update free blocks
                    ab->add_free_block(move_work);
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
                       float                               quantile)               // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    auto nprocs = master.communicator().size();     // global number of procs
    auto my_proc = master.communicator().rank();    // rank of my proc
    bool done;

    // sample other procs for their workload
    int nsamples = 0;
    std::set<int> sample_procs;                     // procs to ask for work info when sampling
    if (ab->my_work_info.proc_work && !ab->sent_reqs)   // only if I have work and have not already sent requests
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
        dynamic_send_block(cp, master, ab, quantile);
        ab->sent_block = true;
    }

    // receive requests for work info and blocks
    dynamic_recv(cp, master, ab);

    // I think I'm done when I have no more free blocks
    // Receives will come in automatically via iexchange, even after I think I'm done
    if (ab->next_free_block(master.size()) == -1)
        done = true;
    else
        done = false;

    return done;
}

void dynamic_balance(Master*                         master,                 // the real master with multiple blocks per process
                     Master*                         aux_master,             // auxiliary master with 1 block per process for communicating between procs
                     DynamicAssigner*                dynamic_assigner,       // dynamic assigner
                     float                           sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                     float                           quantile)               // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    // do the actual load balancing using iexchange
    aux_master->iexchange([&](diy::detail::AuxBlock* b, const diy::Master::ProxyWithLink& cp) -> bool
      { return iexchange_balance(b, cp, *master, sample_frac, quantile); } );
    diy::detail::AuxBlock* ab = static_cast<diy::detail::AuxBlock*>(aux_master->block(0));
    ab->iexchange_done.store(true);

    // fix links
    diy::fix_links(*master, *dynamic_assigner);
}

}

}
