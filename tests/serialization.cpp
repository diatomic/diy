#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <memory>
#include <map>
#include <set>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <diy/link.hpp>
#include <diy/serialization.hpp>

namespace
{
    template<class Bounds>
    void require_bounds_equal(const Bounds& actual, const Bounds& expected)
    {
        REQUIRE(actual.min.dimension() == expected.min.dimension());
        REQUIRE(actual.max.dimension() == expected.max.dimension());
        for (size_t i = 0; i < actual.min.dimension(); ++i)
        {
            REQUIRE(actual.min[i] == expected.min[i]);
            REQUIRE(actual.max[i] == expected.max[i]);
        }
    }

    template<class Point>
    void require_point_equal(const Point& actual, const Point& expected)
    {
        REQUIRE(actual.dimension() == expected.dimension());
        for (size_t i = 0; i < actual.dimension(); ++i)
            REQUIRE(actual[i] == expected[i]);
    }
}

TEST_CASE("array", "[serialization]")
{
    SECTION("save and load")
    {
        diy::MemoryBuffer bb;
        std::vector<int> v { 1,2,3,4,5 };

        diy::save(bb, v.size());
        diy::save(bb, v.data(), v.size());

        bb.reset();

        size_t sz;
        std::vector<int> w;
        diy::load(bb, sz);
        w.resize(sz);
        diy::load(bb, w.data(), sz);

        REQUIRE(v.size() == w.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            REQUIRE(v[i] == w[i]);
        }
    }
}

TEST_CASE("associative containers overwrite stale contents", "[serialization]")
{
    SECTION("map")
    {
        diy::MemoryBuffer bb;
        std::map<int, int> saved { { 1, 10 }, { 2, 20 } };
        std::map<int, int> loaded { { 2, 99 }, { 3, 30 } };

        diy::save(bb, saved);
        bb.reset();
        diy::load(bb, loaded);

        REQUIRE(loaded == saved);
    }

    SECTION("set")
    {
        diy::MemoryBuffer bb;
        std::set<int> saved { 1, 2 };
        std::set<int> loaded { 2, 3 };

        diy::save(bb, saved);
        bb.reset();
        diy::load(bb, loaded);

        REQUIRE(loaded == saved);
    }

    SECTION("unordered_map")
    {
        diy::MemoryBuffer bb;
        std::unordered_map<int, int> saved { { 1, 10 }, { 2, 20 } };
        std::unordered_map<int, int> loaded { { 2, 99 }, { 3, 30 } };

        diy::save(bb, saved);
        bb.reset();
        diy::load(bb, loaded);

        REQUIRE(loaded == saved);
    }

    SECTION("unordered_set")
    {
        diy::MemoryBuffer bb;
        std::unordered_set<int> saved { 1, 2 };
        std::unordered_set<int> loaded { 2, 3 };

        diy::save(bb, saved);
        bb.reset();
        diy::load(bb, loaded);

        REQUIRE(loaded == saved);
    }
}

TEST_CASE("LinkFactory loads built-in link types", "[serialization][link]")
{
    SECTION("plain link")
    {
        diy::Link saved;
        saved.add_neighbor(diy::BlockID(7, 1));
        saved.add_neighbor(diy::BlockID(9, 2));

        diy::MemoryBuffer bb;
        diy::LinkFactory::save(bb, &saved);

        bb.reset();
        std::string saved_id;
        diy::load(bb, saved_id);
        REQUIRE(saved_id == "diy::Link");
        bb.reset();

        std::unique_ptr<diy::Link> loaded(diy::LinkFactory::load(bb));

        REQUIRE(loaded->id() == "diy::Link");
        REQUIRE(loaded->size() == 2);
        REQUIRE(loaded->target(0).gid == 7);
        REQUIRE(loaded->target(0).proc == 1);
        REQUIRE(loaded->target(1).gid == 9);
        REQUIRE(loaded->target(1).proc == 2);
    }

    SECTION("regular grid link")
    {
        diy::DiscreteBounds core(2), bounds(2), neighbor_core(2), neighbor_bounds(2);
        core.min[0] = 0;  core.max[0] = 4;  core.min[1] = 0;  core.max[1] = 4;
        bounds.min[0] = -1; bounds.max[0] = 5; bounds.min[1] = -1; bounds.max[1] = 5;
        neighbor_core.min[0] = 5; neighbor_core.max[0] = 9; neighbor_core.min[1] = 0; neighbor_core.max[1] = 4;
        neighbor_bounds.min[0] = 4; neighbor_bounds.max[0] = 10; neighbor_bounds.min[1] = -1; neighbor_bounds.max[1] = 5;

        diy::RegularGridLink saved(2, core, bounds);
        saved.add_neighbor(diy::BlockID(1, 0));
        saved.add_direction(diy::Direction(2, DIY_X1));
        saved.add_core(neighbor_core);
        saved.add_bounds(neighbor_bounds);
        saved.add_wrap(diy::Direction(2, 0));

        diy::MemoryBuffer bb;
        diy::LinkFactory::save(bb, &saved);

        bb.reset();
        std::string saved_id;
        diy::load(bb, saved_id);
        REQUIRE(saved_id == "diy::RegularLink<diy::DiscreteBounds>");
        bb.reset();

        std::unique_ptr<diy::Link> loaded_base(diy::LinkFactory::load(bb));
        const diy::RegularGridLink* loaded = dynamic_cast<const diy::RegularGridLink*>(loaded_base.get());

        REQUIRE(loaded != 0);
        REQUIRE(loaded->dimension() == 2);
        REQUIRE(loaded->size() == 1);
        REQUIRE(loaded->target(0).gid == 1);
        REQUIRE(loaded->target(0).proc == 0);
        REQUIRE(loaded->direction(0) == diy::Direction(2, DIY_X1));
        require_bounds_equal(loaded->core(), core);
        require_bounds_equal(loaded->bounds(), bounds);
        require_bounds_equal(loaded->core(0), neighbor_core);
        require_bounds_equal(loaded->bounds(0), neighbor_bounds);
        REQUIRE(loaded->wrap(0) == diy::Direction(2, 0));
    }

    SECTION("amr link")
    {
        diy::DiscreteBounds core(2), bounds(2), neighbor_core(2), neighbor_bounds(2);
        core.min[0] = 0; core.max[0] = 3; core.min[1] = 0; core.max[1] = 3;
        bounds.min[0] = -1; bounds.max[0] = 4; bounds.min[1] = -1; bounds.max[1] = 4;
        neighbor_core.min[0] = 4; neighbor_core.max[0] = 7; neighbor_core.min[1] = 0; neighbor_core.max[1] = 3;
        neighbor_bounds.min[0] = 3; neighbor_bounds.max[0] = 8; neighbor_bounds.min[1] = -1; neighbor_bounds.max[1] = 4;

        diy::AMRLink saved(2, 1, 2, core, bounds);
        saved.add_neighbor(diy::BlockID(3, 0));
        saved.add_bounds(2, 4, neighbor_core, neighbor_bounds);
        saved.add_wrap(diy::Direction(2, DIY_X1));

        diy::MemoryBuffer bb;
        diy::LinkFactory::save(bb, &saved);

        bb.reset();
        std::string saved_id;
        diy::load(bb, saved_id);
        REQUIRE(saved_id == "diy::AMRLink");
        bb.reset();

        std::unique_ptr<diy::Link> loaded_base(diy::LinkFactory::load(bb));
        const diy::AMRLink* loaded = dynamic_cast<const diy::AMRLink*>(loaded_base.get());

        REQUIRE(loaded != 0);
        REQUIRE(loaded->dimension() == 2);
        REQUIRE(loaded->size() == 1);
        REQUIRE(loaded->target(0).gid == 3);
        REQUIRE(loaded->target(0).proc == 0);
        REQUIRE(loaded->level() == 1);
        REQUIRE(loaded->level(0) == 2);
        require_point_equal(loaded->refinement(), diy::DiscreteBounds::Point { 2, 2 });
        require_point_equal(loaded->refinement(0), diy::DiscreteBounds::Point { 4, 4 });
        require_bounds_equal(loaded->core(), core);
        require_bounds_equal(loaded->bounds(), bounds);
        require_bounds_equal(loaded->core(0), neighbor_core);
        require_bounds_equal(loaded->bounds(0), neighbor_bounds);
        REQUIRE(loaded->wrap().size() == 1);
        REQUIRE(loaded->wrap()[0] == diy::Direction(2, DIY_X1));
    }

    SECTION("legacy same-compiler ids")
    {
        {
            diy::Link plain;
            plain.add_neighbor(diy::BlockID(4, 1));

            diy::MemoryBuffer bb;
            diy::save(bb, std::string(typeid(diy::Link).name()));
            plain.save(bb);
            bb.reset();

            std::unique_ptr<diy::Link> loaded(diy::LinkFactory::load(bb));

            REQUIRE(loaded->id() == "diy::Link");
            REQUIRE(loaded->size() == 1);
            REQUIRE(loaded->target(0).gid == 4);
            REQUIRE(loaded->target(0).proc == 1);
        }

        {
            diy::DiscreteBounds core(1), bounds(1);
            core.min[0] = 0; core.max[0] = 4;
            bounds.min[0] = -1; bounds.max[0] = 5;
            diy::RegularGridLink regular(1, core, bounds);

            diy::MemoryBuffer bb;
            diy::save(bb, std::string(typeid(diy::RegularGridLink).name()));
            regular.save(bb);
            bb.reset();

            std::unique_ptr<diy::Link> loaded(diy::LinkFactory::load(bb));

            REQUIRE(dynamic_cast<diy::RegularGridLink*>(loaded.get()) != 0);
        }

        {
            diy::DiscreteBounds core(1), bounds(1);
            core.min[0] = 0; core.max[0] = 4;
            bounds.min[0] = -1; bounds.max[0] = 5;
            diy::AMRLink amr(1, 0, 1, core, bounds);

            diy::MemoryBuffer bb;
            diy::save(bb, std::string(typeid(diy::AMRLink).name()));
            amr.save(bb);
            bb.reset();

            std::unique_ptr<diy::Link> loaded(diy::LinkFactory::load(bb));

            REQUIRE(dynamic_cast<diy::AMRLink*>(loaded.get()) != 0);
        }
    }

    SECTION("stable ids create common regular links")
    {
        std::unique_ptr<diy::Link> continuous(diy::LinkFactory::create("diy::RegularLink<diy::ContinuousBounds>"));
        std::unique_ptr<diy::Link> double_precision(diy::LinkFactory::create("diy::RegularLink<diy::Bounds<double>>"));
        std::unique_ptr<diy::Link> long_integer(diy::LinkFactory::create("diy::RegularLink<diy::Bounds<long>>"));

        REQUIRE(dynamic_cast<diy::RegularContinuousLink*>(continuous.get()) != 0);
        REQUIRE(dynamic_cast<diy::RegularLink<diy::Bounds<double>>*>(double_precision.get()) != 0);
        REQUIRE(dynamic_cast<diy::RegularLink<diy::Bounds<long>>*>(long_integer.get()) != 0);
    }
}
