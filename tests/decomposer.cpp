#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <diy/assigner.hpp>
#include <diy/decomposition.hpp>
#include <diy/dynamic-point.hpp>

#include <algorithm>

void  test(int gid,                                         // block global id
           const diy::ContinuousBounds&,                    // block bounds without any ghost added
           const diy::ContinuousBounds& bounds,             // block bounds including ghost region
           const diy::ContinuousBounds&,                    // global data bounds
           const diy::RegularContinuousLink&)               // neighborhood
{
    REQUIRE(gid == 0);
    for (int i = 0; i < 3; ++i)
        REQUIRE(bounds.min[i] == 0);
    REQUIRE(bounds.max[0] == Approx(33.333));
    REQUIRE(bounds.max[1] == Approx(33.333));
    REQUIRE(bounds.max[2] == 100.);
}

void  test_interval(int gid,
                    const diy::DiscreteBounds&,
                    const diy::DiscreteBounds& bounds,
                    const diy::DiscreteBounds&,
                    const diy::RegularGridLink&)
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

        // test the return-by-value gid_to_coords
        auto coords = decomposer.gid_to_coords(2);
        REQUIRE(coords == diy::RegularDecomposer<diy::ContinuousBounds>::DivisionsVector { 2, 0, 0 });

        // test that gid_to_coords clears the vector
        decomposer.gid_to_coords(3, coords);
        REQUIRE(coords == diy::RegularDecomposer<diy::ContinuousBounds>::DivisionsVector { 0, 1, 0 });
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

TEST_CASE("DynamicPoint constructors", "[dynamic-point]")
{
    SECTION("converts between coordinate types")
    {
        diy::DynamicPoint<int> p { 1, 2, 3 };
        diy::DynamicPoint<double> q(p);

        REQUIRE(q.dimension() == 3);
        REQUIRE(q[0] == Approx(1.));
        REQUIRE(q[1] == Approx(2.));
        REQUIRE(q[2] == Approx(3.));
    }

    SECTION("constructs from pointer and dimension")
    {
        double values[] = { 4., 5., 6. };
        diy::DynamicPoint<double> p(values, 3);

        REQUIRE(p.dimension() == 3);
        REQUIRE(p[0] == Approx(4.));
        REQUIRE(p[1] == Approx(5.));
        REQUIRE(p[2] == Approx(6.));
    }
}

TEST_CASE("RegularDecomposer point queries handle wrapping and outside points", "[decomposition]")
{
    SECTION("non-wrapped continuous points outside the domain have no gid")
    {
        diy::ContinuousBounds domain(1);
        domain.min[0] = 0.f;
        domain.max[0] = 10.f;
        diy::RegularDecomposer<diy::ContinuousBounds> decomposer(
            1, domain, 2,
            diy::RegularDecomposer<diy::ContinuousBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::ContinuousBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::ContinuousBounds>::CoordinateVector { 0.f },
            diy::RegularDecomposer<diy::ContinuousBounds>::DivisionsVector { 2 });

        diy::DynamicPoint<float> below { -0.25f };
        diy::DynamicPoint<float> above { 10.25f };
        std::vector<int> gids { 99 };

        decomposer.point_to_gids(gids, below);
        REQUIRE(gids.empty());
        REQUIRE(decomposer.num_gids(below) == 0);
        REQUIRE(decomposer.point_to_gid(below) == -1);
        REQUIRE(decomposer.lowest_gid(below) == -1);

        gids.push_back(99);
        decomposer.point_to_gids(gids, above);
        REQUIRE(gids.empty());
        REQUIRE(decomposer.num_gids(above) == 0);
        REQUIRE(decomposer.point_to_gid(above) == -1);
        REQUIRE(decomposer.lowest_gid(above) == -1);
    }

    SECTION("wrapped continuous points normalize into the domain")
    {
        diy::ContinuousBounds domain(1);
        domain.min[0] = 0.f;
        domain.max[0] = 10.f;
        diy::RegularDecomposer<diy::ContinuousBounds> decomposer(
            1, domain, 2,
            diy::RegularDecomposer<diy::ContinuousBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::ContinuousBounds>::BoolVector { true },
            diy::RegularDecomposer<diy::ContinuousBounds>::CoordinateVector { 0.f },
            diy::RegularDecomposer<diy::ContinuousBounds>::DivisionsVector { 2 });

        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<float> { 12.5f }) == 0);
        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<float> { -2.5f }) == 1);
        REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<float> { 12.5f }) == 0);
        REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<float> { -2.5f }) == 1);
    }

    SECTION("wrapped ghost ranges cross the periodic boundary")
    {
        diy::ContinuousBounds domain(1);
        domain.min[0] = 0.f;
        domain.max[0] = 10.f;
        diy::RegularDecomposer<diy::ContinuousBounds> decomposer(
            1, domain, 2,
            diy::RegularDecomposer<diy::ContinuousBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::ContinuousBounds>::BoolVector { true },
            diy::RegularDecomposer<diy::ContinuousBounds>::CoordinateVector { 1.f },
            diy::RegularDecomposer<diy::ContinuousBounds>::DivisionsVector { 2 });

        std::vector<int> gids;
        decomposer.point_to_gids(gids, diy::DynamicPoint<float> { 0.5f });
        std::sort(gids.begin(), gids.end());

        REQUIRE(gids == std::vector<int> { 0, 1 });
        REQUIRE(decomposer.num_gids(diy::DynamicPoint<float> { 0.5f }) == 2);
        REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<float> { 0.5f }) == 0);
    }

    SECTION("discrete points use mathematical floor division")
    {
        diy::RegularDecomposer<diy::DiscreteBounds> decomposer(
            1, diy::interval(0, 9), 2,
            diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::DiscreteBounds>::CoordinateVector { 0 },
            diy::RegularDecomposer<diy::DiscreteBounds>::DivisionsVector { 2 });

        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<int> { -1 }) == -1);
        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<int> { 10 }) == -1);
        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<int> { 9 }) == 1);
    }

    SECTION("wrapped discrete points normalize into the domain")
    {
        diy::RegularDecomposer<diy::DiscreteBounds> decomposer(
            1, diy::interval(0, 9), 2,
            diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { false },
            diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { true },
            diy::RegularDecomposer<diy::DiscreteBounds>::CoordinateVector { 0 },
            diy::RegularDecomposer<diy::DiscreteBounds>::DivisionsVector { 2 });

        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<int> { -1 }) == 1);
        REQUIRE(decomposer.point_to_gid(diy::DynamicPoint<int> { 10 }) == 0);
    }

    SECTION("wrapped discrete upper bounds do not add extra gids")
    {
        for (bool share_face: { false, true })
        {
            diy::RegularDecomposer<diy::DiscreteBounds> decomposer(
                1, diy::interval(0, 10), 3,
                diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { share_face },
                diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { true },
                diy::RegularDecomposer<diy::DiscreteBounds>::CoordinateVector { 0 },
                diy::RegularDecomposer<diy::DiscreteBounds>::DivisionsVector { 3 });

            std::vector<int> gids;
            decomposer.point_to_gids(gids, diy::DynamicPoint<int> { 9 });
            REQUIRE(gids == std::vector<int> { 2 });
            REQUIRE(decomposer.num_gids(diy::DynamicPoint<int> { 9 }) == 1);
            REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<int> { 9 }) == 2);

            decomposer.point_to_gids(gids, diy::DynamicPoint<int> { 10 });
            REQUIRE(gids == std::vector<int> { 2 });
            REQUIRE(decomposer.num_gids(diy::DynamicPoint<int> { 10 }) == 1);
            REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<int> { 10 }) == 2);
        }
    }

    SECTION("wrapped discrete minimum is not shared with the last block")
    {
        diy::RegularDecomposer<diy::DiscreteBounds> decomposer(
            1, diy::interval(0, 10), 3,
            diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { true },
            diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { true },
            diy::RegularDecomposer<diy::DiscreteBounds>::CoordinateVector { 0 },
            diy::RegularDecomposer<diy::DiscreteBounds>::DivisionsVector { 3 });

        std::vector<int> gids;
        decomposer.point_to_gids(gids, diy::DynamicPoint<int> { 0 });

        REQUIRE(gids == std::vector<int> { 0 });
        REQUIRE(decomposer.num_gids(diy::DynamicPoint<int> { 0 }) == 1);
        REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<int> { 0 }) == 0);
    }

    SECTION("wrapped discrete ghost ranges use periodic block widths")
    {
        for (bool share_face: { false, true })
        {
            diy::RegularDecomposer<diy::DiscreteBounds> decomposer(
                1, diy::interval(0, 10), 3,
                diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { share_face },
                diy::RegularDecomposer<diy::DiscreteBounds>::BoolVector { true },
                diy::RegularDecomposer<diy::DiscreteBounds>::CoordinateVector { 2 },
                diy::RegularDecomposer<diy::DiscreteBounds>::DivisionsVector { 3 });

            std::vector<int> gids;
            decomposer.point_to_gids(gids, diy::DynamicPoint<int> { 10 });
            std::sort(gids.begin(), gids.end());

            REQUIRE(gids == std::vector<int> { 0, 2 });
            REQUIRE(decomposer.num_gids(diy::DynamicPoint<int> { 10 }) == 2);
            REQUIRE(decomposer.lowest_gid(diy::DynamicPoint<int> { 10 }) == 0);
        }
    }
}
