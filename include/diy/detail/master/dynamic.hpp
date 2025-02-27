#pragma once

#include "../algorithms/load-balance-sampling.hpp"
#include "diy/assigner.hpp"
#include "diy/resolve.hpp"

namespace diy
{

namespace detail
{

void dynamic_balance(Master*               master,                 // the real master with multiple blocks per process
                     Master*               aux_master,             // auxiliary master with 1 block per process for communicating between procs
                     DynamicAssigner*      dynamic_assigner,       // dynamic assigner
                     float                 sample_frac,            // fraction of procs to sample 0.0 < sample_size <= 1.0
                     float                 quantile,               // quantile cutoff above which to move blocks (0.0 - 1.0)
                     const WorkInfo&       my_work_info)           // my process' work info
{
   // exchange info about load balance
   std::vector<diy::detail::WorkInfo>   sample_work_info;           // work info collecting from sampling other mpi processes
   diy::detail::exchange_sample_work_info(*master, *aux_master, sample_frac, my_work_info, sample_work_info);

   // move blocks
   diy::detail::move_sample_blocks(*master, *aux_master, sample_work_info, my_work_info, quantile);

   // fix links
   diy::fix_links(*master, *dynamic_assigner);
}

}

}
