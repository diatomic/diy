#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/cast.h>
namespace py = pybind11;

#include <diy/io/block.hpp>
using namespace diy;

#include "serialization.h"

void init_io(py::module& m)
{
    using namespace py::literals;

    m.def("write_blocks",    [](const std::string& outfilename,
                                Master& master,
                                py::object extra,
                                py::object save_)
            {
                MemoryBuffer extra_bb;
                diy::save(extra_bb, extra);
                if (save_.is(py::none()))
                    io::write_blocks(outfilename, master.communicator(), master, extra_bb);
                else
                {
                    io::write_blocks(outfilename, master.communicator(), master, extra_bb,
                                     [save_](const void* b, diy::BinaryBuffer& bb)
                                     {
                                         save_(static_cast<const py::object*>(b), &bb);
                                     });
                }
            }, "outfilename"_a, "master"_a, "extra"_a = py::none(), "save"_a = py::none());

     m.def("read_blocks",     [](const std::string& infilename,
                                 StaticAssigner& assigner,
                                 Master& master,
                                 py::object load_)
             {
                MemoryBuffer extra_bb;
                if (load_.is(py::none()))
                    io::read_blocks(infilename, master.communicator(), assigner, master, extra_bb);
                else
                {
                    io::read_blocks(infilename, master.communicator(), assigner, master, extra_bb,
                                    [load_](void* b_, diy::BinaryBuffer& bb)
                                    {
                                        auto* b = static_cast<py::object*>(b_);
                                        *b = load_(&bb);
                                    });
                }
                py::object extra = py::none();
                if (extra_bb)
                    diy::load(extra_bb, extra);
                return extra;
             }, "infilename"_a, "assigner"_a, "master"_a, "load"_a = py::none());

    py::class_<diy::BinaryBuffer>(m, "BinaryBuffer");
}
