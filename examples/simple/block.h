struct Block
{
  std::vector<int>      values;
  float                 average;
};

namespace diy
{
  template<>
  struct Serialization<Block>
  {
    static void save(BinaryBuffer& bb, const Block& b)
    { diy::save(bb, b.values); diy::save(bb, b.average); }

    static void load(BinaryBuffer& bb, Block& b)
    { diy::load(bb, b.values); diy::load(bb, b.average); }
  };
}

void* create_block()
{
  Block* b = new Block;
  return b;
}

void destroy_block(void* b)
{
  delete static_cast<Block*>(b);
}

void save_block(const void* b, diy::BinaryBuffer& bb)
{
  diy::save(bb, *static_cast<const Block*>(b));
}

void load_block(void* b, diy::BinaryBuffer& bb)
{
  diy::load(bb, *static_cast<Block*>(b));
}
