#include <diy/master.hpp>
#include <diy/io/block.hpp>

#include "block.h"

void output(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
  Block*        b = static_cast<Block*>(b_);
  std::cout << cp.gid() << " " << b->average << std::endl;
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  diy::Communicator         comm(world);
  diy::Master               master(comm,
                                   &create_block,
                                   &destroy_block,
                                   -1,
                                   0,
                                   &save_block,
                                   &load_block);
  diy::RoundRobinAssigner   assigner(world.size(), 0);      // nblocks will be filled by read_blocks()

  diy::io::read_blocks("blocks.out", world, assigner, master);

  master.foreach(&output);
}
