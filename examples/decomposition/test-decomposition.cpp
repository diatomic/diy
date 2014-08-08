#include <vector>
#include <iostream>
#include <bitset>

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

typedef     diy::DiscreteBounds         Bounds;
//typedef     diy::ContinuousBounds       Bounds;

void create(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain,
            const diy::Link& link)
{
  const diy::RegularLink<Bounds>& l = static_cast<const diy::RegularLink<Bounds>&>(link);
  std::cout << "   "
            << "Creating block (" << gid << "): "
            << core.min[0]   << ' ' << core.min[1]   << ' ' << core.min[2] << " - "
            << core.max[0]   << ' ' << core.max[1]   << ' ' << core.max[2] << " : "
            << bounds.min[0] << ' ' << bounds.min[1] << ' ' << bounds.min[2] << " - "
            << bounds.max[0] << ' ' << bounds.max[1] << ' ' << bounds.max[2] << " : "
            << link.count()  << ' ' //<< std::endl
            << std::bitset<32>(l.wrap()) //<< std::endl
            //<< std::bitset<32>(l.direction(0))   << std::endl
            //<< std::bitset<32>(l.direction(0) & l.wrap()) << std::endl
            //<< ((l.direction(0) & l.wrap()) == l.direction(0)) << ' '
            << std::dec
            << std::endl;

}

int main(int argc, char* argv[])
{
  int                       size    = 8;
  int                       nblocks = 32;
  diy::ContiguousAssigner   assigner(size, nblocks);
  //diy::RoundRobinAssigner   assigner(size, nblocks);

  Bounds domain;
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 255;
  //domain.max[0] = domain.max[1] = domain.max[2] = 128;

  for (int rank = 0; rank < size; ++ rank)
  {
    std::cout << "Rank " << rank << ":" << std::endl;
    diy::RegularDecomposer<Bounds>::BoolVector          share_face;
    share_face.push_back(true);
    diy::RegularDecomposer<Bounds>::BoolVector          wrap;
    wrap.push_back(true);
    wrap.push_back(true);
    diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;
    ghosts.push_back(1); ghosts.push_back(2);
    diy::decompose(3, rank, domain, assigner, create, share_face, wrap, ghosts);
  }
}

