#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/reduce.hpp>
#include <diy/reduce-operations.hpp>
#include <diy/partners/swap.hpp>
#include <diy/assigner.hpp>
#include <diy/algorithms.hpp>

#include "opts.h"

static const int kd_x = 0;
static const int kd_y = 1;
static const int kd_z = 2;

struct PointT
{
  float coords[3];

  float& operator[](unsigned i) { return coords[i]; }
  float operator[](unsigned i) const { return coords[i]; }
};

struct BlockT
{
  std::vector<PointT> Points;

  void AddPoints(std::vector<PointT>&& pts)
  {
    this->Points = pts;
  }
};

diy::ContinuousBounds compute_bounds(std::vector<PointT> points)
{
  diy::ContinuousBounds bds(3);
  bds.min[kd_x] = points[0][kd_x];
  bds.min[kd_y] = points[0][kd_y];
  bds.min[kd_z] = points[0][kd_z];
  bds.max[kd_x] = points[0][kd_x];
  bds.max[kd_y] = points[0][kd_y];
  bds.max[kd_z] = points[0][kd_z];
  // just need to find min and max of each coordinate axis
  for (const auto pt : points)
  {
    if (pt[kd_x] < bds.min[kd_x])
      bds.min[kd_x] = pt[kd_x];
    if (pt[kd_y] < bds.min[kd_y])
      bds.min[kd_y] = pt[kd_y];
    if (pt[kd_z] < bds.min[kd_z])
      bds.min[kd_z] = pt[kd_z];

    if (pt[kd_x] > bds.max[kd_x])
      bds.max[kd_x] = pt[kd_x];
    if (pt[kd_y] > bds.max[kd_y])
      bds.max[kd_y] = pt[kd_y];
    if (pt[kd_z] > bds.max[kd_z])
      bds.max[kd_z] = pt[kd_z];
  }

  return bds;
}

std::vector<PointT> read_csv(const char *filename)
{
  std::vector<PointT> points;
  std::ifstream ifs(filename);

  char line[256];
  while (!ifs.eof())
  {
    ifs.getline(line, 256);
    PointT p;
    char* token = strtok(line, ",");
    int i = 0;
    while (token != nullptr)
    {
      p.coords[i++] = std::strtof(token, nullptr);
      token = strtok(nullptr, ",");
    }
    points.push_back(p);
  }

  ifs.close();
  return points;
}

int main (int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator comm;

  bool help;
  int nblocks = 1;
  int bins = 128;
  char filename[512];

  // get command line arguments
  using namespace opts;
  Options ops;
  ops
    >> Option('b', "blocks",    nblocks,        "number of blocks")
    >> Option('f', "filename",  filename,       "csv of points")
    >> Option('n', "bins",      bins,           "number of bins")
    >> Option('h', "help",      help,           "show help")
    ;

  if (!ops.parse(argc,argv) || help)
  {
    if (comm.rank() == 0)
    {
      std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
      std::cout << "Tests Kd tree with dataset where most points are located very closely.\n";
      std::cout << ops;
    }
    return 1;
  }

  diy::Master master(comm, 1, -1, []() { return static_cast<void*>(new BlockT); },
    [](void* b) { delete static_cast<BlockT*>(b); });

  diy::ContiguousAssigner cuts_assigner(comm.size(), nblocks);

  auto points = read_csv(filename);

  const auto gdomain = compute_bounds(points);

  std::vector<int> gids;
  cuts_assigner.local_gids(comm.rank(), gids);
  for (const int gid : gids)
  {
    auto block = new BlockT();
    if (gid == gids[0])
    {
      block->AddPoints(std::move(points));
    }
    auto link = new diy::RegularContinuousLink(3, gdomain, gdomain);
    master.add(gid, block, link);
  }

  diy::kdtree(master, cuts_assigner, 3, gdomain, &BlockT::Points, bins);

  return 0;
}
