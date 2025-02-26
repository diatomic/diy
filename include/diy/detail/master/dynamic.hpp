#pragma once

#include "../algorithms/load-balance-sampling.hpp"
#include "diy/assigner.hpp"
#include "diy/resolve.hpp"

namespace diy
{

namespace detail
{

       struct MasterDynamicLoadBalancer
       {
           void init(Master& master__, Master& aux_master__, DynamicAssigner& dynamic_assigner__, float sample_frac__, float quantile__)
           {
               master_            = &master__;
               aux_master_        = &aux_master__;
               dynamic_assigner_  = &dynamic_assigner__;
               sample_frac_       = sample_frac__;
               quantile_          = quantile__;
           }

           Master* master()                       { return master_; }
           Master* aux_master()                   { return aux_master_; }
           DynamicAssigner* dynamic_assigner()    { return dynamic_assigner_; }
           float sample_frac()                    { return sample_frac_; }
           float quantile()                       { return quantile_; }

       Master*                     master_;
       Master*                     aux_master_;
       DynamicAssigner*            dynamic_assigner_;
       float                       sample_frac_;
       float                       quantile_;
   };

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
