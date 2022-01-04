#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
namespace py = pybind11;

#include <diy/master.hpp>
using namespace diy;

#include "serialization.h"
#include <diy/mpi.hpp>

void init_master(py::module& m)
{
    using CommProxy = Master::ProxyWithLink;
    using Callback  = std::function<void(py::object, const CommProxy*)>;
    using ICallback = std::function<bool(py::object, const CommProxy*)>;

    using Save      = std::function<void(const py::object*, diy::BinaryBuffer*)>;
    using Load      = std::function<py::object (diy::BinaryBuffer*)>;
    using Create    = std::function<py::object ()>;
    using Destroy   = std::function<void (py::object*)>;

    using namespace py::literals;

    //py::class_<FileStorage>(m, "FileStorage");

    py::class_<Master>(m, "Master")
      .def(py::init([](mpi::communicator    comm,
                       int                  threads,
                       int                  limit,
                       Create               create,
                       Destroy              destroy,
                       Save                 save,
                       Load                 load)
                       //FileStorage*         storage)
                       {
                           return new Master(comm,
                                             threads,
                                             limit,
                                             [create]() { return static_cast<void*>(new py::object { create() }); },
                                             [destroy](void* b_) { auto b = static_cast<py::object*>(b_); destroy(b); delete b; },
                                             0, // storage,
                                             [save](const void* b_, diy::BinaryBuffer& bb) { save(static_cast<const py::object*>(b_), &bb); },
                                             [load](void* b_, diy::BinaryBuffer& bb)
                                             {
                                                 auto* b = static_cast<py::object*>(b_);
                                                 *b = load(&bb);
                                             });
                       }),
                       "comm"_a, "threads"_a = 1, "limit"_a = -1,
                       "create"_a = Create([]() -> py::object { return py::none(); }),
                       "destroy"_a = Destroy([](py::object*) { }),
                       "save"_a = Save([](const py::object* b, diy::BinaryBuffer* bb) { diy::save(*bb, *b); }),
                       "load"_a = Load([](diy::BinaryBuffer* bb) -> py::object
                                       {
                                           py::object b;
                                           diy::load(*bb, b);
                                           return b;
                                       })

                       //"storage"_a = 0
          )
      .def("clear",     &Master::clear)
      .def("destroy",   &Master::destroy)
      .def("gid",       &Master::gid)
      .def("add",       [](Master& m, int gid, py::object o, const Link& l)
                        {
                            py::object* b = new py::object(o);
                            m.add(gid, b, l.clone());
                        }
                        , py::keep_alive<1, 3>()      // keep the object alive as long as master is alive
                        //, py::keep_alive<1, 4>()       // keep the link alive as long as master is alive
                        )
      .def("release",   &Master::release)
      .def("block",     [](const Master& m, int i)  { return m.block<py::object>(i); })
      .def("get",       [](Master& m, int i)        { return m.get<py::object>(i); })
      .def("communicator",  [](const Master& m)     { return m.communicator(); })
      .def("communicator",  [](Master& m)           { return m.communicator(); })
      .def("lid",       &Master::lid)
      .def("local",     &Master::local)
      .def("foreach",   [](Master& m, Callback f)
                        {
                            m.foreach([f](py::object* b, const Master::ProxyWithLink& cp) { f(*b, &cp); });
                        })
      .def("foreach",   [](Master& m, Callback f, Master::Skip s)
                        {
                            m.foreach([f](py::object* b, const Master::ProxyWithLink& cp) { f(*b, &cp); }, s);
                        })
      .def("exchange",  [](Master& m, bool remote)
                        {
                            m.exchange(remote);
                        }, "remote"_a = false)
      .def("iexchange", [](Master& m, ICallback f)
                        {
                            m.iexchange([f](py::object* b, const Master::ProxyWithLink& cp) { return f(*b, &cp); });
                        })
      .def("size",      &Master::size)
      .def("create",    &Master::create)
      .def("limit",     &Master::limit)
      .def("threads",   &Master::threads)
      .def("in_memory", &Master::in_memory)
      .def("__repr__",  [](const Master& m)
                        {
                            return fmt::format("Master with {} blocks", m.size());
                        })
    ;
}
