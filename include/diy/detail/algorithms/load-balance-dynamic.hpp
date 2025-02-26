#pragma once

#include "diy/assigner.hpp"

namespace diy
{

namespace detail
{

struct DynamicLoadBalancer
{
   DynamicLoadBalancer(Master&                 master,
                       DynamicAssigner&        dynamic_assigner,
                       float                   sample_frac = 0.5f,
                       float                   quantile = 0.8f)
   {
       // assert that destroyer() exists, will be needed for moving blocks
       if (!master.destroyer())
       {
           fmt::print(stderr, "DIY error: Master must have a block destroyer function in order to use load balancing. Please define one.\n");
           abort();
       }

       // "auxiliary" master and decomposer for using rexchange for load balancing, 1 block per process
       aux_master = new Master(master.communicator(), 1, -1, &diy::detail::AuxBlock::create, &diy::detail::AuxBlock::destroy);
       diy::ContiguousAssigner aux_assigner(aux_master->communicator().size(), aux_master->communicator().size());
       diy::DiscreteBounds aux_domain(1);                               // any fake domain
       aux_domain.min[0] = 0;
       aux_domain.max[0] = aux_master->communicator().size() + 1;
       diy::RegularDecomposer<diy::DiscreteBounds>  aux_decomposer(1, aux_domain, aux_master->communicator().size());
       aux_decomposer.decompose(aux_master->communicator().rank(), aux_assigner, *aux_master);

       // fill Master::DynamicLoadBalancer
       master.dlb().init(master, *aux_master, dynamic_assigner, sample_frac, quantile);
   }

     ~DynamicLoadBalancer() { delete aux_master; }

     // TODO: is it necessary to save this state? Just copying it over to Master::DynamicLoadBalancer anyway
     Master*                     master;
     Master*                     aux_master;
     DynamicAssigner*            dynamic_assigner;
     float                       sample_frac;
     float                       quantile;
};

}

}
