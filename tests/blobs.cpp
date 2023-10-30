#include <iostream>
#include <random>
#include <vector>

#include <diy/mpi.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

#include "opts.h"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"


struct Block
{
  std::vector<std::int64_t> buffer;
};

void *create_block()
{
  return new Block;
}

void destroy_block(void *block)
{
  delete static_cast<Block*>(block);
}


void local_sum(Block* b, const diy::Master::ProxyWithLink& cp)
{
    std::default_random_engine generator(cp.gid());
    std::uniform_int_distribution<std::int64_t> distribution(-9, 9);

    std::int64_t sum = 0;
    for (auto &v : b->buffer)
    {
      v = distribution(generator);
      sum += v;
    }

    diy::Link* l = cp.link();
    for (int i = 0; i < l->size(); ++i)
    {
      cp.enqueue_blob(l->target(i), (const char*) b->buffer.data(), b->buffer.size() * sizeof(std::int64_t));
      cp.enqueue(l->target(i), sum);
    }
}

void verify(Block*, const diy::Master::ProxyWithLink& cp)
{
  for (auto& in : *cp.incoming())
  {
    std::int64_t recvd_sum;

    auto blob = cp.dequeue_blob(in.first);
    cp.dequeue(in.first, recvd_sum);

    std::int64_t* buffer = (std::int64_t*) blob.pointer.get();
    size_t n = blob.size / sizeof(std::int64_t);

    std::int64_t comp_sum = 0;
    bool valid_values = true;
    for (size_t i = 0; i < n; ++i)
    {
      std::int64_t v = buffer[i];
      if (v < -9 || v > 9)
      {
        valid_values = false;
        break;
      }
      comp_sum += v;
    }
    CHECK(valid_values == true);
    CHECK(recvd_sum == comp_sum);
  }
}


struct BlobsFixture
{
  static int nblocks;
  static int threads;
  diy::mpi::communicator world;
};

int BlobsFixture::nblocks = 0;
int BlobsFixture::threads = 2;

TEST_CASE_METHOD(BlobsFixture, "Send/Recv Test", "[blobs]")
{
  diy::Master master(world, threads, -1, &create_block, &destroy_block);
  diy::RoundRobinAssigner assigner(world.size(), nblocks);

  // messages need to be <2GB since binary blobs currently don't support multi-part messages
  static const std::size_t sizes[] = { 128, 64, 32, 64, 32, 32, 64, 32, 32 };
  static const int num_sizes = sizeof(sizes)/sizeof(sizes[0]);

  std::vector<std::size_t> block_sizes(nblocks);
  for (int i = 0; i < nblocks; ++i)
  {
    block_sizes[i] = sizes[i%num_sizes];
  }
  std::shuffle(block_sizes.begin(), block_sizes.end(), std::default_random_engine(nblocks));

  if (world.rank() == 0)
  {
    std::cout << "Testing with: " << std::endl
              << "world.size: " << world.size() << std::endl
              << "nthreads: " << threads << std::endl
              << "nblocks: " << nblocks << std::endl
              << "block sizes (MB): ";
    for (auto v : block_sizes)
    {
      std::cout << v * sizeof(std::int64_t) << " ";
    }
    std::cout << std::endl;
  }

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

    Block* b = static_cast<Block*>(create_block());
    b->buffer.resize(block_sizes[gid] * 1024 * 1024);
    master.add(gid, b, link);
  }

  // this is the same as the default provided in Master, but giving it here as an example
  diy::MemoryManagement mem
  {
      [](int /* gid */, size_t n)   { return new char[n]; },      // allocate
      [](const char* p)             { delete[] p; },              // deallocate
      [](char* dest, const char* src, size_t count) { std::memcpy(dest, src, count); }      // memcopy
  };

  master.foreach(&local_sum);
  master.exchange(false, mem);
  master.foreach(&verify);
}

int main(int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator world;

  Catch::Session session;

  BlobsFixture::nblocks = (std::max)(world.size(), 2);
  bool help;

  std::string log_level   = "info";

  // get command line arguments
  using namespace opts;
  Options ops;
  ops >> Option('b', "blocks", BlobsFixture::nblocks, "number of blocks")
      >> Option('t', "thread", BlobsFixture::threads, "number of threads")
      >> Option('l', "log",    log_level,              "log level")
      >> Option('h', "help",   help,                   "show help");
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
