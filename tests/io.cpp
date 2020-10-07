#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <diy/io/bov.hpp>
#include <diy/io/numpy.hpp>
#include <diy/mpi.hpp>

int full_data[4][3] = { { 1, 2, 3 },
                        { 4, 5, 6 },
                        { 7, 8, 9 },
                        { 10, 11, 12 } };
int block1[2][3]    = { { 1,  2,  3 },
                        { 4,  5,  6 } };
int block2[2][3]    = { { 7,  8,  9 },
                        { 10, 11, 12 } };
int restored_data[4][3];

TEST_CASE("Test BOV", "[io]")
{
    diy::mpi::communicator world;

    SECTION("test BOV io")
    {
        {
            std::vector<int> shape(2); shape[0] = 4; shape[1] = 3;
            diy::mpi::io::file out(world, "test.bin", diy::mpi::io::file::wronly | diy::mpi::io::file::create);
            diy::io::BOV writer(out, shape);

            diy::DiscreteBounds sub_box { 2 };
            sub_box.min[0] = 0; sub_box.min[1] = 0;
            sub_box.max[0] = 1; sub_box.max[1] = 2;
            writer.write(sub_box, &block1[0][0], sub_box);

            sub_box.min[0] = 2; sub_box.min[1] = 0;
            sub_box.max[0] = 3; sub_box.max[1] = 2;
            writer.write(sub_box, &block2[0][0], sub_box);
        }

        std::ifstream in("test.bin", std::ios::binary);
        in.read((char*) (void*) restored_data, 4*3*sizeof(int));

        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 3; ++y)
            {
                INFO("Coordinates " << x << " " << y);
                CHECK(restored_data[x][y] == full_data[x][y]);
            }
    }
}

TEST_CASE("Test NumPy", "[io]")
{
    diy::mpi::communicator world;

    SECTION("test NumPy io")
    {
        {
            std::vector<int> shape(2); shape[0] = 4; shape[1] = 3;
            diy::mpi::io::file out(world, "test.npy", diy::mpi::io::file::wronly | diy::mpi::io::file::create);
            diy::io::NumPy writer(out);
            writer.write_header<int>(shape);

            diy::DiscreteBounds sub_box { 2 };
            sub_box.min[0] = 0; sub_box.min[1] = 0;
            sub_box.max[0] = 1; sub_box.max[1] = 2;
            writer.write(sub_box, &block1[0][0], sub_box);

            sub_box.min[0] = 2; sub_box.min[1] = 0;
            sub_box.max[0] = 3; sub_box.max[1] = 2;
            writer.write(sub_box, &block2[0][0], sub_box);
        }

        diy::mpi::io::file in(world, "test.npy", diy::mpi::io::file::rdonly);
        diy::io::NumPy reader(in);
        reader.read_header();
        diy::DiscreteBounds full_box {2};
        full_box.min[0] = 0; full_box.min[1] = 0;
        full_box.max[0] = 3; full_box.max[1] = 2;
        reader.read(full_box, (int*) restored_data);

        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 3; ++y)
            {
                INFO("Coordinates " << x << " " << y);
                CHECK(restored_data[x][y] == full_data[x][y]);
            }
    }
}

TEST_CASE("Test MPI-IO", "[io]")
{
    diy::mpi::communicator world;

    SECTION("file doesn't exist")
    {
        REQUIRE_THROWS_AS  (diy::mpi::io::file(world, "non-existent-file", diy::mpi::io::file::rdonly), std::runtime_error);
        REQUIRE_THROWS_WITH(diy::mpi::io::file(world, "non-existent-file", diy::mpi::io::file::rdonly), Catch::Contains("DIY cannot open file"));
    }
}

int main(int argc, char** argv)
{
  diy::mpi::environment env;

  Catch::Session session;
  return session.run(argc, argv);
}
