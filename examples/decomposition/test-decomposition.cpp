#include <vector>
#include <iostream>
#include <bitset>

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

typedef     diy::DiscreteBounds         Bounds;
//typedef     diy::ContinuousBounds       Bounds;

void create(int gid, const Bounds& core, const Bounds& bounds, const Bounds&,
            const diy::Link& link)
{
  const diy::RegularLink<Bounds>& l = static_cast<const diy::RegularLink<Bounds>&>(link);
  std::cout << "   "
            << "Creating block (" << gid << "): "
            << core.min   << " - " << core.max   << " : "
            << bounds.min << " - " << bounds.max << " : "
            << link.size()   << ' ' //<< std::endl
            //<< std::bitset<32>(l.direction(0))   << std::endl
            //<< std::bitset<32>(l.direction(0) & l.wrap()) << std::endl
            //<< ((l.direction(0) & l.wrap()) == l.direction(0)) << ' '
            << std::dec
            << std::endl;

  for (int i = 0; i < l.size(); ++i)
  {
      std::cout << "      " << l.target(i).gid
                << "; direction = " << l.direction(i)
                << "; wrap = "      << l.wrap(i)
                << std::endl;
  }
}

int main(int, char* [])
{
  int                       size    = 8;
  int                       nblocks = 32;
  diy::ContiguousAssigner   assigner(size, nblocks);
  //diy::RoundRobinAssigner   assigner(size, nblocks);

  Bounds domain(3);
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 255;
  //domain.max[0] = domain.max[1] = domain.max[2] = 128;

  for (int rank = 0; rank < size; ++ rank)
  {
    std::cout << "Rank " << rank << ":" << std::endl;

    // share_face is an n-dim (size 3 in this example) vector of bools
    // indicating whether faces are shared in each dimension
    // uninitialized values default to false
    diy::RegularDecomposer<Bounds>::BoolVector          share_face;
    share_face.push_back(true);

    // wrap is an n-dim (size 3 in this example) vector of bools
    // indicating whether boundary conditions are periodic in each dimension
    // uninitialized values default to false
    diy::RegularDecomposer<Bounds>::BoolVector          wrap;
    wrap.push_back(true);
    wrap.push_back(true);

    // ghosts is an n-dim (size 3 in this example) vector of ints
    // indicating number of ghost cells per side in each dimension
    // uninitialized values default to 0
    diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;
    ghosts.push_back(1); ghosts.push_back(2);

    diy::decompose(3, rank, domain, assigner, create, share_face, wrap, ghosts);
  }
}

