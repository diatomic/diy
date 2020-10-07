#pragma once

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <diy/serialization.hpp>

struct PyObjectSerialization
{
    static void save(diy::BinaryBuffer& bb, const py::object& o);
    static void load(diy::BinaryBuffer& bb, py::object& o);
};

namespace diy
{

template<>
struct Serialization<py::object>
{
    static void save(BinaryBuffer& bb, const py::object& o)
    {
        PyObjectSerialization::save(bb, o);
    }

    static void load(BinaryBuffer& bb, py::object& o)
    {
        PyObjectSerialization::load(bb, o);
    }
};

}
