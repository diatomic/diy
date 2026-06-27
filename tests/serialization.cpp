#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <diy/serialization.hpp>

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
