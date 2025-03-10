#pragma once

#include "../algorithms/load-balance-sampling.hpp"
#include "diy/assigner.hpp"
#include "diy/resolve.hpp"
#include "diy/thirdparty/fmt/format-inl.h"
#include <cstdio>

namespace diy
{

namespace detail
{

// send requests for work info
inline void dynamic_send_req(const diy::Master::ProxyWithLink&  cp,                        // communication proxy
                             std::set<int>&                     procs)                     // processes to query
{
    // send requests for work info to sample_procs
    int v = 1;                                          // any message will do
    for (auto proc_iter = procs.begin(); proc_iter != procs.end(); proc_iter++)
    {
        int gid    = *proc_iter;
        int proc   = *proc_iter;
        diy::BlockID dest_block = {gid, proc};
        cp.enqueue(dest_block, v);
    }
}

// receive requests for work info
inline void dynamic_recv_req(const diy::Master::ProxyWithLink& cp,                     // communication proxy
                             std::vector<int>&                 req_procs)              // processes requesting work info
{
    // TODO: added do-while cp.fill_incoming() and while cp.incoming(gid) loops as in simple/iexchange example, are these correct?
    std::vector<int> incoming_gids;
    do
    {
        cp.incoming(incoming_gids);

        // debug
        fmt::print(stderr, "incoming_gids [{}] size {}\n", fmt::join(incoming_gids, ","), incoming_gids.size());

        // for anything incoming, dequeue data received in the last exchange
        for (int i = 0; i < incoming_gids.size(); i++)
        {
            int gid = incoming_gids[i];
            while (cp.incoming(gid))
            {
                int v;
                cp.dequeue(gid, v);
                req_procs.push_back(gid);                   // aux_master has 1 gid per proc, so gid = proc
            }
        }
    } while (cp.fill_incoming());
}

// get work information from a random sample of processes
// returns done = true: I think I'm done, no work left to do and no requests for work info from anyone else
// else done = false
template<class Block>
inline bool dynamic_exchange_sample_work_info(
                 diy::Master&                       master,                 // the real master with multiple blocks per process
                 float                              sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                 const Master::WCallback<Block>&    get_work,               // callback function to get amount of local work
                 WorkInfo&                          my_work_info,           // my work info, empty, filled by this function
                 std::vector<WorkInfo>&             sample_work_info,       // (output) vector of sorted sample work info, sorted by increasing total work per process
                 const diy::Master::ProxyWithLink&  cp)
{
    auto nprocs = master.communicator().size();     // global number of procs
    auto my_proc = master.communicator().rank();    // rank of my proc
    bool done;

    // compile my work info
    my_work_info = { master.communicator().rank(), -1, 0, 0, static_cast<int>(master.size()) };
    for (auto i = 0; i < master.size(); i++)
    {
        Block* b = static_cast<Block*>(master.block(i));
        Work w = get_work(b, master.gid(i));
        my_work_info.proc_work += w;
        if (my_work_info.top_gid == -1 || my_work_info.top_work < w)
        {
            my_work_info.top_gid    = master.gid(i);
            my_work_info.top_work   = w;
        }
    }

    // only sample other procs if I have some work to potentially offload
    std::set<int> sample_procs;
    int nsamples = 0;
    if (my_work_info.proc_work)
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
    }

    fmt::print(stderr, "1: sample_frac {} nsamples {} sample_procs [{}]\n", sample_frac, nsamples, fmt::join(sample_procs, ","));

    // exchange requests for work info
    std::vector<int> req_procs;     // requests for work info received from these processes
    dynamic_send_req(cp, sample_procs);
    dynamic_recv_req(cp, req_procs);

    fmt::print(stderr, "2: req_procs [{}]\n", fmt::join(req_procs, ","));

    // send work info
    int work_info_tag = 0;
    std::vector<diy::mpi::request> reqs(req_procs.size());
    for (auto i = 0; i < req_procs.size(); i++)
        reqs[i] = mpi::detail::isend(MPI_Comm(master.communicator()), req_procs[i], work_info_tag, &my_work_info, sizeof(WorkInfo), MPI_BYTE);

    // receive work info
    sample_work_info.resize(nsamples);
    for (auto i = 0; i < nsamples; i++)
        mpi::detail::recv(MPI_Comm(master.communicator()), diy::mpi::any_source, work_info_tag, &sample_work_info[i], sizeof(WorkInfo), MPI_BYTE);

    // ensure all the send requests cleared
    for (auto i = 0; i < req_procs.size(); i++)
        reqs[i].wait();

    // sort sample_work_info by proc_work
    std::sort(sample_work_info.begin(), sample_work_info.end(),
            [&](WorkInfo& a, WorkInfo& b) { return a.proc_work < b.proc_work; });

    // I think I'm done when I have no more local work to do and no requests for work info from others
    if (my_work_info.proc_work == 0 && req_procs.size() == 0)
        done = true;
    else
        done = false;
    // debug
    fmt::print(stderr, "my_work_info.proc_work {} sample_work_info.size() {} req_procs.size() {} done {}\n",
               my_work_info.proc_work, sample_work_info.size(), req_procs.size(), done);
    return done;
}

// send block
inline void dynamic_send_block(const diy::Master::ProxyWithLink&   cp,                 // communication proxy for aux_master
                               diy::Master&                        master,             // real master with multiple blocks per process
                               const std::vector<WorkInfo>&        sample_work_info,   // sampled work info
                               const WorkInfo&                     my_work_info,       // my work info
                               float                               quantile)           // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    MoveInfo move_info = {-1, -1, -1};

    // my rank's position in the sampled work info, sorted by proc_work
    int my_work_idx = (int)(sample_work_info.size());                   // index where my work would be in the sample_work
    for (auto i = 0; i < sample_work_info.size(); i++)
    {
        if (my_work_info.proc_work < sample_work_info[i].proc_work)
        {
            my_work_idx = i;
            break;
        }
    }

    // send my heaviest block if it passes the quantile cutoff
    if (my_work_idx >= quantile * sample_work_info.size())
    {
        // pick the destination process to be the mirror image of my work location in the samples
        // ie, the heavier my process, the lighter the destination process
        int target = (int)(sample_work_info.size()) - my_work_idx;

        auto src_work_info = my_work_info;
        auto dst_work_info = sample_work_info[target];

        // sanity check that the move makes sense
        if (src_work_info.proc_work - dst_work_info.proc_work > src_work_info.top_work &&   // improve load balance
                src_work_info.proc_rank != dst_work_info.proc_rank &&                       // not self
                src_work_info.nlids > 1)                                                    // don't leave a proc with no blocks
        {

            move_info.move_gid = my_work_info.top_gid;
            move_info.src_proc = my_work_info.proc_rank;
            move_info.dst_proc = sample_work_info[target].proc_rank;

            // destination in aux_master, where gid = proc
            diy::BlockID dest_block = {move_info.dst_proc, move_info.dst_proc};

            // enqueue the gid of the moving block
            cp.enqueue(dest_block, move_info.move_gid);

            // enqueue the block
            void* send_b = master.block(master.lid(move_info.move_gid));
            diy::MemoryBuffer bb;
            master.saver()(send_b, bb);
            cp.enqueue(dest_block, bb.buffer);

            // enqueue the link for the block
            diy::Link* send_link = master.link(master.lid(move_info.move_gid));
            diy::LinkFactory::save(bb, send_link);
            cp.enqueue(dest_block, bb.buffer);

            // remove the block from the master
            int move_lid = master.lid(move_info.move_gid);
            master.destroyer()(master.release(move_lid));
        }
    }
}

// receive block
inline void dynamic_recv_block(const diy::Master::ProxyWithLink&   cp,         // communication proxy for aux_master
                               diy::Master&                        master)     // real master with multiple blocks per process
{
    std::vector<int> incoming_gids;
    // TODO: added do-while cp.fill_incoming() and while cp.incoming(gid) loops as in simple/iexchange example, are these correct?
    do
    {
        cp.incoming(incoming_gids);
        // for anything incoming, dequeue data received in the last exchange
        for (int i = 0; i < incoming_gids.size(); i++)
        {
            int gid = incoming_gids[i];
            while (cp.incoming(gid))
            {
                // dequeue the gid of the moving block
                int move_gid;
                cp.dequeue(gid, move_gid);

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
    } while (cp.fill_incoming());
}

// callback function for iexchange to execute dynamic load balancing
template<class Block>
bool iexchange_balance(diy::detail::AuxBlock*,
            const diy::Master::ProxyWithLink&   cp,                     // communication proxy for aux_master
            Master*                             master,                 // the real master with multiple blocks per process
            DynamicAssigner*                    dynamic_assigner,       // dynamic assigner
            float                               sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
            float                               quantile,               // quantile cutoff above which to move blocks (0.0 - 1.0)
            const Master::WCallback<Block>&     get_work,               // callback function to get amount of local work
            WorkInfo&                           my_work_info)           // my work info, empty, filled later
{
    // exchange info about load balance
    std::vector<diy::detail::WorkInfo>   sample_work_info;           // work info collecting from sampling other mpi processes
    bool done = diy::detail::dynamic_exchange_sample_work_info(*master, sample_frac, get_work, my_work_info, sample_work_info, cp);

    if (!done && sample_work_info.size())
    {
        // move blocks
        dynamic_send_block(cp, *master, sample_work_info, my_work_info, quantile);
        dynamic_recv_block(cp, *master);

        // fix links
        diy::fix_links(*master, *dynamic_assigner);
    }

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
    // create empty my work info, filled in later
    diy::detail::WorkInfo my_work_info = { master->communicator().rank(), -1, 0, 0, static_cast<int>(master->size()) };

    // do the actual load balancing using iexchange
    using Block = typename detail::block_traits<G>::type;
    aux_master->iexchange([&](diy::detail::AuxBlock* b, const diy::Master::ProxyWithLink& cp) -> bool
      { return iexchange_balance<Block>(b, cp, master, dynamic_assigner, sample_frac, quantile, get_work, my_work_info); } );

}

}

}
