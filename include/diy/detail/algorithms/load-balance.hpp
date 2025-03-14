#pragma once

#include "diy/dynamic-point.hpp"
#include <stdatomic.h>

namespace diy
{

namespace detail
{

enum MsgType
{
    work_info_req,
    work_info,
    block,
};

// information about work for one process
struct WorkInfo
{
    int     proc_rank;          // mpi rank of this process
    int     top_gid;            // gid of most expensive block in this process TODO: can be top-k-gids, as long as k is fixed and known by all
    Work    top_work;           // work of top_gid TODO: can be vector of top-k work, as long as k is fixed and known by all
    Work    proc_work;          // total work of this process
    int     nlids;              // local number of blocks in this process
};

// information about a block that is moving
struct MoveInfo
{
    MoveInfo(): move_gid(-1), src_proc(-1), dst_proc(-1)   {}
    MoveInfo(int move_gid_, int src_proc_, int dst_proc_) : move_gid(move_gid_), src_proc(src_proc_), dst_proc(dst_proc_) {}
    int move_gid;
    int src_proc;
    int dst_proc;
};

// auxiliary empty block structure
struct AuxBlock
{
    static void*    create()            { return new AuxBlock; }
    static void     destroy(void* b)    { delete static_cast<AuxBlock*>(b); }

    AuxBlock() : nwork_info_recvd(0), sent_reqs(false), sent_block(false)
    {}

    AuxBlock(int nlids)                 // number of blocks in real master
    : nwork_info_recvd(0), sent_reqs(false), sent_block(false)
    {
        free_blocks.access()->resize(nlids);
        for (auto i = 0; i < nlids; i++)
            (*free_blocks.access())[i] = i;
    }

    std::vector<WorkInfo>    sample_work_info;    // work info from procs I sampled
    int                      nwork_info_recvd;    // number of work info items received
    bool                     sent_reqs;           // sent requests for work info
    bool                     sent_block;          // sent block, if one was necessary to move
    critical_resource<std::vector<int>>    free_blocks;    // lids of blocks in main master that are free to use (not locked by another thread)
};

}   // namespace detail

template<>
struct Serialization<detail::WorkInfo>
{
    static void save(BinaryBuffer& bb, const detail::WorkInfo& w)
    {
        diy::save(bb, w.proc_rank);
        diy::save(bb, w.top_gid);
        diy::save(bb, w.top_work);
        diy::save(bb, w.proc_work);
        diy::save(bb, w.nlids);
    }

    static void load(BinaryBuffer& bb, detail::WorkInfo& w)
    {
        diy::load(bb, w.proc_rank);
        diy::load(bb, w.top_gid);
        diy::load(bb, w.top_work);
        diy::load(bb, w.proc_work);
        diy::load(bb, w.nlids);
    }
};

} // namespace diy
