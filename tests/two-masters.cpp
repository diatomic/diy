// Sujin's example with cross-talk between two masters

#include <diy/master.hpp>
#include <diy/mpi.hpp>

#include <cassert>
#include <iostream>
#include <vector>

template <typename T>
inline void PrintVector(std::ostream& out, const std::vector<T>& vec)
{
  out << "size = " << vec.size() << ", values = ";
  for (const T& val : vec)
  {
    out << val << " ";
  }
  out << "\n";
}

template <typename T>
void TestEqual(const std::vector<T>& v1, const std::vector<T>& v2)
{
  bool passed = (v1.size() == v2.size());
  if (passed)
  {
    for (std::size_t i = 0; i < v1.size(); ++i)
    {
      if(v1[i] != v2[i])
      {
        passed = false;
        break;
      }
    }
  }

  if (!passed)
  {
    std::cout << "v1: ";
    PrintVector(std::cout, v1);
    std::cout << "v2: ";
    PrintVector(std::cout, v2);
    abort();
  }
}

//-----------------------------------------------------------------------------
template <typename T>
struct Block
{
  T send;
  T received;
};

template <typename T>
void TestSerializationImpl(const T& obj)
{
  diy::mpi::communicator comm;
  Block<T> block;

  {
  diy::Master master(comm);

  auto nblocks = comm.size();
  diy::RoundRobinAssigner assigner(comm.size(), nblocks);

  std::vector<int> gids;
  assigner.local_gids(comm.rank(), gids);
  assert(gids.size() == 1);
  auto gid = gids[0];

  diy::Link* link = new diy::Link;
  diy::BlockID neighbor;

  // send neighbor
  neighbor.gid = (gid < (nblocks - 1)) ? (gid + 1) : 0;
  neighbor.proc = assigner.rank(neighbor.gid);
  link->add_neighbor(neighbor);

  // recv neighbor
  neighbor.gid = (gid > 0) ? (gid - 1) : (nblocks - 1);
  neighbor.proc = assigner.rank(neighbor.gid);
  link->add_neighbor(neighbor);

  block.send = obj;
  master.add(gid, &block, link);

  // compute, exchange, compute
  master.foreach([](Block<T> *b, const diy::Master::ProxyWithLink& cp) {
    cp.enqueue(cp.link()->target(0), b->send);
  });
  master.exchange();
  master.foreach([](Block<T> *b, const diy::Master::ProxyWithLink& cp) {
    cp.dequeue(cp.link()->target(1).gid, b->received);
  });

  //comm.barrier();
  }

  TestEqual(block.send, block.received);
}

//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  diy::mpi::environment mpienv(argc, argv);

  const std::size_t ArraySize = 10;

  for (int i = 0; i < 2; ++i)
  {
    std::vector<float> array(ArraySize);

    const float step = 0.73f;
    float curval = 1.33f * static_cast<float>(i);
    for (auto& v : array)
    {
      v = curval;
      curval += step;
    }

    TestSerializationImpl(array);
  }

  std::cout << "Test completed successfuly\n";
  return 0;
}
