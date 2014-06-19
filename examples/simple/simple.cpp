#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/communicator.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

struct Block
{
  std::vector<int>      values;
  float                 average;
  int                   all_total;
};

namespace diy
{
  template<>
  struct Serialization<Block>
  {
    static void save(BinaryBuffer& bb, const Block& b)
    { diy::save(bb, b.values); diy::save(bb, b.average); }

    static void load(BinaryBuffer& bb, Block& b)
    { diy::load(bb, b.values); diy::load(bb, b.average); }
  };
}

void* create_block()
{
  Block* b = new Block;
  return b;
}

void destroy_block(void* b)
{
  delete static_cast<Block*>(b);
}

void save_block(const void* b, diy::BinaryBuffer& bb)
{
  diy::save(bb, *static_cast<const Block*>(b));
}

void load_block(void* b, diy::BinaryBuffer& bb)
{
  diy::load(bb, *static_cast<Block*>(b));
}

// Compute average of local values
void local_average(void* b_, const diy::Master::ProxyWithLink& cp)
{
  Block*        b = static_cast<Block*>(b_);
  diy::Link*    l = cp.link();

  int total = 0;
  for (unsigned i = 0; i < b->values.size(); ++i)
    total += b->values[i];

  std::cout << "Total     (" << cp.gid() << "): " << total        << std::endl;

  for (unsigned i = 0; i < l->count(); ++i)
  {
    //std::cout << "Enqueueing: " << cp.gid()
    //          << " -> (" << l->target(i).gid << "," << l->target(i).proc << ")" << std::endl;
    cp.enqueue(l->target(i), total);
  }

  cp.all_reduce(total, offsetof(Block, all_total), std::plus<int>());
}

// Average the values received from the neighbors
void average_neighbors(void* b_, const diy::Master::ProxyWithLink& cp)
{
  Block*        b = static_cast<Block*>(b_);
  diy::Link*    l = cp.link();

  std::cout << "All total (" << cp.gid() << "): " << b->all_total << std::endl;

  std::vector<int> in;
  cp.incoming(in);

  int total = 0;
  for (unsigned i = 0; i < in.size(); ++i)
  {
    int v;
    cp.dequeue(in[i], v);
    total += v;
  }
  b->average = float(total) / in.size();
  std::cout << "Average   (" << cp.gid() << "): " << b->average   << std::endl;
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  int                       nblocks = 4*world.size();

  diy::FileStorage          storage("./DIY.XXXXXX");

  diy::Communicator         comm(world);
  diy::Master               master(comm,
                                   &create_block,
                                   &destroy_block,
                                   2,
                                   &storage,
                                   &save_block,
                                   &load_block);

  //diy::ContiguousAssigner   assigner(world.size(), nblocks);
  diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  // creates a linear chain of blocks
  for (unsigned gid = 0; gid < nblocks; ++gid)
  {
    if (assigner.rank(gid) == comm.rank())
    {
      diy::Link*    link = new diy::Link;
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

      Block* b = new Block;
      for (unsigned i = 0; i < 3; ++i)
      {
        b->values.push_back(gid*3 + i);
        //std::cout << gid << ": " << b->values.back() << std::endl;
      }
      master.add(gid, b, link);
    }
  }

  master.foreach(&local_average);
  comm.exchange();
  comm.flush();

  master.foreach(&average_neighbors);
}
