#include <diy/master.hpp>

#include "opts.h"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

struct Block
{};

struct DoubleForeachFixture
{
  static int nblocks;
  static unsigned int iter;
  diy::mpi::communicator world;
};

int DoubleForeachFixture::nblocks = 0;
unsigned int DoubleForeachFixture::iter    = 2;

TEST_CASE_METHOD(DoubleForeachFixture, "Send/Receive", "[double-foreach]")
{
  diy::Master master(world);
  diy::RoundRobinAssigner assigner(world.size(), nblocks);

  // create a linear chain of blocks
  std::vector<int> gids;
  assigner.local_gids(world.rank(), gids);
  for (unsigned i = 0; i < gids.size(); ++i)
  {
    int gid = gids[i];

    diy::Link* link = new diy::Link;
    diy::BlockID  neighbor;
    if (gid < nblocks - 1)
    {
      neighbor.gid  = gid + 1;
      neighbor.proc = assigner.rank(neighbor.gid);
      link->add_neighbor(neighbor);
    }
    if (gid > 0)
    {
      neighbor.gid  = gid - 1;
      neighbor.proc = assigner.rank(neighbor.gid);
      link->add_neighbor(neighbor);
    }

    master.add(gid, new Block, link);
  }

  for (unsigned i = 0; i < iter; ++i)
      master.foreach([i](Block*, const diy::Master::ProxyWithLink& cp)
      {
        for (auto target : cp.link()->neighbors())
            cp.enqueue(target, i);
      });
  master.exchange();

  master.foreach([](Block*, const diy::Master::ProxyWithLink& cp)
  {
    for (auto target : cp.link()->neighbors())
    {
        unsigned x; unsigned count = 0;
        while (cp.incoming(target.gid))
        {
            cp.dequeue(target.gid, x);
            ++count;
        }
        CHECK(count == iter);
    }
  });
}

int main(int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator world;

  Catch::Session session;

  DoubleForeachFixture::nblocks = world.size();
  bool help;

  std::string log_level   = "info";

  // get command line arguments
  using namespace opts;
  Options ops;
  ops >> Option('b', "blocks", DoubleForeachFixture::nblocks,   "number of blocks")
      >> Option('i', "iter",   DoubleForeachFixture::iter,      "number of iterations")
      >> Option('l', "log",    log_level,                       "log level")
      >> Option('h', "help",   help,                            "show help");
  if (!ops.parse(argc,argv) || help)
  {
    if (world.rank() == 0)
    {
      std::cout << ops;
      return 1;
    }
  }

  diy::create_logger(log_level);

  return session.run();
}
