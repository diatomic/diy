//
// block structure
// the contents of a block are completely user-defined
//
struct Block
{
    std::vector<int>      values;
    float                 average;
};

//
// When a block has mutliple fields of different types, diy needs a little more information
// about how to serialize the block. This is done by overloading the save and load functions
// of the diy::Serialization object. Usually, this amounts to listing which simple types are
// included; diy knows how to serialize simple types.
//
namespace diy
{
    template<>
        struct Serialization<Block>
    {
        static void save(BinaryBuffer& bb, const Block& b)
        {
            diy::save(bb, b.values);
            diy::save(bb, b.average);
        }

        static void load(BinaryBuffer& bb, Block& b)
        {
            diy::load(bb, b.values);
            diy::load(bb, b.average);
        }
    };
}

//
// Functions must be defined to create, destroy, save, and load a block.
// Create and destroy allocate and free the block, while
// save and load serialize and deserialize the block.
// These four functions are called when blocks are cycled in- and out-of-core.
// They could have been member functions of the block above, or separate, as below
//
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
