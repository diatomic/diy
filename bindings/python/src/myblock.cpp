#include <iostream>
#include <diy/serialization.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
namespace py = pybind11;

struct MyBlock { int x; };

#include <diy/master.hpp>
#include <diy/link.hpp>

namespace diy
{

template<>
struct Serialization<MyBlock>
{
    static void save(BinaryBuffer& bb, const MyBlock& o)
    {
        diy::save(bb, o.x);
    }

    static void load(BinaryBuffer& bb, MyBlock& o)
    {
        diy::load(bb, o.x);
    }
};

}

void init_myblock(py::module& m)
{
    py::class_<MyBlock>(m, "MyBlock")
        .def(py::init())
        .def_readwrite("x", &MyBlock::x)
    ;

    m.def("my_save_block", [](const py::object* b, diy::BinaryBuffer* bb)
                           {
                               std::cout << "my_save_block" << std::endl;
                               diy::save(*bb, *b->cast<MyBlock*>());
                           });
    m.def("my_load_block", [](diy::BinaryBuffer* bb)
                           {
                               std::cout << "my_load_block" << std::endl;
                               std::unique_ptr<MyBlock> b { new MyBlock };
                               diy::load(*bb, *b);
                               return b;
                           });

    m.def("add_my_block", [](diy::Master& m, int gid)
                          {
                              auto* b = new MyBlock { gid };
                              m.add(gid, new py::object(py::cast(b)), new diy::Link);
                          });
}
