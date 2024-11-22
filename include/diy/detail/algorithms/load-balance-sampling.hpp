#pragma once

#include "load-balance.hpp"

namespace diy
{

namespace detail
{

// send requests for work info
void send_req(AuxBlock* b,                              // local block
              const diy::Master::ProxyWithLink& cp,     // communication proxy for neighbor blocks
              std::set<int>& procs)                     // processes to query
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
void recv_req(AuxBlock* b,                              // local block
              const diy::Master::ProxyWithLink& cp,     // communication proxy for neighbor blocks
              std::vector<int>& req_procs)              // processes requesting work info
{
    std::vector<int> incoming_gids;
    cp.incoming(incoming_gids);

    // for anything incoming, dequeue data received in the last exchange
    for (int i = 0; i < incoming_gids.size(); i++)
    {
        int gid = incoming_gids[i];
        if (cp.incoming(gid).size())
        {
            int v;
            cp.dequeue(gid, v);
            req_procs.push_back(gid);                   // aux_master has 1 gid per proc, so gid = proc
        }
    }
}

// get work information from a random sample of processes
template<class Block>
void exchange_sample_work_info(diy::Master&             master,                 // the real master with multiple blocks per process
                               diy::Master&             aux_master,             // auxiliary master with 1 block per process for communicating between procs
                               float                    sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                               WorkInfo&                my_work_info,           // (output) my work info
                               std::vector<WorkInfo>&   sample_work_info)       // (output) vector of sorted sample work info, sorted by increasing total work per process
{
    auto nlids  = master.size();                    // my local number of blocks
    auto nprocs = master.communicator().size();     // global number of procs
    auto my_proc = master.communicator().rank();    // rank of my proc

    // compile my work info
    my_work_info = { master.communicator().rank(), -1, 0, 0, (int)nlids };
    for (auto i = 0; i < master.size(); i++)
    {
        Block* block = static_cast<Block*>(master.block(i));
        my_work_info.proc_work += master.get_work(i);
        if (my_work_info.top_gid == -1 || my_work_info.top_work < master.get_work(i))
        {
            my_work_info.top_gid    = master.gid(i);
            my_work_info.top_work   = master.get_work(i);
        }
    }

    // vectors of integers from WorkInfo
    std::vector<int> my_work_info_vec =
    {
        my_work_info.proc_rank,
        my_work_info.top_gid,
        my_work_info.top_work,
        my_work_info.proc_work,
        my_work_info.nlids
    };

    // pick a random sample of processes, w/o duplicates, and excluding myself
    int nsamples = sample_frac * (nprocs - 1);
    std::set<int> sample_procs;
    for (auto i = 0; i < nsamples; i++)
    {
        int rand_proc;
        do
        {
            std::uniform_int_distribution<> distrib(0, nprocs - 1);     // inclusive
            rand_proc = distrib(master.mt_gen);
        } while (sample_procs.find(rand_proc) != sample_procs.end() || rand_proc == my_proc);
        sample_procs.insert(rand_proc);
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
        reqs[i] = master.communicator().isend(req_procs[i], work_info_tag, my_work_info_vec);

    // receive work info
    // TODO: std::length error below when using aux_master instead of master.communicator()
    std::vector<int>   other_work_info_vec(5);
    sample_work_info.resize(nsamples);
    for (auto i = 0; i < nsamples; i++)
    {
        master.communicator().recv(diy::mpi::any_source, work_info_tag, other_work_info_vec);

        sample_work_info[i].proc_rank = other_work_info_vec[0];
        sample_work_info[i].top_gid   = other_work_info_vec[1];
        sample_work_info[i].top_work  = other_work_info_vec[2];
        sample_work_info[i].proc_work = other_work_info_vec[3];
        sample_work_info[i].nlids     = other_work_info_vec[4];
    }

    // ensure all the send requests cleared
    for (auto i = 0; i < req_procs.size(); i++)
        reqs[i].wait();

    // sort sample_work_info by proc_work
    std::sort(sample_work_info.begin(), sample_work_info.end(),
            [&](WorkInfo& a, WorkInfo& b) { return a.proc_work < b.proc_work; });
}

// send block
template<class Block>
void send_block(AuxBlock*                           b,                  // local block
                const diy::Master::ProxyWithLink&   cp,                 // communication proxy for neighbor blocks
                diy::Master&                        master,             // real master with multiple blocks per process
                diy::DynamicAssigner&               dynamic_assigner,   // dynamic assigner
                const std::vector<WorkInfo>&        sample_work_info,   // sampled work info
                const WorkInfo&                     my_work_info,       // my work info
                float                               quantile)           // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    MoveInfo move_info = {-1, -1, -1};

    // my rank's position in the sampled work info, sorted by proc_work
    int my_work_idx = sample_work_info.size();                                          // index where my work would be in the sample_work
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
        int target = sample_work_info.size() - my_work_idx;

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

            // update the dynamic assigner
            dynamic_assigner.set_rank(move_info.dst_proc, move_info.move_gid, true);

            // destination in aux_master, where gid = proc
            diy::BlockID dest_block = {move_info.dst_proc, move_info.dst_proc};

            // enqueue the gid of the moving block
            cp.enqueue(dest_block, move_info.move_gid);

            // enqueue the block
            void* send_b;
            send_b = master.block(master.lid(move_info.move_gid));
            diy::MemoryBuffer bb;
            master.saver()(send_b, bb);
            cp.enqueue(dest_block, bb.buffer);

            // enqueue the link for the block
            diy::Link* send_link = master.link(master.lid(move_info.move_gid));
            diy::LinkFactory::save(bb, send_link);
            cp.enqueue(dest_block, bb.buffer);

            // remove the block from the master
            Block* delete_block = static_cast<Block*>(master.get(master.lid(move_info.move_gid)));
            master.release(master.lid(move_info.move_gid));
            delete delete_block;

            // debug
//             if (master.communicator().rank() == move_info.src_proc)
//                 fmt::print(stderr, "moving gid {} from src rank {} to dst rank {}\n",
//                         move_info.move_gid, move_info.src_proc, move_info.dst_proc);
        }
    }
}

// receive block
template<class Block>
void recv_block(AuxBlock*                           b,          // local block
                const diy::Master::ProxyWithLink&   cp,         // communication proxy for neighbor blocks
                diy::Master&                        master)     // real master with multiple blocks per process
{
    std::vector<int> incoming_gids;
    cp.incoming(incoming_gids);

    Block* recv_b;

    // for anything incoming, dequeue data received in the last exchange
    for (int i = 0; i < incoming_gids.size(); i++)
    {
        int gid = incoming_gids[i];
        if (cp.incoming(gid).size())
        {
            // dequeue the gid of the moving block
            int move_gid;
            cp.dequeue(gid, move_gid);

            // dequeue the block
            recv_b = static_cast<Block*>(master.creator()());
            diy::MemoryBuffer bb;
            cp.dequeue(gid, bb.buffer);
            recv_b->load(recv_b, bb);

            // dequeue the link
            diy::Link* recv_link;
            cp.dequeue(gid, bb.buffer);
            recv_link = diy::LinkFactory::load(bb);

            // add block to the master
            master.add(move_gid, recv_b, recv_link);
        }
    }
}

// move blocks based on sampled work info
template<class Block>
void move_sample_blocks(diy::Master&                    master,                 // real master with multiple blocks per process
                        diy::Master&                    aux_master,             // auxiliary master with 1 block per process for communcating between procs
                        diy::DynamicAssigner&           dynamic_assigner,       // dynamic assigner
                        const std::vector<WorkInfo>&    sample_work_info,       // sampled work info
                        const WorkInfo&                 my_work_info,           // my work info
                        float                           quantile)               // quantile cutoff above which to move blocks (0.0 - 1.0)
{
    // rexchange moving blocks
    aux_master.foreach([&](AuxBlock* b, const diy::Master::ProxyWithLink& cp)
            { send_block<Block>(b, cp, master, dynamic_assigner, sample_work_info, my_work_info, quantile); });
    aux_master.exchange(true);      // true = remote
    aux_master.foreach([&](AuxBlock* b, const diy::Master::ProxyWithLink& cp)
            { recv_block<Block>(b, cp, master); });
}

}

}

