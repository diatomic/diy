#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

struct PythonPartners: py::object
{
    using GidVector = std::vector<int>;

            PythonPartners(py::object o_):
                py::object(o_)   {}

    size_t  rounds() const      { return this->attr("rounds")().cast<size_t>(); }
    bool    active(int round, int gid, const Master& m) const   { return this->attr("active")(round,gid,m).cast<bool>(); }
    void    incoming(int round, int gid, GidVector& partners, const Master& m) const
    {
        partners = this->attr("incoming")(round, gid, m).cast<GidVector>();
    }
    void    outgoing(int round, int gid, GidVector& partners, const Master& m) const
    {
        partners = this->attr("outgoing")(round, gid, m).cast<GidVector>();
    }
};
