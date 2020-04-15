#include    <iostream>

#include    <diy/mpi.hpp>
#include    <diy/log.hpp>

#include    "opts.h"

#define     CATCH_CONFIG_RUNNER
#include    "catch.hpp"

struct SimpleFixture
{
  static int width;
  diy::mpi::communicator world;
};

int SimpleFixture::width   = 4;

TEST_CASE_METHOD(SimpleFixture, "MPI Window Test", "[mpi-window]")
{
    diy::mpi::window<int> window(world, width);

    int rank = world.rank();

    window.lock_all(diy::mpi::nocheck);

    // put the values
    int target_rank = (rank + 2) % world.size();
    std::vector<int> out(width);
    for (int i = 0; i < width; ++i)
    {
        out[i] = target_rank * width + i;
        window.put(out[i], target_rank, i);
        //window.replace(out[i], target_rank, i);
    }
    window.flush(target_rank);

    world.barrier();        // we need to synchronize since we'll be reading from a different window than we wrote

    // get the values
    int source_rank = (rank + 1) % world.size();
    std::vector<int> values(width);
    for (int i = 0; i < width; ++i)
    {
        window.get(values[i], source_rank, i);
        //window.fetch(values[i], source_rank, i);
    }
    window.flush_local(source_rank);

    for (int i = 0; i < width; ++i)
        CHECK(values[i] == source_rank*width + i);

    window.unlock_all();
}

int main(int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator world;

  Catch::Session session;

  using namespace opts;
  Options ops;

  bool help;
  ops
      >> Option('w', "width", SimpleFixture::width, "per process width of the window")
      >> Option('h', "help",  help,                 "show help")
  ;

  if (!ops.parse(argc,argv) || help)
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
          std::cout << ops;
      }
      return 1;
  }

  return session.run();
}
