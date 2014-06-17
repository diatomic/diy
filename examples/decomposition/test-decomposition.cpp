#include <vector>
#include <iostream>

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

typedef     diy::DiscreteBounds         Bounds;
//typedef     diy::ContinuousBounds       Bounds;

void create(const Bounds& bounds, const diy::Link& link)
{
  std::cout << "   "
            << "Creating block: "
            << bounds.min[0] << ' ' << bounds.min[1] << ' ' << bounds.min[2] << " - "
            << bounds.max[0] << ' ' << bounds.max[1] << ' ' << bounds.max[2] << ": "
            << link.count() << std::endl;
}

int main(int argc, char* argv[])
{
  int                       size    = 8;
  int                       nblocks = 4*size;
  //diy::ContiguousAssigner   assigner(size, nblocks);
  diy::RoundRobinAssigner   assigner(size, nblocks);

  Bounds domain;
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 127;
  //domain.max[0] = domain.max[1] = domain.max[2] = 128;

  for (int rank = 0; rank < size; ++ rank)
  {
    std::cout << "Rank " << rank << ":" << std::endl;
    diy::decompose(3, rank, domain, assigner, create);
  }
}

