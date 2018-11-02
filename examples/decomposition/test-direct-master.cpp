#include <vector>
#include <iostream>
#include <bitset>

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/master.hpp>

typedef     diy::DiscreteBounds         Bounds;
//typedef     diy::ContinuousBounds       Bounds;

struct Block
{
    static void*    create()            { return new Block; }
    static void     destroy(void* b)    { delete static_cast<Block*>(b); }

    void show_link(const diy::Master::ProxyWithLink& cp)
    {
      diy::RegularLink<Bounds>* link = static_cast<diy::RegularLink<Bounds>*>(cp.link());
      std::cout << "Block (" << cp.gid() << "): "
                << link->core().min[0]   << ' ' << link->core().min[1]   << ' ' << link->core().min[2] << " - "
                << link->core().max[0]   << ' ' << link->core().max[1]   << ' ' << link->core().max[2] << " : "
                << link->bounds().min[0] << ' ' << link->bounds().min[1] << ' ' << link->bounds().min[2] << " - "
                << link->bounds().max[0] << ' ' << link->bounds().max[1] << ' ' << link->bounds().max[2] << " : "
                << link->size()   << ' ' //<< std::endl
                << std::dec
                << std::endl;
    }
};

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  int                       size    = 8;
  int                       nblocks = 32;
  diy::ContiguousAssigner   assigner(size, nblocks);
  //diy::RoundRobinAssigner   assigner(size, nblocks);

  Bounds domain(3);
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 255;
  //domain.max[0] = domain.max[1] = domain.max[2] = 128;

  int rank = world.rank();
  std::cout << "Rank " << rank << ":" << std::endl;
  diy::Master master(world, 1, -1, &Block::create, &Block::destroy);

  diy::RegularDecomposer<Bounds>::BoolVector          share_face;
  share_face.push_back(true);
  diy::RegularDecomposer<Bounds>::BoolVector          wrap;
  wrap.push_back(true);
  wrap.push_back(true);
  diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;
  ghosts.push_back(1); ghosts.push_back(2);
  diy::decompose(3, rank, domain, assigner, master, share_face, wrap, ghosts);

  master.foreach(&Block::show_link);
}


