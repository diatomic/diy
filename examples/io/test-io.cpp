#include <fstream>
#include <iostream>

#include <diy/types.hpp>
#include <diy/io/bov.hpp>

int main()
{
  std::vector<unsigned> shape;
  shape.push_back(16);
  shape.push_back(16);

  diy::DiscreteBounds box;
  box.min[0] = box.min[1] = 4;
  box.max[0] = box.max[1] = 7;

  std::ifstream in("test.bin", std::ios::binary);
  diy::io::BOVReader reader(in, shape);

  std::cout << "Reading" << std::endl;
  std::vector<float> data(16);
  reader.read(box, &data[0]);

  for (unsigned i = 0; i < data.size(); ++i)
    std::cout << data[i] << std::endl;
}
