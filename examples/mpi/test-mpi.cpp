#include <iostream>

#include <diy/mpi.hpp>

namespace mpi = diy::mpi;

int main(int argc, char** argv)
{
  mpi::environment  env(argc, argv);        // RAII
  mpi::communicator world;

  if (world.size() != 2)
  {
    std::cout << "Need exactly 2 processes" << std::endl;
    return 1;
  }

  std::vector<int>  vec;
  if (world.rank() == 0)
  {
    for(int i = 0; i < 6; ++i)
      vec.push_back(i*i);
    world.send(1,0,vec);
    int val = 42;
    mpi::broadcast(world, val, 0);

    std::vector<int> in; in.push_back(4); in.push_back(5);
    std::vector<int> total(in.size());
    mpi::reduce(world, in, total, 0, std::plus<int>());
    std::cout << "Sum:" << std::endl;
    for (size_t i = 0; i < total.size(); ++i)
      std::cout << "  " << total[i] << std::endl;
  }
  else if (world.rank() == 1)
  {
    world.recv(0,0,vec);
    std::cout << "Received: " << std::endl;
    for (size_t i = 0; i < vec.size(); ++i)
      std::cout << vec[i] << std::endl;
    int val;
    mpi::broadcast(world, val, 0);
    std::cout << "Received broadcast: " << val << std::endl;

    std::vector<int> in; in.push_back(14); in.push_back(9);
    mpi::reduce(world, in, 0, std::plus<int>());
  }

  int in = 3 + world.rank()*2;
  int out;
  mpi::scan(world, in, out, std::plus<int>());
  std::cout << "Scan (" << world.rank() << "): " << in << " -> " << out << std::endl;

  mpi::optional<mpi::status>  status = world.iprobe(mpi::any_source, mpi::any_tag);
  std::cout << "Messages pending (" << world.rank() << "): " << status << std::endl;

  std::cout << "all_gather:" << std::endl;
  std::vector<int> in_vec;
  in_vec.push_back(world.rank()*3 + 0);
  in_vec.push_back(world.rank()*3 + 1);
  in_vec.push_back(world.rank()*3 + 2);

  std::vector< std::vector<int> > all_vec;
  mpi::all_gather(world, in_vec, all_vec);
  for (size_t i = 0; i < all_vec.size(); ++i)
      for (size_t j = 0; j < all_vec[i].size(); ++j)
          std::cout << world.rank() << ": " << all_vec[i][j] << std::endl;
}
