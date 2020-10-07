#include <string>
#include <functional>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
namespace py = pybind11;

#include <diy/reduce.hpp>
#include <diy/partners/merge.hpp>
#include <diy/partners/swap.hpp>
#include <diy/partners/broadcast.hpp>
#include <diy/partners/all-reduce.hpp>
using namespace diy;

#include "serialization.h"
#include "partners.h"

template <class Partners>
void init_reducer(py::module& m)
{
    using Callback  = std::function<void(py::object, const ReduceProxy*, const Partners*)>;

    m.def("reduce", [](Master& m, const Assigner& a, const Partners& p, Callback r)
            {
                diy::reduce(m, a, p,
                        [r](py::object* b, const ReduceProxy& rp, const Partners& p) { r(*b, &rp, &p); });
            });
}

void init_reducer_with_python_partners(py::module& m)
{
    using Callback  = std::function<void(py::object, const ReduceProxy*, const PythonPartners*)>;

    m.def("reduce", [](Master& m, const Assigner& a, py::object op, Callback r)
            {
                PythonPartners p(op);
                diy::reduce(m, a, p,
                        [r](py::object* b, const ReduceProxy& rp, const PythonPartners& p) { r(*b, &rp, &p); });
            });
}


void init_reducer(py::module& m)
{
    init_reducer<RegularMergePartners>(m);
    init_reducer<RegularSwapPartners>(m);
    init_reducer<RegularBroadcastPartners>(m);
    init_reducer<RegularAllReducePartners>(m);
    init_reducer_with_python_partners(m);
}
