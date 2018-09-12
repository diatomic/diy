#define CATCH_CONFIG_MAIN
#include "catch.hpp"

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
