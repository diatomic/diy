#pragma once

#include <vector>
#include <chrono>
#include <thread>
#include <diy/fmt/format.h>
#include <diy/fmt/ostream.h>

#define WORK_MAX    100                 // maximum work a block can have (in some user-defined units)

typedef     diy::DiscreteBounds         Bounds;
typedef     diy::RegularGridLink        RGLink;

struct WorkInfo
{
    int         proc_rank;          // mpi rank of this process
    int         top_gid;            // gid of most expensive block in this process TODO: can be top-k-gids, as long as k is fixed and known by all
    diy::Work   top_work;           // work of top_gid TODO: can be vector of top-k work, as long as k is fixed and known by all
    diy::Work   proc_work;          // total work of this process
    int         nlids;              // local number of blocks in this process
};

// the block structure
struct Block
{
    Block() : bounds(0)                 {}
    static void*    create()            { return new Block; }
    static void     destroy(void* b)    { delete static_cast<Block*>(b); }

    static void save(const void* b_, diy::BinaryBuffer& bb)
    {
        const Block* b = static_cast<const Block*>(b_);

        diy::save(bb, b->gid);
        diy::save(bb, b->bounds);
        diy::save(bb, b->x);
        diy::save(bb, b->pred_work);
        diy::save(bb, b->act_work);
    }

    static void load(void* b_, diy::BinaryBuffer& bb)
    {
        Block* b = static_cast<Block*>(b_);

        diy::load(bb, b->gid);
        diy::load(bb, b->bounds);
        diy::load(bb, b->x);
        diy::load(bb, b->pred_work);
        diy::load(bb, b->act_work);
    }

    void show_block(const diy::Master::ProxyWithLink&)      // communication proxy (unused)
    {
        fmt::print(stderr, "Block {} bounds min [{}] max [{}] pred_work {} act_work {}\n",
                gid, bounds.min, bounds.max, pred_work, act_work);
    }

    void assign_work(const diy::Master::ProxyWithLink&,      // communication proxy (unused)
                     int iter,                               // current iteration
                     float noise_factor)                     // scaling factor on noise for predicted work -> actual work
    {
         // TODO: comment out the following 2 lines for actual random work
         // generation, leave uncommented for reproducible work generation
         std::srand(gid + iter + 1);
         std::rand();
         pred_work = static_cast<diy::Work>(double(std::rand()) / RAND_MAX * WORK_MAX);

         // actual work is a perturbation of the predicted work
         // act_work = pred_work +- noise_factor * rand[0,  pred_work]
         int perturb = static_cast<int>(double(std::rand()) / RAND_MAX * 2 * pred_work) - pred_work;
         act_work = static_cast<diy::Work>(pred_work + noise_factor * perturb);

         // debug
         // fmt::print(stderr, "assign_work: iter {} gid {} pred_work {} act_work {} noise_factor * perturb {}\n",
         //            iter, gid, pred_work, act_work, noise_factor * perturb);
     }

    void compute(const diy::Master::ProxyWithLink&,                         // communication proxy (unused)
                 int                                max_time,               // maximum time for a block to compute
                 int)                                                       // curent iteration (unused)
    {
        unsigned int usec = max_time * act_work * 10000L;

        // debug
//         fmt::print(stderr, "iteration {} block gid {} computing for {} s.\n", iter, gid, double(usec) / 1e6);

        std::this_thread::sleep_for(std::chrono::microseconds(usec));
    }

    // the block data
    int                 gid;
    Bounds              bounds;
    std::vector<double> x;                                              // some block data, e.g.
    diy::Work           pred_work;                                      // some estimate of how much work this block involves
    diy::Work           act_work;                                       // actual work this block involves
};

// callback function returns the predicted work for a block
diy::Work get_block_work(Block* block,
                         int    gid)
{
    DIY_UNUSED(gid);
    return block->pred_work;
}

// print DynamicAssigner
void print_dynamic_assigner(const diy::Master&            master,
                            const diy::DynamicAssigner&   dynamic_assigner)
{
    fmt::print(stderr, "DynamicAssigner: ");
    for (auto i = 0; i < master.size(); i++)
        fmt::print(stderr, "[gid, proc] = [{}, {}] ", master.gid(i), dynamic_assigner.rank(master.gid(i)));
    fmt::print(stderr, "\n");
}

// print the link for each block
void print_links(const diy::Master& master)
{
    for (auto i = 0; i < master.size(); i++)
    {
        Block*      b    = static_cast<Block*>(master.block(i));
        diy::Link*  link = master.link(i);
        fmt::print(stderr, "Link for gid {} is size {}: ", b->gid, link->size());
        for (auto j = 0; j < link->size(); j++)
            fmt::print(stderr, "[gid, proc] = [{}, {}] ", link->target(j).gid, link->target(j).proc);
        fmt::print(stderr, "\n");
    }
}

// gather information from all processes in order to collect summary stats
void gather_stats(const diy::Master&                  master,
                  std::vector<diy::Work>&             local_work,          // work for each local block
                  std::vector<WorkInfo>&              all_work_info,       // (output) global work info
                  std::vector<diy::detail::MoveInfo>& moved_blocks,        // local blocks that moved
                  std::vector<diy::detail::MoveInfo>& all_moved_blocks)    // all blocks that moved
{
    auto nlids  = master.size();                    // my local number of blocks
    auto nprocs = master.communicator().size();     // global number of procs

    WorkInfo my_work_info = { master.communicator().rank(), -1, 0, 0, (int)nlids };

    // compile my work info
    for (auto i = 0; i < master.size(); i++)
    {
        my_work_info.proc_work += local_work[i];
        if (my_work_info.top_gid == -1 || my_work_info.top_work < local_work[i])
        {
            my_work_info.top_gid    = master.gid(i);
            my_work_info.top_work   = local_work[i];
        }
    }

    // gather work info
    all_work_info.resize(nprocs);
    diy::mpi::detail::gather(master.communicator(), &my_work_info.proc_rank,
            sizeof(WorkInfo) / sizeof(WorkInfo::proc_rank), MPI_INT, &all_work_info[0].proc_rank, 0);  // assumes all elements of WorkInfo are sizeof(int)

    // empty moved_blocks list still needs to send something to the gather
    if (moved_blocks.size() == 0)
    {
        diy::detail::MoveInfo empty(-1, -1, -1, 0, 0);
        moved_blocks.push_back(empty);
    }

    // gather number of move info records
    int num_move_info = int(moved_blocks.size());
    int tot_num_move_info = 0;
    std::vector<int> counts(master.communicator().size());
    std::vector<int> offsets(master.communicator().size(), 0);
    diy::mpi::detail::gather(master.communicator(), &num_move_info, 1, MPI_INT, &counts[0], 0);
    for (auto i = 0; i < counts.size(); i++)
    {
        tot_num_move_info += counts[i];
        counts[i] *= sizeof(diy::detail::MoveInfo) / sizeof(diy::detail::MoveInfo::move_gid);
        if (i < counts.size() - 1)
            offsets[i + 1] = offsets[i] + counts[i];
    }

    // debug
    // if (master.communicator().rank() == 0)
    // {
    //     fmt::print(stderr, "counts: [ ");
    //     for (auto i = 0; i < counts.size(); i++)
    //         fmt::print(stderr, "{} ", counts[i]);
    //     fmt::print(stderr, "]\n");
    //     fmt::print(stderr, "offsets: [ ");
    //     for (auto i = 0; i < offsets.size(); i++)
    //         fmt::print(stderr, "{} ", offsets[i]);
    //     fmt::print(stderr, "]\n");

    //     fmt::print(stderr, "num_move_info {} tot_num_move_info {}\n", num_move_info, tot_num_move_info);
    // }

    // gather move info
    all_moved_blocks.resize(tot_num_move_info);
    diy::mpi::detail::gather_v(master.communicator(),
                             &moved_blocks[0].move_gid,
                             num_move_info * sizeof(diy::detail::MoveInfo) / sizeof(diy::detail::MoveInfo::move_gid),
                             MPI_INT,
                             &all_moved_blocks[0].move_gid,
                             &counts[0],
                             &offsets[0],
                             0);  // assumes all elements of MoveInfo are sizeof(int)
}

// compute and print summary stats on root process
void print_stats(const diy::Master&                     master,
                 std::vector<WorkInfo>&                 all_work_info,
                 std::vector<diy::detail::MoveInfo>&    all_moved_blocks)
{
    diy::Work tot_work = 0;
    diy::Work max_work = 0;
    diy::Work min_work = 0;
    float avg_work;
    float rel_imbalance;

    if (all_work_info.size())
    {
        for (auto i = 0; i < all_work_info.size(); i++)
        {
            if (i == 0 || all_work_info[i].proc_work < min_work)
                min_work = all_work_info[i].proc_work;
            if (i == 0 || all_work_info[i].proc_work > max_work)
                max_work = all_work_info[i].proc_work;
            tot_work += all_work_info[i].proc_work;
        }

        avg_work        = float(tot_work) / all_work_info.size();
        rel_imbalance   = float(max_work - min_work) / max_work;

        if (master.communicator().rank() == 0)
        {
            fmt::print(stderr, "Max process work {} Min process work {} Avg process work {} Rel process imbalance [(max - min) / max] {:.3}\n",
                    max_work, min_work, avg_work, rel_imbalance);

            // count nonempty moved blocks
            int num_moved = 0;
            for (auto i = 0; i < all_moved_blocks.size(); i++)
            {
                if (all_moved_blocks[i].move_gid >= 0)
                    num_moved++;
            }

            if (num_moved)
            {
                fmt::print(stderr, "List of all moved blocks:\n");
                for (auto i = 0; i < all_moved_blocks.size(); i++)
                {
                    if (all_moved_blocks[i].move_gid >= 0)
                    {
                        fmt::print(stderr, "gid {} src_proc {} dst_proc {} pred_work {} act_work {}\n",
                        all_moved_blocks[i].move_gid, all_moved_blocks[i].src_proc, all_moved_blocks[i].dst_proc, all_moved_blocks[i].pred_work, all_moved_blocks[i].act_work);
                    }
                }
            }
//             fmt::print(stderr, "Detailed list of all procs work:\n");
//             for (auto i = 0; i < all_work_info.size(); i++)
//                 fmt::print(stderr, "proc rank {} proc work {} top gid {} top gid work {}\n",
//                         all_work_info[i].proc_rank, all_work_info[i].proc_work, all_work_info[i].top_gid, all_work_info[i].top_work);
        }
    }
}

// gather summary stats on work information from all processes
void summary_stats(const diy::Master&                     master,
                   std::vector<diy::detail::MoveInfo>&    moved_blocks)
{
    std::vector<WorkInfo>  all_work_info;
    std::vector<diy::Work> local_work(master.size());
    std::vector<diy::detail::MoveInfo> all_moved_blocks;

    for (auto i = 0; i < master.size(); i++)
        local_work[i] = static_cast<Block*>(master.block(i))->pred_work;

    gather_stats(master, local_work, all_work_info, moved_blocks, all_moved_blocks);
    if (master.communicator().rank() == 0)
        print_stats(master, all_work_info, all_moved_blocks);
}
