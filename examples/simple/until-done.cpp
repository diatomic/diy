#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/serialization.hpp>

struct Block
{
  int   count;

        Block(): count(0)                   {}
};

void*   create_block()                      { return new Block; }
void    destroy_block(void* b)              { delete static_cast<Block*>(b); }
void    save_block(const void* b,
                   diy::BinaryBuffer& bb)   { diy::save(bb, *static_cast<const Block*>(b)); }
void    load_block(void* b,
                   diy::BinaryBuffer& bb)   { diy::load(bb, *static_cast<Block*>(b)); }

void flip_coin(Block* b, const diy::Master::ProxyWithLink& cp)
{
  b->count++;
  int done = rand() % 2;
  //std::cout << cp.gid() << "  " << done << " " << b->count << std::endl;
  cp.collectives()->clear();
  cp.all_reduce(done, std::logical_and<int>());
}

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  int                       nblocks = 4*world.size();

  diy::FileStorage          storage("./DIY.XXXXXX");

  diy::Master               master(world,
                                   -1,
                                   2,
                                   &create_block,
                                   &destroy_block,
                                   &storage,
                                   &save_block,
                                   &load_block);

  srand(static_cast<unsigned int>(time(NULL)));

  //diy::ContiguousAssigner   assigner(world.size(), nblocks);
  diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  for (int gid = 0; gid < nblocks; ++gid)
    if (assigner.rank(gid) == world.rank())
      master.add(gid, new Block, new diy::Link);

  bool all_done = false;
  while (!all_done)
  {
    master.foreach(&flip_coin);
    master.exchange();
    all_done = master.proxy(master.loaded_block()).read<bool>();
  }

  if (world.rank() == 0)
    std::cout << "Total iterations: " << master.block<Block>(master.loaded_block())->count << std::endl;
}

