#pragma once

#include "diy/dynamic-point.hpp"
#include <cstdlib>
#include <stdatomic.h>

// uninitialized values
#define NO_GID      -1
#define NO_WORK     0
#define NO_PROC     -1
#define NO_IDX      -1
#define NO_REQS     0

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
    MoveInfo(): move_gid(NO_GID), src_proc(NO_PROC), dst_proc(NO_PROC)   {}
    MoveInfo(int move_gid_, int src_proc_, int dst_proc_) : move_gid(move_gid_), src_proc(src_proc_), dst_proc(dst_proc_) {}
    int move_gid;
    int src_proc;
    int dst_proc;
};

// block in free_blocks vector
struct FreeBlock
{
    int      gid;                // gid of the block
    Work     work;               // amount of work that computing the block takes
    int      src_proc;           // sender of the block, if it migrated from elsewhere

    FreeBlock(): gid(NO_GID), work(NO_WORK), src_proc(NO_PROC) {}
    FreeBlock(int gid_, Work work_, int src_proc_):
    gid(gid_), work(work_), src_proc(src_proc_) {}
};

// auxiliary empty block structure
struct AuxBlock
{
    typedef           resource_accessor<std::vector<int>, fast_mutex>         accessor;

    AuxBlock(Master* master_)            // real master, (not auxiliary master)
    : master(master_), nsent_reqs(NO_REQS), proc_work(NO_WORK), prev_proc_work(NO_WORK) {}

    // initialize free blocks
    template<class GetWork>
    void init_free_blocks(const GetWork& get_block_work)
    {
        using Block = typename detail::block_traits<GetWork>::type;
        auto free_blocks_access = free_blocks.access();
        free_blocks_access->resize(master->size());
        Work tot_work = 0;
        for (auto i = 0; i < master->size(); i++)
        {
            (*free_blocks_access)[i].gid = master->gid(i);
            Block* b = static_cast<Block*>(master->block(i));
            Work w = get_block_work(b, master->gid(i));
            (*free_blocks_access)[i].work = w;
            tot_work += w;
        }
        proc_work = tot_work;
        prev_proc_work = tot_work;
    }

    // debug: print free blocks
    void print_free_blocks()
    {
        auto free_blocks_access = free_blocks.access();
        for (auto i = 0; i < (*free_blocks_access).size(); i++)
            fmt::print(stderr, "free_blocks[{}]: gid {} work {} src_proc {}\n",
                       i, (*free_blocks_access)[i].gid, (*free_blocks_access)[i].work, (*free_blocks_access)[i].src_proc);
    }

    // whether there are any free blocks
    bool any_free_blocks()
    {
        auto free_blocks_access = free_blocks.access();
        return (*free_blocks_access).size();
    }

    // return gid of the heaviest free block or -1 if none available, and remove the block
    // fill free block with the block being grabbed
    // if no block is available, fill free block with uninitialized values
    int grab_heaviest_free_block(FreeBlock& free_block)
    {
        int retval          = NO_GID;
        size_t heaviest_idx = NO_IDX;
        Work max_work       = NO_WORK;
        free_block.gid      = NO_GID;
        free_block.work     = NO_WORK;
        free_block.src_proc = NO_PROC;
        auto free_blocks_access = free_blocks.access();
        for (auto i = 0; i < (*free_blocks_access).size(); i++)
        {
            if (i == 0 || (*free_blocks_access)[i].work > max_work)
            {
                retval = (*free_blocks_access)[i].gid;
                max_work = (*free_blocks_access)[i].work;
                heaviest_idx = i;

                // debug
                if (retval == -1)
                    fmt::print(stderr, "grab_heaviest_block(): free_blocks size {} i {} gid {} work {}\n",
                               (*free_blocks_access).size(), i, (*free_blocks_access)[i].gid, (*free_blocks_access)[i].work);
            }
        }
        if (retval >= 0)
        {
            // debug
            // fmt::print(stderr, "grab_heaviest_block(): retval {} heaviest_idx {} size {}\n", retval, heaviest_idx, (*free_blocks_access).size());

            std::swap((*free_blocks_access)[heaviest_idx], (*free_blocks_access).back());
            proc_work           -= (*free_blocks_access).back().work;
            free_block.gid      = (*free_blocks_access).back().gid;
            free_block.work     = (*free_blocks_access).back().work;
            free_block.src_proc = (*free_blocks_access).back().src_proc;
            (*free_blocks_access).pop_back();

            // debug
            // fmt::print(stderr, "grab_heaviest_block(): free_block.gid {} free_block.work {} size after popping back {}\n",
            //            free_block.gid, free_block.work, (*free_blocks_access).size());
        }
        return retval;
    }

    // add a block to the end of the free blocks list
    void add_free_block(FreeBlock& free_block)
    {
        auto free_blocks_access = free_blocks.access();
        (*free_blocks_access).push_back(free_block);
        proc_work += (*free_blocks_access).back().work;
    }

    // update my work info based on current state of free blocks
    void update_my_work_info(WorkInfo& work_info)
    {
        auto free_blocks_access = free_blocks.access();

        // initialize
        work_info.proc_rank = master->communicator().rank();
        work_info.top_gid   = NO_GID;
        work_info.top_work  = NO_WORK;
        work_info.proc_work = proc_work;
        work_info.nlids     = static_cast<int>((*free_blocks_access).size());

        for (auto i = 0; i < (*free_blocks_access).size(); i++)
        {
            if (i == 0 || work_info.top_work < (*free_blocks_access)[i].work)
            {
                work_info.top_gid    = (*free_blocks_access)[i].gid;
                work_info.top_work   = (*free_blocks_access)[i].work;
            }
        }
    }

    // update sampled work info with incoming work info
    void update_sample_work_info(WorkInfo& work_info)
    {
        size_t i = 0;
        for (i = 0; i < sample_work_info.size(); i++)
        {
            if (sample_work_info[i].proc_rank == work_info.proc_rank)
                break;
        }
        if (i < sample_work_info.size())
            sample_work_info[i] = work_info;
        else
            sample_work_info.push_back(work_info);

        // sort sample_work_info by proc_work
        std::sort(sample_work_info.begin(), sample_work_info.end(),
                [&](WorkInfo& a, WorkInfo& b) { return a.proc_work < b.proc_work; });
    }

    std::vector<WorkInfo>                  sample_work_info;        // work info from procs I sampled
    std::vector<int>                       requesters;              // procs that requested work info from me
    Master*                                master;                  // real (not auxiliary) master
    int                                    nsent_reqs;              // number of sent requests for work info
    critical_resource<std::vector<FreeBlock>> free_blocks;          // lids of blocks in main master that are free to use (not locked by another thread)
    std::atomic<bool>                      iexchange_done{false};   // whether iexchange_balance is still running or is done
    std::atomic<Work>                      proc_work;               // total work for my process
    Work                                   prev_proc_work;          // previous proc_work, to know if it changed
};

}  // namespace detail

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
