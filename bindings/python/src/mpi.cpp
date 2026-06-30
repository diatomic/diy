#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
namespace py = pybind11;

#include <mpi.h>

#include <stdexcept>

#include <diy/mpi/communicator.hpp>

namespace
{
    bool diy_initialized_mpi = false;
}

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

    m.def("init",       []()
                         {
                             int finalized = 0;
                             MPI_Finalized(&finalized);
                             if (finalized)
                                 throw std::runtime_error("Cannot initialize MPI after MPI_Finalize");

                             int initialized = 0;
                             MPI_Initialized(&initialized);
                             if (initialized)
                                 return false;

                             int argc = 0;
                             char** argv = 0;
                             MPI_Init(&argc, &argv);
                             diy_initialized_mpi = true;
                             return true;
                         });
    m.def("finalize",   []()
                         {
                             if (!diy_initialized_mpi)
                                 return;

                             int initialized = 0;
                             int finalized = 0;
                             MPI_Initialized(&initialized);
                             MPI_Finalized(&finalized);
                             if (initialized && !finalized)
                                 MPI_Finalize();
                             diy_initialized_mpi = false;
                         });
}
