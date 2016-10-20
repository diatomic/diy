#include <iostream>

#include <diy/master.hpp>
#include <diy/io/block.hpp>

#include "block.h"

void output(Block* b, const diy::Master::ProxyWithLink& cp)
{
  std::cout << cp.gid() << " " << b->average << std::endl;
  for (int i = 0; i < cp.link()->size(); ++i)
    std::cout << "  " << cp.link()->target(i).gid << " " << cp.link()->target(i).proc << std::endl;
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  diy::Master               master(world, 1, -1,
                                   &create_block,           // master will take ownership after read_blocks(),
                                   &destroy_block);         // so it needs create and destroy functions
  diy::ContiguousAssigner   assigner(world.size(), 0);
  //diy::RoundRobinAssigner   assigner(world.size(), 0);      // nblocks will be filled by read_blocks()

  diy::io::read_blocks("blocks.out", world, assigner, master, &load_block);

  master.foreach(&output);
}
