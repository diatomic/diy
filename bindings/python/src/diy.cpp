#include <pybind11/pybind11.h>
namespace py = pybind11;

void init_master(py::module& m);
void init_decomposer(py::module& m);
void init_types(py::module& m);
void init_assigner(py::module& m);
void init_link(py::module& m);
void init_proxy(py::module& m);
void init_mpi(py::module& m);
void init_reducer(py::module& m);
void init_partners(py::module& m);
void init_io(py::module& m);

void init_myblock(py::module& m);

PYBIND11_MODULE(_diy, m)
{
    init_master(m);
    init_decomposer(m);
    init_types(m);
    init_assigner(m);
    init_link(m);
    init_proxy(m);
    init_reducer(m);
    init_partners(m);
    init_io(m);

    py::module diy_mpi = m.def_submodule("mpi", "DIY MPI module");
    init_mpi(diy_mpi);

    init_myblock(m);
}
