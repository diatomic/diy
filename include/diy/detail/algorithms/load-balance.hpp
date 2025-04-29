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
    AuxBlock() : nwork_info_recvd(0), sent_reqs(false), sent_block(false)
    {}

    typedef           resource_accessor<std::vector<int>, fast_mutex>         accessor;

    AuxBlock(int nlids)                 // number of blocks in real master
    : nwork_info_recvd(0), sent_reqs(false), sent_block(false)
    {
        reset_free_blocks(nlids);
    }

    // reset free blocks to all local blocks being free
    void reset_free_blocks(int nlids)
    {
        auto free_blocks_access = free_blocks.access();
        free_blocks_access->resize(nlids);
        for (auto i = 0; i < nlids; i++)
        {
            (*free_blocks_access)[i].first = i;
            (*free_blocks_access)[i].second = 0;        // caller needs to fill in the work; AuxBlock does not have access to master
        }
    }

    // return next free block, or -1 if none available
    // does not lock the returned block
    int next_free_block(int nlids)
    {
        int retval = -1;
        auto free_blocks_access = free_blocks.access();
        for(auto i = 0; i < nlids; i++)
        {
            if ((*free_blocks_access)[i].first >= 0)
            {
                retval = i;
                break;
            }
        }
        return retval;
    }

    // return lid of the block or -1 if none available
    // locks the returned block
    int grab_free_block(int nlids)
    {
        int retval = -1;
        auto free_blocks_access = free_blocks.access();
        for(auto i = 0; i < nlids; i++)
        {
            if ((*free_blocks_access)[i].first >= 0)
            {
                (*free_blocks_access)[i].first = -1;
                retval = i;
                break;
            }
        }
        return retval;
    }

    // remove a block from the free blocks list
    void remove_free_block(int lid)
    {
        auto free_blocks_access = free_blocks.access();
        std::swap((*free_blocks_access)[static_cast<size_t>(lid)], (*free_blocks_access).back());
        (*free_blocks_access).pop_back();
    }

    // add a block to the end of the free blocks list
    void add_free_block(Work work)        // work associated with the block
    {
        auto free_blocks_access = free_blocks.access();
        int lid = static_cast<int>((*free_blocks_access).size());
        (*free_blocks_access).push_back(std::make_pair(lid, work));
    }

    using FreeBlock = std::pair<int, Work>;                         // [block lid in master (-1 indicates locked), block work]
    WorkInfo                               my_work_info;            // work info for blocks in the main master
    std::vector<WorkInfo>                  sample_work_info;        // work info from procs I sampled
    int                                    nwork_info_recvd;        // number of work info items received
    bool                                   sent_reqs;               // sent requests for work info already
    bool                                   sent_block;              // sent block already
    critical_resource<std::vector<FreeBlock>> free_blocks;          // lids of blocks in main master that are free to use (not locked by another thread)
    std::atomic<bool>                      iexchange_done{false};   // whether iexchange_balance is still running or is done
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
