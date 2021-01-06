#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
namespace py = pybind11;

#include <mpi.h>

#include <diy/mpi/communicator.hpp>

void init_mpi(py::module& m)
{
    using PyMPIComm = diy::mpi::communicator;

    py::class_<PyMPIComm>(m, "MPIComm")
        .def(py::init<>())
        .def(py::init([](long comm_)
             {
                return new diy::mpi::communicator(*static_cast<MPI_Comm*>(reinterpret_cast<void*>(comm_)));
             }))
        .def_property_readonly("size", &PyMPIComm::size)
        .def_property_readonly("rank", &PyMPIComm::rank)
        .def_property_readonly("comm", &PyMPIComm::handle)
    ;

    m.def("init",       []() { int argc = 0; char** argv = 0; MPI_Init(&argc, &argv); });
    m.def("finalize",   []() { MPI_Finalize(); });
}
