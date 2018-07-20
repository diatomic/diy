#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <diy/grid.hpp>
#include <diy/vertices.hpp>

TEST_CASE("diy::for_each", "[grid]")
{
    SECTION("iterate over 3D grid")
    {
        using Grid   = diy::Grid<int,3>;
        using Vertex = Grid::Vertex;

        Vertex  shape { 8, 9, 10 };
        Grid    g(shape);

        int total = 0;
        diy::for_each(g.shape(), [&g,&total](Vertex x)
        {
            g(x) = total++;
        });

        REQUIRE(total == shape[0] * shape[1] * shape[2]);
        Vertex x { 3, 4, 5 };
        REQUIRE(g(x) == x[2] + x[1] * shape[2] + x[0] * shape[2] * shape[1]);       // C order
    }
}
