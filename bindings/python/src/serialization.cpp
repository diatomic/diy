#include "serialization.h"

#include <diy/serialization.hpp>
using namespace diy;

void PyObjectSerialization::save(BinaryBuffer& bb, const py::object& o)
{
    // TODO: find a way to avoid this overhead on every call
    py::module pickle = py::module::import("pickle");
    py::object dumps  = pickle.attr("dumps");

    py::bytes data = dumps(o);
    auto data_str = std::string(data);

    diy::save(bb, data_str);
}

void PyObjectSerialization::load(BinaryBuffer& bb, py::object& o)
{
    // TODO: find a way to avoid this overhead on every call
    py::module pickle = py::module::import("pickle");
    py::object loads  = pickle.attr("loads");

    std::string data_str;
    diy::load(bb, data_str);

    py::bytes data_bytes = data_str;
    o = loads(data_bytes);
}
