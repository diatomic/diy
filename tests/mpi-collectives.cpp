#include    <iostream>

#include    <diy/mpi.hpp>
#include    <diy/log.hpp>

#define     CATCH_CONFIG_RUNNER
#include    "catch.hpp"

using namespace std;

struct Elem
{
    char   a;
    short  b;
    int    c;
    size_t d;
};

const size_t vec_size = 6;

namespace diy
{
namespace mpi
{
namespace detail
{
    template<>
    struct mpi_datatype<Elem>
    {
        static diy::mpi::datatype datatype()              { return get_mpi_datatype<unsigned char>(); }
        static const void*        address(Elem const& x)  { return &x; }
        static void*              address(Elem& x)        { return &x; }
        static int                count(Elem const&)      { return sizeof(Elem); }
    };
}
}
}

namespace mpi = diy::mpi;

struct SimpleFixture
{
  static int nblocks;
  static int threads;
  diy::mpi::communicator world;
};

int SimpleFixture::nblocks = 0;
int SimpleFixture::threads = 1;

TEST_CASE_METHOD(SimpleFixture, "MPI Collectives Test", "[mpi-collectives]")
{
    int sum = 0;                                // checksum of received values

    // broadcast test

    int             simple_sca;                 // simple scalar
    Elem            complex_sca;                // complex scalar
    vector<int>     simple_vec(vec_size);       // simple vector
    vector<Elem>    complex_vec(vec_size);      // complex vector

    if (world.rank() == 0)
    {
        // init simple scalar
        simple_sca = 42;

        // init complex scalar
        complex_sca.a = 1;
        complex_sca.b = 2;
        complex_sca.c = 3;
        complex_sca.d = 4;

        // init simple vector
        for (size_t i = 0; i < vec_size; ++i)
            simple_vec[i] = static_cast<int>(i);

        // init complex vector
        for (size_t i = 0; i < vec_size; i++)
        {
            Elem elem;
            elem.a = static_cast<char>(i + 1);
            elem.b = static_cast<short>(2 * (i + 1));
            elem.c = static_cast<int>(3 * (i + 1));
            elem.d = static_cast<size_t>(4 * (i + 1));
            complex_vec[i] = elem;
        }
    }

    // broadcast simple scalar
    mpi::broadcast(world, simple_sca, 0);
    fmt::print(stderr, "Simple scalar broadcast:\n");
    fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), simple_sca);
    CHECK(simple_sca == 42);

    // broadcast complex scalar
    mpi::broadcast(world, complex_sca, 0);
    fmt::print(stderr, "Complex scalar broadcast:\n");
    sum = static_cast<int>(complex_sca.a + complex_sca.b + complex_sca.c + complex_sca.d);
    fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
    CHECK(sum == 10);

    // broadcast simple vector
    mpi::broadcast(world, simple_vec, 0);
    fmt::print(stderr, "Simple vector broadcast:\n");
    sum = 0;
    for (size_t i = 0; i < vec_size; i++)
        sum += simple_vec[i];
    fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
    CHECK(sum == 15);

    // broadcast complex vector
    mpi::broadcast(world, complex_vec, 0);
    fmt::print(stderr, "Complex vector broadcast:\n");
    sum = 0;
    for (size_t i = 0; i < vec_size; i++)
        sum += static_cast<int>(complex_vec[i].a + complex_vec[i].b + complex_vec[i].c + complex_vec[i].d);
    fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
    CHECK(sum == 210);

    // gather test

    vector<int>             gathered_simple_sca;
    vector<Elem>            gathered_complex_sca;
    vector< vector<int> >   gathered_simple_vec;
    vector< vector<Elem> >  gathered_complex_vec;

    auto world_size = static_cast<size_t>(world.size());

    // gather simple scalar
    mpi::gather(world, simple_sca, gathered_simple_sca, 0);
    if (world.rank() == 0)
    {
        sum = 0;
        fmt::print(stderr, "Simple scalar gather:\n");
        for (size_t j = 0; j < world_size; j++)
            sum += gathered_simple_sca[j];
        fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
        CHECK(sum == 42 * world.size());
    }

    // gather complex scalar
    mpi::gather(world, complex_sca, gathered_complex_sca, 0);
    if (world.rank() == 0)
    {
        sum = 0;
        fmt::print(stderr, "Complex scalar gather:\n");
        for (size_t j = 0; j < world_size; j++)
            sum += static_cast<int>(gathered_complex_sca[j].a + gathered_complex_sca[j].b + gathered_complex_sca[j].c + gathered_complex_sca[j].d);
        fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
        CHECK(sum == 10 * world.size());
    }

    // gather simple vector
    mpi::gather(world, simple_vec, gathered_simple_vec, 0);
    if (world.rank() == 0)
    {
        sum = 0;
        fmt::print(stderr, "Simple vector gather:\n");
        for (size_t j = 0; j < world_size; j++)
            for (size_t i = 0; i < vec_size; i++)
                sum += (gathered_simple_vec[j][i]);
        fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
        CHECK(sum == 15 * world.size());
    }

    // gather complex vector
    mpi::gather(world, complex_vec, gathered_complex_vec, 0);
    if (world.rank() == 0)
    {
        sum = 0;
        fmt::print(stderr, "Complex vector gather:\n");
        for (size_t j = 0; j < world_size; j++)
            for (size_t i = 0; i < vec_size; i++)
                sum += static_cast<int>(gathered_complex_vec[j][i].a + gathered_complex_vec[j][i].b + gathered_complex_vec[j][i].c + gathered_complex_vec[j][i].d);
        fmt::print(stderr, "[rank {}] received sum {}\n", world.rank(), sum);
        CHECK(sum == 210 * world.size());
    }
}

int main(int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator world;

  Catch::Session session;

  SimpleFixture::nblocks = world.size();

  return session.run();
}
