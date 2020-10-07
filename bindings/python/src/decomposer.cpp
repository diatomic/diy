#include <string>
#include <functional>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
namespace py = pybind11;

#include <diy/decomposition.hpp>

template<class Bounds, class PointBounds>
void init_decomposer_point_methods(py::class_<diy::RegularDecomposer<Bounds>>& decomposer_class)
{
    using RegularDecomposer = diy::RegularDecomposer<Bounds>;
    using Coordinate        = typename PointBounds::Coordinate;

    decomposer_class
      .def("lowest_gid",    [](const RegularDecomposer& d, const diy::DynamicPoint<Coordinate>& p)
                        {
                            return d.lowest_gid(p);
                        })
      .def("point_to_gids", [](const RegularDecomposer& d, std::vector<int>& gids, const diy::DynamicPoint<Coordinate>& p)
                        {
                            d.point_to_gids(gids, p);
                        })
      .def("point_to_gid", [](const RegularDecomposer& d, const diy::DynamicPoint<Coordinate>& p)
                        {
                            return d.point_to_gid(p);
                        })
      .def("num_gids", [](const RegularDecomposer& d, const diy::DynamicPoint<Coordinate>& p)
                        {
                            return d.num_gids(p);
                        })
      .def("top_bottom", [](const RegularDecomposer& d, int& top, int& bottom, const diy::DynamicPoint<Coordinate>& p, int axis)
                        {
                            d.top_bottom(top, bottom, p, axis);
                        })
    ;
}

template<class Bounds>
void init_decomposer(py::module& m, std::string name)
{
    using RegularDecomposer = diy::RegularDecomposer<Bounds>;
    using Creator           = typename RegularDecomposer::Creator;
    using Updater           = typename RegularDecomposer::Updater;
    using BoolVector        = typename RegularDecomposer::BoolVector;
    using CoordinateVector  = typename RegularDecomposer::CoordinateVector;
    using DivisionsVector   = typename RegularDecomposer::DivisionsVector;

    using namespace py::literals;

    py::class_<RegularDecomposer> regular_decomposer_class(m, name.c_str());
    regular_decomposer_class
      .def(py::init([](int dim, const Bounds& domain, int nblocks,
                          BoolVector        share_face,
                          BoolVector        wrap,
                          CoordinateVector  ghosts,
                          DivisionsVector   divisions)
                       {
                          return new RegularDecomposer(dim, domain, nblocks, share_face, wrap, ghosts, divisions);
                       }),
                       py::arg("dim"),
                       py::arg("domain"),
                       py::arg("nblocks"),
                       py::arg("share_face") = BoolVector(),
                       py::arg("wrap")       = BoolVector(),
                       py::arg("ghosts")     = CoordinateVector(),
                       py::arg("divisions")  = DivisionsVector()
        )
      .def("decompose",     [](RegularDecomposer& d, int rank, const diy::StaticAssigner& assigner, Creator create)
                            {
                                d.decompose(rank, assigner, create);
                            })
      .def("decompose",     [](RegularDecomposer& d, int rank, const diy::StaticAssigner& assigner, diy::Master& master, const Updater update)
                            {
                                d.decompose(rank, assigner, master, update);
                            })
      .def("decompose",     [](RegularDecomposer& d, int rank, const diy::StaticAssigner& assigner, diy::Master& master)
                            {
                                d.decompose(rank, assigner, master);
                            })
      .def("gid_to_coords", [](const RegularDecomposer& d, int gid)
                            {
                                return d.gid_to_coords(gid);
                            })
      .def("gid_to_coords", [](const RegularDecomposer& d, int gid, DivisionsVector& coords)
                            {
                                d.gid_to_coords(gid, coords);
                            })
      .def("coords_to_gid", [](const RegularDecomposer& d, const DivisionsVector& coords)
                            {
                                return d.coords_to_gid(coords);
                            })
      .def("fill_divisions",&RegularDecomposer::fill_divisions)
      .def("fill_bounds",   [](const RegularDecomposer& d, Bounds& bounds, const DivisionsVector& coords, bool add_ghosts)
                            {
                                d.fill_bounds(bounds, coords, add_ghosts);
                            }, "bounds"_a, "coords"_a, "add_ghosts"_a=false)
      .def("fill_bounds",   [](const RegularDecomposer& d, Bounds& bounds, int gid, bool add_ghosts)
                            {
                                d.fill_bounds(bounds, gid, add_ghosts);
                            }, "bounds"_a, "gid"_a, "add_ghosts"_a=false)
      .def_static("all",    &RegularDecomposer::all)
      .def_static("factor", &RegularDecomposer::factor)
      ;

      m.def("gid_to_coords", [](int gid, DivisionsVector& coords, const DivisionsVector& divs)
                            {
                                RegularDecomposer::gid_to_coords(gid, coords, divs);
                            });
      m.def("coords_to_gid", [](const DivisionsVector& coords, const DivisionsVector& divs)
                            {
                                return RegularDecomposer::coords_to_gid(coords, divs);
                            });

      init_decomposer_point_methods<Bounds, diy::DiscreteBounds>(regular_decomposer_class);
      init_decomposer_point_methods<Bounds, diy::ContinuousBounds>(regular_decomposer_class);
      // TP: I added this
      init_decomposer_point_methods<Bounds, diy::Bounds<double>>(regular_decomposer_class);
}

void init_decomposer(py::module& m)
{
    init_decomposer<diy::DiscreteBounds>  (m, "DiscreteDecomposer");
    init_decomposer<diy::ContinuousBounds>(m, "ContinuousDecomposer");
    // TP: I added this
    init_decomposer<diy::Bounds<double>>(m, "DoubleContinuousDecomposer");
}
