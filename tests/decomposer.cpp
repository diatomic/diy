#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <diy/assigner.hpp>
#include <diy/decomposition.hpp>

void  test(int gid,                                         // block global id
           const diy::ContinuousBounds& core,               // block bounds without any ghost added
           const diy::ContinuousBounds& bounds,             // block bounds including ghost region
           const diy::ContinuousBounds& domain,             // global data bounds
           const diy::RegularContinuousLink& link)          // neighborhood
{
    REQUIRE(gid == 0);
    for (int i = 0; i < 3; ++i)
        REQUIRE(bounds.min[i] == 0);
    REQUIRE(bounds.max[0] == Approx(33.333));
    REQUIRE(bounds.max[1] == Approx(33.333));
    REQUIRE(bounds.max[2] == 100.);
}

void  test_interval(int gid,
                    const diy::DiscreteBounds& core,
                    const diy::DiscreteBounds& bounds,
                    const diy::DiscreteBounds& domain,
                    const diy::RegularGridLink& link)
{
    REQUIRE(bounds.min[0] == gid);
    REQUIRE(bounds.max[0] == gid);
    REQUIRE(bounds.min[0] == gid);
    REQUIRE(bounds.max[0] == gid);
}


TEST_CASE("RegularDecomposer", "[decomposition]")
{
    SECTION("simple 3D decomposition")
    {
        int nblocks = 9;
        diy::ContinuousBounds domain(3);
        for(int i = 0; i < 3; ++i)
        {
            domain.min[i] = 0.;
            domain.max[i] = 100.;
        }
        diy::RegularDecomposer<diy::ContinuousBounds>   decomposer(3, domain, nblocks);

        diy::ContiguousAssigner assigner(nblocks, nblocks);
        decomposer.decompose(0, assigner, test);
    }

    SECTION("1D decomposition of an interval")
    {
        for (int nblocks = 1; nblocks < 33; ++nblocks)
        {
            diy::RegularDecomposer<diy::DiscreteBounds> decomposer(1, diy::interval(0,nblocks-1), nblocks);
            diy::ContiguousAssigner assigner(1, nblocks);
            decomposer.decompose(0, assigner, test_interval);
        }
    }
}
