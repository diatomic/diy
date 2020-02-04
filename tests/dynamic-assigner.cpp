#include    <iostream>
#include    <vector>
#include    <tuple>

#include    <diy/assigner.hpp>
#include    <diy/mpi.hpp>
#include    <diy/log.hpp>

#include    "opts.h"

#define     CATCH_CONFIG_RUNNER
#include    "catch.hpp"

struct SimpleFixture
{
    static int nblocks;
    diy::mpi::communicator world;
};
int SimpleFixture::nblocks = 0;

TEST_CASE_METHOD(SimpleFixture, "Dynamic Assigner Test", "[dynamic-assigner]")
{
    auto log = diy::get_logger();

    // select gids for which we will choose the random ranks
    diy::ContiguousAssigner contiguous(world.size(), nblocks);
    std::vector<int> gids;
    contiguous.local_gids(world.rank(), gids);

    diy::DynamicAssigner dynamic(world, world.size(), nblocks);

    // global window of rank assignments, used for checking
    std::vector<int> ranks(nblocks, 0);
    std::vector<int> all_ranks;

    // generate rank assignment for the gids
    std::vector<std::tuple<int,int>>    rank_gids;
    for (int gid : gids)
    {
        int rank = (world.rank() + 1) % world.size();
        rank_gids.emplace_back(rank, gid);
        ranks[gid] = rank;
        log->debug("[{}] Generated {} -> {}", world.rank(), gid, rank);
    }

    // all_reduce the rank assignments
    diy::mpi::all_reduce(world, ranks, all_ranks, std::plus<int>());        // std::plus works because ranks that we did not set are initialized to 0

    dynamic.set_ranks(rank_gids);

    // need to separate the setting and getting of the ranks into separate
    // stages (that's how it's meant to be used normally)
    world.barrier();

    // check that every rank has the same correct view
    for (int gid = 0; gid < nblocks; ++gid)
    {
        int rank = dynamic.rank(gid);
        CAPTURE(world.rank());
        CAPTURE(gid);
        CHECK(rank == all_ranks[gid]);
    }
}

int main(int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator world;

  auto log = diy::create_logger("debug");

  Catch::Session session;

  using namespace opts;
  Options ops;

  SimpleFixture::nblocks = world.size() * 8 + 3;
  bool help;
  ops
      >> Option('n', "nblocks", SimpleFixture::nblocks, "total number of blocks")
      >> Option('h', "help",    help,                   "show help")
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
