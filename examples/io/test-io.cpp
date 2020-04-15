#include <fstream>
#include <iostream>

#include <diy/types.hpp>
#include <diy/io/bov.hpp>
#include <diy/io/numpy.hpp>

#include <diy/mpi.hpp>

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  std::vector<unsigned> shape;
  shape.push_back(16);
  shape.push_back(16);

  diy::DiscreteBounds box(2);
  box.min[0] = box.min[1] = 4;
  box.max[0] = box.max[1] = 7;

  diy::mpi::io::file in(world, "test.bin", diy::mpi::io::file::rdonly);
  diy::io::BOV reader(in, shape);

  std::cout << "Reading" << std::endl;
  std::vector<float> data(16);
  reader.read(box, &data[0]);

  for (unsigned i = 0; i < data.size(); ++i)
    std::cout << data[i] << std::endl;

  std::cout << "---" << std::endl;
  diy::mpi::io::file in2(world, "test.npy", diy::mpi::io::file::rdonly);
  diy::io::NumPy  reader2(in2);
  reader2.read_header();
  std::vector<float> data2(16);
  reader2.read(box, &data2[0]);

  for (unsigned i = 0; i < data2.size(); ++i)
    std::cout << data2[i] << std::endl;

  diy::mpi::io::file out(world, "out.npy", diy::mpi::io::file::wronly | diy::mpi::io::file::create);
  diy::io::NumPy     writer(out);
  diy::DiscreteBounds full_box(2), sub_box(2);
  full_box.min[0] = full_box.min[1] = 0;
  full_box.max[0] = full_box.max[1] = 3;
  sub_box.min[0] = sub_box.min[1] = 1;
  sub_box.max[0] = sub_box.max[1] = 2;
  writer.write_header<float>(2, full_box);
  //writer.write(full_box, &data2[0]);
  writer.write(full_box, &data2[0], sub_box);
}
