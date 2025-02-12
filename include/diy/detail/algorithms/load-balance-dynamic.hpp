#pragma once

#include "load-balance.hpp"

namespace diy
{

namespace detail
{

struct DynamicLoadBalancer
{
    DynamicLoadBalancer(diy::Master&            master_,
                        diy::DynamicAssigner&   dynamic_assigner_,
                        float                   sample_frac_ = 0.5f,
                        float                   quantile_ = 0.8f) :
        master(master_), dynamic_assigner(dynamic_assigner_), sample_frac(sample_frac_), quantile(quantile_)
    {}

    void dynamic_load_balance()
    {
        fmt::print(stderr, "dynamic_load_balance\n");
    }

    diy::Master &master;
    diy::DynamicAssigner&       dynamic_assigner;
    float                       sample_frac;
    float                       quantile;
};

}

}
