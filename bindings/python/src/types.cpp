#include <string>
#include <functional>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>
namespace py = pybind11;

#include <diy/types.hpp>
#include <diy/log.hpp>      // for fmt::format
#include <diy/pick.hpp>

#include <iostream>

template<class Coordinate>
void init_dynamic_point(py::module& m, std::string name)
{
    // TODO: see if there is a cleaner way to do this in pybind

    using Point = diy::DynamicPoint<Coordinate>;
    using CoordinateVector = std::vector<Coordinate>;
    py::class_<Point>(m, name.c_str())
        .def(py::init<int>())
        .def(py::init<CoordinateVector>())
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self +  py::self)
        .def(py::self -  py::self)
        .def("__getitem__",
             [](const Point& p, size_t i)
             {
                if (i >= p.size())
                    throw py::index_error();
                return p[i];
             })
        .def("__setitem__",
             [](Point& p, size_t i, Coordinate x)
             {
                if (i >= p.size())
                    throw py::index_error();
                p[i] = x;
             })
        .def("__repr__", [](const Point& p) { return fmt::format("[{}]", p); })
        .def(py::pickle(
            [](const Point& p)        // __getstate__
            {
                CoordinateVector vp(p.begin(), p.end());
                return py::make_tuple(vp);
            },
            [](py::tuple t)                 // __setstate__
            {
                CoordinateVector vp = t[0].cast<CoordinateVector>();
                return Point(vp);
            }
        ));
    ;
}

void init_direction(py::module& m, std::string name)
{
    using Direction         = diy::Direction;
    using Point             = Direction::Parent;
    using Coordinate        = Point::Coordinate;
    using CoordinateVector  = std::vector<Coordinate>;
    py::class_<Direction, Point>(m, name.c_str())
        .def(py::init<>())
        .def(py::init<CoordinateVector>() )
        .def(py::init<int, int>() )
        .def(py::self == py::self)
        .def(py::self < py::self)
        .def("__repr__",  [](const Direction& d) { return fmt::format("({})", d); })
        .def(py::pickle(
            [](const Direction& d)        // __getstate__
            {
                Point p = d;
                return py::make_tuple(p);
            },
            [](py::tuple t)                 // __setstate__
            {
                Point p = t[0].cast<Point>();
                // not sure why this yoga is necessary, but something is not
                // working out with constructors without it.
                Direction d(p.size());
                for (size_t i = 0; i < p.size(); ++i)
                    d[i] = p[i];
                return d;
            }
        ));
    ;
}

template<class Bounds>
void init_bounds(py::module& m, std::string name)
{
    using Coordinate = typename Bounds::Coordinate;
    using Point      = typename Bounds::Point;

    py::class_<Bounds>(m, name.c_str())
        .def(py::init<int>())
        .def(py::init<Point,Point>())
        .def(py::init<std::vector<Coordinate>,std::vector<Coordinate>>())
        .def_readwrite("min", &Bounds::min)
        .def_readwrite("max", &Bounds::max)
        .def("__repr__",  [](const Bounds& b) { return fmt::format("[{} - {}]", b.min, b.max); })
        .def("__contains__", [](const Bounds& b, const Point& p)                    { return diy::distance(b,p) == 0; })
        .def("__contains__", [](const Bounds& b, const std::vector<Coordinate>& p)  { return diy::distance(b,Point(p)) == 0; })
        .def(py::pickle(
            [](const Bounds& p)        // __getstate__
            {
                return py::make_tuple(p.min, p.max);
            },
            [](py::tuple t)                 // __setstate__
            {
                Point min = t[0].cast<Point>();
                Point max = t[1].cast<Point>();
                return Bounds(min, max);
            }
        ));
    ;
}

void init_types(py::module& m)
{
    using BlockID   = diy::BlockID;

    py::class_<BlockID>(m, "BlockID")
        .def(py::init<int,int>())
        .def("__repr__", [](const BlockID& bid)              { return fmt::format("[{},{}]", bid.gid, bid.proc); })
        .def_readonly("gid",  &BlockID::gid)
        .def_readonly("proc", &BlockID::proc)
        .def(py::pickle(
            [](const BlockID& bid)          // __getstate__
            {
                return py::make_tuple(bid.gid, bid.proc);
            },
            [](py::tuple t)                 // __setstate__
            {
                int gid     = t[0].cast<int>();
                int proc    = t[1].cast<int>();
                return BlockID(gid, proc);
            }
        ));
    ;

    init_dynamic_point<diy::DiscreteBounds::Coordinate>  (m, "DiscreteDynamicPoint");
    init_dynamic_point<diy::ContinuousBounds::Coordinate>(m, "ContinuousDynamicPoint");
    // TP: I added this
    init_dynamic_point<double>(m, "DoubleContinuousDynamicPoint");

    init_direction(m, "Direction");

    init_bounds<diy::DiscreteBounds>  (m, "DiscreteBounds");
    init_bounds<diy::ContinuousBounds>(m, "ContinuousBounds");
    // TP: I added this
    init_bounds<diy::Bounds<double>>(m, "DoubleContinuousBounds");
}
