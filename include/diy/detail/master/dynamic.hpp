#pragma once

#include "../algorithms/load-balance.hpp"
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

// send work info
inline void dynamic_send_work_info(const diy::Master::ProxyWithLink&      cp,                 // communication proxy for aux_master
                                   AuxBlock*                              ab,                 // current block
                                   int                                    dest_proc)          // destination process
{
    WorkInfo work_info;
    ab->update_my_work_info(work_info);
    diy::BlockID dest_block = {dest_proc, dest_proc};
    cp.enqueue(dest_block, MsgType::work_info);
    cp.enqueue(dest_block, work_info);
}

// send block
inline void dynamic_send_block(const diy::Master::ProxyWithLink&   cp,                 // communication proxy for aux_master
                               diy::Master&                        master,             // real master with multiple blocks per process
                               diy::detail::AuxBlock*              ab,                 // current block
                               float                               quantile)           // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    Work proc_work = ab->proc_work;                                                    // total work of my proc
    FreeBlock heaviest_block;                                                          // my heaviest block

    // my rank's position in the sampled work info, sorted by proc_work
    int my_work_idx = (int)(ab->sample_work_info.size());                              // index where my work would be in the sample_work
    for (auto i = 0; i < ab->sample_work_info.size(); i++)
    {
        if (proc_work < ab->sample_work_info[i].proc_work)
        {
            my_work_idx = i;
            break;
        }
    }
    if (my_work_idx < quantile * ab->sample_work_info.size())
        return;

    // pick the destination process to be the mirror image of my work location in the samples
    // ie, the heavier my process, the lighter the destination process
    int target = (int)(ab->sample_work_info.size()) - my_work_idx;
    auto dst_work_info = ab->sample_work_info[target];

    // send my heaviest block if it passes multiple tests
    int retval;
    if ((retval = ab->grab_heaviest_free_block(heaviest_block)) >= 0     &&                // heaviest block is free to grab
        proc_work - dst_work_info.proc_work > heaviest_block.work        &&                // improves load balance
        ab->any_free_blocks())                                                             // doesn't leave me with no blocks
    {
        int move_gid = heaviest_block.gid;
        int move_lid = master.lid(move_gid);
        int dst_proc = dst_work_info.proc_rank;
        int move_work = heaviest_block.work;

        diy::BlockID dest_block = {dst_proc, dst_proc};

        // enqueue the message type
        cp.enqueue(dest_block, MsgType::block);

        // enqueue the gid of the moving block
        cp.enqueue(dest_block, move_gid);

        // enqueue the work of the moving block
        cp.enqueue(dest_block, move_work);

        // debug
        // fmt::print(stderr, "dynamic_send_block(): gid {} is free, sending to rank {}\n", move_gid, dst_proc);

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
    }
    else if (retval >= 0)        // replace the block if there was one but it wasn't used
        ab->add_free_block(heaviest_block);
}

// receive requests for work info, work info, and migrated blocks
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

                // switch on the type of message (work info request, work info, migrated block)
                if (t == MsgType::work_info_req)
                {
                    dynamic_send_work_info(cp, ab, gid);
                    ab->requesters.push_back(gid);
                }
                else if (t == MsgType::work_info)
                {
                    WorkInfo work_info;
                    cp.dequeue(gid, work_info);
                    ab->update_sample_work_info(work_info);
                }
                else if (t == MsgType::block)
                {
                    // dequeue the gid of the moving block
                    int move_gid;
                    cp.dequeue(gid, move_gid);

                    // dequeue the work of the moving block
                    Work move_work;
                    cp.dequeue(gid, move_work);

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
                    FreeBlock free_block(move_gid, move_work, gid);
                    ab->add_free_block(free_block);
                }
            }
        }
    } while (cp.fill_incoming());
}

// callback function for iexchange to execute dynamic load balancing
bool iexchange_balance(diy::detail::AuxBlock*              ab,                     // auxiliary block
                       const diy::Master::ProxyWithLink&   cp,                     // communication proxy for aux_master
                       Master&                             master,                 // the real master with multiple blocks per process
                       float                               sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                       float                               quantile)               // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    auto nprocs = master.communicator().size();     // global number of procs
    auto my_proc = master.communicator().rank();    // rank of my proc

    // update requesters of my work info if it changed
    if (ab->prev_proc_work != ab->proc_work)
    {
        for (auto i = 0; i < ab->requesters.size(); i++)
            dynamic_send_work_info(cp, ab, ab->requesters[i]);
        ab->prev_proc_work = ab->proc_work;
    }

    // sample other procs for their workload
    int nsamples = 0;
    std::set<int> sample_procs;                         // procs to ask for work info when sampling
    if (ab->any_free_blocks() && ab->nsent_reqs == 0)   // I have work and have not already sent requests
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

        // send the requests for work info
        dynamic_send_req(cp, sample_procs);
        ab->nsent_reqs = nsamples;
    }

    if (ab->nsent_reqs && ab->sample_work_info.size() == ab->nsent_reqs)
        dynamic_send_block(cp, master, ab, quantile);

    // receive requests for work info and blocks
    dynamic_recv(cp, master, ab);

    // I think I'm done when I have no more free blocks
    // Receives will come in automatically via iexchange, even after I think I'm done
    return(!ab->any_free_blocks());
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
    ab->iexchange_done = true;

    // fix links
    diy::fix_links(*master, *dynamic_assigner);
}

}

}
