#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <diy/master.hpp>
#include <diy/reduce.hpp>
using namespace diy;

#include "serialization.h"

void init_proxy(py::module& m)
{
    using Proxy = Master::Proxy;
    py::class_<Proxy> proxy_class(m, "Proxy");
    proxy_class
        .def("gid",     &Proxy::gid)
        .def("enqueue", [](Proxy& p, const BlockID& bid, py::object o)  { p.enqueue(bid, o); })
        .def("dequeue", [](Proxy& p, int gid)
                        {
                            py::object o;
                            p.dequeue(gid, o);
                            return o;
                        })
        .def("fill_incoming",   &Proxy::fill_incoming)
        .def("incoming",    [](const Proxy& p, int from) -> bool    { return p.incoming(from); })
        // TODO: need to test vector version
        .def("incoming",    [](const Proxy& p, std::vector<int>& v) { p.incoming(v); })
        .def("outgoing",    [](const Proxy& p)                      { p.outgoing(); })
        .def("outgoing",    [](const Proxy& p, const BlockID& to)   { p.outgoing(to); })
    ;

    using CommProxy = Master::ProxyWithLink;
    py::class_<CommProxy>(m, "ProxyWithLink", proxy_class)
        .def("link",    [](const CommProxy& cp) { return cp.link(); }, py::return_value_policy::reference_internal)
    ;

    using ReduceProxy = diy::ReduceProxy;
    py::class_<ReduceProxy>(m, "ReduceProxy", proxy_class)
        .def("in_link",     [](const ReduceProxy& rp) { return rp.in_link(); }, py::return_value_policy::reference_internal)
        .def("out_link",    [](const ReduceProxy& rp) { return rp.out_link(); }, py::return_value_policy::reference_internal)
        .def("block",       &ReduceProxy::block)
        .def("round",       &ReduceProxy::round)
        .def("set_round",   &ReduceProxy::set_round)
        .def("assigner",    &ReduceProxy::assigner)
        .def("nblocks",     &ReduceProxy::nblocks)
    ;
}
