#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

#include <diy/partners/merge.hpp>
#include <diy/partners/swap.hpp>
#include <diy/partners/broadcast.hpp>
#include <diy/partners/all-reduce.hpp>
#include <diy/partners/common.hpp>

using namespace diy;

void init_partners(py::module& m)
{
    // partners don't depend on the type of bounds, arbitrarily fix to be discrete
    using RegularDecomposer = diy::RegularDecomposer<diy::DiscreteBounds>;
    using DivisionVector    = std::vector<int>;
    using KVSVector         = std::vector<diy::RegularPartners::DimK>;
    using namespace py::literals;

    py::class_<RegularPartners> regular_partners_class(m, "RegularPartners");
    regular_partners_class
        .def(py::init([](const RegularDecomposer& d, int k, bool contiguous)
                    {
                        return new RegularPartners(d, k, contiguous);
                    }), "d"_a, "k"_a = 2, "contiguous"_a = true
                )
        .def("rounds",          &RegularPartners::rounds)
        .def("size",            &RegularPartners::size)
        .def("dim",             &RegularPartners::dim)
        .def("step",            &RegularPartners::step)
        .def("divisions",       &RegularPartners::divisions)
        .def("kvs",             &RegularPartners::kvs)
        .def("contiguous",      &RegularPartners::contiguous)
        .def("fill",            &RegularPartners::fill)
        .def("group_position",  &RegularPartners::group_position)
    ;

    m.def("factor", [](RegularPartners& p, int k, const DivisionVector& divisions, KVSVector& kvs)
        {
            RegularPartners::factor(k, divisions, kvs);
        });

    py::class_<RegularMergePartners> regular_merge_partners_class(m, "RegularMergePartners", regular_partners_class);
    regular_merge_partners_class
        .def(py::init([](const RegularDecomposer& d, int k, bool contiguous)
                {
                    return new RegularMergePartners(d, k, contiguous);
                }), "d"_a, "k"_a = 2, "contiguous"_a = true
                )
        .def("active",          &RegularMergePartners::active)
        .def("incoming",        &RegularMergePartners::incoming)
        .def("outgoing",        &RegularMergePartners::outgoing)
    ;

    py::class_<RegularSwapPartners>(m, "RegularSwapPartners", regular_partners_class)
        .def(py::init([](const RegularDecomposer& d, int k, bool contiguous)
                {
                    return new RegularSwapPartners(d, k, contiguous);
                }), "d"_a, "k"_a = 2, "contiguous"_a = true
                )
        .def("active",          &RegularSwapPartners::active)
        .def("incoming",        &RegularSwapPartners::incoming)
        .def("outgoing",        &RegularSwapPartners::outgoing)
    ;

    py::class_<RegularBroadcastPartners>(m, "RegularBroadcastPartners", regular_merge_partners_class)
        .def(py::init([](const RegularDecomposer& d, int k, bool contiguous)
                    {
                        return new RegularBroadcastPartners(d, k, contiguous);
                    }), "d"_a, "k"_a = 2, "contiguous"_a = true
                )
        .def("parent_round",    &RegularBroadcastPartners::parent_round)
        .def("active",          &RegularBroadcastPartners::active)
        .def("incoming",        &RegularBroadcastPartners::incoming)
        .def("outgoing",        &RegularBroadcastPartners::outgoing)
    ;

    py::class_<RegularAllReducePartners>(m, "RegularAllReducePartners", regular_merge_partners_class)
        .def(py::init([](const RegularDecomposer& d, int k, bool contiguous)
                    {
                        return new RegularAllReducePartners(d, k, contiguous);
                    }), "d"_a, "k"_a = 2, "contiguous"_a = true
                )
        .def("parent_round",    &RegularAllReducePartners::parent_round)
        .def("active",          &RegularAllReducePartners::active)
        .def("incoming",        &RegularAllReducePartners::incoming)
        .def("outgoing",        &RegularAllReducePartners::outgoing)
    ;
}
