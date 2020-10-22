#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
namespace py = pybind11;

#include <mpi.h>

struct PyMPIComm
{
                PyMPIComm(MPI_Comm comm_ = MPI_COMM_WORLD):
                    comm(comm_)
    {
        MPI_Comm_size(comm, &size);
        MPI_Comm_rank(comm, &rank);
    }

    MPI_Comm    comm;
    int         size;
    int         rank;
};

void init_mpi(py::module& m)
{
    py::class_<PyMPIComm>(m, "MPIComm")
        .def(py::init<>())
        .def_readonly("size", &PyMPIComm::size)
        .def_readonly("rank", &PyMPIComm::rank)
        .def("comm",          [](const PyMPIComm& comm) { return (long) &comm.comm; })
    ;

    m.def("init",       []() { int argc = 0; char** argv = 0; MPI_Init(&argc, &argv); });
    m.def("finalize",   []() { MPI_Finalize(); });
}
