#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <diy/decomposition.hpp>
#include <diy/partners/common.hpp>

void test(int n, int k)
{
    int dim = 2;

    diy::DiscreteBounds global_bounds(dim);
    global_bounds.min[0] = global_bounds.min[1] = 0;
    global_bounds.max[0] = global_bounds.min[1] = 1023;

    diy::RegularDecomposer<diy::DiscreteBounds> decomposer(dim, global_bounds, n);

    diy::RegularPartners partners(decomposer, k, false);

    int kvs_product = 1;
    for (int i = 0; i < static_cast<int>(partners.rounds()); ++i)
        kvs_product *= partners.size(i);
    REQUIRE(kvs_product == n);

    for (int gid = 0; gid < n; ++gid)
        for (int i = 0; i < static_cast<int>(partners.rounds()); ++i)
        {
            std::vector<int> nbr_gids;
            partners.fill(i, gid, nbr_gids);
            for (int nbr_gid : nbr_gids)
                CHECK(nbr_gid <= n);
        }
}

TEST_CASE("RegularPartners", "[partners]")
{
    SECTION("n = 189, k = 8")
    {
        test(189, 8);
    }

    SECTION("n = 10, k = 8")
    {
        test(10, 8);
    }
}
