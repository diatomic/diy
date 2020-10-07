#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <diy/link.hpp>
using namespace diy;

void init_link(py::module& m)
{
    py::class_<Link> link_class(m, "Link");
    link_class
        .def(py::init<>())
        .def("target",  [](const Link& l, int i) { return l.target(i); })
        .def("__len__", &Link::size)
    ;

    py::class_<RegularGridLink>(m, "RegularGridLink", link_class)
        .def(py::init<>())      // temporary addition (for io-example.py); shouldn't need this in general
        .def("direction",  [](const RegularGridLink& l, Direction dir) { return l.direction(dir); })
        .def("direction",  [](const RegularGridLink& l, int i)         { return (l.direction(i)); })
        .def("__repr__", [](const RegularGridLink&) { return "RegularGridLink"; })
    ;

    py::class_<RegularContinuousLink>(m, "RegularContinuousLink", link_class)
        .def("direction",  [](const RegularContinuousLink& l, Direction dir) { return l.direction(dir); })
        .def("direction",  [](const RegularContinuousLink& l, int i)         { return (l.direction(i)); })
        .def("__repr__", [](const RegularContinuousLink&) { return "RegularContinuousLink"; })
    ;

    // TP: I added this
    py::class_<RegularLink<Bounds<double>>>(m, "DoubleRegularContinuousLink", link_class)
        .def("direction",  [](const RegularLink<Bounds<double>>& l, Direction dir) { return l.direction(dir); })
        .def("direction",  [](const RegularLink<Bounds<double>>& l, int i)         { return (l.direction(i)); })
        .def("__repr__", [](const RegularLink<Bounds<double>>&) { return "DoubleRegularContinuousLink"; })
    ;
}
