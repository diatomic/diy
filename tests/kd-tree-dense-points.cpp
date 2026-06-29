#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cctype>
#include <stdexcept>

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

std::vector<PointT> read_csv(const std::string& filename)
{
  std::vector<PointT> points;
  std::ifstream ifs(filename);
  if (!ifs)
    throw std::runtime_error("Failed to open " + filename);

  std::string line;
  while (std::getline(ifs, line))
  {
    bool blank = true;
    for (char c : line)
      if (!std::isspace(static_cast<unsigned char>(c)))
        blank = false;
    if (blank)
      continue;

    PointT p;

    size_t start = 0;
    int i = 0;
    while (start <= line.size())
    {
      if (i == 3)
        throw std::runtime_error("Too many coordinates in " + filename);

      size_t end = line.find(',', start);
      std::string token = line.substr(start, end == std::string::npos ? std::string::npos : end - start);

      char* parse_end = nullptr;
      p.coords[i] = std::strtof(token.c_str(), &parse_end);
      while (parse_end != nullptr && *parse_end != '\0' && std::isspace(static_cast<unsigned char>(*parse_end)))
        ++parse_end;
      if (parse_end == token.c_str() || *parse_end != '\0')
        throw std::runtime_error("Invalid coordinate in " + filename + ": " + token);

      ++i;
      if (end == std::string::npos)
        break;
      start = end + 1;
    }

    if (i == 0)
      continue;
    if (i != 3)
      throw std::runtime_error("Expected three coordinates in " + filename);

    points.push_back(p);
  }

  if (points.empty())
    throw std::runtime_error("No points loaded from " + filename);

  return points;
}

int main (int argc, char* argv[])
{
  diy::mpi::environment env(argc, argv);
  diy::mpi::communicator comm;

  bool help;
  int nblocks = 1;
  int bins = 128;
  std::string filename = "";

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

  try
  {
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
  } catch (const std::exception& e)
  {
    if (comm.rank() == 0)
      std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
