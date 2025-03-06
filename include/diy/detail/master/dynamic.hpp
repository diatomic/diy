#pragma once

#include "../algorithms/load-balance-sampling.hpp"
#include "diy/assigner.hpp"
#include "diy/resolve.hpp"
#include "diy/thirdparty/fmt/format-inl.h"

namespace diy
{

namespace detail
{

// get work information from a random sample of processes
// returns done = true: I think I'm done, no work left to do and no requests for work info from anyone else
// else done = false
template<class Block>
inline bool dynamic_exchange_sample_work_info(
                               diy::Master&                     master,                 // the real master with multiple blocks per process
                               diy::Master&                     aux_master,             // auxiliary master with 1 block per process for communicating between procs
                               float                            sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                               const Master::WCallback<Block>&  get_work,               // callback function to get amount of local work
                               WorkInfo&                        my_work_info,           // my work info, empty, filled by this function
                               std::vector<WorkInfo>&           sample_work_info)       // (output) vector of sorted sample work info, sorted by increasing total work per process
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

    // rexchange requests for work info
    std::vector<int> req_procs;     // requests for work info received from these processes
    aux_master.foreach([&](AuxBlock* b, const diy::Master::ProxyWithLink& cp)
            { send_req(b, cp, sample_procs); });
    aux_master.exchange(true);      // true = remote
    aux_master.foreach([&](AuxBlock* b, const diy::Master::ProxyWithLink& cp)
            { recv_req(b, cp, req_procs); });

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

// callback function for iexchange to execute dynamic load balancing
template<class Block>
bool balance(diy::detail::AuxBlock*             b,
            const diy::Master::ProxyWithLink&   cp,
            Master*                             master,                 // the real master with multiple blocks per process
            Master*                             aux_master,             // auxiliary master with 1 block per process for communicating between procs
            DynamicAssigner*                    dynamic_assigner,       // dynamic assigner
            float                               sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
            float                               quantile,               // quantile cutoff above which to move blocks (0.0 - 1.0)
            const Master::WCallback<Block>&     get_work,               // callback function to get amount of local work
            WorkInfo&                           my_work_info)           // my work info, empty, filled later
{
    DIY_UNUSED(b);
    DIY_UNUSED(cp);

    // exchange info about load balance
    std::vector<diy::detail::WorkInfo>   sample_work_info;           // work info collecting from sampling other mpi processes
    bool done = diy::detail::dynamic_exchange_sample_work_info(*master, *aux_master, sample_frac, get_work, my_work_info, sample_work_info);

    if (!done && sample_work_info.size())
    {
        // move blocks
        diy::detail::move_sample_blocks(*master, *aux_master, sample_work_info, my_work_info, quantile);

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
      { return balance<Block>(b, cp, master, aux_master, dynamic_assigner, sample_frac, quantile, get_work, my_work_info); } );

}

}

}
