#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/communicator.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

#include <diy/io/block.hpp>

#include "block.h"

// Compute average of local values
void local_average(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
  Block*        b = static_cast<Block*>(b_);
  diy::Link*    l = cp.link();

  int total = 0;
  for (unsigned i = 0; i < b->values.size(); ++i)
    total += b->values[i];

  std::cout << "Total     (" << cp.gid() << "): " << total        << std::endl;

  for (unsigned i = 0; i < l->size(); ++i)
  {
    //std::cout << "Enqueueing: " << cp.gid()
    //          << " -> (" << l->target(i).gid << "," << l->target(i).proc << ")" << std::endl;
    cp.enqueue(l->target(i), total);
  }

  cp.all_reduce(total, std::plus<int>());
}

// Average the values received from the neighbors
void average_neighbors(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
  Block*        b = static_cast<Block*>(b_);
  diy::Link*    l = cp.link();

  int all_total = cp.get<int>();
  std::cout << "All total (" << cp.gid() << "): " << all_total << std::endl;

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
  //int                       nblocks = 4*world.size();
  int                       nblocks = 128;
  diy::FileStorage          storage("./DIY.XXXXXX");
  diy::Communicator         comm(world);
  diy::Master               master(comm,
                                   &create_block,
                                   &destroy_block,
                                   8,
                                   4,
                                   &storage,
                                   &save_block,
                                   &load_block);

  //diy::ContiguousAssigner   assigner(world.size(), nblocks);
  diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  // creates a linear chain of blocks
  std::vector<int> gids;
  assigner.local_gids(comm.rank(), gids);
  for (unsigned i = 0; i < gids.size(); ++i)
  {
    int gid = gids[i];

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

  master.foreach(&local_average);
  comm.exchange();
  comm.flush();

  master.foreach(&average_neighbors);

  diy::io::write_blocks("blocks.out", world, master);
}
