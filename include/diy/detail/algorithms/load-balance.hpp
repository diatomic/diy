#pragma once

namespace diy
{

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
};


// set dynamic assigner blocks to local blocks of master
void set_dynamic_assigner(diy::DynamicAssigner&   dynamic_assigner,
                          diy::Master&            master)
{
    std::vector<std::tuple<int, int>> rank_gids(master.size());
    int rank = master.communicator().rank();

    for (auto i = 0; i < master.size(); i++)
        rank_gids[i] = std::make_tuple(rank, master.gid(i));

    dynamic_assigner.set_ranks(rank_gids);
}

}

