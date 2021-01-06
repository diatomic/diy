#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

#include <diy/assigner.hpp>
#include <diy/resolve.hpp>
using namespace diy;

void init_assigner(py::module& m)
{
    using namespace py::literals;

    py::class_<Assigner> assigner_class(m, "Assigner");
    assigner_class
        .def("rank",        &Assigner::rank)
        .def("size",        &Assigner::size)
        .def("nblocks",     &Assigner::nblocks)
        .def("set_nblocks", &Assigner::set_nblocks)
        .def("ranks",       &Assigner::ranks)
    ;

    py::class_<StaticAssigner> static_assigner_class(m, "StaticAssigner", assigner_class);
    static_assigner_class
        .def("local_gids",  [](const StaticAssigner& a, int rank)
                            {
                                std::vector<int> gids;
                                a.local_gids(rank, gids);
                                return gids;
                            })
    ;

    py::class_<ContiguousAssigner>(m, "ContiguousAssigner", static_assigner_class)
        .def(py::init<int, int>())
    ;

    py::class_<RoundRobinAssigner>(m, "RoundRobinAssigner", static_assigner_class)
        .def(py::init<int, int>())
    ;

    py::class_<DynamicAssigner> dynamic_assigner_class(m, "DynamicAssigner", assigner_class);
    dynamic_assigner_class
        .def(py::init<const mpi::communicator&, int, int>())
        .def("ranks",       &DynamicAssigner::ranks)
        .def("get_rank",    &DynamicAssigner::get_rank)
        .def("set_rank",    [](DynamicAssigner& a, const int& rk, int gid, bool flush)
                            {
                                return a.set_rank(rk, gid, flush);
                            }, "rk"_a, "gid"_a, "flush"_a = true)
        .def("set_ranks",   &DynamicAssigner::set_ranks)
        .def("rank_offset", &DynamicAssigner::rank_offset)
    ;

    m.def("fix_links", &fix_links);
    m.def("record_local_gids", &record_local_gids);
    m.def("update_links", &update_links);
}
